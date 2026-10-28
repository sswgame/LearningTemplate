#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardService.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Guard/RequestLimits.h"
#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Mail/ServiceMail.h"
#include "GameFramework/Base/Online/Schedule/ServiceScheduler.h"
#include "GameFramework/Base/Online/Service/OnlineLimit.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"

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
            static bool areSeasonsValid( const LeaderboardDefinition& definition )
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

            static vector<uint8> encodeScore( int64 score, int64 updatedMs, string_view displayName )
            {
                BitWriter writer;
                writer.writeBits( kFormatVersion, 8 );
                writer.writeVarInt( score );
                writer.writeVarInt( updatedMs );
                ServiceKeyUtil::writeString( writer, displayName );
                return writer.getBytes();
            }

            static bool isAchievementIDLess( const AchievementDefinition& left, const AchievementDefinition& right ) { return left._achievementID < right._achievementID; }

            /** @brief 점수와 표시 이름을 읽습니다. 계정 목록 조회가 씁니다. */
            [[nodiscard]] static bool decodeScoreWithName( const vector<uint8>& bytes, int64& outScore, string& outDisplayName )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                if ( reader.readBits( 8 ) != kFormatVersion )
                    return false;
                outScore                               = reader.readVarInt();
                [[maybe_unused]] const int64 updatedMs = reader.readVarInt(); // 쓰지 않지만 읽어야 이름 필드에 닿는다. 넘쳤는지는 아래 hasOverflowed 가 본다
                return ServiceKeyUtil::readString( reader, RequestLimits::kMaxDisplayNameSize, outDisplayName ) && reader.hasOverflowed() == false;
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
                        return std::clamp( oldValue + value, -LeaderboardLimit::kMaxAbsScore, LeaderboardLimit::kMaxAbsScore );
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

        /** @brief 점수(또는 통계) 하나를 버전 조건으로 쓰는 작업입니다. 갱신 방식에 따라 새 값을 정합니다. */
        class LeaderboardWriteWork final : public IServiceStoreWork
        {
        public:
            hashed_string        _table{};
            string               _key{};
            string               _boardID{}; ///< 점수이면 보드 id 이고 통계이면 통계 이름이다. 완료에서 연결된 보드를 찾는 데 쓴다
            string               _displayName{};
            LeaderboardService*  _pService{ nullptr };
            AccountID            _accountID{ kInvalidAccountID };
            uint64               _periodID{ 0 };
            uint64               _requestTag{ 0 };
            int64                _value{ 0 };
            UnixMilliseconds     _nowMs{ 0 };
            int64                _finalValue{ 0 };
            LeaderboardUpdate    _update{ LeaderboardUpdate::Best };
            LeaderboardOrder     _order{ LeaderboardOrder::Descending };
            LeaderboardOperation _operation{ LeaderboardOperation::Submit };
            LeaderboardResult    _result{ LeaderboardResult::Unavailable };
            uint8                _bChanged{ SW_FALSE };

            void run( IServiceStoreConnection& connection ) override
            {
                using Internal = LeaderboardServiceInternal;
                for ( int32 attempt = 0; attempt < OnlineLimit::kMaxConflictRetry; ++attempt )
                {
                    ServiceRecord            record;
                    const ServiceStoreResult readResult = connection.readRecord( _table, _key, record );
                    if ( readResult != ServiceStoreResult::Ok && readResult != ServiceStoreResult::NotFound )
                        return;
                    int64      oldValue = 0;
                    const bool bHasOld  = readResult == ServiceStoreResult::Ok && Internal::decodeScore( record._bytes, oldValue );
                    bool       bChanged = false;
                    _finalValue         = Internal::applyUpdate( _update, _order, bHasOld, oldValue, _value, bChanged );
                    if ( bChanged == false )
                    {
                        _result = LeaderboardResult::Ok; // 더 좋지 않으므로 쓰지 않는다
                        return;
                    }
                    ServiceTransaction transaction;
                    transaction.put( _table, _key, Internal::encodeScore( _finalValue, _nowMs.count(), _displayName ), record._version );
                    const ServiceStoreResult commitResult = connection.commit( transaction );
                    if ( commitResult == ServiceStoreResult::Ok )
                    {
                        _result   = LeaderboardResult::Ok;
                        _bChanged = SW_TRUE;
                        return;
                    }
                    if ( commitResult != ServiceStoreResult::Conflict )
                        return;
                }
                _result = LeaderboardResult::Conflict;
            }

            void complete() override
            {
                _pService->applyScoreWrite( _boardID, _periodID, _accountID, _displayName, _finalValue, _bChanged == SW_TRUE, _result, _requestTag, _operation, _nowMs );
            }
        };

        /** @brief 캐시를 잃은 기간의 점수를 영속 저장소에서 모두 읽는 작업입니다. */
        class LeaderboardRebuildWork final : public IServiceStoreWork
        {
        public:
            LeaderboardRebuildWork( LeaderboardService* pService, string rankKey, string scorePrefix )
                : _listScore{}
                , _rankKey{ std::move( rankKey ) }
                , _scorePrefix{ std::move( scorePrefix ) }
                , _pService{ pService }
                , _bReadOk{ SW_FALSE }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                _bReadOk = LeaderboardServiceInternal::readAllScores( connection, _scorePrefix, _listScore ) ? SW_TRUE : SW_FALSE;
            }

            void complete() override { _pService->applyRebuildRead( _rankKey, std::move( _listScore ), _bReadOk == SW_TRUE ); }

        private:
            vector<LeaderboardScoreRow> _listScore;
            string                      _rankKey;
            string                      _scorePrefix;
            LeaderboardService*         _pService;
            uint8                       _bReadOk;
        };

        /** @brief 계정의 통계를 모두 읽는 작업입니다. */
        class LeaderboardStatReadWork final : public IServiceStoreWork
        {
        public:
            /** @brief @p bCache 이면 응답 대신 접속한 계정의 통계 메모리를 채운다. */
            LeaderboardStatReadWork( LeaderboardService* pService, AccountID accountID, uint64 requestTag, bool bCache )
                : _listStat{}
                , _pService{ pService }
                , _accountID{ accountID }
                , _requestTag{ requestTag }
                , _result{ LeaderboardResult::Unavailable }
                , _bCache{ static_cast<uint8>( bCache ? SW_TRUE : SW_FALSE ) }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                vector<ServiceRecord> listRecord;
                const string          prefix = ServiceKeyUtil::makeHex64( _accountID ) + "/";
                if ( connection.listRecords( LeaderboardServiceInternal::getStatTable(), prefix, "", LeaderboardLimit::kMaxStatCount, false, listRecord ) !=
                     ServiceStoreResult::Ok )
                    return;
                for ( const ServiceRecord& record : listRecord )
                {
                    LeaderboardStat stat;
                    stat._name = record._key.substr( prefix.size() );
                    if ( LeaderboardServiceInternal::decodeScore( record._bytes, stat._value ) == false )
                    {
                        _listStat.clear(); // 손상된 통계는 건너뛰지 않고 이 읽기를 실패로 돌려준다
                        return;
                    }
                    _listStat.push_back( std::move( stat ) );
                }
                _result = LeaderboardResult::Ok;
            }

            void complete() override
            {
                if ( _bCache == SW_TRUE )
                    _pService->applyStatCache( _accountID, _result == LeaderboardResult::Ok, isCancelled(), std::move( _listStat ) );
                else
                    _pService->applyStats( _requestTag, _result, std::move( _listStat ) );
            }

        private:
            vector<LeaderboardStat> _listStat;
            LeaderboardService*     _pService;
            AccountID               _accountID;
            uint64                  _requestTag;
            LeaderboardResult       _result;
            uint8                   _bCache;
        };

        /** @brief 업적 하나를 "없어야 함" 조건으로 쓰고 보상 우편을 같은 트랜잭션에 넣는 작업입니다. 이미 있으면 아무것도 하지 않습니다. */
        class AchievementUnlockWork final : public IServiceStoreWork
        {
        public:
            AchievementUnlockWork( LeaderboardService* pService, const AchievementDefinition& definition, AccountID accountID, UnixMilliseconds nowMs )
                : _definition{ definition }
                , _pService{ pService }
                , _accountID{ accountID }
                , _nowMs{ nowMs }
                , _bUnlocked{ SW_FALSE }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                using Internal    = LeaderboardServiceInternal;
                const string  key = ServiceKeyUtil::makeHex64( _accountID ) + "/" + _definition._achievementID;
                ServiceRecord existing;
                if ( connection.readRecord( Internal::getAchievementTable(), key, existing ) != ServiceStoreResult::NotFound )
                    return; // 이미 달성했거나 저장소가 실패했다. 다음 통계 변화 때 다시 확인한다
                ServiceTransaction transaction;
                transaction.put( Internal::getAchievementTable(), key, Internal::encodeScore( _nowMs.count(), _nowMs.count(), string_view{} ), ServiceRecord::kAbsentVersion ); // 값은 달성 시각이다
                if ( _definition._rewardAssetID.empty() == false )
                {
                    const string       idempotencyKey = string( "ach." ) + ServiceKeyUtil::makeHex64( _accountID ) + "." + _definition._achievementID;
                    const LedgerResult staged         = Internal::stageRewardMail( connection, transaction, _accountID, _definition._rewardAssetID, _definition._rewardAmount,
                                                                                   _definition._mailTitleKey, Internal::kAchievementSender, idempotencyKey, _nowMs );
                    if ( staged != LedgerResult::Ok )
                        return; // 우편 규칙이나 저장소 때문에 실패했으므로 업적도 쓰지 않는다. 다음에 다시 시도한다
                }
                _bUnlocked = connection.commit( transaction ) == ServiceStoreResult::Ok ? SW_TRUE : SW_FALSE;
            }

            void complete() override
            {
                _pService->applyAchievementUnlock( _accountID, AchievementState{ _definition._achievementID, _nowMs.count() }, _bUnlocked == SW_TRUE );
            }

        private:
            AchievementDefinition _definition;
            LeaderboardService*   _pService;
            AccountID             _accountID;
            UnixMilliseconds      _nowMs;
            uint8                 _bUnlocked;
        };

        /** @brief 계정이 달성한 업적을 모두 읽는 작업입니다. */
        class AchievementReadWork final : public IServiceStoreWork
        {
        public:
            AchievementReadWork( LeaderboardService* pService, AccountID accountID, uint64 requestTag )
                : _listAchievement{}
                , _pService{ pService }
                , _accountID{ accountID }
                , _requestTag{ requestTag }
                , _result{ LeaderboardResult::Unavailable }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                vector<ServiceRecord> listRecord;
                const string          prefix = ServiceKeyUtil::makeHex64( _accountID ) + "/";
                if ( connection.listRecords( LeaderboardServiceInternal::getAchievementTable(), prefix, "", LeaderboardLimit::kMaxAchievementCount, false,
                                             listRecord ) != ServiceStoreResult::Ok )
                    return;
                for ( const ServiceRecord& record : listRecord )
                {
                    AchievementState achievement;
                    achievement._achievementID = record._key.substr( prefix.size() );
                    if ( LeaderboardServiceInternal::decodeScore( record._bytes, achievement._unlockedMs ) )
                        _listAchievement.push_back( std::move( achievement ) );
                }
                _result = LeaderboardResult::Ok;
            }

            void complete() override { _pService->applyAchievements( _requestTag, _result, std::move( _listAchievement ) ); }

        private:
            vector<AchievementState> _listAchievement;
            LeaderboardService*      _pService;
            AccountID                _accountID;
            uint64                   _requestTag;
            LeaderboardResult        _result;
        };

        /** @brief 업적 진행률을 읽는 작업입니다. 통계와 달성 기록을 읽어 등록한 업적(복사본)마다 한 줄을 만듭니다. */
        class AchievementProgressWork final : public IServiceStoreWork
        {
        public:
            AchievementProgressWork( LeaderboardService* pService, vector<AchievementDefinition>&& listDefinition, AccountID accountID, uint64 requestTag )
                : _listDefinition{ std::move( listDefinition ) }
                , _listProgress{}
                , _pService{ pService }
                , _accountID{ accountID }
                , _requestTag{ requestTag }
                , _result{ LeaderboardResult::Unavailable }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                using Internal = LeaderboardServiceInternal;
                vector<ServiceRecord> listStatRecord;
                vector<ServiceRecord> listAchievementRecord;
                const string          prefix  = ServiceKeyUtil::makeHex64( _accountID ) + "/";
                const bool            bReadOk = connection.listRecords( Internal::getStatTable(), prefix, "", LeaderboardLimit::kMaxStatCount, false, listStatRecord ) ==
                                         ServiceStoreResult::Ok &&
                                     connection.listRecords( Internal::getAchievementTable(), prefix, "", LeaderboardLimit::kMaxAchievementCount, false,
                                                             listAchievementRecord ) == ServiceStoreResult::Ok;
                if ( bReadOk == false )
                    return;
                for ( const AchievementDefinition& definition : _listDefinition )
                {
                    AchievementProgress progress;
                    if ( findValue( listAchievementRecord, prefix.size(), definition._achievementID, progress._unlockedMs ) == false )
                    {
                        _listProgress.clear(); // 손상된 달성 기록은 건너뛰지 않고 이 읽기를 실패로 돌려준다
                        return;
                    }
                    if ( definition._bHidden && progress._unlockedMs == 0 )
                    {
                        progress._bHidden = SW_TRUE;
                        _listProgress.push_back( std::move( progress ) );
                        continue;
                    }
                    if ( findValue( listStatRecord, prefix.size(), definition._statName, progress._progress ) == false )
                    {
                        _listProgress.clear(); // 손상된 통계는 건너뛰지 않고 이 읽기를 실패로 돌려준다
                        return;
                    }
                    progress._achievementID = definition._achievementID;
                    progress._threshold     = definition._threshold;
                    progress._bHidden       = definition._bHidden ? SW_TRUE : SW_FALSE;
                    _listProgress.push_back( std::move( progress ) );
                }
                _result = LeaderboardResult::Ok;
            }

            void complete() override { _pService->applyAchievementProgress( _requestTag, _result, std::move( _listProgress ) ); }

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

            vector<AchievementDefinition> _listDefinition;
            vector<AchievementProgress>   _listProgress;
            LeaderboardService*           _pService;
            AccountID                     _accountID;
            uint64                        _requestTag;
            LeaderboardResult             _result;
        };

        /** @brief 계정 목록의 점수를 읽어 목록 안 순위를 매기는 작업입니다. */
        class ScoresOfReadWork final : public IServiceStoreWork
        {
        public:
            ScoresOfReadWork( LeaderboardService* pService, string&& prefix, vector<AccountID>&& listAccountID, LeaderboardOrder order, uint64 periodID, uint64 requestTag )
                : _listAccountID{ std::move( listAccountID ) }
                , _listEntry{}
                , _prefix{ std::move( prefix ) }
                , _pService{ pService }
                , _periodID{ periodID }
                , _requestTag{ requestTag }
                , _order{ order }
                , _result{ LeaderboardResult::Unavailable }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                for ( const AccountID accountID : _listAccountID )
                {
                    ServiceRecord            record;
                    const ServiceStoreResult read = connection.readRecord( LeaderboardServiceInternal::getScoreTable(), _prefix + ServiceKeyUtil::makeHex64( accountID ), record );
                    if ( read == ServiceStoreResult::NotFound )
                        continue; // 이 기간에 점수가 없다
                    LeaderboardEntry entry;
                    entry._accountID = accountID;
                    if ( read != ServiceStoreResult::Ok || LeaderboardServiceInternal::decodeScoreWithName( record._bytes, entry._score, entry._displayName ) == false )
                    {
                        _listEntry.clear(); // 손상된 점수는 건너뛰지 않고 이 읽기를 실패로 돌려준다
                        return;
                    }
                    _listEntry.push_back( std::move( entry ) );
                }
                if ( _order == LeaderboardOrder::Descending )
                    std::sort( _listEntry.begin(), _listEntry.end(), isHigherFirst );
                else
                    std::sort( _listEntry.begin(), _listEntry.end(), isLowerFirst );
                for ( size_t index = 0; index < _listEntry.size(); ++index )
                {
                    _listEntry[index]._rank = static_cast<int32>( index ) + 1;
                }
                _result = LeaderboardResult::Ok;
            }

            void complete() override { _pService->applyScoresOf( _requestTag, _periodID, _result, std::move( _listEntry ) ); }

        private:
            /** @brief 같은 점수는 계정 id 오름차순입니다(정산과 같은 규칙). */
            static bool isHigherFirst( const LeaderboardEntry& left, const LeaderboardEntry& right )
            {
                return left._score != right._score ? left._score > right._score : left._accountID < right._accountID;
            }

            static bool isLowerFirst( const LeaderboardEntry& left, const LeaderboardEntry& right )
            {
                return left._score != right._score ? left._score < right._score : left._accountID < right._accountID;
            }

            vector<AccountID>        _listAccountID;
            vector<LeaderboardEntry> _listEntry;
            string                   _prefix;
            LeaderboardService*      _pService;
            uint64                   _periodID;
            uint64                   _requestTag;
            LeaderboardOrder         _order;
            LeaderboardResult        _result;
        };

        /** @brief 시즌 결과 하나를 읽는 작업입니다. 값은 점수와 순위입니다. */
        class SeasonResultReadWork final : public IServiceStoreWork
        {
        public:
            SeasonResultReadWork( LeaderboardService* pService, string key, uint32 seasonID, AccountID accountID, uint64 requestTag )
                : _entry{}
                , _key{ std::move( key ) }
                , _pService{ pService }
                , _requestTag{ requestTag }
                , _seasonID{ seasonID }
                , _result{ LeaderboardResult::Unavailable }
            {
                _entry._accountID = accountID;
            }

            void run( IServiceStoreConnection& connection ) override
            {
                ServiceRecord            record;
                const ServiceStoreResult read = connection.readRecord( LeaderboardServiceInternal::getSeasonResultTable(), _key, record );
                if ( read == ServiceStoreResult::NotFound )
                {
                    _result = LeaderboardResult::NotRanked;
                    return;
                }
                if ( read != ServiceStoreResult::Ok )
                    return;
                BitReader reader( record._bytes.data(), static_cast<int32>( record._bytes.size() ) );
                if ( reader.readBits( 8 ) != LeaderboardServiceInternal::kFormatVersion )
                    return;
                _entry._score = reader.readVarInt();
                _entry._rank  = reader.readVarInt32();
                _result       = reader.hasOverflowed() ? LeaderboardResult::Unavailable : LeaderboardResult::Ok;
            }

            void complete() override { _pService->applySeasonResult( _requestTag, _seasonID, _result, _entry ); }

        private:
            LeaderboardEntry    _entry;
            string              _key;
            LeaderboardService* _pService;
            uint64              _requestTag;
            uint32              _seasonID;
            LeaderboardResult   _result;
        };

        /**
         * @brief 정산의 첫 단계입니다. 시즌 점수를 모두 읽어 순위대로 정렬합니다. 쓰기는 하지 않고, 정렬한 목록을 서비스에 넘겨 페이지 일로 나눠 쓰게 합니다.
         * @details 순위는 정렬 방향으로 정하고 같은 점수는 계정 id 오름차순으로 정한다. 정산이 다시 돌아도 같은 순위여야 하기 때문이다.
         */
        class SeasonRankReadWork final : public IServiceStoreWork
        {
        public:
            SeasonRankReadWork( LeaderboardService* pService, uint64 settlementID, string scorePrefix, LeaderboardOrder order )
                : _listRanked{}
                , _scorePrefix{ std::move( scorePrefix ) }
                , _pService{ pService }
                , _settlementID{ settlementID }
                , _order{ order }
                , _bReadOk{ SW_FALSE }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                if ( LeaderboardServiceInternal::readAllScores( connection, _scorePrefix, _listRanked ) == false )
                    return;
                if ( _order == LeaderboardOrder::Descending )
                    std::sort( _listRanked.begin(), _listRanked.end(), isHigherFirst );
                else
                    std::sort( _listRanked.begin(), _listRanked.end(), isLowerFirst );
                _bReadOk = SW_TRUE;
            }

            void complete() override { _pService->applySettlementRanking( _settlementID, _bReadOk == SW_TRUE, std::move( _listRanked ) ); }

        private:
            static bool isHigherFirst( const LeaderboardScoreRow& left, const LeaderboardScoreRow& right )
            {
                return left._score != right._score ? left._score > right._score : left._accountID < right._accountID;
            }

            static bool isLowerFirst( const LeaderboardScoreRow& left, const LeaderboardScoreRow& right )
            {
                return left._score != right._score ? left._score < right._score : left._accountID < right._accountID;
            }

            vector<LeaderboardScoreRow> _listRanked;
            string                      _scorePrefix;
            LeaderboardService*         _pService;
            uint64                      _settlementID;
            LeaderboardOrder            _order;
            uint8                       _bReadOk;
        };

        /**
         * @brief 정산 페이지 하나입니다. 순위가 정해진 사람 `kPlayersPerPage` 명까지 12명씩 결과와 보상 우편을 한 트랜잭션으로 씁니다.
         * @details 결과가 있는 사람은 건너뛰므로 다시 돌아도 한 번만 처리된다. 실패하면 다음 실행(임대가 지나 다른 서버가 맡거나 다시 부름)이 남은 사람부터 잇는다.
         */
        class SeasonSettlementPageWork final : public IServiceStoreWork
        {
        public:
            SeasonSettlementPageWork( LeaderboardService* pService, uint64 settlementID, const LeaderboardDefinition& board, const LeaderboardSeason& season,
                                      vector<LeaderboardScoreRow> listPage, int32 firstRank, UnixMilliseconds nowMs )
                : _listPage{ std::move( listPage ) }
                , _board{ board }
                , _season{ season }
                , _pService{ pService }
                , _settlementID{ settlementID }
                , _nowMs{ nowMs }
                , _firstRank{ firstRank }
                , _bCommitted{ SW_FALSE }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                using Internal = LeaderboardServiceInternal;
                for ( size_t first = 0; first < _listPage.size(); first += static_cast<size_t>( Internal::kPlayersPerCommit ) )
                {
                    ServiceTransaction transaction;
                    const size_t       last = std::min( _listPage.size(), first + static_cast<size_t>( Internal::kPlayersPerCommit ) );
                    for ( size_t index = first; index < last; ++index )
                    {
                        if ( stagePlayer( connection, transaction, _listPage[index], _firstRank + static_cast<int32>( index ) ) == false )
                            return;
                    }
                    if ( transaction.isEmpty() == false && connection.commit( transaction ) != ServiceStoreResult::Ok )
                        return;
                }
                _bCommitted = SW_TRUE;
            }

            void complete() override { _pService->applySettlementPage( _settlementID, _bCommitted == SW_TRUE ); }

        private:
            /** @brief 한 사람의 결과와 보상을 붙입니다. 이미 결과가 있으면 건너뜁니다. 저장소나 우편이 실패하면 false 를 반환합니다. */
            bool stagePlayer( IServiceStoreConnection& connection, ServiceTransaction& inoutTransaction, const LeaderboardScoreRow& row, int32 rank )
            {
                using Internal               = LeaderboardServiceInternal;
                const string             key = Internal::makeSeasonResultKey( _board._boardID, _season._seasonID, row._accountID );
                ServiceRecord            existing;
                const ServiceStoreResult read = connection.readRecord( Internal::getSeasonResultTable(), key, existing );
                if ( read == ServiceStoreResult::Ok )
                    return true; // 앞선 실행이 이미 정산했다
                if ( read != ServiceStoreResult::NotFound )
                    return false;
                inoutTransaction.put( Internal::getSeasonResultTable(), key, Internal::encodeScore( row._score, rank, string_view{} ), ServiceRecord::kAbsentVersion );
                const LeaderboardRewardTier* pTier = findTier( rank );
                if ( pTier == nullptr )
                    return true;
                const string idempotencyKey = string( "lb." ) + _board._boardID + "." + Internal::makeSeasonHex( _season._seasonID ) + "." +
                                              ServiceKeyUtil::makeHex64( row._accountID );
                return Internal::stageRewardMail( connection, inoutTransaction, row._accountID, pTier->_assetID, pTier->_amount, pTier->_mailTitleKey,
                                                  Internal::kSeasonSender, idempotencyKey, _nowMs ) == LedgerResult::Ok;
            }

            const LeaderboardRewardTier* findTier( int32 rank ) const
            {
                for ( const LeaderboardRewardTier& tier : _season._listRewardTier )
                {
                    if ( tier._rankFrom <= rank && rank <= tier._rankTo )
                        return &tier;
                }
                return nullptr;
            }

            vector<LeaderboardScoreRow> _listPage;
            LeaderboardDefinition       _board;
            LeaderboardSeason           _season;
            LeaderboardService*         _pService;
            uint64                      _settlementID;
            UnixMilliseconds            _nowMs;
            int32                       _firstRank; ///< 페이지 첫 사람의 순위(1 부터)
            uint8                       _bCommitted;
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
        , _pendingCount{ 0 }
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
    }

    bool LeaderboardService::registerBoard( const LeaderboardDefinition& definition )
    {
        const bool bValid = LeaderboardNameRule::isValidID( definition._boardID ) &&
                            ( definition._sourceStat.empty() || LeaderboardNameRule::isValidID( definition._sourceStat ) ) &&
                            definition._order < LeaderboardOrder::Count && definition._update < LeaderboardUpdate::Count && definition._reset < LeaderboardReset::Count;
        if ( bValid == false || LeaderboardServiceInternal::areSeasonsValid( definition ) == false )
            return false;
        const bool bAlreadyRegistered  = _mapBoard.find( definition._boardID ) != _mapBoard.end();
        _mapBoard[definition._boardID] = definition;
        const bool bSkipSeasonJobs     = definition._reset != LeaderboardReset::Season || _pScheduler == nullptr || bAlreadyRegistered;
        if ( bSkipSeasonJobs )
            return true; // 같은 보드를 다시 등록해도 정산 작업은 한 번만 만든다
        for ( const LeaderboardSeason& season : definition._listSeason )
        {
            ScheduleDefinition job;
            job._jobID         = makeSettlementJobID( definition._boardID, season._seasonID );
            job._kind          = ScheduleKind::Window;
            job._windowStartMs = season._endMs;
            job._windowEndMs   = season._endMs + LeaderboardServiceInternal::kSettlementWindowMs;
            job._leaseMs       = LeaderboardServiceInternal::kSettlementLeaseMs;
            if ( _pScheduler->registerJob( job, this ) == false )
                return false;
        }
        return true;
    }

    bool LeaderboardService::registerAchievement( const AchievementDefinition& definition )
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

    const LeaderboardDefinition* LeaderboardService::findBoard( string_view boardID ) const
    {
        const auto boardIt = _mapBoard.find( string( boardID ) );
        return boardIt != _mapBoard.end() ? &boardIt->second : nullptr;
    }

    uint64 LeaderboardService::computePeriodID( const LeaderboardDefinition& definition, UnixMilliseconds nowMs ) const
    {
        ScheduleDefinition schedule;
        schedule._minuteOfDay = definition._resetMinuteOfDay;
        schedule._dayOfWeek   = definition._resetDayOfWeek;
        switch ( definition._reset )
        {
            case LeaderboardReset::Daily:
            {
                schedule._kind = ScheduleKind::Daily;
                return static_cast<uint64>( std::max<int64>( 0, ServiceScheduler::computeLatestOccurrence( schedule, nowMs.count() ) ) );
            }
            case LeaderboardReset::Weekly:
            {
                schedule._kind = ScheduleKind::Weekly;
                return static_cast<uint64>( std::max<int64>( 0, ServiceScheduler::computeLatestOccurrence( schedule, nowMs.count() ) ) );
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
        const LeaderboardDefinition* pBoard = findBoard( boardID );
        if ( pBoard == nullptr )
        {
            pushCompletion( requestTag, LeaderboardOperation::Submit, LeaderboardResult::UnknownBoard );
            return;
        }
        const bool bValid = -LeaderboardLimit::kMaxAbsScore <= score && score <= LeaderboardLimit::kMaxAbsScore &&
                            displayName.size() <= static_cast<size_t>( RequestLimits::kMaxDisplayNameSize ) && accountID != kInvalidAccountID;
        if ( bValid == false )
        {
            pushCompletion( requestTag, LeaderboardOperation::Submit, LeaderboardResult::Invalid );
            return;
        }
        const uint64 periodID = computePeriodID( *pBoard, nowMs );
        if ( pBoard->_reset == LeaderboardReset::Season && periodID == 0 )
        {
            pushCompletion( requestTag, LeaderboardOperation::Submit, LeaderboardResult::NotRanked );
            return;
        }
        unique_ptr<LeaderboardWriteWork> work = sw::make_unique<LeaderboardWriteWork>();
        work->_table                          = LeaderboardServiceInternal::getScoreTable();
        work->_key                            = LeaderboardServiceInternal::makeScorePrefix( pBoard->_boardID, periodID ) + ServiceKeyUtil::makeHex64( accountID );
        work->_boardID                        = pBoard->_boardID;
        work->_displayName                    = string( displayName );
        work->_pService                       = this;
        work->_accountID                      = accountID;
        work->_periodID                       = periodID;
        work->_requestTag                     = requestTag;
        work->_value                          = score;
        work->_nowMs                          = nowMs;
        work->_update                         = pBoard->_update;
        work->_order                          = pBoard->_order;
        work->_operation                      = LeaderboardOperation::Submit;
        ++_pendingCount;
        _dependencies._pStore->submit( std::move( work ) );
    }

    void LeaderboardService::changeStat( AccountID accountID, string_view displayName, string_view statName, int64 value, LeaderboardUpdate update, UnixMilliseconds nowMs,
                                         uint64 requestTag )
    {
        const bool bValid = LeaderboardNameRule::isValidID( statName ) && update < LeaderboardUpdate::Count && accountID != kInvalidAccountID &&
                          -LeaderboardLimit::kMaxAbsScore <= value && value <= LeaderboardLimit::kMaxAbsScore;
        if ( bValid == false )
        {
            pushCompletion( requestTag, LeaderboardOperation::StatChange, LeaderboardResult::Invalid );
            return;
        }
        unique_ptr<LeaderboardWriteWork> work = sw::make_unique<LeaderboardWriteWork>();
        work->_table                          = LeaderboardServiceInternal::getStatTable();
        work->_key                            = ServiceKeyUtil::makeHex64( accountID ) + "/" + string( statName );
        work->_boardID                        = string( statName );
        work->_displayName                    = string( displayName );
        work->_pService                       = this;
        work->_accountID                      = accountID;
        work->_requestTag                     = requestTag;
        work->_value                          = value;
        work->_nowMs                          = nowMs;
        work->_update                         = update;
        work->_order                          = LeaderboardOrder::Descending; // 통계의 Best 는 큰 값이다
        work->_operation                      = LeaderboardOperation::StatChange;
        ++_pendingCount;
        _dependencies._pStore->submit( std::move( work ) );
    }

    void LeaderboardService::applyScoreWrite( const string& boardID, uint64 periodID, AccountID accountID, const string& displayName, int64 finalScore, bool bChanged,
                                              LeaderboardResult result, uint64 requestTag, LeaderboardOperation operation, UnixMilliseconds nowMs )
    {
        --_pendingCount;
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
                    ++_pendingCount;
                    _dependencies._pStore->submit( sw::make_unique<AchievementUnlockWork>( this, achievement, accountID, nowMs ) );
                }
            }
            return;
        }
        const LeaderboardDefinition* pBoard = findBoard( boardID );
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
                                                                       vector<uint8>( reinterpret_cast<const uint8*>( displayName.data() ), reinterpret_cast<const uint8*>( displayName.data() ) + displayName.size() ), LeaderboardLimit::kNameTtlMs ) );
    }

    int64 LeaderboardService::computeRankRetentionMs( const LeaderboardDefinition& definition, uint64 periodID, UnixMilliseconds nowMs ) const
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
        return std::max<int64>( 1, periodEndMs + Internal::kRankRetentionGraceMs - nowMs.count() );
    }

    void LeaderboardService::submitCacheScore( const string& rankKey, AccountID accountID, int64 cacheScore )
    {
        const auto waitIt = _mapRankKeyToWaitingRead.find( rankKey );
        if ( waitIt != _mapRankKeyToWaitingRead.end() )
            _mapRankKeyToLateWrite[rankKey].push_back( LeaderboardScoreRow{ accountID, cacheScore } ); // 다시 채우는 중이므로 채운 뒤에 한 번 더 싣는다. 이전 값을 덮기 위해서이다
        _dependencies._pRouter->submit( EphemeralRequest::makeScoreSet( rankKey, ServiceKeyUtil::makeHex64( accountID ), cacheScore ) );
    }

    void LeaderboardService::readTop( string_view boardID, int32 offset, int32 count, UnixMilliseconds nowMs, uint64 requestTag )
    {
        PendingRead read;
        read._boardID                       = string( boardID );
        read._requestTag                    = requestTag;
        read._offset                        = offset;
        read._count                         = count;
        read._operation                     = LeaderboardOperation::Top;
        const LeaderboardDefinition* pBoard = findBoard( boardID );
        const bool                   bRange = 0 <= offset && 1 <= count && count <= LeaderboardLimit::kMaxPage;
        if ( pBoard == nullptr || bRange == false )
        {
            completeRead( read, pBoard == nullptr ? LeaderboardResult::UnknownBoard : LeaderboardResult::Invalid );
            return;
        }
        read._periodID = computePeriodID( *pBoard, nowMs );
        read._nowMs    = nowMs;
        startRead( read );
    }

    void LeaderboardService::readAround( string_view boardID, AccountID accountID, int32 radius, UnixMilliseconds nowMs, uint64 requestTag )
    {
        PendingRead read;
        read._boardID                       = string( boardID );
        read._accountID                     = accountID;
        read._requestTag                    = requestTag;
        read._count                         = radius;
        read._operation                     = LeaderboardOperation::Around;
        const LeaderboardDefinition* pBoard = findBoard( boardID );
        const bool                   bRange = 0 <= radius && radius <= LeaderboardLimit::kMaxAround;
        if ( pBoard == nullptr || bRange == false )
        {
            completeRead( read, pBoard == nullptr ? LeaderboardResult::UnknownBoard : LeaderboardResult::Invalid );
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
            completeRead( read, LeaderboardResult::Unavailable );
            return;
        }
        const string rankKey = makeRankKey( read._boardID, read._periodID );
        if ( reply._result == EphemeralResult::NotFound ) // 캐시를 잃었거나 처음이므로 영속 저장소에서 다시 채운다
        {
            vector<PendingRead>& listWaiting = _mapRankKeyToWaitingRead[rankKey];
            listWaiting.push_back( read );
            if ( listWaiting.size() == 1 )
            {
                ++_pendingCount;
                _dependencies._pStore->submit(
                    sw::make_unique<LeaderboardRebuildWork>( this, rankKey, LeaderboardServiceInternal::makeScorePrefix( read._boardID, read._periodID ) ) );
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
        const uint64 requestID        = _dependencies._pRouter->submit( EphemeralRequest::makeScoreRank( rankKey, ServiceKeyUtil::makeHex64( read._accountID ) ),
                                                                        EphemeralStoreRouter::ReplyDelegate::create<&LeaderboardService::onRankReply>( this ), _replyOwner );
        _mapRequestToQuery[requestID] = queryID;
    }

    void LeaderboardService::applyRebuildRead( const string& rankKey, vector<LeaderboardScoreRow>&& listScore, bool bReadOk )
    {
        --_pendingCount;
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
                completeRead( read, LeaderboardResult::Unavailable );
            }
            return;
        }
        const LeaderboardDefinition* pBoard = listWaiting.empty() ? nullptr : findBoard( listWaiting.front()._boardID );
        const LeaderboardOrder       order  = pBoard != nullptr ? pBoard->_order : LeaderboardOrder::Descending;
        for ( const LeaderboardScoreRow& row : listScore )
        {
            _dependencies._pRouter->submit( EphemeralRequest::makeScoreSet( rankKey, ServiceKeyUtil::makeHex64( row._accountID ), toCacheScore( order, row._score ) ) );
        }
        for ( const LeaderboardScoreRow& row : listLateWrite ) // 다시 채우는 동안 쓴 점수이다. 영속 저장소에서 읽은 이전 값을 덮으며 이미 캐시 점수이다
        {
            _dependencies._pRouter->submit( EphemeralRequest::makeScoreSet( rankKey, ServiceKeyUtil::makeHex64( row._accountID ), row._score ) );
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
            completeRead( query._read, reply._result == EphemeralResult::NotFound ? LeaderboardResult::NotRanked : LeaderboardResult::Unavailable );
            _mapQuery.erase( queryIt );
            return;
        }
        const int32 myIndex           = static_cast<int32>( reply._integer );
        const int32 radius            = query._read._count;
        const int32 offset            = std::max( 0, myIndex - radius );
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
            completeRead( query._read, LeaderboardResult::Unavailable );
            _mapQuery.erase( queryIt );
            return;
        }
        const LeaderboardDefinition* pBoard = findBoard( query._read._boardID );
        const LeaderboardOrder       order  = pBoard != nullptr ? pBoard->_order : LeaderboardOrder::Descending;
        for ( size_t memberIndex = 0; memberIndex < reply._listMember.size(); ++memberIndex )
        {
            LeaderboardEntry entry;
            if ( ServiceKeyUtil::parseHex64( reply._listMember[memberIndex]._member, entry._accountID ) == false )
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

    void LeaderboardService::completeRead( const PendingRead& read, LeaderboardResult result )
    {
        LeaderboardCompletion completion;
        completion._requestTag = read._requestTag;
        completion._operation  = read._operation;
        completion._periodID   = read._periodID;
        completion._result     = result;
        _completionBuffer.push( std::move( completion ) );
    }

    void LeaderboardService::pushCompletion( uint64 requestTag, LeaderboardOperation operation, LeaderboardResult result )
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
        ++_pendingCount;
        _dependencies._pStore->submit( sw::make_unique<LeaderboardStatReadWork>( this, accountID, requestTag, false ) );
    }

    void LeaderboardService::loadAccountStats( AccountID accountID )
    {
        AccountStats& stats = _mapAccountStats[accountID];
        stats._bLoaded      = SW_FALSE;
        stats._mapValue.clear();
        ++_pendingCount;
        _dependencies._pStore->submit( sw::make_unique<LeaderboardStatReadWork>( this, accountID, 0, true ) );
    }

    void LeaderboardService::dropAccountStats( AccountID accountID )
    {
        _mapAccountStats.erase( accountID );
    }

    void LeaderboardService::applyStatCache( AccountID accountID, bool bReadOk, bool bCancelled, vector<LeaderboardStat>&& listStat )
    {
        --_pendingCount;
        const auto statsIt = _mapAccountStats.find( accountID );
        if ( statsIt == _mapAccountStats.end() )
            return; // 그사이 떠났다
        if ( bReadOk == false )
        {
            if ( bCancelled == false ) // 종료 때문에 취소된 읽기는 경고하지 않는다
                SW_LOG_WARNING( "Leaderboard: stats of account %# could not be read - matchmaking reports RatingUnavailable until it reconnects", accountID );
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

    void LeaderboardService::applyStats( uint64 requestTag, LeaderboardResult result, vector<LeaderboardStat>&& listStat )
    {
        --_pendingCount;
        LeaderboardCompletion completion;
        completion._requestTag = requestTag;
        completion._operation  = LeaderboardOperation::Stats;
        completion._result     = result;
        completion._listStat   = std::move( listStat );
        _completionBuffer.push( std::move( completion ) );
    }

    void LeaderboardService::readAchievements( AccountID accountID, uint64 requestTag )
    {
        ++_pendingCount;
        _dependencies._pStore->submit( sw::make_unique<AchievementReadWork>( this, accountID, requestTag ) );
    }

    void LeaderboardService::applyAchievements( uint64 requestTag, LeaderboardResult result, vector<AchievementState>&& listAchievement )
    {
        --_pendingCount;
        LeaderboardCompletion completion;
        completion._requestTag      = requestTag;
        completion._operation       = LeaderboardOperation::Achievements;
        completion._result          = result;
        completion._listAchievement = std::move( listAchievement );
        _completionBuffer.push( std::move( completion ) );
    }

    void LeaderboardService::readAchievementProgress( AccountID accountID, uint64 requestTag )
    {
        vector<AchievementDefinition> listDefinition;
        listDefinition.reserve( _mapAchievement.size() );
        for ( const auto& [achievementID, definition] : _mapAchievement )
        {
            (void)achievementID;
            listDefinition.push_back( definition );
        }
        std::sort( listDefinition.begin(), listDefinition.end(), LeaderboardServiceInternal::isAchievementIDLess );
        ++_pendingCount;
        _dependencies._pStore->submit( sw::make_unique<AchievementProgressWork>( this, std::move( listDefinition ), accountID, requestTag ) );
    }

    void LeaderboardService::applyAchievementProgress( uint64 requestTag, LeaderboardResult result, vector<AchievementProgress>&& listProgress )
    {
        --_pendingCount;
        LeaderboardCompletion completion;
        completion._requestTag   = requestTag;
        completion._operation    = LeaderboardOperation::AchievementProgress;
        completion._result       = result;
        completion._listProgress = std::move( listProgress );
        _completionBuffer.push( std::move( completion ) );
    }

    void LeaderboardService::readScoresOf( string_view boardID, const vector<AccountID>& listAccountID, UnixMilliseconds nowMs, uint64 requestTag )
    {
        const LeaderboardDefinition* pBoard = findBoard( boardID );
        if ( pBoard == nullptr )
        {
            pushCompletion( requestTag, LeaderboardOperation::ScoresOf, LeaderboardResult::UnknownBoard );
            return;
        }
        if ( listAccountID.empty() || listAccountID.size() > static_cast<size_t>( LeaderboardLimit::kMaxScoresOfCount ) )
        {
            pushCompletion( requestTag, LeaderboardOperation::ScoresOf, LeaderboardResult::Invalid );
            return;
        }
        vector<AccountID> listUnique = listAccountID;
        VectorUtil::sortUnique( listUnique ); // 같은 계정을 두 번 세지 않는다
        const uint64 periodID = computePeriodID( *pBoard, nowMs );
        ++_pendingCount;
        _dependencies._pStore->submit(
            sw::make_unique<ScoresOfReadWork>( this, LeaderboardServiceInternal::makeScorePrefix( boardID, periodID ), std::move( listUnique ), pBoard->_order, periodID, requestTag ) );
    }

    void LeaderboardService::applyScoresOf( uint64 requestTag, uint64 periodID, LeaderboardResult result, vector<LeaderboardEntry>&& listEntry )
    {
        --_pendingCount;
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
        --_pendingCount;
        if ( bUnlocked )
            _unlockBuffer.push( AchievementUnlock{ achievement, accountID } );
    }

    void LeaderboardService::readSeasonResult( string_view boardID, uint32 seasonID, AccountID accountID, uint64 requestTag )
    {
        const LeaderboardDefinition* pBoard = findBoard( boardID );
        if ( pBoard == nullptr || pBoard->_reset != LeaderboardReset::Season )
        {
            pushCompletion( requestTag, LeaderboardOperation::SeasonResult, pBoard == nullptr ? LeaderboardResult::UnknownBoard : LeaderboardResult::Invalid );
            return;
        }
        ++_pendingCount;
        _dependencies._pStore->submit( sw::make_unique<SeasonResultReadWork>( this, LeaderboardServiceInternal::makeSeasonResultKey( pBoard->_boardID, seasonID, accountID ),
                                                                              seasonID, accountID, requestTag ) );
    }

    void LeaderboardService::applySeasonResult( uint64 requestTag, uint32 seasonID, LeaderboardResult result, const LeaderboardEntry& entry )
    {
        --_pendingCount;
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
        const LeaderboardDefinition* pBoard  = findBoard( boardID );
        const LeaderboardSeason*     pSeason = nullptr;
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
        const LeaderboardDefinition* pBoard  = bParsed ? findBoard( jobID.substr( prefix.size(), lastDot - prefix.size() ) ) : nullptr;
        const LeaderboardSeason*     pSeason = nullptr;
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

    void LeaderboardService::startSettlement( const LeaderboardDefinition& board, const LeaderboardSeason& season, string jobID, UnixMilliseconds nowMs, const SettlementDelegate& onDone )
    {
        const uint64        settlementID = _settlementIDAllocator.allocate();
        SettlementProgress& progress     = _mapSettlement[settlementID];
        progress._board                  = board;
        progress._season                 = season;
        progress._jobID                  = std::move( jobID );
        progress._onDone                 = onDone;
        progress._nowMs                  = nowMs;
        ++_pendingCount; // 정산 하나에 하나 — 페이지가 몇이든 끝날 때(`applySettlement`) 한 번 줄인다
        _dependencies._pStore->submit(
            sw::make_unique<SeasonRankReadWork>( this, settlementID, LeaderboardServiceInternal::makeScorePrefix( board._boardID, season._seasonID ), board._order ) );
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
        if ( progress._nextIndex >= progress._listRanked.size() || _dependencies._pStore == nullptr )
        {
            finishSettlement( settlementID, progress._nextIndex >= progress._listRanked.size() );
            return;
        }
        const size_t                first = progress._nextIndex;
        const size_t                last  = std::min( progress._listRanked.size(), first + static_cast<size_t>( LeaderboardServiceInternal::kPlayersPerPage ) );
        vector<LeaderboardScoreRow> listPage( progress._listRanked.begin() + static_cast<ptrdiff_t>( first ), progress._listRanked.begin() + static_cast<ptrdiff_t>( last ) );
        progress._nextIndex = last;
        _dependencies._pStore->submit( sw::make_unique<SeasonSettlementPageWork>( this, settlementID, progress._board, progress._season, std::move( listPage ),
                                                                                  static_cast<int32>( first ) + 1, progress._nowMs ) );
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
        --_pendingCount;
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
