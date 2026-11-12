#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Server/SocialService.h"

#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Kits/Feature/Online/Social/Runtime/SocialProtocol.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/SocialCounts.h"
#include "GameFramework/Runtime/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStore.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStoreRouter.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Service/OnlineLimit.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct SocialServiceInternal
        {
            static constexpr uint8 kFormatVersion     = 1;
            static constexpr int64 kPresenceTTLMs     = 90000;
            static constexpr int64 kPresenceRefreshMs = 30000;

            static const hashed_string& getLinkTable()
            {
                static const hashed_string s_table{ "social_link" };
                return s_table;
            }

            static const hashed_string& getCountTable()
            {
                static const hashed_string s_table{ "social_count" };
                return s_table;
            }

            static string makeLinkKey( AccountID ownerID, AccountID otherID )
            {
                string key = ServiceKeyUtil::makeHex64( ownerID );
                key.push_back( '/' );
                ServiceKeyUtil::appendHex64( key, otherID );
                return key;
            }

            static string makePresenceKey( AccountID accountID ) { return string( "social/rp/" ) + ServiceKeyUtil::makeHex64( accountID ); }

            static vector<uint8> encodeLink( SocialLinkState state, int64 sinceMs )
            {
                BitWriter writer;
                writer.writeBits( kFormatVersion, 8 );
                writer.writeVarUint( static_cast<uint64>( state ) );
                writer.writeVarInt( sinceMs );
                return writer.getBytes();
            }

            [[nodiscard]] static bool decodeLink( const vector<uint8>& bytes, SocialLinkState& outState, int64& outSinceMs )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                if ( reader.readBits( 8 ) != kFormatVersion )
                    return false;
                const uint64 state = reader.readVarUint();
                outSinceMs         = reader.readVarInt();
                if ( reader.hasOverflowed() || state >= static_cast<uint64>( SocialLinkState::Count ) )
                    return false;
                outState = static_cast<SocialLinkState>( state );
                return true;
            }

            static vector<uint8> encodeCounts( const SocialCounts& counts )
            {
                BitWriter writer;
                writer.writeBits( kFormatVersion, 8 );
                writer.writeVarInt( counts._friendCount );
                writer.writeVarInt( counts._incomingCount );
                writer.writeVarInt( counts._outgoingCount );
                writer.writeVarInt( counts._blockedCount );
                return writer.getBytes();
            }

            [[nodiscard]] static bool decodeCounts( const vector<uint8>& bytes, SocialCounts& outCounts )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                if ( reader.readBits( 8 ) != kFormatVersion )
                    return false;
                outCounts._friendCount   = reader.readVarInt32();
                outCounts._incomingCount = reader.readVarInt32();
                outCounts._outgoingCount = reader.readVarInt32();
                outCounts._blockedCount  = reader.readVarInt32();
                return reader.hasOverflowed() == false;
            }

            static void addCounts( SocialCounts& inoutCounts, const SocialCounts& delta )
            {
                inoutCounts._friendCount   = MathUtil::max( 0, inoutCounts._friendCount + delta._friendCount );
                inoutCounts._incomingCount = MathUtil::max( 0, inoutCounts._incomingCount + delta._incomingCount );
                inoutCounts._outgoingCount = MathUtil::max( 0, inoutCounts._outgoingCount + delta._outgoingCount );
                inoutCounts._blockedCount  = MathUtil::max( 0, inoutCounts._blockedCount + delta._blockedCount );
            }

            /** @brief 레코드를 읽습니다. 저장소를 쓸 수 없으면 false 를 반환하고, 레코드가 없는 것은 true 이며 버전이 `kAbsentVersion` 입니다. */
            [[nodiscard]] static bool readOrAbsent( IServiceStoreConnection& connection, const hashed_string& table, string_view key, ServiceRecord& outRecord )
            {
                const ServiceStoreResult result = connection.readRecord( table, key, outRecord );
                return result == ServiceStoreResult::Ok || result == ServiceStoreResult::NotFound;
            }

            /** @brief 줄 하나의 새 상태를 트랜잭션에 붙입니다. 상태가 같으면 아무것도 쓰지 않습니다. */
            static void stageLink( ServiceTransaction& inoutTransaction, const string& key, const ServiceRecord& record, SocialLinkState oldState, SocialLinkState newState,
                                   UnixMilliseconds nowMs )
            {
                if ( newState == SocialLinkState::None )
                {
                    if ( record._version != ServiceRecord::kAbsentVersion )
                        inoutTransaction.erase( getLinkTable(), key, record._version );
                    return;
                }
                if ( newState != oldState )
                    inoutTransaction.put( getLinkTable(), key, encodeLink( newState, nowMs.count() ), record._version );
            }

            /** @brief 개수 변화를 트랜잭션에 붙입니다. 바뀌지 않아도 버전은 확인합니다. 상한 판정의 근거가 그사이 바뀌면 안 되기 때문입니다. */
            static void stageCounts( ServiceTransaction& inoutTransaction, const string& key, const ServiceRecord& record, SocialCounts counts, const SocialCounts& delta )
            {
                if ( delta.isZero() )
                {
                    inoutTransaction.requireVersion( getCountTable(), key, record._version );
                    return;
                }
                addCounts( counts, delta );
                inoutTransaction.put( getCountTable(), key, encodeCounts( counts ), record._version );
            }
        };

        /** @brief 관계 바꾸기 하나를 처리합니다. 이름으로 신청했으면 이름 인덱스를 읽고, 두 줄과 두 개수를 읽어 규칙으로 정한 뒤 버전 조건으로 씁니다. 충돌이 나면 다시 읽고 다시 시도합니다. */
        struct SocialLinkResult
        {
            SocialLinkDecision _decision{};
            AccountID          _otherID{ kInvalidAccountID }; ///< 이름으로 신청했으면 찾은 계정이다
        };

        class SocialLinkStoreRun final
        {
        public:
            SocialLinkStoreRun( const IAccountNameIndex* pNameIndex, SocialLinkOperation operation, AccountID accountID, AccountID otherID, string_view displayName, UnixMilliseconds nowMs )
                : _decision{}
                , _displayName{ displayName }
                , _pNameIndex{ pNameIndex }
                , _accountID{ accountID }
                , _otherID{ otherID }
                , _nowMs{ nowMs }
                , _operation{ operation }
            {
            }

            /** @brief 저장소 스레드에서 관계를 바꾸고 결정과 상대 계정을 넘깁니다. 한 번 쓰고 버린다. */
            SocialLinkResult run( IServiceStoreConnection& connection )
            {
                runAttempts( connection );
                return SocialLinkResult{ _decision, _otherID };
            }

        private:
            void runAttempts( IServiceStoreConnection& connection )
            {
                if ( _displayName.empty() == false && resolveName( connection ) == false )
                    return;
                for ( int32 attempt = 0; attempt < OnlineLimit::kMaxConflictRetry; ++attempt )
                {
                    const ServiceStoreResult commitResult = runOnce( connection );
                    if ( commitResult == ServiceStoreResult::Ok )
                        return;
                    if ( commitResult != ServiceStoreResult::Conflict )
                    {
                        _decision         = SocialLinkDecision{};
                        _decision._result = SocialServiceResult::fail( OnlineError::kUnavailable );
                        return;
                    }
                }
                _decision         = SocialLinkDecision{};
                _decision._result = SocialServiceResult::fail( OnlineError::kBusy );
            }

            /** @brief 이름으로 계정 id 를 찾습니다. 못 찾으면 결과를 적고 false 를 반환합니다. */
            [[nodiscard]] bool resolveName( IServiceStoreConnection& connection )
            {
                AccountIdentity          identity;
                const ServiceStoreResult found = _pNameIndex != nullptr ? _pNameIndex->readIdentityByDisplayName( connection, _displayName, identity )
                                                                        : ServiceStoreResult::NotFound;
                if ( found != ServiceStoreResult::Ok )
                {
                    _decision._result = found == ServiceStoreResult::NotFound ? SocialResult::NotFound : SocialServiceResult::fail( OnlineError::kUnavailable );
                    return false;
                }
                _otherID = identity._accountID;
                if ( _otherID == _accountID || _otherID == kInvalidAccountID )
                {
                    _decision._result = SocialServiceResult::fail( OnlineError::kInvalidRequest );
                    return false;
                }
                return true;
            }

            /** @brief 읽고, 정하고, 커밋합니다. 쓸 것이 없으면 Ok 이며 결정의 결과가 응답입니다. */
            ServiceStoreResult runOnce( IServiceStoreConnection& connection )
            {
                using Internal = SocialServiceInternal;
                ServiceRecord selfLink;
                ServiceRecord otherLink;
                ServiceRecord selfCount;
                ServiceRecord otherCount;
                const string  selfLinkKey   = Internal::makeLinkKey( _accountID, _otherID );
                const string  otherLinkKey  = Internal::makeLinkKey( _otherID, _accountID );
                const string  selfCountKey  = ServiceKeyUtil::makeHex64( _accountID );
                const string  otherCountKey = ServiceKeyUtil::makeHex64( _otherID );
                const bool    bReadOk       = Internal::readOrAbsent( connection, Internal::getLinkTable(), selfLinkKey, selfLink ) &&
                                     Internal::readOrAbsent( connection, Internal::getLinkTable(), otherLinkKey, otherLink ) &&
                                     Internal::readOrAbsent( connection, Internal::getCountTable(), selfCountKey, selfCount ) &&
                                     Internal::readOrAbsent( connection, Internal::getCountTable(), otherCountKey, otherCount );
                if ( bReadOk == false )
                    return ServiceStoreResult::Unavailable;

                SocialLinkState selfState  = SocialLinkState::None;
                SocialLinkState otherState = SocialLinkState::None;
                int64           sinceMs    = 0;
                SocialCounts    selfCounts;
                SocialCounts    otherCounts;
                if ( selfLink._version != ServiceRecord::kAbsentVersion && Internal::decodeLink( selfLink._bytes, selfState, sinceMs ) == false )
                    selfState = SocialLinkState::None;
                if ( otherLink._version != ServiceRecord::kAbsentVersion && Internal::decodeLink( otherLink._bytes, otherState, sinceMs ) == false )
                    otherState = SocialLinkState::None;
                if ( selfCount._version != ServiceRecord::kAbsentVersion && Internal::decodeCounts( selfCount._bytes, selfCounts ) == false )
                    selfCounts = SocialCounts{};
                if ( otherCount._version != ServiceRecord::kAbsentVersion && Internal::decodeCounts( otherCount._bytes, otherCounts ) == false )
                    otherCounts = SocialCounts{};

                _decision = SocialCounts::decide( _operation, selfState, otherState, selfCounts, otherCounts );
                if ( _decision._bWrite == SW_FALSE )
                    return ServiceStoreResult::Ok;

                ServiceTransaction transaction;
                Internal::stageLink( transaction, selfLinkKey, selfLink, selfState, _decision._newSelfState, _nowMs );
                Internal::stageLink( transaction, otherLinkKey, otherLink, otherState, _decision._newOtherState, _nowMs );
                Internal::stageCounts( transaction, selfCountKey, selfCount, selfCounts, _decision._selfDelta );
                Internal::stageCounts( transaction, otherCountKey, otherCount, otherCounts, _decision._otherDelta );
                return connection.commit( transaction );
            }

        private:
            SocialLinkDecision       _decision;
            string                   _displayName;
            const IAccountNameIndex* _pNameIndex;
            AccountID                _accountID;
            AccountID                _otherID;
            UnixMilliseconds         _nowMs;
            SocialLinkOperation      _operation;
        };

        /** @brief 한 계정의 관계를 모두 읽습니다. 읽지 못한 줄은 건너뛴다. */
        struct SocialStoreRun
        {
            static ServiceStoreRead<vector<SocialLink>> readLinks( IServiceStoreConnection& connection, AccountID accountID )
            {
                ServiceStoreRead<vector<SocialLink>> read;
                vector<ServiceRecord>                listRecord;
                const string                         prefix = ServiceKeyUtil::makeHex64( accountID ) + "/";
                read._result                                = connection.listRecords( SocialServiceInternal::getLinkTable(), prefix, "", SocialLimit::kMaxLinkPage, false, listRecord );
                if ( read._result != ServiceStoreResult::Ok )
                    return read;
                for ( const ServiceRecord& record : listRecord )
                {
                    SocialLink link;
                    const bool bParsed = ServiceKeyUtil::parseHex64( string_view( record._key ).substr( prefix.size() ), link._otherID ) &&
                                         SocialServiceInternal::decodeLink( record._bytes, link._state, link._sinceMs );
                    if ( bParsed )
                        read._value.push_back( link );
                }
                return read;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SocialService::SocialService()
        : _mapAccountToLocal{}
        , _mapFriendToWatchers{}
        , _mapCacheRequestToRead{}
        , _mapQuery{}
        , _completionBuffer{}
        , _notificationBuffer{}
        , _dependencies{}
        , _queryIDAllocator{}
        , _store{}
    {
    }

    SocialService::~SocialService()
    {
        shutdown();
    }

    void SocialService::initialize( const SocialServiceDependencies& dependencies )
    {
        SW_ASSERT( dependencies._pStore != nullptr );
        _dependencies = dependencies;
        _store.bindStore( dependencies._pStore );
        _replyOwner = dependencies._pRouter != nullptr ? dependencies._pRouter->openReplyOwner() : Subscription{};
    }

    void SocialService::shutdown()
    {
        _replyOwner.reset(); // 기다리던 접속 상태 답이 이 객체를 부르지 않게 지운다
        _mapCacheRequestToRead.clear();
        _mapQuery.clear();
        _mapAccountToLocal.clear();
        _mapFriendToWatchers.clear();
        _dependencies = SocialServiceDependencies{};
        _store.bindStore( nullptr );
    }

    void SocialService::update( UnixMilliseconds nowMs )
    {
        for ( auto& [accountID, local] : _mapAccountToLocal ) // 접속 상태의 시한을 연장한다
        {
            if ( local._status == SocialPresenceStatus::Offline || nowMs.count() - local._presenceWrittenMs < SocialServiceInternal::kPresenceRefreshMs )
                continue;
            local._presenceWrittenMs = nowMs.count();
            if ( writePresenceRecord( SocialPresence{ local._activity, accountID, local._status } ) == false )
                SW_LOG_WARNING_ID( "SocialService.SocialServicePresenceAccountNot", "SocialService: presence of account %# was not refreshed - it expires from the cache", accountID );
        }
    }

    void SocialService::changeLink( SocialLinkOperation operation, AccountID accountID, AccountID otherID, UnixMilliseconds nowMs, uint64 requestTag )
    {
        if ( otherID == kInvalidAccountID || otherID == accountID )
        {
            pushCompletion( requestTag, SocialServiceResult::fail( OnlineError::kInvalidRequest ) );
            return;
        }
        submitLinkJob( operation, accountID, otherID, string_view{}, nowMs, requestTag );
    }

    void SocialService::requestFriendByName( AccountID accountID, string_view displayName, UnixMilliseconds nowMs, uint64 requestTag )
    {
        if ( displayName.empty() )
        {
            pushCompletion( requestTag, SocialServiceResult::fail( OnlineError::kInvalidRequest ) );
            return;
        }
        submitLinkJob( SocialLinkOperation::Request, accountID, kInvalidAccountID, displayName, nowMs, requestTag );
    }

    void SocialService::submitLinkJob( SocialLinkOperation operation, AccountID accountID, AccountID otherID, string_view displayName, UnixMilliseconds nowMs, uint64 requestTag )
    {
        (void)ensureLocal( accountID ); // 항목을 있게만 한다 — 쓰는 쪽은 저마다 다시 찾는다
        _store.submit( [pNameIndex = _dependencies._pNameIndex, operation, accountID, otherID, name = string( displayName ), nowMs]( IServiceStoreConnection& connection )
        {
            SocialLinkStoreRun runner{ pNameIndex, operation, accountID, otherID, name, nowMs };
            return runner.run( connection );
        }, [this, requestTag, accountID]( const SocialLinkResult& done )
        { applyLinkChange( requestTag, accountID, done._otherID, done._decision ); } );
    }

    void SocialService::applyLinkChange( uint64 requestTag, AccountID accountID, AccountID otherID, const SocialLinkDecision& decision )
    {
        SocialCompletion completion;
        completion._requestTag = requestTag;
        completion._otherID    = otherID;
        completion._result     = decision._result;
        _completionBuffer.push( std::move( completion ) );
        if ( decision._result != SocialResult::Ok || decision._bWrite == SW_FALSE )
            return;

        // 이 서버의 메모리는 두 사람 모두(접속해 있으면) 다시 읽는다. 다른 서버에는 버스로 알린다.
        reloadIfLocal( accountID );
        reloadIfLocal( otherID );
        if ( _dependencies._pBus != nullptr )
        {
            (void)SocialBus::kLinksTopic.publish( *_dependencies._pBus, SocialLinkChanged{ accountID, otherID } ); // 실패는 발행이 로그로 남겼다. 다른 서버는 다음 로그인에 다시 읽는다
        }

        if ( decision._bNotifyOtherRequested == SW_TRUE )
            _notificationBuffer.push( SocialNotification{ SocialPresence{}, otherID, accountID, 0, SocialNotificationKind::FriendRequested } );
        if ( decision._bNotifyBothAdded == SW_TRUE )
        {
            _notificationBuffer.push( SocialNotification{ SocialPresence{}, otherID, accountID, 0, SocialNotificationKind::FriendAdded } );
            _notificationBuffer.push( SocialNotification{ SocialPresence{}, accountID, otherID, 0, SocialNotificationKind::FriendAdded } );
        }
        if ( decision._bNotifyOtherRemoved == SW_TRUE )
            _notificationBuffer.push( SocialNotification{ SocialPresence{}, otherID, accountID, 0, SocialNotificationKind::FriendRemoved } );
    }

    void SocialService::listLinks( AccountID accountID, uint64 requestTag )
    {
        (void)_mapAccountToLocal[accountID];
        startLoad( accountID, requestTag ); // 목록은 항상 저장소에서 읽고, 메모리는 그 결과로 교체한다
    }

    void SocialService::applyLinksLoaded( AccountID accountID, uint64 requestTag, bool bReadOk, vector<SocialLink>&& listLink )
    {
        const auto localIt = _mapAccountToLocal.find( accountID );
        if ( localIt != _mapAccountToLocal.end() )
        {
            localIt->second._bLoading = SW_FALSE;
            if ( bReadOk )
            {
                replaceLinks( accountID, localIt->second, listLink );
                localIt->second._bLoaded = SW_TRUE;
            }
            if ( localIt->second._bReloadPending == SW_TRUE ) // 이 읽기는 바뀌기 전 값일 수 있다 - 막음이 빠지면 차단이 새므로 다시 읽는다
            {
                localIt->second._bReloadPending = SW_FALSE;
                startLoad( accountID, 0 );
            }
        }
        if ( requestTag == 0 )
            return; // 내부에서 건 읽기이다. 첫 요청이나 버스 알림에서 시작한 것이다
        SocialCompletion completion;
        completion._requestTag = requestTag;
        completion._result     = bReadOk ? SocialResult::Ok : SocialServiceResult::fail( OnlineError::kUnavailable );
        completion._listLink   = std::move( listLink );
        _completionBuffer.push( std::move( completion ) );
    }

    bool SocialService::isBlockedLocal( AccountID ownerID, AccountID otherID ) const
    {
        const auto localIt = _mapAccountToLocal.find( ownerID );
        if ( localIt == _mapAccountToLocal.end() )
            return false;
        for ( const SocialLink& link : localIt->second._listLink )
        {
            if ( link._otherID == otherID )
                return link._state == SocialLinkState::Blocked;
        }
        return false;
    }

    void SocialService::setPresence( AccountID accountID, SocialPresenceStatus status, string_view activity, UnixMilliseconds nowMs, uint64 requestTag )
    {
        const bool bValid = status != SocialPresenceStatus::Offline && status < SocialPresenceStatus::Count &&
                            activity.size() <= static_cast<size_t>( SocialLimit::kMaxActivitySize );
        if ( bValid == false )
        {
            pushCompletion( requestTag, SocialServiceResult::fail( OnlineError::kInvalidRequest ) );
            return;
        }
        if ( writePresenceRecord( SocialPresence{ string( activity ), accountID, status } ) == false )
        {
            pushCompletion( requestTag, SocialServiceResult::fail( OnlineError::kInvalidRequest ) ); // 캐시 값을 못 쓰면 이 서버의 상태도 바꾸지 않는다
            return;
        }
        LocalAccount& local      = ensureLocal( accountID );
        local._status            = status;
        local._activity          = string( activity );
        local._presenceWrittenMs = nowMs.count();
        publishPresence( accountID, local );
        pushCompletion( requestTag, SocialResult::Ok );
    }

    void SocialService::queryFriendPresence( AccountID accountID, uint64 requestTag )
    {
        const LocalAccount& local   = ensureLocal( accountID );
        const uint64        queryID = _queryIDAllocator.allocate();
        PresenceQuery       query;
        query._requestTag = requestTag;
        for ( const SocialLink& link : local._listLink )
        {
            if ( link._state != SocialLinkState::Friend )
                continue;
            const auto friendIt = _mapAccountToLocal.find( link._otherID );
            if ( friendIt != _mapAccountToLocal.end() && friendIt->second._status != SocialPresenceStatus::Offline )
            {
                query._listPresence.push_back( SocialPresence{ friendIt->second._activity, link._otherID, friendIt->second._status } ); // 이 서버의 계정이므로 캐시를 읽지 않는다
                continue;
            }
            if ( _dependencies._pRouter == nullptr )
            {
                query._listPresence.push_back( SocialPresence{ string{}, link._otherID, SocialPresenceStatus::Offline } );
                continue;
            }
            const uint64 cacheRequestID            = _dependencies._pRouter->submit( EphemeralRequest::makeGet( SocialServiceInternal::makePresenceKey( link._otherID ) ),
                                                                                     EphemeralStoreRouter::ReplyDelegate::create<&SocialService::onPresenceReply>( this ), _replyOwner );
            _mapCacheRequestToRead[cacheRequestID] = PresenceRead{ queryID, link._otherID };
            ++query._outstandingCount;
        }
        if ( query._outstandingCount > 0 )
        {
            _mapQuery[queryID] = std::move( query );
            return;
        }
        SocialCompletion completion;
        completion._requestTag   = requestTag;
        completion._listPresence = std::move( query._listPresence );
        _completionBuffer.push( std::move( completion ) );
    }

    void SocialService::removeAccount( AccountID accountID )
    {
        const auto localIt = _mapAccountToLocal.find( accountID );
        if ( localIt == _mapAccountToLocal.end() )
            return;
        if ( localIt->second._status != SocialPresenceStatus::Offline )
        {
            localIt->second._status = SocialPresenceStatus::Offline;
            localIt->second._activity.clear();
            publishPresence( accountID, localIt->second );
            if ( _dependencies._pRouter != nullptr )
                _dependencies._pRouter->submit( EphemeralRequest::makeErase( SocialServiceInternal::makePresenceKey( accountID ) ) );
        }
        removeFriendWatcher( accountID, localIt->second._listLink );
        _mapAccountToLocal.erase( localIt );
    }

    void SocialService::handleBusMessage( const ServerBusMessage& message )
    {
        SocialLinkChanged changed;
        if ( SocialBus::kLinksTopic.read( message, changed ) == ServerBusReadResult::Read )
        {
            reloadIfLocal( changed._firstID );
            reloadIfLocal( changed._secondID );
            return;
        }
        SocialPresence presence;
        if ( SocialBus::kPresenceTopic.read( message, presence ) == ServerBusReadResult::Read )
            notifyFriendsOfPresence( presence );
    }

    SocialService::LocalAccount& SocialService::ensureLocal( AccountID accountID )
    {
        LocalAccount& local = _mapAccountToLocal[accountID];
        if ( local._bLoaded == SW_FALSE && local._bLoading == SW_FALSE )
            startLoad( accountID, 0 );
        return local;
    }

    void SocialService::startLoad( AccountID accountID, uint64 requestTag )
    {
        _mapAccountToLocal[accountID]._bLoading = SW_TRUE;
        _store.submit( [accountID]( IServiceStoreConnection& connection )
        { return SocialStoreRun::readLinks( connection, accountID ); },
                       [this, accountID, requestTag]( ServiceStoreRead<vector<SocialLink>>& read )
        { applyLinksLoaded( accountID, requestTag, read.isOk(), std::move( read._value ) ); } );
    }

    void SocialService::reloadIfLocal( AccountID accountID )
    {
        const auto localIt = _mapAccountToLocal.find( accountID );
        if ( localIt == _mapAccountToLocal.end() )
            return;
        if ( localIt->second._bLoading == SW_TRUE )
            localIt->second._bReloadPending = SW_TRUE;
        else
            startLoad( accountID, 0 );
    }

    void SocialService::replaceLinks( AccountID accountID, LocalAccount& local, const vector<SocialLink>& listLink )
    {
        removeFriendWatcher( accountID, local._listLink );
        local._listLink = listLink;
        addFriendWatcher( accountID, local._listLink );
    }

    void SocialService::addFriendWatcher( AccountID accountID, const vector<SocialLink>& listLink )
    {
        for ( const SocialLink& link : listLink )
        {
            if ( link._state == SocialLinkState::Friend )
                _mapFriendToWatchers[link._otherID].push_back( accountID ); // 저장소 키가 `<나>/<상대>` 라 한 목록에 같은 상대가 두 번 오지 않는다
        }
    }

    void SocialService::removeFriendWatcher( AccountID accountID, const vector<SocialLink>& listLink )
    {
        for ( const SocialLink& link : listLink )
        {
            if ( link._state != SocialLinkState::Friend )
                continue;
            const auto watchersIt = _mapFriendToWatchers.find( link._otherID );
            if ( watchersIt == _mapFriendToWatchers.end() ) // addFriendWatcher 가 넣은 것만 뺀다 — 없으면 역색인이 관계 목록과 갈라진 것이다
                SW_FATAL( "SocialService: friend index has no watchers of account %# while account %# lists it", link._otherID, accountID );
            // shortcut: 지켜보는 계정 목록을 선형으로 찾는다. 한 서버에 같은 사람을 친구로 둔 계정이 수천을 넘는 일이 측정되면 집합으로 바꾼다
            vector<AccountID>& listWatcher = watchersIt->second;
            const auto         watcherIt   = std::find( listWatcher.begin(), listWatcher.end(), accountID );
            if ( watcherIt == listWatcher.end() )
                SW_FATAL( "SocialService: friend index lost watcher %# of account %#", accountID, link._otherID );
            *watcherIt = listWatcher.back();
            listWatcher.pop_back();
            if ( listWatcher.empty() )
                _mapFriendToWatchers.erase( watchersIt );
        }
    }

    bool SocialService::writePresenceRecord( const SocialPresence& presence )
    {
        vector<uint8> bytes;
        if ( SocialProtocol::encodePresenceRecord( presence, bytes ) == false )
            return false;
        if ( _dependencies._pRouter == nullptr )
            return true;
        _dependencies._pRouter->submit( EphemeralRequest::makeSet( SocialServiceInternal::makePresenceKey( presence._accountID ), std::move( bytes ),
                                                                   SocialServiceInternal::kPresenceTTLMs ) );
        return true;
    }

    void SocialService::publishPresence( AccountID accountID, const LocalAccount& local )
    {
        const SocialPresence presence{ local._activity, accountID, local._status };
        notifyFriendsOfPresence( presence ); // 이 서버의 계정이다
        if ( _dependencies._pBus == nullptr )
            return;
        (void)SocialBus::kPresenceTopic.publish( *_dependencies._pBus, presence ); // 실패는 발행이 로그로 남겼다
    }

    void SocialService::notifyFriendsOfPresence( const SocialPresence& presence )
    {
        const auto watchersIt = _mapFriendToWatchers.find( presence._accountID ); // 서버 전 계정을 훑지 않고 이 계정을 친구로 둔 계정만 본다
        if ( watchersIt == _mapFriendToWatchers.end() )
            return;
        for ( const AccountID watcherID : watchersIt->second )
        {
            _notificationBuffer.push( SocialNotification{ presence, watcherID, presence._accountID, 0, SocialNotificationKind::PresenceChanged } );
        }
    }

    void SocialService::onPresenceReply( const EphemeralReply& reply )
    {
        const auto readIt = _mapCacheRequestToRead.find( reply._requestID );
        if ( readIt == _mapCacheRequestToRead.end() )
            return;
        const PresenceRead read = readIt->second;
        _mapCacheRequestToRead.erase( readIt );
        const auto queryIt = _mapQuery.find( read._queryID );
        if ( queryIt == _mapQuery.end() )
            return;
        PresenceQuery& query = queryIt->second;
        SocialPresence presence{ string{}, read._friendID, SocialPresenceStatus::Offline };
        if ( reply._result == EphemeralResult::Ok )
        {
            if ( SocialProtocol::decodePresenceRecord( reply._value, presence ) == false || presence._accountID != read._friendID )
                presence = SocialPresence{ string{}, read._friendID, SocialPresenceStatus::Offline };
        }
        query._listPresence.push_back( std::move( presence ) );
        if ( --query._outstandingCount > 0 )
            return;
        SocialCompletion completion;
        completion._requestTag   = query._requestTag;
        completion._listPresence = std::move( query._listPresence );
        _completionBuffer.push( std::move( completion ) );
        _mapQuery.erase( queryIt );
    }

    void SocialService::pushCompletion( uint64 requestTag, SocialServiceResult result )
    {
        SocialCompletion completion;
        completion._requestTag = requestTag;
        completion._result     = result;
        _completionBuffer.push( std::move( completion ) );
    }
} // namespace sw
