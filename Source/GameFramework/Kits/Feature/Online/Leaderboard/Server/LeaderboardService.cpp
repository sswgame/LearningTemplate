#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardService.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStore.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStoreRouter.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Mail/Server/ServiceMail.h"
#include "GameFramework/Runtime/Online/Schedule/Server/ServiceScheduler.h"
#include "GameFramework/Runtime/Online/Service/OnlineLimit.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct LeaderboardServiceInternal
        {
            static constexpr uint8       kFormatVersion        = 1;
            static constexpr int32       kRebuildBatch         = 256;
            static constexpr int32       kPlayersPerCommit     = 12;  ///< 사람마다 쓰기가 다섯이다. 결과, 우편, 보낸 기록, 만료 인덱스, 새 우편 알림이며 트랜잭션 상한 64 안에 들어간다
            static constexpr int32       kPlayersPerPage       = 600; ///< 정산 저장소 일 하나가 쓰는 사람 수(커밋 50 번). 페이지 사이에 다른 요청이 저장소 워커를 쓴다
            static constexpr int64       kDayMs                = 86400000;
            static constexpr int64       kWeekMs               = 604800000;
            static constexpr int64       kRankRetentionGraceMs = 86400000;  ///< 기간이 끝난 뒤 캐시 순위를 남기는 시간(시계 차이와 경계 직후 읽기를 흡수한다)
            static constexpr int64       kSettlementWindowMs   = 604800000; ///< 7일이다. 시즌이 끝난 뒤 이 안에 정산이 돈다
            static constexpr int64       kSettlementLeaseMs    = 600000;    ///< 10분이다. 돌던 서버가 죽으면 다른 서버가 이어받는다
            static constexpr const utf8* kSettlementJobPrefix  = "lb.settle.";
            static constexpr const utf8* kAchievementSender    = "system.achievement";
            static constexpr const utf8* kSeasonSender         = "system.season";

            static const hashed_string& getAchievementTable()
            {
                static const hashed_string s_table{ "lb_achievement" };
                return s_table;
            }

            static const hashed_string& getSeasonResultTable()
            {
                static const hashed_string s_table{ "lb_season_result" };
                return s_table;
            }

            /** @brief 시즌 id 를 16진 8자리 문자열로 만듭니다. 작업 id 와 우편 멱등 키를 64바이트 안에 두려는 것입니다. */
            static string makeSeasonHex( uint32 seasonID ) { return ServiceKeyUtil::makeHex64( seasonID ).substr( 8 ); }

            static string makeSeasonResultKey( string_view boardID, uint32 seasonID, AccountID accountID )
            {
                return string( boardID ) + "/" + makeSeasonHex( seasonID ) + "/" + ServiceKeyUtil::makeHex64( accountID );
            }

            /** @brief 시즌 보드의 시즌 목록 규칙을 검사합니다. id 는 1 이상이고 유일해야 하며, 시작이 끝보다 앞서야 합니다. 보상 구간은 1 ≤ from ≤ to 이고 자산, 수량, 우편 제목이 있어야 합니다. */
            static bool areSeasonsValid( const LeaderboardDef& definition )
            {
                if ( definition._reset != LeaderboardReset::Season )
                    return definition._listSeason.empty();
                for ( size_t seasonIndex = 0; seasonIndex < definition._listSeason.size(); ++seasonIndex )
                {
                    const LeaderboardSeason& season = definition._listSeason[seasonIndex];
                    if ( season._seasonID == 0 || season._startMs >= season._endMs )
                        return false;
                    for ( size_t otherIndex = 0; otherIndex < seasonIndex; ++otherIndex )
                    {
                        if ( definition._listSeason[otherIndex]._seasonID == season._seasonID )
                            return false;
                    }
                    for ( const LeaderboardRewardTier& tier : season._listRewardTier )
                    {
                        const bool bTierOk = 1 <= tier._rankFrom && tier._rankFrom <= tier._rankTo && tier._amount > 0 && tier._assetID.empty() == false &&
                                             tier._mailTitleKey.empty() == false;
                        if ( bTierOk == false )
                            return false;
                    }
                }
                return true;
            }

            /** @brief 보상 우편 하나를 트랜잭션에 붙입니다. 재원을 발행하며 30일 보관하고 만료되면 첨부가 사라집니다. */
            static LedgerResult stageRewardMail( IServiceStoreConnection& connection, ServiceTransaction& inoutTransaction, AccountID accountID, string_view assetID,
                                                 int64 amount, string_view titleKey, string_view senderName, string idempotencyKey, UnixMilliseconds nowMs )
            {
                ServiceMailMessage mail;
                mail._listAttachment.push_back( ServiceMailAttachment{ string( assetID ), amount } );
                mail._titleKey           = string( titleKey );
                mail._senderName         = string( senderName );
                mail._idempotencyKey     = std::move( idempotencyKey );
                mail._fundingHolder      = LedgerHolder::makeMint();
                mail._recipientAccountID = accountID;
                mail._createdMs          = nowMs.count();
                mail._expiresMs          = nowMs.count() + ServiceMailConstant::kDefaultRetentionMs;
                mail._expiryAction       = ServiceMail::getDefaultExpiryAction( mail._fundingHolder );
                string mailKey;
                bool   bReplayed = false;
                return ServiceMail::stageSend( connection, mail, inoutTransaction, mailKey, bReplayed );
            }

            static const hashed_string& getScoreTable()
            {
                static const hashed_string s_table{ "lb_score" };
                return s_table;
            }

            static const hashed_string& getStatTable()
            {
                static const hashed_string s_table{ "lb_stat" };
                return s_table;
            }

            static string makeScorePrefix( string_view boardID, uint64 periodID ) { return string( boardID ) + "/" + ServiceKeyUtil::makeHex64( periodID ) + "/"; }
            static string makeNameKey( AccountID accountID ) { return string( "lb/name/" ) + ServiceKeyUtil::makeHex64( accountID ); }
            static string makeReadyKey( const string& rankKey ) { return rankKey + "/ready"; }
            /** @brief 캐시 순위의 멤버입니다. 캐시가 같은 점수를 멤버 역순으로 세우므로 보수를 써서 계정 id 오름차순이 되게 합니다. */
            static string             makeRankMember( AccountID accountID ) { return ServiceKeyUtil::makeHex64( ~accountID ); }
            [[nodiscard]] static bool parseRankMember( string_view member, AccountID& outAccountID )
            {
                uint64     inverted = 0;
                const bool bParsed  = ServiceKeyUtil::parseHex64( member, inverted );
                outAccountID        = ~inverted;
                return bParsed;
            }

            static vector<uint8> encodeScore( int64 score, int64 updatedMs, string_view displayName )
            {
                BitWriter writer;
                writer.writeBits( kFormatVersion, 8 );
                writer.writeVarInt( score );
                writer.writeVarInt( updatedMs );
                ServiceKeyUtil::writeString( writer, displayName );
                return writer.getBytes();
            }

            static bool isAchievementIDLess( const OnlineAchievementDef& left, const OnlineAchievementDef& right ) { return left._achievementID < right._achievementID; }

            /** @brief 점수와 표시 이름을 읽습니다. 계정 목록 조회가 씁니다. */
            [[nodiscard]] static bool decodeScoreWithName( const vector<uint8>& bytes, int64& outScore, string& outDisplayName )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                if ( reader.readBits( 8 ) != kFormatVersion )
                    return false;
                outScore                               = reader.readVarInt();
                [[maybe_unused]] const int64 updatedMs = reader.readVarInt(); // 쓰지 않지만 읽어야 이름 필드에 닿는다. 넘쳤는지는 아래 hasOverflowed 가 본다
                return ServiceKeyUtil::readString( reader, OnlineLimit::kMaxDisplayNameSize, outDisplayName ) && reader.hasOverflowed() == false;
            }

            [[nodiscard]] static bool decodeScore( const vector<uint8>& bytes, int64& outScore )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                if ( reader.readBits( 8 ) != kFormatVersion )
                    return false;
                outScore = reader.readVarInt();
                return reader.hasOverflowed() == false;
            }

            /** @brief 갱신 방식에 따라 새 값을 정합니다. 쓸 필요가 없으면 @p outbChanged 가 false 입니다. */
            static int64 applyUpdate( LeaderboardUpdate update, LeaderboardOrder order, bool bHasOld, int64 oldValue, int64 value, bool& outbChanged )
            {
                outbChanged = true;
                if ( bHasOld == false )
                    return value;
                switch ( update )
                {
                    case LeaderboardUpdate::Latest:
                    {
                        outbChanged = oldValue != value;
                        return value;
                    }
                    case LeaderboardUpdate::Sum:
                    {
                        return MathUtil::clamp( oldValue + value, -LeaderboardLimit::kMaxAbsScore, LeaderboardLimit::kMaxAbsScore );
                    }
                    default:
                    {
                        const bool bBetter = order == LeaderboardOrder::Descending ? value > oldValue : value < oldValue;
                        outbChanged        = bBetter;
                        return bBetter ? value : oldValue;
                    }
                }
            }

            /** @brief @p prefix 아래의 점수를 모두 읽습니다. 배치마다 커서를 씁니다. */
            [[nodiscard]] static bool readAllScores( IServiceStoreConnection& connection, const string& prefix, vector<LeaderboardScoreRow>& outListScore )
            {
                string cursor;
                while ( true )
                {
                    vector<ServiceRecord> listRecord;
                    if ( connection.listRecords( getScoreTable(), prefix, cursor, kRebuildBatch, false, listRecord ) != ServiceStoreResult::Ok )
                        return false;
                    for ( const ServiceRecord& record : listRecord )
                    {
                        LeaderboardScoreRow row;
                        const bool          bParsed = ServiceKeyUtil::parseHex64( string_view( record._key ).substr( prefix.size() ), row._accountID ) &&
                                             decodeScore( record._bytes, row._score );
                        if ( bParsed )
                            outListScore.push_back( row );
                    }
                    if ( static_cast<int32>( listRecord.size() ) < kRebuildBatch )
                        return true;
                    cursor = listRecord.back()._key;
                }
            }
        };

        /** @brief 순위 규칙 하나입니다. 정렬 방향으로 세우고 같은 점수는 계정 id 오름차순입니다. 다시 돌아도 같은 순위여야 하고, 캐시 순위(`makeRankMember`)도 이 순서입니다. */
        struct LeaderboardRankOrder
        {
            LeaderboardOrder _order{ LeaderboardOrder::Descending };

            template <typename TRow>
            bool operator()( const TRow& left, const TRow& right ) const
            {
                if ( left._score != right._score )
                    return _order == LeaderboardOrder::Descending ? left._score > right._score : left._score < right._score;
                return left._accountID < right._accountID;
            }
        };

    } // namespace

    /** @brief 점수(또는 통계) 하나를 버전 조건으로 쓰는 일의 입력입니다. 저장소 스레드가 읽는 값 복사입니다(`LeaderboardService::submitWrite` 가 받으므로 이름 있는 이름공간에 둔다). */
    struct LeaderboardWriteInput
    {
        hashed_string     _table{};
        string            _key{};
        string            _displayName{};
        int64             _value{ 0 };
        UnixMilliseconds  _nowMs{ 0 };
        LeaderboardUpdate _update{ LeaderboardUpdate::Best };
        LeaderboardOrder  _order{ LeaderboardOrder::Descending };
    };

    namespace
    {
        /** @brief 쓰기 일이 완료로 넘기는 값입니다. */
        struct LeaderboardWriteResult
        {
            int64                    _finalValue{ 0 };
            LeaderboardServiceResult _result{ LeaderboardServiceResult::fail( OnlineError::kUnavailable ) };
            uint8                    _bChanged{ SW_FALSE };
        };

        /** @brief 읽기 일이 완료로 넘기는 값과 결과입니다. */
        template <typename TValue>
        struct LeaderboardRead
        {
            TValue                   _value{};
            LeaderboardServiceResult _result{ LeaderboardServiceResult::fail( OnlineError::kUnavailable ) };
        };

        /** @brief 저장소 스레드에서 도는 일입니다. 서비스 멤버를 만지지 않고 인자로 받은 값만 씁니다. */
        struct LeaderboardStoreRun
        {
            /** @brief 갱신 방식에 따라 새 값을 정하고 버전 조건으로 씁니다. 충돌이면 다시 읽어 `OnlineLimit::kMaxConflictRetry` 번까지 다시 한다. */
            static LeaderboardWriteResult writeValue( IServiceStoreConnection& connection, const LeaderboardWriteInput& input )
            {
                using Internal = LeaderboardServiceInternal;
                LeaderboardWriteResult write;
                for ( int32 attempt = 0; attempt < OnlineLimit::kMaxConflictRetry; ++attempt )
                {
                    ServiceRecord            record;
                    const ServiceStoreResult readResult = connection.readRecord( input._table, input._key, record );
                    if ( readResult != ServiceStoreResult::Ok && readResult != ServiceStoreResult::NotFound )
                        return write;
                    int64      oldValue = 0;
                    const bool bHasOld  = readResult == ServiceStoreResult::Ok && Internal::decodeScore( record._bytes, oldValue );
                    bool       bChanged = false;
                    write._finalValue   = Internal::applyUpdate( input._update, input._order, bHasOld, oldValue, input._value, bChanged );
                    if ( bChanged == false )
                    {
                        write._result = LeaderboardResult::Ok; // 더 좋지 않으므로 쓰지 않는다
                        return write;
                    }
                    ServiceTransaction transaction;
                    transaction.put( input._table, input._key, Internal::encodeScore( write._finalValue, input._nowMs.count(), input._displayName ), record._version );
                    const ServiceStoreResult commitResult = connection.commit( transaction );
                    if ( commitResult == ServiceStoreResult::Ok )
                    {
                        write._result   = LeaderboardResult::Ok;
                        write._bChanged = SW_TRUE;
                        return write;
                    }
                    if ( commitResult != ServiceStoreResult::Conflict )
                        return write;
                }
                write._result = LeaderboardServiceResult::fail( OnlineError::kBusy );
                return write;
            }

            /** @brief 계정의 통계를 모두 읽습니다. 손상된 통계는 건너뛰지 않고 이 읽기를 실패로 돌려준다. */
            static LeaderboardRead<vector<LeaderboardStat>> readStats( IServiceStoreConnection& connection, AccountID accountID )
            {
                LeaderboardRead<vector<LeaderboardStat>> read;
                vector<ServiceRecord>                    listRecord;
                const string                             prefix = ServiceKeyUtil::makeHex64( accountID ) + "/";
                if ( connection.listRecords( LeaderboardServiceInternal::getStatTable(), prefix, "", LeaderboardLimit::kMaxStatCount, false, listRecord ) != ServiceStoreResult::Ok )
                    return read;
                for ( const ServiceRecord& record : listRecord )
                {
                    LeaderboardStat stat;
                    stat._name = record._key.substr( prefix.size() );
                    if ( LeaderboardServiceInternal::decodeScore( record._bytes, stat._value ) == false )
                    {
                        read._value.clear();
                        return read;
                    }
                    read._value.push_back( std::move( stat ) );
                }
                read._result = LeaderboardResult::Ok;
                return read;
            }

            /** @brief 업적 하나를 "없어야 함" 조건으로 쓰고 보상 우편을 같은 트랜잭션에 넣습니다. 이미 있으면 아무것도 하지 않고 false 입니다. */
            static bool unlockAchievement( IServiceStoreConnection& connection, const OnlineAchievementDef& definition, AccountID accountID, UnixMilliseconds nowMs )
            {
                using Internal    = LeaderboardServiceInternal;
                const string  key = ServiceKeyUtil::makeHex64( accountID ) + "/" + definition._achievementID;
                ServiceRecord existing;
                if ( connection.readRecord( Internal::getAchievementTable(), key, existing ) != ServiceStoreResult::NotFound )
                    return false; // 이미 달성했거나 저장소가 실패했다. 다음 통계 변화 때 다시 확인한다
                ServiceTransaction transaction;
                transaction.put( Internal::getAchievementTable(), key, Internal::encodeScore( nowMs.count(), nowMs.count(), string_view{} ), ServiceRecord::kAbsentVersion ); // 값은 달성 시각이다
                if ( definition._rewardAssetID.empty() == false )
                {
                    const string       idempotencyKey = string( "ach." ) + ServiceKeyUtil::makeHex64( accountID ) + "." + definition._achievementID;
                    const LedgerResult staged         = Internal::stageRewardMail( connection, transaction, accountID, definition._rewardAssetID, definition._rewardAmount,
                                                                                   definition._mailTitleKey, Internal::kAchievementSender, idempotencyKey, nowMs );
                    if ( staged != LedgerResult::Ok )
                        return false; // 우편 규칙이나 저장소 때문에 실패했으므로 업적도 쓰지 않는다. 다음에 다시 시도한다
                }
                return connection.commit( transaction ) == ServiceStoreResult::Ok;
            }

            /** @brief 계정이 달성한 업적을 모두 읽습니다. */
            static LeaderboardRead<vector<AchievementState>> readAchievements( IServiceStoreConnection& connection, AccountID accountID )
            {
                LeaderboardRead<vector<AchievementState>> read;
                vector<ServiceRecord>                     listRecord;
                const string                              prefix = ServiceKeyUtil::makeHex64( accountID ) + "/";
                if ( connection.listRecords( LeaderboardServiceInternal::getAchievementTable(), prefix, "", LeaderboardLimit::kMaxAchievementCount, false, listRecord ) !=
                     ServiceStoreResult::Ok )
                    return read;
                for ( const ServiceRecord& record : listRecord )
                {
                    AchievementState achievement;
                    achievement._achievementID = record._key.substr( prefix.size() );
                    if ( LeaderboardServiceInternal::decodeScore( record._bytes, achievement._unlockedMs ) )
                        read._value.push_back( std::move( achievement ) );
                }
                read._result = LeaderboardResult::Ok;
                return read;
            }

            /** @brief 통계와 달성 기록을 읽어 등록한 업적(복사본)마다 진행률 한 줄을 만듭니다. 손상된 기록은 건너뛰지 않고 이 읽기를 실패로 돌려준다. */
            static LeaderboardRead<vector<AchievementProgress>> readAchievementProgress( IServiceStoreConnection& connection, const vector<OnlineAchievementDef>& listDefinition, AccountID accountID )
            {
                using Internal = LeaderboardServiceInternal;
                LeaderboardRead<vector<AchievementProgress>> read;
                vector<ServiceRecord>                        listStatRecord;
                vector<ServiceRecord>                        listAchievementRecord;
                const string                                 prefix  = ServiceKeyUtil::makeHex64( accountID ) + "/";
                const bool                                   bReadOk = connection.listRecords( Internal::getStatTable(), prefix, "", LeaderboardLimit::kMaxStatCount, false, listStatRecord ) ==
                                         ServiceStoreResult::Ok &&
                                     connection.listRecords( Internal::getAchievementTable(), prefix, "", LeaderboardLimit::kMaxAchievementCount, false, listAchievementRecord ) ==
                                         ServiceStoreResult::Ok;
                if ( bReadOk == false )
                    return read;
                for ( const OnlineAchievementDef& definition : listDefinition )
                {
                    AchievementProgress progress;
                    if ( findValue( listAchievementRecord, prefix.size(), definition._achievementID, progress._unlockedMs ) == false )
                    {
                        read._value.clear();
                        return read;
                    }
                    if ( definition._bHidden && progress._unlockedMs == 0 )
                    {
                        progress._bHidden = SW_TRUE;
                        read._value.push_back( std::move( progress ) );
                        continue;
                    }
                    if ( findValue( listStatRecord, prefix.size(), definition._statName, progress._progress ) == false )
                    {
                        read._value.clear();
                        return read;
                    }
                    progress._achievementID = definition._achievementID;
                    progress._threshold     = definition._threshold;
                    progress._bHidden       = definition._bHidden ? SW_TRUE : SW_FALSE;
                    read._value.push_back( std::move( progress ) );
                }
                read._result = LeaderboardResult::Ok;
                return read;
            }

            /** @brief 계정 목록의 점수를 읽어 목록 안 순위를 매깁니다. 손상된 점수는 건너뛰지 않고 이 읽기를 실패로 돌려준다. */
            static LeaderboardRead<vector<LeaderboardEntry>> readScoresOf( IServiceStoreConnection& connection, const string& prefix, const vector<AccountID>& listAccountID,
                                                                           LeaderboardOrder order )
            {
                LeaderboardRead<vector<LeaderboardEntry>> read;
                for ( const AccountID accountID : listAccountID )
                {
                    ServiceRecord            record;
                    const ServiceStoreResult readResult = connection.readRecord( LeaderboardServiceInternal::getScoreTable(), prefix + ServiceKeyUtil::makeHex64( accountID ), record );
                    if ( readResult == ServiceStoreResult::NotFound )
                        continue; // 이 기간에 점수가 없다
                    LeaderboardEntry entry;
                    entry._accountID = accountID;
                    if ( readResult != ServiceStoreResult::Ok || LeaderboardServiceInternal::decodeScoreWithName( record._bytes, entry._score, entry._displayName ) == false )
                    {
                        read._value.clear();
                        return read;
                    }
                    read._value.push_back( std::move( entry ) );
                }
                VectorUtil::sort( read._value, LeaderboardRankOrder{ order } );
                for ( size_t index = 0; index < read._value.size(); ++index )
                {
                    read._value[index]._rank = static_cast<int32>( index ) + 1;
                }
                read._result = LeaderboardResult::Ok;
                return read;
            }

            /** @brief 시즌 결과 하나(점수와 순위)를 읽습니다. 없으면 NotRanked 입니다. */
            static LeaderboardRead<LeaderboardEntry> readSeasonResult( IServiceStoreConnection& connection, const string& key, AccountID accountID )
            {
                LeaderboardRead<LeaderboardEntry> read;
                read._value._accountID = accountID;
                ServiceRecord            record;
                const ServiceStoreResult readResult = connection.readRecord( LeaderboardServiceInternal::getSeasonResultTable(), key, record );
                if ( readResult == ServiceStoreResult::NotFound )
                {
                    read._result = LeaderboardResult::NotRanked;
                    return read;
                }
                if ( readResult != ServiceStoreResult::Ok )
                    return read;
                BitReader reader( record._bytes.data(), static_cast<int32>( record._bytes.size() ) );
                if ( reader.readBits( 8 ) != LeaderboardServiceInternal::kFormatVersion )
                    return read;
                read._value._score = reader.readVarInt();
                read._value._rank  = reader.readVarInt32();
                read._result       = reader.hasOverflowed() ? LeaderboardServiceResult::fail( OnlineError::kUnavailable ) : LeaderboardResult::Ok;
                return read;
            }

            /** @brief 정산의 첫 단계입니다. 시즌 점수를 모두 읽어 순위대로 정렬합니다. 쓰기는 하지 않는다. 못 읽었으면 결과가 실패다. */
            static LeaderboardRead<vector<LeaderboardScoreRow>> readRanking( IServiceStoreConnection& connection, const string& scorePrefix, LeaderboardOrder order )
            {
                LeaderboardRead<vector<LeaderboardScoreRow>> read;
                if ( LeaderboardServiceInternal::readAllScores( connection, scorePrefix, read._value ) == false )
                    return read;
                VectorUtil::sort( read._value, LeaderboardRankOrder{ order } );
                read._result = LeaderboardResult::Ok;
                return read;
            }

            /**
             * @brief 정산 페이지 하나입니다. 순위가 정해진 사람 `kPlayersPerPage` 명까지 12명씩 결과와 보상 우편을 한 트랜잭션으로 씁니다. 다 썼으면 true 입니다.
             * @details 결과가 있는 사람은 건너뛰므로 다시 돌아도 한 번만 처리된다. 실패하면 다음 실행(임대가 지나 다른 서버가 맡거나 다시 부름)이 남은 사람부터 잇는다.
             */
            static bool writeSettlementPage( IServiceStoreConnection& connection, const LeaderboardDef& board, const LeaderboardSeason& season, const vector<LeaderboardScoreRow>& listPage,
                                             int32 firstRank, UnixMilliseconds nowMs )
            {
                using Internal = LeaderboardServiceInternal;
                for ( size_t first = 0; first < listPage.size(); first += static_cast<size_t>( Internal::kPlayersPerCommit ) )
                {
                    ServiceTransaction transaction;
                    const size_t       last = MathUtil::min( listPage.size(), first + static_cast<size_t>( Internal::kPlayersPerCommit ) );
                    for ( size_t index = first; index < last; ++index )
                    {
                        if ( stagePlayer( connection, transaction, board, season, listPage[index], firstRank + static_cast<int32>( index ), nowMs ) == false )
                            return false;
                    }
                    if ( transaction.isEmpty() == false && connection.commit( transaction ) != ServiceStoreResult::Ok )
                        return false;
                }
                return true;
            }

        private:
            /** @brief @p listRecord 에서 키 꼬리가 @p name 인 값을 읽습니다. 없으면 @p outValue 는 그대로이고 true, 있는데 읽지 못하면 false 입니다. */
            [[nodiscard]] static bool findValue( const vector<ServiceRecord>& listRecord, size_t prefixSize, string_view name, int64& outValue )
            {
                for ( const ServiceRecord& record : listRecord )
                {
                    if ( string_view( record._key ).substr( prefixSize ) == name )
                        return LeaderboardServiceInternal::decodeScore( record._bytes, outValue );
                }
                return true;
            }

            /** @brief 한 사람의 결과와 보상을 붙입니다. 이미 결과가 있으면 건너뜁니다. 저장소나 우편이 실패하면 false 를 반환합니다. */
            static bool stagePlayer( IServiceStoreConnection& connection, ServiceTransaction& inoutTransaction, const LeaderboardDef& board, const LeaderboardSeason& season,
                                     const LeaderboardScoreRow& row, int32 rank, UnixMilliseconds nowMs )
            {
                using Internal               = LeaderboardServiceInternal;
                const string             key = Internal::makeSeasonResultKey( board._boardID, season._seasonID, row._accountID );
                ServiceRecord            existing;
                const ServiceStoreResult read = connection.readRecord( Internal::getSeasonResultTable(), key, existing );
                if ( read == ServiceStoreResult::Ok )
                    return true; // 앞선 실행이 이미 정산했다
                if ( read != ServiceStoreResult::NotFound )
                    return false;
                inoutTransaction.put( Internal::getSeasonResultTable(), key, Internal::encodeScore( row._score, rank, string_view{} ), ServiceRecord::kAbsentVersion );
                const LeaderboardRewardTier* pTier = findTier( season, rank );
                if ( pTier == nullptr )
                    return true;
                const string idempotencyKey = string( "lb." ) + board._boardID + "." + Internal::makeSeasonHex( season._seasonID ) + "." + ServiceKeyUtil::makeHex64( row._accountID );
                return Internal::stageRewardMail( connection, inoutTransaction, row._accountID, pTier->_assetID, pTier->_amount, pTier->_mailTitleKey, Internal::kSeasonSender,
                                                  idempotencyKey, nowMs ) == LedgerResult::Ok;
            }

            static const LeaderboardRewardTier* findTier( const LeaderboardSeason& season, int32 rank )
            {
                for ( const LeaderboardRewardTier& tier : season._listRewardTier )
                {
                    if ( tier._rankFrom <= rank && rank <= tier._rankTo )
                        return &tier;
                }
                return nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    LeaderboardService::LeaderboardService()
        : _mapBoard{}
        , _mapRankKeyToWaitingRead{}
        , _mapRankKeyToLateWrite{}
        , _mapReadyRequestToRead{}
        , _mapRequestToQuery{}
        , _mapNameRequestToIndex{}
        , _mapQuery{}
        , _mapAchievement{}
        , _mapAccountStats{}
        , _mapJobToRun{}
        , _mapSettlement{}
        , _uniqueExpiringRankKeys{}
        , _unlockBuffer{}
        , _completionBuffer{}
        , _dependencies{}
        , _pScheduler{ nullptr }
        , _queryIDAllocator{}
        , _settlementIDAllocator{}
        , _store{}
    {
    }

    LeaderboardService::~LeaderboardService()
    {
        shutdown();
    }

    void LeaderboardService::initialize( const LeaderboardServiceDependencies& dependencies )
    {
        SW_ASSERT( dependencies._pStore != nullptr && dependencies._pRouter != nullptr );
        _dependencies = dependencies;
        _replyOwner   = dependencies._pRouter->openReplyOwner();
        _store.bindStore( dependencies._pStore );
    }

    void LeaderboardService::shutdown()
    {
        _replyOwner.reset(); // 기다리던 캐시 답이 이 객체를 부르지 않게 지운다
        _mapReadyRequestToRead.clear();
        _mapRequestToQuery.clear();
        _mapNameRequestToIndex.clear();
        _mapQuery.clear();
        _mapRankKeyToWaitingRead.clear();
        _mapRankKeyToLateWrite.clear();
        _mapJobToRun.clear();
        _mapAccountStats.clear();
        _mapSettlement.clear(); // 남은 페이지는 맡기지 않는다 — 결과가 있는 사람은 건너뛰므로 다음 실행이 이어서 처리한다
        _uniqueExpiringRankKeys.clear();
        _dependencies = LeaderboardServiceDependencies{};
        _pScheduler   = nullptr;
        _store.bindStore( nullptr );
    }

    bool LeaderboardService::registerBoard( const LeaderboardDef& definition )
    {
        const bool bValid = LeaderboardNameRule::isValidID( definition._boardID ) &&
                            ( definition._sourceStat.empty() || LeaderboardNameRule::isValidID( definition._sourceStat ) ) &&
                            definition._order < LeaderboardOrder::Count && definition._update < LeaderboardUpdate::Count && definition._reset < LeaderboardReset::Count;
        // 통계에 연결된 보드는 통계의 누적값을 받는다 — Sum 이면 누적값을 또 더해 부풀린다.
        const bool bSourceOk = definition._sourceStat.empty() || definition._update != LeaderboardUpdate::Sum;
        if ( bValid == false || bSourceOk == false || LeaderboardServiceInternal::areSeasonsValid( definition ) == false )
            return false;
        const bool bAlreadyRegistered  = _mapBoard.find( definition._boardID ) != _mapBoard.end();
        _mapBoard[definition._boardID] = definition;
        const bool bSkipSeasonJobs     = definition._reset != LeaderboardReset::Season || _pScheduler == nullptr || bAlreadyRegistered;
        if ( bSkipSeasonJobs )
            return true; // 같은 보드를 다시 등록해도 정산 작업은 한 번만 만든다
        for ( const LeaderboardSeason& season : definition._listSeason )
        {
            ScheduleDef job;
            job._jobID         = makeSettlementJobID( definition._boardID, season._seasonID );
            job._kind          = ScheduleKind::Window;
            job._windowStartMs = season._endMs;
            job._windowEndMs   = season._endMs + LeaderboardServiceInternal::kSettlementWindowMs;
            job._leaseMs       = LeaderboardServiceInternal::kSettlementLeaseMs;
            if ( _pScheduler->registerJob( job, ScheduledJobFunction::create<&LeaderboardService::onScheduledRun>( this ) ) == false )
                return false;
        }
        return true;
    }

    bool LeaderboardService::registerAchievement( const OnlineAchievementDef& definition )
    {
        const bool bRewardOk = definition._rewardAssetID.empty() || ( definition._rewardAmount > 0 && definition._mailTitleKey.empty() == false );
        const bool bValid    = LeaderboardNameRule::isValidID( definition._achievementID ) && LeaderboardNameRule::isValidID( definition._statName ) && bRewardOk;
        if ( bValid == false )
            return false;
        _mapAchievement[definition._achievementID] = definition;
        return true;
    }

    string LeaderboardService::makeSettlementJobID( string_view boardID, uint32 seasonID )
    {
        return string( LeaderboardServiceInternal::kSettlementJobPrefix ) + string( boardID ) + "." + LeaderboardServiceInternal::makeSeasonHex( seasonID );
    }

    const LeaderboardDef* LeaderboardService::findBoard( string_view boardID ) const
    {
        const auto boardIt = _mapBoard.find( string( boardID ) );
        return boardIt != _mapBoard.end() ? &boardIt->second : nullptr;
    }

    uint64 LeaderboardService::computePeriodID( const LeaderboardDef& definition, UnixMilliseconds nowMs ) const
    {
        ScheduleDef schedule;
        schedule._minuteOfDay = definition._resetMinuteOfDay;
        schedule._dayOfWeek   = definition._resetDayOfWeek;
        switch ( definition._reset )
        {
            case LeaderboardReset::Daily:
            {
                schedule._kind = ScheduleKind::Daily;
                return static_cast<uint64>( MathUtil::max<int64>( 0, ServiceScheduler::computeLatestOccurrence( schedule, nowMs.count() ) ) );
            }
            case LeaderboardReset::Weekly:
            {
                schedule._kind = ScheduleKind::Weekly;
                return static_cast<uint64>( MathUtil::max<int64>( 0, ServiceScheduler::computeLatestOccurrence( schedule, nowMs.count() ) ) );
            }
            case LeaderboardReset::Season:
            {
                for ( const LeaderboardSeason& season : definition._listSeason )
                {
                    if ( season._startMs <= nowMs.count() && nowMs.count() < season._endMs )
                        return season._seasonID;
                }
                return 0; // 시즌 밖이므로 제출은 NotRanked 로 끝난다
            }
            default:
            {
                return 0;
            }
        }
    }

    string LeaderboardService::makeRankKey( string_view boardID, uint64 periodID )
    {
        return string( "lb/" ) + string( boardID ) + "/" + ServiceKeyUtil::makeHex64( periodID );
    }

    void LeaderboardService::submitScore( string_view boardID, AccountID accountID, string_view displayName, int64 score, UnixMilliseconds nowMs, uint64 requestTag )
    {
        const LeaderboardDef* pBoard = findBoard( boardID );
        if ( pBoard == nullptr )
        {
            pushCompletion( requestTag, LeaderboardOperation::Submit, LeaderboardResult::UnknownBoard );
            return;
        }
        const bool bValid = -LeaderboardLimit::kMaxAbsScore <= score && score <= LeaderboardLimit::kMaxAbsScore &&
                            displayName.size() <= static_cast<size_t>( OnlineLimit::kMaxDisplayNameSize ) && accountID != kInvalidAccountID;
        if ( bValid == false )
        {
            pushCompletion( requestTag, LeaderboardOperation::Submit, LeaderboardServiceResult::fail( OnlineError::kInvalidRequest ) );
            return;
        }
        const uint64 periodID = computePeriodID( *pBoard, nowMs );
        if ( pBoard->_reset == LeaderboardReset::Season && periodID == 0 )
        {
            pushCompletion( requestTag, LeaderboardOperation::Submit, LeaderboardResult::NotRanked );
            return;
        }
        LeaderboardWriteInput input;
        input._table       = LeaderboardServiceInternal::getScoreTable();
        input._key         = LeaderboardServiceInternal::makeScorePrefix( pBoard->_boardID, periodID ) + ServiceKeyUtil::makeHex64( accountID );
        input._displayName = string( displayName );
        input._value       = score;
        input._nowMs       = nowMs;
        input._update      = pBoard->_update;
        input._order       = pBoard->_order;
        submitWrite( input, pBoard->_boardID, accountID, periodID, requestTag, LeaderboardOperation::Submit );
    }

    void LeaderboardService::submitWrite( const LeaderboardWriteInput& input, const string& boardID, AccountID accountID, uint64 periodID, uint64 requestTag,
                                          LeaderboardOperation operation )
    {
        _store.submit( [input]( IServiceStoreConnection& connection )
        { return LeaderboardStoreRun::writeValue( connection, input ); },
                       [this, boardID, displayName = input._displayName, nowMs = input._nowMs, accountID, periodID, requestTag, operation]( const LeaderboardWriteResult& write )
        { applyScoreWrite( boardID, periodID, accountID, displayName, write._finalValue, write._bChanged == SW_TRUE, write._result, requestTag, operation, nowMs ); } );
    }

    void LeaderboardService::changeStat( AccountID accountID, string_view displayName, string_view statName, int64 value, LeaderboardUpdate update, UnixMilliseconds nowMs,
                                         uint64 requestTag )
    {
        const bool bValid = LeaderboardNameRule::isValidID( statName ) && update < LeaderboardUpdate::Count && accountID != kInvalidAccountID &&
                          -LeaderboardLimit::kMaxAbsScore <= value && value <= LeaderboardLimit::kMaxAbsScore;
        if ( bValid == false )
        {
            pushCompletion( requestTag, LeaderboardOperation::StatChange, LeaderboardServiceResult::fail( OnlineError::kInvalidRequest ) );
            return;
        }
        LeaderboardWriteInput input;
        input._table       = LeaderboardServiceInternal::getStatTable();
        input._key         = ServiceKeyUtil::makeHex64( accountID ) + "/" + string( statName );
        input._displayName = string( displayName );
        input._value       = value;
        input._nowMs       = nowMs;
        input._update      = update;
        input._order       = LeaderboardOrder::Descending; // 통계의 Best 는 큰 값이다
        submitWrite( input, string( statName ), accountID, 0, requestTag, LeaderboardOperation::StatChange );
    }

    void LeaderboardService::applyScoreWrite( const string& boardID, uint64 periodID, AccountID accountID, const string& displayName, int64 finalScore, bool bChanged,
                                              LeaderboardServiceResult result, uint64 requestTag, LeaderboardOperation operation, UnixMilliseconds nowMs )
    {
        if ( requestTag != 0 )
        {
            LeaderboardCompletion completion;
            completion._requestTag = requestTag;
            completion._operation  = operation;
            completion._result     = result;
            completion._score      = finalScore;
            completion._periodID   = periodID;
            _completionBuffer.push( std::move( completion ) );
        }
        if ( result != LeaderboardResult::Ok || bChanged == false )
            return;
        if ( operation == LeaderboardOperation::StatChange )
        {
            const auto statsIt = _mapAccountStats.find( accountID );
            if ( statsIt != _mapAccountStats.end() && statsIt->second._bLoaded == SW_TRUE )
                statsIt->second._mapValue[boardID] = finalScore; // 접속한 계정의 메모리 통계를 같이 고친다(매칭 실력)
            for ( const auto& [id, board] : _mapBoard )          // 통계에 연결된 보드에 같은 값을 낸다. 요청 태그가 0이라 완료가 없다
            {
                if ( board._sourceStat == boardID )
                    submitScore( board._boardID, accountID, displayName, finalScore, nowMs, 0 );
            }
            for ( const auto& [achievementID, achievement] : _mapAchievement ) // 임계값을 넘은 업적이다. 이미 달성했으면 작업은 아무것도 하지 않는다
            {
                if ( achievement._statName == boardID && finalScore >= achievement._threshold )
                {
                    _store.submit( [achievement = achievement, accountID, nowMs]( IServiceStoreConnection& connection )
                    { return LeaderboardStoreRun::unlockAchievement( connection, achievement, accountID, nowMs ); },
                                   [this, accountID, state = AchievementState{ achievement._achievementID, nowMs.count() }]( bool bUnlocked )
                    { applyAchievementUnlock( accountID, state, bUnlocked ); } );
                }
            }
            return;
        }
        const LeaderboardDef* pBoard = findBoard( boardID );
        if ( pBoard == nullptr || _dependencies._pRouter == nullptr )
            return;
        const string rankKey = makeRankKey( boardID, periodID );
        submitCacheScore( rankKey, accountID, toCacheScore( pBoard->_order, finalScore ) );
        // 기간 보드의 정렬 집합은 기간이 끝나면 캐시가 지우게 한다. 기간마다 이 프로세스에서 한 번만 건다(점수 쓰기마다 요청이 늘지 않게).
        const int64 retentionMs = computeRankRetentionMs( *pBoard, periodID, nowMs );
        if ( retentionMs > 0 && _uniqueExpiringRankKeys.insert( rankKey ).second )
            _dependencies._pRouter->submit( EphemeralRequest::makeExpire( rankKey, retentionMs ) );
        if ( displayName.empty() == false )
            _dependencies._pRouter->submit( EphemeralRequest::makeSet( LeaderboardServiceInternal::makeNameKey( accountID ),
                                                                       vector<uint8>( reinterpret_cast<const uint8*>( displayName.data() ), reinterpret_cast<const uint8*>( displayName.data() ) + displayName.size() ), LeaderboardLimit::kNameTTLMs ) );
    }

    int64 LeaderboardService::computeRankRetentionMs( const LeaderboardDef& definition, uint64 periodID, UnixMilliseconds nowMs ) const
    {
        using Internal    = LeaderboardServiceInternal;
        int64 periodEndMs = 0;
        switch ( definition._reset )
        {
            case LeaderboardReset::Daily:
            {
                periodEndMs = static_cast<int64>( periodID ) + Internal::kDayMs; // 기간 id 는 기간 시작 시각이다
                break;
            }
            case LeaderboardReset::Weekly:
            {
                periodEndMs = static_cast<int64>( periodID ) + Internal::kWeekMs;
                break;
            }
            case LeaderboardReset::Season:
            {
                for ( const LeaderboardSeason& season : definition._listSeason )
                {
                    if ( season._seasonID == periodID )
                        periodEndMs = season._endMs;
                }
                break;
            }
            default:
            {
                return 0; // 초기화가 없는 보드의 순위는 하나뿐이다 — 지우지 않는다
            }
        }
        if ( periodEndMs <= 0 )
            return 0;
        return MathUtil::max<int64>( 1, periodEndMs + Internal::kRankRetentionGraceMs - nowMs.count() );
    }

    void LeaderboardService::submitCacheScore( const string& rankKey, AccountID accountID, int64 cacheScore )
    {
        const auto waitIt = _mapRankKeyToWaitingRead.find( rankKey );
        if ( waitIt != _mapRankKeyToWaitingRead.end() )
            _mapRankKeyToLateWrite[rankKey].push_back( LeaderboardScoreRow{ accountID, cacheScore } ); // 다시 채우는 중이므로 채운 뒤에 한 번 더 싣는다. 이전 값을 덮기 위해서이다
        _dependencies._pRouter->submit( EphemeralRequest::makeScoreSet( rankKey, LeaderboardServiceInternal::makeRankMember( accountID ), cacheScore ) );
    }

    void LeaderboardService::readTop( string_view boardID, int32 offset, int32 count, UnixMilliseconds nowMs, uint64 requestTag )
    {
        PendingRead read;
        read._boardID                = string( boardID );
        read._requestTag             = requestTag;
        read._offset                 = offset;
        read._count                  = count;
        read._operation              = LeaderboardOperation::Top;
        const LeaderboardDef* pBoard = findBoard( boardID );
        const bool            bRange = 0 <= offset && 1 <= count && count <= LeaderboardLimit::kMaxPage;
        if ( pBoard == nullptr || bRange == false )
        {
            completeRead( read, pBoard == nullptr ? LeaderboardResult::UnknownBoard : LeaderboardServiceResult::fail( OnlineError::kInvalidRequest ) );
            return;
        }
        read._periodID = computePeriodID( *pBoard, nowMs );
        read._nowMs    = nowMs;
        startRead( read );
    }

    void LeaderboardService::readAround( string_view boardID, AccountID accountID, int32 radius, UnixMilliseconds nowMs, uint64 requestTag )
    {
        PendingRead read;
        read._boardID                = string( boardID );
        read._accountID              = accountID;
        read._requestTag             = requestTag;
        read._count                  = radius;
        read._operation              = LeaderboardOperation::Around;
        const LeaderboardDef* pBoard = findBoard( boardID );
        const bool            bRange = 0 <= radius && radius <= LeaderboardLimit::kMaxAround;
        if ( pBoard == nullptr || bRange == false )
        {
            completeRead( read, pBoard == nullptr ? LeaderboardResult::UnknownBoard : LeaderboardServiceResult::fail( OnlineError::kInvalidRequest ) );
            return;
        }
        read._periodID = computePeriodID( *pBoard, nowMs );
        read._nowMs    = nowMs;
        startRead( read );
    }

    void LeaderboardService::startRead( const PendingRead& read )
    {
        const string rankKey = makeRankKey( read._boardID, read._periodID );
        const auto   waitIt  = _mapRankKeyToWaitingRead.find( rankKey );
        if ( waitIt != _mapRankKeyToWaitingRead.end() )
        {
            waitIt->second.push_back( read ); // 다시 채우는 중이므로 대기시킨다
            return;
        }
        const uint64 requestID            = _dependencies._pRouter->submit( EphemeralRequest::makeGet( LeaderboardServiceInternal::makeReadyKey( rankKey ) ),
                                                                            EphemeralStoreRouter::ReplyDelegate::create<&LeaderboardService::onReadyReply>( this ), _replyOwner );
        _mapReadyRequestToRead[requestID] = read;
    }

    void LeaderboardService::onReadyReply( const EphemeralReply& reply )
    {
        const auto readIt = _mapReadyRequestToRead.find( reply._requestID );
        if ( readIt == _mapReadyRequestToRead.end() )
            return;
        const PendingRead read = readIt->second;
        _mapReadyRequestToRead.erase( readIt );
        if ( reply._result == EphemeralResult::Unavailable || reply._result == EphemeralResult::Invalid )
        {
            completeRead( read, LeaderboardServiceResult::fail( OnlineError::kUnavailable ) );
            return;
        }
        const string rankKey = makeRankKey( read._boardID, read._periodID );
        if ( reply._result == EphemeralResult::NotFound ) // 캐시를 잃었거나 처음이므로 영속 저장소에서 다시 채운다
        {
            vector<PendingRead>& listWaiting = _mapRankKeyToWaitingRead[rankKey];
            listWaiting.push_back( read );
            if ( listWaiting.size() == 1 )
            {
                _store.submit( [scorePrefix = LeaderboardServiceInternal::makeScorePrefix( read._boardID, read._periodID )]( IServiceStoreConnection& connection )
                {
                    LeaderboardRead<vector<LeaderboardScoreRow>> rebuild;
                    if ( LeaderboardServiceInternal::readAllScores( connection, scorePrefix, rebuild._value ) )
                        rebuild._result = LeaderboardResult::Ok;
                    return rebuild;
                }, [this, rankKey]( LeaderboardRead<vector<LeaderboardScoreRow>>& rebuild )
                { applyRebuildRead( rankKey, std::move( rebuild._value ), rebuild._result == LeaderboardResult::Ok ); } );
            }
            return;
        }
        const uint64 queryID = _queryIDAllocator.allocate();
        RankQuery&   query   = _mapQuery[queryID];
        query._read          = read;
        if ( read._operation == LeaderboardOperation::Top )
        {
            const uint64 requestID        = _dependencies._pRouter->submit( EphemeralRequest::makeScoreRange( rankKey, read._offset, read._count ),
                                                                            EphemeralStoreRouter::ReplyDelegate::create<&LeaderboardService::onRangeReply>( this ), _replyOwner );
            _mapRequestToQuery[requestID] = queryID;
            return;
        }
        const uint64 requestID        = _dependencies._pRouter->submit( EphemeralRequest::makeScoreRank( rankKey, LeaderboardServiceInternal::makeRankMember( read._accountID ) ),
                                                                        EphemeralStoreRouter::ReplyDelegate::create<&LeaderboardService::onRankReply>( this ), _replyOwner );
        _mapRequestToQuery[requestID] = queryID;
    }

    void LeaderboardService::applyRebuildRead( const string& rankKey, vector<LeaderboardScoreRow>&& listScore, bool bReadOk )
    {
        vector<PendingRead>         listWaiting;
        vector<LeaderboardScoreRow> listLateWrite;
        const auto                  waitIt = _mapRankKeyToWaitingRead.find( rankKey );
        if ( waitIt != _mapRankKeyToWaitingRead.end() )
        {
            listWaiting.swap( waitIt->second );
            _mapRankKeyToWaitingRead.erase( waitIt );
        }
        const auto lateIt = _mapRankKeyToLateWrite.find( rankKey );
        if ( lateIt != _mapRankKeyToLateWrite.end() )
        {
            listLateWrite.swap( lateIt->second );
            _mapRankKeyToLateWrite.erase( lateIt );
        }
        if ( bReadOk == false || _dependencies._pRouter == nullptr )
        {
            for ( const PendingRead& read : listWaiting )
            {
                completeRead( read, LeaderboardServiceResult::fail( OnlineError::kUnavailable ) );
            }
            return;
        }
        const LeaderboardDef*  pBoard = listWaiting.empty() ? nullptr : findBoard( listWaiting.front()._boardID );
        const LeaderboardOrder order  = pBoard != nullptr ? pBoard->_order : LeaderboardOrder::Descending;
        for ( const LeaderboardScoreRow& row : listScore )
        {
            _dependencies._pRouter->submit( EphemeralRequest::makeScoreSet( rankKey, LeaderboardServiceInternal::makeRankMember( row._accountID ), toCacheScore( order, row._score ) ) );
        }
        for ( const LeaderboardScoreRow& row : listLateWrite ) // 다시 채우는 동안 쓴 점수이다. 영속 저장소에서 읽은 이전 값을 덮으며 이미 캐시 점수이다
        {
            _dependencies._pRouter->submit( EphemeralRequest::makeScoreSet( rankKey, LeaderboardServiceInternal::makeRankMember( row._accountID ), row._score ) );
        }
        // 기간 보드면 다시 채운 집합과 준비 표시도 기간이 끝나면 사라지게 한다(0 이면 만료 없음).
        const int64 retentionMs = pBoard != nullptr ? computeRankRetentionMs( *pBoard, listWaiting.front()._periodID, listWaiting.front()._nowMs ) : 0;
        if ( retentionMs > 0 )
            _dependencies._pRouter->submit( EphemeralRequest::makeExpire( rankKey, retentionMs ) );
        _dependencies._pRouter->submit( EphemeralRequest::makeSet( LeaderboardServiceInternal::makeReadyKey( rankKey ), vector<uint8>{ 1 }, retentionMs ) );
        // 캐시는 맡긴 순서대로 응답한다. 아래 읽기는 위 쓰기 뒤에 처리된다
        for ( const PendingRead& read : listWaiting )
        {
            startRead( read );
        }
    }

    void LeaderboardService::onRankReply( const EphemeralReply& reply )
    {
        const auto queryIDIt = _mapRequestToQuery.find( reply._requestID );
        if ( queryIDIt == _mapRequestToQuery.end() )
            return;
        const uint64 queryID = queryIDIt->second;
        _mapRequestToQuery.erase( queryIDIt );
        const auto queryIt = _mapQuery.find( queryID );
        if ( queryIt == _mapQuery.end() )
            return;
        RankQuery& query = queryIt->second;
        if ( reply._result != EphemeralResult::Ok )
        {
            completeRead( query._read, reply._result == EphemeralResult::NotFound ? LeaderboardResult::NotRanked : LeaderboardServiceResult::fail( OnlineError::kUnavailable ) );
            _mapQuery.erase( queryIt );
            return;
        }
        const int32 myIndex           = static_cast<int32>( reply._integer );
        const int32 radius            = query._read._count;
        const int32 offset            = MathUtil::max( 0, myIndex - radius );
        const int32 count             = myIndex + radius + 1 - offset;
        query._read._offset           = offset;
        const uint64 requestID        = _dependencies._pRouter->submit( EphemeralRequest::makeScoreRange( makeRankKey( query._read._boardID, query._read._periodID ), offset, count ),
                                                                        EphemeralStoreRouter::ReplyDelegate::create<&LeaderboardService::onRangeReply>( this ), _replyOwner );
        _mapRequestToQuery[requestID] = queryID;
    }

    void LeaderboardService::onRangeReply( const EphemeralReply& reply )
    {
        const auto queryIDIt = _mapRequestToQuery.find( reply._requestID );
        if ( queryIDIt == _mapRequestToQuery.end() )
            return;
        const uint64 queryID = queryIDIt->second;
        _mapRequestToQuery.erase( queryIDIt );
        const auto queryIt = _mapQuery.find( queryID );
        if ( queryIt == _mapQuery.end() )
            return;
        RankQuery& query = queryIt->second;
        if ( reply._result != EphemeralResult::Ok && reply._result != EphemeralResult::NotFound )
        {
            completeRead( query._read, LeaderboardServiceResult::fail( OnlineError::kUnavailable ) );
            _mapQuery.erase( queryIt );
            return;
        }
        const LeaderboardDef*  pBoard = findBoard( query._read._boardID );
        const LeaderboardOrder order  = pBoard != nullptr ? pBoard->_order : LeaderboardOrder::Descending;
        for ( size_t memberIndex = 0; memberIndex < reply._listMember.size(); ++memberIndex )
        {
            LeaderboardEntry entry;
            if ( LeaderboardServiceInternal::parseRankMember( reply._listMember[memberIndex]._member, entry._accountID ) == false )
                continue;
            entry._score = toCacheScore( order, reply._listMember[memberIndex]._score ); // 같은 함수로 부호를 되돌린다
            entry._rank  = query._read._offset + static_cast<int32>( memberIndex ) + 1;
            query._listEntry.push_back( std::move( entry ) );
        }
        for ( int32 entryIndex = 0; entryIndex < static_cast<int32>( query._listEntry.size() ); ++entryIndex )
        {
            const uint64 requestID = _dependencies._pRouter->submit(
                EphemeralRequest::makeGet( LeaderboardServiceInternal::makeNameKey( query._listEntry[static_cast<size_t>( entryIndex )]._accountID ) ),
                EphemeralStoreRouter::ReplyDelegate::create<&LeaderboardService::onNameReply>( this ), _replyOwner );
            _mapRequestToQuery[requestID]     = queryID;
            _mapNameRequestToIndex[requestID] = entryIndex;
            ++query._outstandingNameCount;
        }
        if ( query._outstandingNameCount == 0 )
            finishQuery( queryID );
    }

    void LeaderboardService::onNameReply( const EphemeralReply& reply )
    {
        const auto queryIDIt = _mapRequestToQuery.find( reply._requestID );
        const auto indexIt   = _mapNameRequestToIndex.find( reply._requestID );
        if ( queryIDIt == _mapRequestToQuery.end() || indexIt == _mapNameRequestToIndex.end() )
            return;
        const uint64 queryID    = queryIDIt->second;
        const int32  entryIndex = indexIt->second;
        _mapRequestToQuery.erase( queryIDIt );
        _mapNameRequestToIndex.erase( indexIt );
        const auto queryIt = _mapQuery.find( queryID );
        if ( queryIt == _mapQuery.end() )
            return;
        RankQuery& query = queryIt->second;
        if ( reply._result == EphemeralResult::Ok )
            query._listEntry[static_cast<size_t>( entryIndex )]._displayName.assign( reply._value.begin(), reply._value.end() );
        if ( --query._outstandingNameCount == 0 )
            finishQuery( queryID );
    }

    void LeaderboardService::finishQuery( uint64 queryID )
    {
        const auto queryIt = _mapQuery.find( queryID );
        if ( queryIt == _mapQuery.end() )
            return;
        LeaderboardCompletion completion;
        completion._requestTag = queryIt->second._read._requestTag;
        completion._operation  = queryIt->second._read._operation;
        completion._periodID   = queryIt->second._read._periodID;
        completion._listEntry  = std::move( queryIt->second._listEntry );
        _completionBuffer.push( std::move( completion ) );
        _mapQuery.erase( queryIt );
    }

    void LeaderboardService::completeRead( const PendingRead& read, LeaderboardServiceResult result )
    {
        LeaderboardCompletion completion;
        completion._requestTag = read._requestTag;
        completion._operation  = read._operation;
        completion._periodID   = read._periodID;
        completion._result     = result;
        _completionBuffer.push( std::move( completion ) );
    }

    void LeaderboardService::pushCompletion( uint64 requestTag, LeaderboardOperation operation, LeaderboardServiceResult result )
    {
        if ( requestTag == 0 )
            return;
        LeaderboardCompletion completion;
        completion._requestTag = requestTag;
        completion._operation  = operation;
        completion._result     = result;
        _completionBuffer.push( std::move( completion ) );
    }

    void LeaderboardService::readStats( AccountID accountID, uint64 requestTag )
    {
        _store.submit( [accountID]( IServiceStoreConnection& connection )
        { return LeaderboardStoreRun::readStats( connection, accountID ); },
                       [this, requestTag]( LeaderboardRead<vector<LeaderboardStat>>& read )
        { applyStats( requestTag, read._result, std::move( read._value ) ); } );
    }

    void LeaderboardService::retainStats( AccountID accountID )
    {
        AccountStats& stats = _mapAccountStats[accountID];
        ++stats._retainCount;
        if ( stats._bLoaded == SW_TRUE || stats._bLoading == SW_TRUE )
            return;
        stats._bLoading = SW_TRUE;
        _store.submit( [accountID]( IServiceStoreConnection& connection )
        { return LeaderboardStoreRun::readStats( connection, accountID ); },
                       [this, accountID]( LeaderboardRead<vector<LeaderboardStat>>& read, ServiceStoreCompletion completion )
        { applyStatCache( accountID, read._result == LeaderboardResult::Ok, completion, std::move( read._value ) ); } );
    }

    void LeaderboardService::releaseStats( AccountID accountID )
    {
        const auto statsIt = _mapAccountStats.find( accountID );
        if ( statsIt != _mapAccountStats.end() && --statsIt->second._retainCount <= 0 )
            _mapAccountStats.erase( statsIt );
    }

    void LeaderboardService::applyStatCache( AccountID accountID, bool bReadOk, ServiceStoreCompletion completion, vector<LeaderboardStat>&& listStat )
    {
        const auto statsIt = _mapAccountStats.find( accountID );
        if ( statsIt == _mapAccountStats.end() )
            return; // 그사이 떠났다
        statsIt->second._bLoading = SW_FALSE;
        if ( bReadOk == false )
        {
            if ( completion.isCancelled() == false ) // 종료 때문에 취소된 읽기는 경고하지 않는다
                SW_LOG_WARNING_ID( "LeaderboardService.LeaderboardStatsAccountCould", "Leaderboard: stats of account %# could not be read - matchmaking reports RatingUnavailable until it reconnects", accountID );
            return;
        }
        for ( LeaderboardStat& stat : listStat )
        {
            statsIt->second._mapValue[std::move( stat._name )] = stat._value;
        }
        statsIt->second._bLoaded = SW_TRUE;
    }

    AccountStatLookup LeaderboardService::findStat( AccountID accountID, string_view statName, int64& outValue ) const
    {
        const auto statsIt = _mapAccountStats.find( accountID );
        if ( statsIt == _mapAccountStats.end() || statsIt->second._bLoaded == SW_FALSE )
            return AccountStatLookup::NotLoaded;
        const auto valueIt = statsIt->second._mapValue.find( string( statName ) );
        if ( valueIt == statsIt->second._mapValue.end() )
            return AccountStatLookup::Missing;
        outValue = valueIt->second;
        return AccountStatLookup::Found;
    }

    void LeaderboardService::applyStats( uint64 requestTag, LeaderboardServiceResult result, vector<LeaderboardStat>&& listStat )
    {
        LeaderboardCompletion completion;
        completion._requestTag = requestTag;
        completion._operation  = LeaderboardOperation::Stats;
        completion._result     = result;
        completion._listStat   = std::move( listStat );
        _completionBuffer.push( std::move( completion ) );
    }

    void LeaderboardService::readAchievements( AccountID accountID, uint64 requestTag )
    {
        _store.submit( [accountID]( IServiceStoreConnection& connection )
        { return LeaderboardStoreRun::readAchievements( connection, accountID ); },
                       [this, requestTag]( LeaderboardRead<vector<AchievementState>>& read )
        { applyAchievements( requestTag, read._result, std::move( read._value ) ); } );
    }

    void LeaderboardService::applyAchievements( uint64 requestTag, LeaderboardServiceResult result, vector<AchievementState>&& listAchievement )
    {
        LeaderboardCompletion completion;
        completion._requestTag      = requestTag;
        completion._operation       = LeaderboardOperation::Achievements;
        completion._result          = result;
        completion._listAchievement = std::move( listAchievement );
        _completionBuffer.push( std::move( completion ) );
    }

    void LeaderboardService::readAchievementProgress( AccountID accountID, uint64 requestTag )
    {
        vector<OnlineAchievementDef> listDefinition;
        listDefinition.reserve( _mapAchievement.size() );
        for ( const auto& [achievementID, definition] : _mapAchievement )
        {
            (void)achievementID;
            listDefinition.push_back( definition );
        }
        VectorUtil::sort( listDefinition, LeaderboardServiceInternal::isAchievementIDLess );
        _store.submit( [listDefinition = std::move( listDefinition ), accountID]( IServiceStoreConnection& connection )
        { return LeaderboardStoreRun::readAchievementProgress( connection, listDefinition, accountID ); },
                       [this, requestTag]( LeaderboardRead<vector<AchievementProgress>>& read )
        { applyAchievementProgress( requestTag, read._result, std::move( read._value ) ); } );
    }

    void LeaderboardService::applyAchievementProgress( uint64 requestTag, LeaderboardServiceResult result, vector<AchievementProgress>&& listProgress )
    {
        LeaderboardCompletion completion;
        completion._requestTag   = requestTag;
        completion._operation    = LeaderboardOperation::AchievementProgress;
        completion._result       = result;
        completion._listProgress = std::move( listProgress );
        _completionBuffer.push( std::move( completion ) );
    }

    void LeaderboardService::readScoresOf( string_view boardID, const vector<AccountID>& listAccountID, UnixMilliseconds nowMs, uint64 requestTag )
    {
        const LeaderboardDef* pBoard = findBoard( boardID );
        if ( pBoard == nullptr )
        {
            pushCompletion( requestTag, LeaderboardOperation::ScoresOf, LeaderboardResult::UnknownBoard );
            return;
        }
        if ( listAccountID.empty() || listAccountID.size() > static_cast<size_t>( LeaderboardLimit::kMaxScoresOfCount ) )
        {
            pushCompletion( requestTag, LeaderboardOperation::ScoresOf, LeaderboardServiceResult::fail( OnlineError::kInvalidRequest ) );
            return;
        }
        vector<AccountID> listUnique = listAccountID;
        VectorUtil::sortUnique( listUnique ); // 같은 계정을 두 번 세지 않는다
        const uint64 periodID = computePeriodID( *pBoard, nowMs );
        _store.submit( [prefix = LeaderboardServiceInternal::makeScorePrefix( boardID, periodID ), listUnique = std::move( listUnique ), order = pBoard->_order]( IServiceStoreConnection& connection )
        { return LeaderboardStoreRun::readScoresOf( connection, prefix, listUnique, order ); },
                       [this, requestTag, periodID]( LeaderboardRead<vector<LeaderboardEntry>>& read )
        { applyScores( requestTag, periodID, read._result, std::move( read._value ) ); } );
    }

    void LeaderboardService::applyScores( uint64 requestTag, uint64 periodID, LeaderboardServiceResult result, vector<LeaderboardEntry>&& listEntry )
    {
        LeaderboardCompletion completion;
        completion._requestTag = requestTag;
        completion._operation  = LeaderboardOperation::ScoresOf;
        completion._result     = result;
        completion._periodID   = periodID;
        completion._listEntry  = std::move( listEntry );
        _completionBuffer.push( std::move( completion ) );
    }

    void LeaderboardService::applyAchievementUnlock( AccountID accountID, const AchievementState& achievement, bool bUnlocked )
    {
        if ( bUnlocked )
            _unlockBuffer.push( AchievementUnlock{ achievement, accountID } );
    }

    void LeaderboardService::readSeasonResult( string_view boardID, uint32 seasonID, AccountID accountID, uint64 requestTag )
    {
        const LeaderboardDef* pBoard = findBoard( boardID );
        if ( pBoard == nullptr || pBoard->_reset != LeaderboardReset::Season )
        {
            pushCompletion( requestTag, LeaderboardOperation::SeasonResult, pBoard == nullptr ? LeaderboardResult::UnknownBoard : LeaderboardServiceResult::fail( OnlineError::kInvalidRequest ) );
            return;
        }
        _store.submit( [key = LeaderboardServiceInternal::makeSeasonResultKey( pBoard->_boardID, seasonID, accountID ), accountID]( IServiceStoreConnection& connection )
        { return LeaderboardStoreRun::readSeasonResult( connection, key, accountID ); },
                       [this, requestTag, seasonID]( const LeaderboardRead<LeaderboardEntry>& read )
        { applySeasonResult( requestTag, seasonID, read._result, read._value ); } );
    }

    void LeaderboardService::applySeasonResult( uint64 requestTag, uint32 seasonID, LeaderboardServiceResult result, const LeaderboardEntry& entry )
    {
        LeaderboardCompletion completion;
        completion._requestTag = requestTag;
        completion._operation  = LeaderboardOperation::SeasonResult;
        completion._result     = result;
        completion._periodID   = seasonID;
        if ( result == LeaderboardResult::Ok )
            completion._listEntry.push_back( entry );
        _completionBuffer.push( std::move( completion ) );
    }

    void LeaderboardService::settleSeason( string_view boardID, uint32 seasonID, UnixMilliseconds nowMs, const SettlementDelegate& onDone )
    {
        const LeaderboardDef*    pBoard  = findBoard( boardID );
        const LeaderboardSeason* pSeason = nullptr;
        if ( pBoard != nullptr )
        {
            for ( const LeaderboardSeason& season : pBoard->_listSeason )
            {
                if ( season._seasonID == seasonID )
                    pSeason = &season;
            }
        }
        if ( pSeason == nullptr )
        {
            if ( onDone.isBound() )
                onDone( false );
            return;
        }
        startSettlement( *pBoard, *pSeason, string{}, nowMs, onDone );
    }

    void LeaderboardService::onScheduledRun( const ScheduledRun& run )
    {
        // 작업 id 는 "lb.settle.<보드>.<시즌 16진 8자리>" 이다. 보드 id 에 '.' 가 없으므로 마지막 '.' 뒤가 시즌이다
        const string_view jobID( run._jobID );
        const string_view prefix( LeaderboardServiceInternal::kSettlementJobPrefix );
        const size_t      lastDot  = jobID.rfind( '.' );
        uint64            seasonID = 0;
        const bool        bParsed  = StringUtil::startsWith( jobID, prefix ) && lastDot != string_view::npos && lastDot > prefix.size() &&
                             StringUtil::parseUint64( jobID.substr( lastDot + 1 ), seasonID, 16 );
        const LeaderboardDef*    pBoard  = bParsed ? findBoard( jobID.substr( prefix.size(), lastDot - prefix.size() ) ) : nullptr;
        const LeaderboardSeason* pSeason = nullptr;
        if ( pBoard != nullptr )
        {
            for ( const LeaderboardSeason& season : pBoard->_listSeason )
            {
                if ( season._seasonID == seasonID )
                    pSeason = &season;
            }
        }
        if ( pSeason == nullptr )
        {
            if ( _pScheduler != nullptr )
                _pScheduler->completeRun( run, false );
            return;
        }
        _mapJobToRun[run._jobID] = run; // 끝나면 completeRun 에 넘긴다. 작업마다 하나씩 두므로 보드 둘의 시즌이 같은 날 끝나도 섞이지 않는다
        startSettlement( *pBoard, *pSeason, run._jobID, run._occurrenceMs, SettlementDelegate{} );
    }

    void LeaderboardService::startSettlement( const LeaderboardDef& board, const LeaderboardSeason& season, string jobID, UnixMilliseconds nowMs, const SettlementDelegate& onDone )
    {
        const uint64        settlementID = _settlementIDAllocator.allocate();
        SettlementProgress& progress     = _mapSettlement[settlementID];
        progress._board                  = board;
        progress._season                 = season;
        progress._jobID                  = std::move( jobID );
        progress._onDone                 = onDone;
        progress._nowMs                  = nowMs;
        _store.submit( [scorePrefix = LeaderboardServiceInternal::makeScorePrefix( board._boardID, season._seasonID ), order = board._order]( IServiceStoreConnection& connection )
        { return LeaderboardStoreRun::readRanking( connection, scorePrefix, order ); },
                       [this, settlementID]( LeaderboardRead<vector<LeaderboardScoreRow>>& read )
        { applySettlementRanking( settlementID, read._result == LeaderboardResult::Ok, std::move( read._value ) ); } );
    }

    void LeaderboardService::applySettlementRanking( uint64 settlementID, bool bReadOk, vector<LeaderboardScoreRow>&& listRanked )
    {
        const auto progressIt = _mapSettlement.find( settlementID );
        if ( progressIt == _mapSettlement.end() )
            return; // 서비스가 종료된 뒤 회수한 작업이다
        if ( bReadOk == false )
        {
            finishSettlement( settlementID, false );
            return;
        }
        progressIt->second._listRanked = std::move( listRanked );
        submitNextSettlementPage( settlementID );
    }

    void LeaderboardService::applySettlementPage( uint64 settlementID, bool bCommitted )
    {
        if ( _mapSettlement.find( settlementID ) == _mapSettlement.end() )
            return;
        if ( bCommitted == false )
        {
            finishSettlement( settlementID, false );
            return;
        }
        submitNextSettlementPage( settlementID );
    }

    void LeaderboardService::submitNextSettlementPage( uint64 settlementID )
    {
        SettlementProgress& progress = _mapSettlement[settlementID];
        if ( progress._nextIndex >= progress._listRanked.size() || _store.hasStore() == false )
        {
            finishSettlement( settlementID, progress._nextIndex >= progress._listRanked.size() );
            return;
        }
        const size_t                first = progress._nextIndex;
        const size_t                last  = MathUtil::min( progress._listRanked.size(), first + static_cast<size_t>( LeaderboardServiceInternal::kPlayersPerPage ) );
        vector<LeaderboardScoreRow> listPage( progress._listRanked.begin() + static_cast<ptrdiff_t>( first ), progress._listRanked.begin() + static_cast<ptrdiff_t>( last ) );
        progress._nextIndex = last;
        _store.submit( [board = progress._board, season = progress._season, listPage = std::move( listPage ), firstRank = static_cast<int32>( first ) + 1,
                        nowMs = progress._nowMs]( IServiceStoreConnection& connection )
        { return LeaderboardStoreRun::writeSettlementPage( connection, board, season, listPage, firstRank, nowMs ); },
                       [this, settlementID]( bool bCommitted )
        { applySettlementPage( settlementID, bCommitted ); } );
    }

    void LeaderboardService::finishSettlement( uint64 settlementID, bool bSucceeded )
    {
        const auto progressIt = _mapSettlement.find( settlementID );
        if ( progressIt == _mapSettlement.end() )
            return;
        const string             jobID  = std::move( progressIt->second._jobID );
        const SettlementDelegate onDone = progressIt->second._onDone;
        _mapSettlement.erase( progressIt );
        applySettlement( jobID, onDone, bSucceeded );
    }

    void LeaderboardService::applySettlement( const string& jobID, const SettlementDelegate& onDone, bool bSucceeded )
    {
        if ( onDone.isBound() )
            onDone( bSucceeded );
        if ( jobID.empty() )
            return;
        const auto runIt = _mapJobToRun.find( jobID );
        if ( runIt == _mapJobToRun.end() )
            return;
        if ( _pScheduler != nullptr )
            _pScheduler->completeRun( runIt->second, bSucceeded );
        _mapJobToRun.erase( runIt );
    }
} // namespace sw
