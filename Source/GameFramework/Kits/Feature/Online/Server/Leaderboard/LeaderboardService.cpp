#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Server/Leaderboard/LeaderboardService.h"

#include "Core/Network/BitStream.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Guard/RequestLimits.h"
#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Mail/ServiceMail.h"
#include "GameFramework/Base/Online/Schedule/ServiceScheduler.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct LeaderboardServiceInternal
        {
            static constexpr uint8       kFormatVersion       = 1;
            static constexpr int32       kMaxConflictRetry    = 4;
            static constexpr int32       kRebuildBatch        = 256;
            static constexpr int32       kPlayersPerCommit    = 15;        ///< 사람마다 쓰기 넷(결과 + 우편 · 보낸 기록 · 만료 색인) — 트랜잭션 상한 64 안
            static constexpr int64       kSettlementWindowMs  = 604800000; ///< 7 일 — 시즌 끝부터 이 안에 정산이 돈다
            static constexpr int64       kSettlementLeaseMs   = 600000;    ///< 10 분 — 돌던 서버가 죽으면 다른 서버가 이어받는다
            static constexpr const utf8* kSettlementJobPrefix = "lb.settle.";
            static constexpr const utf8* kAchievementSender   = "system.achievement";
            static constexpr const utf8* kSeasonSender        = "system.season";

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

            /** @brief 시즌 id 의 16 진 8 자리입니다(작업 id · 우편 멱등 키를 64 바이트 안에). */
            static string makeSeasonHex( uint32 seasonId ) { return ServiceKeyUtil::makeHex64( seasonId ).substr( 8 ); }

            static string makeSeasonResultKey( string_view boardId, uint32 seasonId, AccountId accountId )
            {
                return string( boardId ) + "/" + makeSeasonHex( seasonId ) + "/" + ServiceKeyUtil::makeHex64( accountId );
            }

            /** @brief 시즌 표의 시즌 목록 규칙 — id 1 이상 · 유일, 시작 < 끝, 보상 구간 1 ≤ from ≤ to · 자산 · 수량 · 우편 제목. */
            static bool areSeasonsValid( const LeaderboardDefinition& definition )
            {
                if ( definition._reset != LeaderboardReset::Season )
                    return definition._listSeason.empty();
                for ( size_t seasonIndex = 0; seasonIndex < definition._listSeason.size(); ++seasonIndex )
                {
                    const LeaderboardSeason& season = definition._listSeason[seasonIndex];
                    if ( season._seasonId == 0 || season._startMs >= season._endMs )
                        return false;
                    for ( size_t otherIndex = 0; otherIndex < seasonIndex; ++otherIndex )
                    {
                        if ( definition._listSeason[otherIndex]._seasonId == season._seasonId )
                            return false;
                    }
                    for ( const LeaderboardRewardTier& tier : season._listRewardTier )
                    {
                        const bool bTierOk = 1 <= tier._rankFrom && tier._rankFrom <= tier._rankTo && tier._amount > 0 && tier._assetId.empty() == false &&
                                             tier._mailTitleKey.empty() == false;
                        if ( bTierOk == false )
                            return false;
                    }
                }
                return true;
            }

            /** @brief 보상 우편 하나를 트랜잭션에 붙입니다(재원 발행 · 보관 30 일 · 만료 때 첨부 소멸). */
            static LedgerResult stageRewardMail( IServiceStoreConnection& connection, ServiceTransaction& inoutTransaction, AccountId accountId, string_view assetId,
                                                 int64 amount, string_view titleKey, string_view senderName, string idempotencyKey, int64 nowMs )
            {
                ServiceMailMessage mail;
                mail._listAttachment.push_back( ServiceMailAttachment{ string( assetId ), amount } );
                mail._titleKey           = string( titleKey );
                mail._senderName         = string( senderName );
                mail._idempotencyKey     = std::move( idempotencyKey );
                mail._fundingHolder      = LedgerHolder::makeMint();
                mail._recipientAccountId = accountId;
                mail._createdMs          = nowMs;
                mail._expiresMs          = nowMs + ServiceMailConstant::kDefaultRetentionMs;
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

            static string makeScorePrefix( string_view boardId, uint64 periodId ) { return string( boardId ) + "/" + ServiceKeyUtil::makeHex64( periodId ) + "/"; }
            static string makeNameKey( AccountId accountId ) { return string( "lb/name/" ) + ServiceKeyUtil::makeHex64( accountId ); }
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

            [[nodiscard]] static bool decodeScore( const vector<uint8>& bytes, int64& outScore )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                if ( reader.readBits( 8 ) != kFormatVersion )
                    return false;
                outScore = reader.readVarInt();
                return reader.hasOverflowed() == false;
            }

            /** @brief 갱신 방식으로 새 값을 정합니다. 쓸 필요가 없으면 @p outbChanged 가 거짓. */
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

            /** @brief @p prefix 아래의 점수를 모두 읽습니다(묶음마다 커서). */
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
                        const bool          bParsed = ServiceKeyUtil::parseHex64( string_view( record._key ).substr( prefix.size() ), row._accountId ) &&
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

        /** @brief 점수(또는 통계) 하나를 판 조건으로 쓰는 일 — 갱신 방식에 따라 새 값을 정한다. */
        class LeaderboardWriteWork final : public IServiceStoreWork
        {
        public:
            hashed_string        _table{};
            string               _key{};
            string               _boardId{}; ///< 점수면 표 id, 통계면 통계 이름(완료가 연동 표를 찾는다)
            string               _displayName{};
            LeaderboardService*  _pService{ nullptr };
            AccountId            _accountId{ kInvalidAccountId };
            uint64               _periodId{ 0 };
            uint64               _requestTag{ 0 };
            int64                _value{ 0 };
            int64                _nowMs{ 0 };
            int64                _finalValue{ 0 };
            LeaderboardUpdate    _update{ LeaderboardUpdate::Best };
            LeaderboardOrder     _order{ LeaderboardOrder::Descending };
            LeaderboardOperation _operation{ LeaderboardOperation::Submit };
            LeaderboardResult    _result{ LeaderboardResult::Unavailable };
            uint8                _bChanged{ SW_FALSE };

            void run( IServiceStoreConnection& connection ) override
            {
                using Internal = LeaderboardServiceInternal;
                for ( int32 attempt = 0; attempt < Internal::kMaxConflictRetry; ++attempt )
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
                        _result = LeaderboardResult::Ok; // 더 좋지 않다 — 쓰지 않는다
                        return;
                    }
                    ServiceTransaction transaction;
                    transaction.put( _table, _key, Internal::encodeScore( _finalValue, _nowMs, _displayName ), record._version );
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
                _pService->applyScoreWrite( _boardId, _periodId, _accountId, _displayName, _finalValue, _bChanged == SW_TRUE, _result, _requestTag, _operation, _nowMs );
            }
        };

        /** @brief 캐시를 잃은 기간의 점수를 영속에서 모두 읽는 일입니다. */
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

        /** @brief 계정의 통계를 모두 읽는 일입니다. */
        class LeaderboardStatReadWork final : public IServiceStoreWork
        {
        public:
            LeaderboardStatReadWork( LeaderboardService* pService, AccountId accountId, uint64 requestTag )
                : _listStat{}
                , _pService{ pService }
                , _accountId{ accountId }
                , _requestTag{ requestTag }
                , _result{ LeaderboardResult::Unavailable }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                vector<ServiceRecord> listRecord;
                const string          prefix = ServiceKeyUtil::makeHex64( _accountId ) + "/";
                if ( connection.listRecords( LeaderboardServiceInternal::getStatTable(), prefix, "", LeaderboardLimit::kMaxStatCount, false, listRecord ) !=
                     ServiceStoreResult::Ok )
                    return;
                for ( const ServiceRecord& record : listRecord )
                {
                    LeaderboardStat stat;
                    stat._name = record._key.substr( prefix.size() );
                    if ( LeaderboardServiceInternal::decodeScore( record._bytes, stat._value ) )
                        _listStat.push_back( std::move( stat ) );
                }
                _result = LeaderboardResult::Ok;
            }

            void complete() override { _pService->applyStats( _requestTag, _result, std::move( _listStat ) ); }

        private:
            vector<LeaderboardStat> _listStat;
            LeaderboardService*     _pService;
            AccountId               _accountId;
            uint64                  _requestTag;
            LeaderboardResult       _result;
        };

        /** @brief 업적 하나를 "없어야 함" 으로 쓰고 보상 우편을 같은 트랜잭션에 넣는 일 — 이미 있으면 아무것도 하지 않는다. */
        class AchievementUnlockWork final : public IServiceStoreWork
        {
        public:
            AchievementUnlockWork( LeaderboardService* pService, const AchievementDefinition& definition, AccountId accountId, int64 nowMs )
                : _definition{ definition }
                , _pService{ pService }
                , _accountId{ accountId }
                , _nowMs{ nowMs }
                , _bUnlocked{ SW_FALSE }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                using Internal    = LeaderboardServiceInternal;
                const string  key = ServiceKeyUtil::makeHex64( _accountId ) + "/" + _definition._achievementId;
                ServiceRecord existing;
                if ( connection.readRecord( Internal::getAchievementTable(), key, existing ) != ServiceStoreResult::NotFound )
                    return; // 이미 달성 · 저장소 실패 — 다음 통계 변화 때 다시 본다
                ServiceTransaction transaction;
                transaction.put( Internal::getAchievementTable(), key, Internal::encodeScore( _nowMs, _nowMs, string_view{} ), ServiceRecord::kAbsentVersion ); // 값 = 달성 시각
                if ( _definition._rewardAssetId.empty() == false )
                {
                    const string       idempotencyKey = string( "ach." ) + ServiceKeyUtil::makeHex64( _accountId ) + "." + _definition._achievementId;
                    const LedgerResult staged         = Internal::stageRewardMail( connection, transaction, _accountId, _definition._rewardAssetId, _definition._rewardAmount,
                                                                                   _definition._mailTitleKey, Internal::kAchievementSender, idempotencyKey, _nowMs );
                    if ( staged != LedgerResult::Ok )
                        return; // 우편 규칙 · 저장소 — 업적도 쓰지 않는다(다음에 다시)
                }
                _bUnlocked = connection.commit( transaction ) == ServiceStoreResult::Ok ? SW_TRUE : SW_FALSE;
            }

            void complete() override
            {
                _pService->applyAchievementUnlock( _accountId, AchievementState{ _definition._achievementId, _nowMs }, _bUnlocked == SW_TRUE );
            }

        private:
            AchievementDefinition _definition;
            LeaderboardService*   _pService;
            AccountId             _accountId;
            int64                 _nowMs;
            uint8                 _bUnlocked;
        };

        /** @brief 계정이 달성한 업적을 모두 읽는 일입니다. */
        class AchievementReadWork final : public IServiceStoreWork
        {
        public:
            AchievementReadWork( LeaderboardService* pService, AccountId accountId, uint64 requestTag )
                : _listAchievement{}
                , _pService{ pService }
                , _accountId{ accountId }
                , _requestTag{ requestTag }
                , _result{ LeaderboardResult::Unavailable }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                vector<ServiceRecord> listRecord;
                const string          prefix = ServiceKeyUtil::makeHex64( _accountId ) + "/";
                if ( connection.listRecords( LeaderboardServiceInternal::getAchievementTable(), prefix, "", LeaderboardLimit::kMaxAchievementCount, false,
                                             listRecord ) != ServiceStoreResult::Ok )
                    return;
                for ( const ServiceRecord& record : listRecord )
                {
                    AchievementState achievement;
                    achievement._achievementId = record._key.substr( prefix.size() );
                    if ( LeaderboardServiceInternal::decodeScore( record._bytes, achievement._unlockedMs ) )
                        _listAchievement.push_back( std::move( achievement ) );
                }
                _result = LeaderboardResult::Ok;
            }

            void complete() override { _pService->applyAchievements( _requestTag, _result, std::move( _listAchievement ) ); }

        private:
            vector<AchievementState> _listAchievement;
            LeaderboardService*      _pService;
            AccountId                _accountId;
            uint64                   _requestTag;
            LeaderboardResult        _result;
        };

        /** @brief 시즌 결과 하나를 읽는 일입니다(값 = 점수 · 순위). */
        class SeasonResultReadWork final : public IServiceStoreWork
        {
        public:
            SeasonResultReadWork( LeaderboardService* pService, string key, uint32 seasonId, AccountId accountId, uint64 requestTag )
                : _entry{}
                , _key{ std::move( key ) }
                , _pService{ pService }
                , _requestTag{ requestTag }
                , _seasonId{ seasonId }
                , _result{ LeaderboardResult::Unavailable }
            {
                _entry._accountId = accountId;
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
                _entry._rank  = static_cast<int32>( reader.readVarInt() );
                _result       = reader.hasOverflowed() ? LeaderboardResult::Unavailable : LeaderboardResult::Ok;
            }

            void complete() override { _pService->applySeasonResult( _requestTag, _seasonId, _result, _entry ); }

        private:
            LeaderboardEntry    _entry;
            string              _key;
            LeaderboardService* _pService;
            uint64              _requestTag;
            uint32              _seasonId;
            LeaderboardResult   _result;
        };

        /** @brief 시즌 하나의 정산 — 점수를 모두 읽어 순위를 매기고, 15 명씩 결과 + 보상 우편을 한 트랜잭션으로. 결과가 있는 사람은 건너뛴다(다시 돌아도 한 번). */
        class SeasonSettlementWork final : public IServiceStoreWork
        {
        public:
            SeasonSettlementWork( LeaderboardService* pService, const LeaderboardDefinition& board, const LeaderboardSeason& season, string jobId, int64 nowMs,
                                  const SettlementDelegate& onDone )
                : _board{ board }
                , _season{ season }
                , _jobId{ std::move( jobId ) }
                , _onDone{ onDone }
                , _pService{ pService }
                , _nowMs{ nowMs }
                , _bSucceeded{ SW_FALSE }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                using Internal = LeaderboardServiceInternal;
                vector<LeaderboardScoreRow> listScore;
                if ( Internal::readAllScores( connection, Internal::makeScorePrefix( _board._boardId, _season._seasonId ), listScore ) == false )
                    return;
                // 순위 — 정렬 방향, 같은 점수는 계정 id 오름차순(정산은 다시 돌아도 같은 순위여야 한다)
                if ( _board._order == LeaderboardOrder::Descending )
                    std::sort( listScore.begin(), listScore.end(), isHigherFirst );
                else
                    std::sort( listScore.begin(), listScore.end(), isLowerFirst );
                for ( size_t first = 0; first < listScore.size(); first += static_cast<size_t>( Internal::kPlayersPerCommit ) )
                {
                    ServiceTransaction transaction;
                    const size_t       last = std::min( listScore.size(), first + static_cast<size_t>( Internal::kPlayersPerCommit ) );
                    for ( size_t index = first; index < last; ++index )
                    {
                        if ( stagePlayer( connection, transaction, listScore[index], static_cast<int32>( index ) + 1 ) == false )
                            return;
                    }
                    if ( transaction.isEmpty() == false && connection.commit( transaction ) != ServiceStoreResult::Ok )
                        return; // 다음 실행(임대가 지나 다른 서버 · 다시 부름)이 남은 사람부터 — 결과가 있는 사람은 건너뛴다
                }
                _bSucceeded = SW_TRUE;
            }

            void complete() override { _pService->applySettlement( _jobId, _onDone, _bSucceeded == SW_TRUE ); }

        private:
            static bool isHigherFirst( const LeaderboardScoreRow& left, const LeaderboardScoreRow& right )
            {
                return left._score != right._score ? left._score > right._score : left._accountId < right._accountId;
            }

            static bool isLowerFirst( const LeaderboardScoreRow& left, const LeaderboardScoreRow& right )
            {
                return left._score != right._score ? left._score < right._score : left._accountId < right._accountId;
            }

            /** @brief 한 사람의 결과 + 보상을 붙입니다. 이미 결과가 있으면 건너뛴다. 저장소 · 우편 실패면 false. */
            bool stagePlayer( IServiceStoreConnection& connection, ServiceTransaction& inoutTransaction, const LeaderboardScoreRow& row, int32 rank )
            {
                using Internal               = LeaderboardServiceInternal;
                const string             key = Internal::makeSeasonResultKey( _board._boardId, _season._seasonId, row._accountId );
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
                const string idempotencyKey = string( "lb." ) + _board._boardId + "." + Internal::makeSeasonHex( _season._seasonId ) + "." +
                                              ServiceKeyUtil::makeHex64( row._accountId );
                return Internal::stageRewardMail( connection, inoutTransaction, row._accountId, pTier->_assetId, pTier->_amount, pTier->_mailTitleKey,
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

            LeaderboardDefinition _board;
            LeaderboardSeason     _season;
            string                _jobId;
            SettlementDelegate    _onDone;
            LeaderboardService*   _pService;
            int64                 _nowMs;
            uint8                 _bSucceeded;
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
        , _mapJobToRun{}
        , _unlockBuffer{}
        , _completionBuffer{}
        , _dependencies{}
        , _pScheduler{ nullptr }
        , _nextQueryId{ 1 }
        , _pendingCount{ 0 }
    {
    }

    LeaderboardService::~LeaderboardService() { shutdown(); }

    void LeaderboardService::initialize( const LeaderboardServiceDependencies& dependencies )
    {
        SW_ASSERT( dependencies._pStore != nullptr && dependencies._pRouter != nullptr );
        _dependencies = dependencies;
    }

    void LeaderboardService::shutdown()
    {
        if ( _dependencies._pRouter != nullptr )
        {
            for ( const auto& [requestId, read] : _mapReadyRequestToRead )
            {
                _dependencies._pRouter->cancel( requestId );
            }
            for ( const auto& [requestId, queryId] : _mapRequestToQuery )
            {
                _dependencies._pRouter->cancel( requestId );
            }
        }
        _mapReadyRequestToRead.clear();
        _mapRequestToQuery.clear();
        _mapNameRequestToIndex.clear();
        _mapQuery.clear();
        _mapRankKeyToWaitingRead.clear();
        _mapRankKeyToLateWrite.clear();
        _mapJobToRun.clear();
        _dependencies = LeaderboardServiceDependencies{};
        _pScheduler   = nullptr;
    }

    bool LeaderboardService::registerBoard( const LeaderboardDefinition& definition )
    {
        const bool bValid = LeaderboardNameRule::isValidId( definition._boardId ) &&
                            ( definition._sourceStat.empty() || LeaderboardNameRule::isValidId( definition._sourceStat ) ) &&
                            definition._order < LeaderboardOrder::Count && definition._update < LeaderboardUpdate::Count && definition._reset < LeaderboardReset::Count;
        if ( bValid == false || LeaderboardServiceInternal::areSeasonsValid( definition ) == false )
            return false;
        const bool bAlreadyRegistered  = _mapBoard.find( definition._boardId ) != _mapBoard.end();
        _mapBoard[definition._boardId] = definition;
        if ( definition._reset != LeaderboardReset::Season || _pScheduler == nullptr || bAlreadyRegistered )
            return true; // 같은 표를 다시 올려도 정산 작업은 한 번
        for ( const LeaderboardSeason& season : definition._listSeason )
        {
            ScheduleDefinition job;
            job._jobId         = makeSettlementJobId( definition._boardId, season._seasonId );
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
        const bool bRewardOk = definition._rewardAssetId.empty() || ( definition._rewardAmount > 0 && definition._mailTitleKey.empty() == false );
        const bool bValid    = LeaderboardNameRule::isValidId( definition._achievementId ) && LeaderboardNameRule::isValidId( definition._statName ) && bRewardOk;
        if ( bValid == false )
            return false;
        _mapAchievement[definition._achievementId] = definition;
        return true;
    }

    string LeaderboardService::makeSettlementJobId( string_view boardId, uint32 seasonId )
    {
        return string( LeaderboardServiceInternal::kSettlementJobPrefix ) + string( boardId ) + "." + LeaderboardServiceInternal::makeSeasonHex( seasonId );
    }

    const LeaderboardDefinition* LeaderboardService::findBoard( string_view boardId ) const
    {
        const auto boardIt = _mapBoard.find( string( boardId ) );
        return boardIt != _mapBoard.end() ? &boardIt->second : nullptr;
    }

    uint64 LeaderboardService::computePeriodId( const LeaderboardDefinition& definition, int64 nowMs ) const
    {
        ScheduleDefinition schedule;
        schedule._minuteOfDay = definition._resetMinuteOfDay;
        schedule._dayOfWeek   = definition._resetDayOfWeek;
        switch ( definition._reset )
        {
            case LeaderboardReset::Daily:
            {
                schedule._kind = ScheduleKind::Daily;
                return static_cast<uint64>( std::max<int64>( 0, ServiceScheduler::computeLatestOccurrence( schedule, nowMs ) ) );
            }
            case LeaderboardReset::Weekly:
            {
                schedule._kind = ScheduleKind::Weekly;
                return static_cast<uint64>( std::max<int64>( 0, ServiceScheduler::computeLatestOccurrence( schedule, nowMs ) ) );
            }
            case LeaderboardReset::Season:
            {
                for ( const LeaderboardSeason& season : definition._listSeason )
                {
                    if ( season._startMs <= nowMs && nowMs < season._endMs )
                        return season._seasonId;
                }
                return 0; // 시즌 밖 — 제출은 NotRanked
            }
            default:
            {
                return 0;
            }
        }
    }

    string LeaderboardService::makeRankKey( string_view boardId, uint64 periodId ) { return string( "lb/" ) + string( boardId ) + "/" + ServiceKeyUtil::makeHex64( periodId ); }

    void LeaderboardService::submitScore( string_view boardId, AccountId accountId, string_view displayName, int64 score, int64 nowMs, uint64 requestTag )
    {
        const LeaderboardDefinition* pBoard = findBoard( boardId );
        if ( pBoard == nullptr )
        {
            pushCompletion( requestTag, LeaderboardOperation::Submit, LeaderboardResult::UnknownBoard );
            return;
        }
        const bool bValid = -LeaderboardLimit::kMaxAbsScore <= score && score <= LeaderboardLimit::kMaxAbsScore &&
                            displayName.size() <= static_cast<size_t>( RequestLimits::kMaxDisplayNameSize ) && accountId != kInvalidAccountId;
        if ( bValid == false )
        {
            pushCompletion( requestTag, LeaderboardOperation::Submit, LeaderboardResult::Invalid );
            return;
        }
        const uint64 periodId = computePeriodId( *pBoard, nowMs );
        if ( pBoard->_reset == LeaderboardReset::Season && periodId == 0 )
        {
            pushCompletion( requestTag, LeaderboardOperation::Submit, LeaderboardResult::NotRanked );
            return;
        }
        unique_ptr<LeaderboardWriteWork> work = sw::make_unique<LeaderboardWriteWork>();
        work->_table                          = LeaderboardServiceInternal::getScoreTable();
        work->_key                            = LeaderboardServiceInternal::makeScorePrefix( pBoard->_boardId, periodId ) + ServiceKeyUtil::makeHex64( accountId );
        work->_boardId                        = pBoard->_boardId;
        work->_displayName                    = string( displayName );
        work->_pService                       = this;
        work->_accountId                      = accountId;
        work->_periodId                       = periodId;
        work->_requestTag                     = requestTag;
        work->_value                          = score;
        work->_nowMs                          = nowMs;
        work->_update                         = pBoard->_update;
        work->_order                          = pBoard->_order;
        work->_operation                      = LeaderboardOperation::Submit;
        ++_pendingCount;
        _dependencies._pStore->submit( std::move( work ) );
    }

    void LeaderboardService::changeStat( AccountId accountId, string_view displayName, string_view statName, int64 value, LeaderboardUpdate update, int64 nowMs,
                                         uint64 requestTag )
    {
        const bool bValid = LeaderboardNameRule::isValidId( statName ) && update < LeaderboardUpdate::Count && accountId != kInvalidAccountId &&
                          -LeaderboardLimit::kMaxAbsScore <= value && value <= LeaderboardLimit::kMaxAbsScore;
        if ( bValid == false )
        {
            pushCompletion( requestTag, LeaderboardOperation::StatChange, LeaderboardResult::Invalid );
            return;
        }
        unique_ptr<LeaderboardWriteWork> work = sw::make_unique<LeaderboardWriteWork>();
        work->_table                          = LeaderboardServiceInternal::getStatTable();
        work->_key                            = ServiceKeyUtil::makeHex64( accountId ) + "/" + string( statName );
        work->_boardId                        = string( statName );
        work->_displayName                    = string( displayName );
        work->_pService                       = this;
        work->_accountId                      = accountId;
        work->_requestTag                     = requestTag;
        work->_value                          = value;
        work->_nowMs                          = nowMs;
        work->_update                         = update;
        work->_order                          = LeaderboardOrder::Descending; // 통계의 Best = 큰 값
        work->_operation                      = LeaderboardOperation::StatChange;
        ++_pendingCount;
        _dependencies._pStore->submit( std::move( work ) );
    }

    void LeaderboardService::applyScoreWrite( const string& boardId, uint64 periodId, AccountId accountId, const string& displayName, int64 finalScore, bool bChanged,
                                              LeaderboardResult result, uint64 requestTag, LeaderboardOperation operation, int64 nowMs )
    {
        --_pendingCount;
        if ( requestTag != 0 )
        {
            LeaderboardCompletion completion;
            completion._requestTag = requestTag;
            completion._operation  = operation;
            completion._result     = result;
            completion._score      = finalScore;
            completion._periodId   = periodId;
            _completionBuffer.push( std::move( completion ) );
        }
        if ( result != LeaderboardResult::Ok || bChanged == false )
            return;
        if ( operation == LeaderboardOperation::StatChange )
        {
            for ( const auto& [id, board] : _mapBoard ) // 통계 연동 표 — 같은 값을 낸다(꼬리표 0 — 완료 없음)
            {
                if ( board._sourceStat == boardId )
                    submitScore( board._boardId, accountId, displayName, finalScore, nowMs, 0 );
            }
            for ( const auto& [achievementId, achievement] : _mapAchievement ) // 문턱을 넘은 업적 — 이미 달성했으면 일이 아무것도 하지 않는다
            {
                if ( achievement._statName == boardId && finalScore >= achievement._threshold )
                {
                    ++_pendingCount;
                    _dependencies._pStore->submit( sw::make_unique<AchievementUnlockWork>( this, achievement, accountId, nowMs ) );
                }
            }
            return;
        }
        const LeaderboardDefinition* pBoard = findBoard( boardId );
        if ( pBoard == nullptr || _dependencies._pRouter == nullptr )
            return;
        submitCacheScore( makeRankKey( boardId, periodId ), accountId, toCacheScore( pBoard->_order, finalScore ) );
        if ( displayName.empty() == false )
            (void)_dependencies._pRouter->submit( EphemeralRequest::makeSet( LeaderboardServiceInternal::makeNameKey( accountId ),
                                                                             vector<uint8>( displayName.begin(), displayName.end() ), LeaderboardLimit::kNameTtlMs ),
                                                  EphemeralStoreRouter::ReplyDelegate{} );
    }

    void LeaderboardService::submitCacheScore( const string& rankKey, AccountId accountId, int64 cacheScore )
    {
        const auto waitIt = _mapRankKeyToWaitingRead.find( rankKey );
        if ( waitIt != _mapRankKeyToWaitingRead.end() )
            _mapRankKeyToLateWrite[rankKey].push_back( LeaderboardScoreRow{ accountId, cacheScore } ); // 다시 채우는 중 — 채운 뒤(옛 값을 덮도록) 한 번 더
        (void)_dependencies._pRouter->submit( EphemeralRequest::makeScoreSet( rankKey, ServiceKeyUtil::makeHex64( accountId ), cacheScore ),
                                              EphemeralStoreRouter::ReplyDelegate{} );
    }

    void LeaderboardService::readTop( string_view boardId, int32 offset, int32 count, int64 nowMs, uint64 requestTag )
    {
        PendingRead read;
        read._boardId                       = string( boardId );
        read._requestTag                    = requestTag;
        read._offset                        = offset;
        read._count                         = count;
        read._operation                     = LeaderboardOperation::Top;
        const LeaderboardDefinition* pBoard = findBoard( boardId );
        const bool                   bRange = 0 <= offset && 1 <= count && count <= LeaderboardLimit::kMaxPage;
        if ( pBoard == nullptr || bRange == false )
        {
            completeRead( read, pBoard == nullptr ? LeaderboardResult::UnknownBoard : LeaderboardResult::Invalid );
            return;
        }
        read._periodId = computePeriodId( *pBoard, nowMs );
        startRead( read );
    }

    void LeaderboardService::readAround( string_view boardId, AccountId accountId, int32 radius, int64 nowMs, uint64 requestTag )
    {
        PendingRead read;
        read._boardId                       = string( boardId );
        read._accountId                     = accountId;
        read._requestTag                    = requestTag;
        read._count                         = radius;
        read._operation                     = LeaderboardOperation::Around;
        const LeaderboardDefinition* pBoard = findBoard( boardId );
        const bool                   bRange = 0 <= radius && radius <= LeaderboardLimit::kMaxAround;
        if ( pBoard == nullptr || bRange == false )
        {
            completeRead( read, pBoard == nullptr ? LeaderboardResult::UnknownBoard : LeaderboardResult::Invalid );
            return;
        }
        read._periodId = computePeriodId( *pBoard, nowMs );
        startRead( read );
    }

    void LeaderboardService::startRead( const PendingRead& read )
    {
        const string rankKey = makeRankKey( read._boardId, read._periodId );
        const auto   waitIt  = _mapRankKeyToWaitingRead.find( rankKey );
        if ( waitIt != _mapRankKeyToWaitingRead.end() )
        {
            waitIt->second.push_back( read ); // 다시 채우는 중 — 줄
            return;
        }
        const uint64 requestId            = _dependencies._pRouter->submit( EphemeralRequest::makeGet( LeaderboardServiceInternal::makeReadyKey( rankKey ) ),
                                                                            EphemeralStoreRouter::ReplyDelegate::create<&LeaderboardService::onReadyReply>( this ) );
        _mapReadyRequestToRead[requestId] = read;
    }

    void LeaderboardService::onReadyReply( const EphemeralReply& reply )
    {
        const auto readIt = _mapReadyRequestToRead.find( reply._requestId );
        if ( readIt == _mapReadyRequestToRead.end() )
            return;
        const PendingRead read = readIt->second;
        _mapReadyRequestToRead.erase( readIt );
        if ( reply._result == EphemeralResult::Unavailable || reply._result == EphemeralResult::Invalid )
        {
            completeRead( read, LeaderboardResult::Unavailable );
            return;
        }
        const string rankKey = makeRankKey( read._boardId, read._periodId );
        if ( reply._result == EphemeralResult::NotFound ) // 캐시를 잃었다(또는 처음) — 영속에서 다시 채운다
        {
            vector<PendingRead>& listWaiting = _mapRankKeyToWaitingRead[rankKey];
            listWaiting.push_back( read );
            if ( listWaiting.size() == 1 )
            {
                ++_pendingCount;
                _dependencies._pStore->submit(
                    sw::make_unique<LeaderboardRebuildWork>( this, rankKey, LeaderboardServiceInternal::makeScorePrefix( read._boardId, read._periodId ) ) );
            }
            return;
        }
        const uint64 queryId = _nextQueryId++;
        RankQuery&   query   = _mapQuery[queryId];
        query._read          = read;
        if ( read._operation == LeaderboardOperation::Top )
        {
            const uint64 requestId        = _dependencies._pRouter->submit( EphemeralRequest::makeScoreRange( rankKey, read._offset, read._count ),
                                                                            EphemeralStoreRouter::ReplyDelegate::create<&LeaderboardService::onRangeReply>( this ) );
            _mapRequestToQuery[requestId] = queryId;
            return;
        }
        const uint64 requestId        = _dependencies._pRouter->submit( EphemeralRequest::makeScoreRank( rankKey, ServiceKeyUtil::makeHex64( read._accountId ) ),
                                                                        EphemeralStoreRouter::ReplyDelegate::create<&LeaderboardService::onRankReply>( this ) );
        _mapRequestToQuery[requestId] = queryId;
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
        const LeaderboardDefinition* pBoard = listWaiting.empty() ? nullptr : findBoard( listWaiting.front()._boardId );
        const LeaderboardOrder       order  = pBoard != nullptr ? pBoard->_order : LeaderboardOrder::Descending;
        for ( const LeaderboardScoreRow& row : listScore )
        {
            (void)_dependencies._pRouter->submit( EphemeralRequest::makeScoreSet( rankKey, ServiceKeyUtil::makeHex64( row._accountId ), toCacheScore( order, row._score ) ),
                                                  EphemeralStoreRouter::ReplyDelegate{} );
        }
        for ( const LeaderboardScoreRow& row : listLateWrite ) // 다시 채우는 동안 쓴 점수 — 영속에서 읽은 옛 값을 덮는다(이미 캐시 점수)
        {
            (void)_dependencies._pRouter->submit( EphemeralRequest::makeScoreSet( rankKey, ServiceKeyUtil::makeHex64( row._accountId ), row._score ),
                                                  EphemeralStoreRouter::ReplyDelegate{} );
        }
        (void)_dependencies._pRouter->submit( EphemeralRequest::makeSet( LeaderboardServiceInternal::makeReadyKey( rankKey ), vector<uint8>{ 1 }, 0 ),
                                              EphemeralStoreRouter::ReplyDelegate{} );
        // 캐시 앞은 맡긴 순서대로 답한다 — 아래 읽기는 위 쓰기 뒤에 처리된다
        for ( const PendingRead& read : listWaiting )
        {
            startRead( read );
        }
    }

    void LeaderboardService::onRankReply( const EphemeralReply& reply )
    {
        const auto queryIdIt = _mapRequestToQuery.find( reply._requestId );
        if ( queryIdIt == _mapRequestToQuery.end() )
            return;
        const uint64 queryId = queryIdIt->second;
        _mapRequestToQuery.erase( queryIdIt );
        const auto queryIt = _mapQuery.find( queryId );
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
        const uint64 requestId        = _dependencies._pRouter->submit( EphemeralRequest::makeScoreRange( makeRankKey( query._read._boardId, query._read._periodId ), offset, count ),
                                                                        EphemeralStoreRouter::ReplyDelegate::create<&LeaderboardService::onRangeReply>( this ) );
        _mapRequestToQuery[requestId] = queryId;
    }

    void LeaderboardService::onRangeReply( const EphemeralReply& reply )
    {
        const auto queryIdIt = _mapRequestToQuery.find( reply._requestId );
        if ( queryIdIt == _mapRequestToQuery.end() )
            return;
        const uint64 queryId = queryIdIt->second;
        _mapRequestToQuery.erase( queryIdIt );
        const auto queryIt = _mapQuery.find( queryId );
        if ( queryIt == _mapQuery.end() )
            return;
        RankQuery& query = queryIt->second;
        if ( reply._result != EphemeralResult::Ok && reply._result != EphemeralResult::NotFound )
        {
            completeRead( query._read, LeaderboardResult::Unavailable );
            _mapQuery.erase( queryIt );
            return;
        }
        const LeaderboardDefinition* pBoard = findBoard( query._read._boardId );
        const LeaderboardOrder       order  = pBoard != nullptr ? pBoard->_order : LeaderboardOrder::Descending;
        for ( size_t memberIndex = 0; memberIndex < reply._listMember.size(); ++memberIndex )
        {
            LeaderboardEntry entry;
            if ( ServiceKeyUtil::parseHex64( reply._listMember[memberIndex]._member, entry._accountId ) == false )
                continue;
            entry._score = toCacheScore( order, reply._listMember[memberIndex]._score ); // 부호 되돌림(같은 함수)
            entry._rank  = query._read._offset + static_cast<int32>( memberIndex ) + 1;
            query._listEntry.push_back( std::move( entry ) );
        }
        for ( int32 entryIndex = 0; entryIndex < static_cast<int32>( query._listEntry.size() ); ++entryIndex )
        {
            const uint64 requestId = _dependencies._pRouter->submit(
                EphemeralRequest::makeGet( LeaderboardServiceInternal::makeNameKey( query._listEntry[static_cast<size_t>( entryIndex )]._accountId ) ),
                EphemeralStoreRouter::ReplyDelegate::create<&LeaderboardService::onNameReply>( this ) );
            _mapRequestToQuery[requestId]     = queryId;
            _mapNameRequestToIndex[requestId] = entryIndex;
            ++query._outstandingNameCount;
        }
        if ( query._outstandingNameCount == 0 )
            finishQuery( queryId );
    }

    void LeaderboardService::onNameReply( const EphemeralReply& reply )
    {
        const auto queryIdIt = _mapRequestToQuery.find( reply._requestId );
        const auto indexIt   = _mapNameRequestToIndex.find( reply._requestId );
        if ( queryIdIt == _mapRequestToQuery.end() || indexIt == _mapNameRequestToIndex.end() )
            return;
        const uint64 queryId    = queryIdIt->second;
        const int32  entryIndex = indexIt->second;
        _mapRequestToQuery.erase( queryIdIt );
        _mapNameRequestToIndex.erase( indexIt );
        const auto queryIt = _mapQuery.find( queryId );
        if ( queryIt == _mapQuery.end() )
            return;
        RankQuery& query = queryIt->second;
        if ( reply._result == EphemeralResult::Ok )
            query._listEntry[static_cast<size_t>( entryIndex )]._displayName.assign( reply._value.begin(), reply._value.end() );
        if ( --query._outstandingNameCount == 0 )
            finishQuery( queryId );
    }

    void LeaderboardService::finishQuery( uint64 queryId )
    {
        const auto queryIt = _mapQuery.find( queryId );
        if ( queryIt == _mapQuery.end() )
            return;
        LeaderboardCompletion completion;
        completion._requestTag = queryIt->second._read._requestTag;
        completion._operation  = queryIt->second._read._operation;
        completion._periodId   = queryIt->second._read._periodId;
        completion._listEntry  = std::move( queryIt->second._listEntry );
        _completionBuffer.push( std::move( completion ) );
        _mapQuery.erase( queryIt );
    }

    void LeaderboardService::completeRead( const PendingRead& read, LeaderboardResult result )
    {
        LeaderboardCompletion completion;
        completion._requestTag = read._requestTag;
        completion._operation  = read._operation;
        completion._periodId   = read._periodId;
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

    void LeaderboardService::readStats( AccountId accountId, uint64 requestTag )
    {
        ++_pendingCount;
        _dependencies._pStore->submit( sw::make_unique<LeaderboardStatReadWork>( this, accountId, requestTag ) );
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

    void LeaderboardService::readAchievements( AccountId accountId, uint64 requestTag )
    {
        ++_pendingCount;
        _dependencies._pStore->submit( sw::make_unique<AchievementReadWork>( this, accountId, requestTag ) );
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

    void LeaderboardService::applyAchievementUnlock( AccountId accountId, const AchievementState& achievement, bool bUnlocked )
    {
        --_pendingCount;
        if ( bUnlocked )
            _unlockBuffer.push( AchievementUnlock{ achievement, accountId } );
    }

    void LeaderboardService::readSeasonResult( string_view boardId, uint32 seasonId, AccountId accountId, uint64 requestTag )
    {
        const LeaderboardDefinition* pBoard = findBoard( boardId );
        if ( pBoard == nullptr || pBoard->_reset != LeaderboardReset::Season )
        {
            pushCompletion( requestTag, LeaderboardOperation::SeasonResult, pBoard == nullptr ? LeaderboardResult::UnknownBoard : LeaderboardResult::Invalid );
            return;
        }
        ++_pendingCount;
        _dependencies._pStore->submit( sw::make_unique<SeasonResultReadWork>( this, LeaderboardServiceInternal::makeSeasonResultKey( pBoard->_boardId, seasonId, accountId ),
                                                                              seasonId, accountId, requestTag ) );
    }

    void LeaderboardService::applySeasonResult( uint64 requestTag, uint32 seasonId, LeaderboardResult result, const LeaderboardEntry& entry )
    {
        --_pendingCount;
        LeaderboardCompletion completion;
        completion._requestTag = requestTag;
        completion._operation  = LeaderboardOperation::SeasonResult;
        completion._result     = result;
        completion._periodId   = seasonId;
        if ( result == LeaderboardResult::Ok )
            completion._listEntry.push_back( entry );
        _completionBuffer.push( std::move( completion ) );
    }

    void LeaderboardService::settleSeason( string_view boardId, uint32 seasonId, int64 nowMs, const SettlementDelegate& onDone )
    {
        const LeaderboardDefinition* pBoard  = findBoard( boardId );
        const LeaderboardSeason*     pSeason = nullptr;
        if ( pBoard != nullptr )
        {
            for ( const LeaderboardSeason& season : pBoard->_listSeason )
            {
                if ( season._seasonId == seasonId )
                    pSeason = &season;
            }
        }
        if ( pSeason == nullptr )
        {
            if ( onDone.isBound() )
                onDone( false );
            return;
        }
        ++_pendingCount;
        _dependencies._pStore->submit( sw::make_unique<SeasonSettlementWork>( this, *pBoard, *pSeason, string{}, nowMs, onDone ) );
    }

    void LeaderboardService::onScheduledRun( const ScheduledRun& run )
    {
        // 작업 id "lb.settle.<표>.<시즌 16 진 8>" — 표 id 에 '.' 가 없으므로 마지막 '.' 뒤가 시즌
        const string_view jobId( run._jobId );
        const string_view prefix( LeaderboardServiceInternal::kSettlementJobPrefix );
        const size_t      lastDot  = jobId.rfind( '.' );
        uint64            seasonId = 0;
        const bool        bParsed  = StringUtil::startsWith( jobId, prefix ) && lastDot != string_view::npos && lastDot > prefix.size() &&
                             StringUtil::parseUint64( jobId.substr( lastDot + 1 ), seasonId, 16 );
        const LeaderboardDefinition* pBoard  = bParsed ? findBoard( jobId.substr( prefix.size(), lastDot - prefix.size() ) ) : nullptr;
        const LeaderboardSeason*     pSeason = nullptr;
        if ( pBoard != nullptr )
        {
            for ( const LeaderboardSeason& season : pBoard->_listSeason )
            {
                if ( season._seasonId == seasonId )
                    pSeason = &season;
            }
        }
        if ( pSeason == nullptr )
        {
            if ( _pScheduler != nullptr )
                _pScheduler->completeRun( run, false );
            return;
        }
        _mapJobToRun[run._jobId] = run; // 끝나면 completeRun 에 넘긴다(작업마다 하나 — 표 둘의 시즌이 같은 날 끝나도 섞이지 않는다)
        ++_pendingCount;
        _dependencies._pStore->submit( sw::make_unique<SeasonSettlementWork>( this, *pBoard, *pSeason, run._jobId, run._occurrenceMs, SettlementDelegate{} ) );
    }

    void LeaderboardService::applySettlement( const string& jobId, const SettlementDelegate& onDone, bool bSucceeded )
    {
        --_pendingCount;
        if ( onDone.isBound() )
            onDone( bSucceeded );
        if ( jobId.empty() )
            return;
        const auto runIt = _mapJobToRun.find( jobId );
        if ( runIt == _mapJobToRun.end() )
            return;
        if ( _pScheduler != nullptr )
            _pScheduler->completeRun( runIt->second, bSucceeded );
        _mapJobToRun.erase( runIt );
    }
} // namespace sw
