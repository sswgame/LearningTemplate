#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Server/SocialService.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Identity/AccountNameIndex.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/Kits/Feature/Online/Social/Shared/SocialProtocol.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct SocialServiceInternal
        {
            static constexpr uint8 kFormatVersion     = 1;
            static constexpr int32 kMaxConflictRetry  = 4;
            static constexpr int64 kPresenceTtlMs     = 90000;
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

            static string makeLinkKey( AccountId ownerId, AccountId otherId )
            {
                string key = ServiceKeyUtil::makeHex64( ownerId );
                key.push_back( '/' );
                ServiceKeyUtil::appendHex64( key, otherId );
                return key;
            }

            static string makePresenceKey( AccountId accountId ) { return string( "social/rp/" ) + ServiceKeyUtil::makeHex64( accountId ); }

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
                outCounts._friendCount   = static_cast<int32>( reader.readVarInt() );
                outCounts._incomingCount = static_cast<int32>( reader.readVarInt() );
                outCounts._outgoingCount = static_cast<int32>( reader.readVarInt() );
                outCounts._blockedCount  = static_cast<int32>( reader.readVarInt() );
                return reader.hasOverflowed() == false;
            }

            static void addCounts( SocialCounts& inoutCounts, const SocialCounts& delta )
            {
                inoutCounts._friendCount   = std::max( 0, inoutCounts._friendCount + delta._friendCount );
                inoutCounts._incomingCount = std::max( 0, inoutCounts._incomingCount + delta._incomingCount );
                inoutCounts._outgoingCount = std::max( 0, inoutCounts._outgoingCount + delta._outgoingCount );
                inoutCounts._blockedCount  = std::max( 0, inoutCounts._blockedCount + delta._blockedCount );
            }

            /** @brief 레코드를 읽습니다 — 저장소가 아프면 false(없음은 true — 판이 `kAbsentVersion`). */
            [[nodiscard]] static bool readOrAbsent( IServiceStoreConnection& connection, const hashed_string& table, string_view key, ServiceRecord& outRecord )
            {
                const ServiceStoreResult result = connection.readRecord( table, key, outRecord );
                return result == ServiceStoreResult::Ok || result == ServiceStoreResult::NotFound;
            }

            /** @brief 줄 하나의 새 상태를 트랜잭션에 붙입니다(같으면 아무것도 쓰지 않는다). */
            static void stageLink( ServiceTransaction& inoutTransaction, const string& key, const ServiceRecord& record, SocialLinkState oldState, SocialLinkState newState,
                                   int64 nowMs )
            {
                if ( newState == SocialLinkState::None )
                {
                    if ( record._version != ServiceRecord::kAbsentVersion )
                        inoutTransaction.erase( getLinkTable(), key, record._version );
                    return;
                }
                if ( newState != oldState )
                    inoutTransaction.put( getLinkTable(), key, encodeLink( newState, nowMs ), record._version );
            }

            /** @brief 개수 차이를 붙입니다 — 바뀌지 않아도 판은 본다(상한 판정의 근거가 그새 바뀌면 안 된다). */
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

        /** @brief 관계 바꾸기 하나 — (이름이면 이름 색인을 읽고) 두 줄 · 두 개수를 읽어 규칙으로 정하고 판 조건으로 쓴다. 충돌이면 다시 읽고 다시. */
        class SocialLinkWork final : public IServiceStoreWork
        {
        public:
            SocialLinkWork( SocialService* pService, const IAccountNameIndex* pNameIndex, SocialLinkOperation operation, AccountId accountId, AccountId otherId,
                            string_view displayName, int64 nowMs, uint64 requestTag )
                : _decision{}
                , _displayName{ displayName }
                , _pService{ pService }
                , _pNameIndex{ pNameIndex }
                , _accountId{ accountId }
                , _otherId{ otherId }
                , _nowMs{ nowMs }
                , _requestTag{ requestTag }
                , _operation{ operation }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                if ( _displayName.empty() == false && resolveName( connection ) == false )
                    return;
                for ( int32 attempt = 0; attempt < SocialServiceInternal::kMaxConflictRetry; ++attempt )
                {
                    const ServiceStoreResult commitResult = runOnce( connection );
                    if ( commitResult == ServiceStoreResult::Ok )
                        return;
                    if ( commitResult != ServiceStoreResult::Conflict )
                    {
                        _decision         = SocialLinkDecision{};
                        _decision._result = SocialResult::Unavailable;
                        return;
                    }
                }
                _decision         = SocialLinkDecision{};
                _decision._result = SocialResult::Conflict;
            }

            void complete() override { _pService->applyLinkChange( _requestTag, _accountId, _otherId, _decision ); }

        private:
            /** @brief 이름 → 계정 id. 못 찾으면 결과를 적고 false. */
            bool resolveName( IServiceStoreConnection& connection )
            {
                AccountIdentity          identity;
                const ServiceStoreResult found = _pNameIndex != nullptr ? _pNameIndex->readIdentityByDisplayName( connection, _displayName, identity )
                                                                        : ServiceStoreResult::NotFound;
                if ( found != ServiceStoreResult::Ok )
                {
                    _decision._result = found == ServiceStoreResult::NotFound ? SocialResult::NotFound : SocialResult::Unavailable;
                    return false;
                }
                _otherId = identity._accountId;
                if ( _otherId == _accountId || _otherId == kInvalidAccountId )
                {
                    _decision._result = SocialResult::Invalid;
                    return false;
                }
                return true;
            }

            /** @brief 읽고 · 정하고 · 커밋합니다. 쓸 것이 없으면 Ok(결정의 결과가 답이다). */
            ServiceStoreResult runOnce( IServiceStoreConnection& connection )
            {
                using Internal = SocialServiceInternal;
                ServiceRecord selfLink;
                ServiceRecord otherLink;
                ServiceRecord selfCount;
                ServiceRecord otherCount;
                const string  selfLinkKey   = Internal::makeLinkKey( _accountId, _otherId );
                const string  otherLinkKey  = Internal::makeLinkKey( _otherId, _accountId );
                const string  selfCountKey  = ServiceKeyUtil::makeHex64( _accountId );
                const string  otherCountKey = ServiceKeyUtil::makeHex64( _otherId );
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

                _decision = SocialLinkRules::decide( _operation, selfState, otherState, selfCounts, otherCounts );
                if ( _decision._bWrite == SW_FALSE )
                    return ServiceStoreResult::Ok;

                ServiceTransaction transaction;
                Internal::stageLink( transaction, selfLinkKey, selfLink, selfState, _decision._newSelfState, _nowMs );
                Internal::stageLink( transaction, otherLinkKey, otherLink, otherState, _decision._newOtherState, _nowMs );
                Internal::stageCounts( transaction, selfCountKey, selfCount, selfCounts, _decision._selfDelta );
                Internal::stageCounts( transaction, otherCountKey, otherCount, otherCounts, _decision._otherDelta );
                return connection.commit( transaction );
            }

            SocialLinkDecision       _decision;
            string                   _displayName;
            SocialService*           _pService;
            const IAccountNameIndex* _pNameIndex;
            AccountId                _accountId;
            AccountId                _otherId;
            int64                    _nowMs;
            uint64                   _requestTag;
            SocialLinkOperation      _operation;
        };

        /** @brief 한 계정의 관계를 모두 읽는 일입니다. */
        class SocialLoadWork final : public IServiceStoreWork
        {
        public:
            SocialLoadWork( SocialService* pService, AccountId accountId, uint64 requestTag )
                : _listLink{}
                , _pService{ pService }
                , _accountId{ accountId }
                , _requestTag{ requestTag }
                , _bReadOk{ SW_FALSE }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                vector<ServiceRecord> listRecord;
                const string          prefix = ServiceKeyUtil::makeHex64( _accountId ) + "/";
                if ( connection.listRecords( SocialServiceInternal::getLinkTable(), prefix, "", SocialLimit::kMaxLinkPage, false, listRecord ) != ServiceStoreResult::Ok )
                    return;
                for ( const ServiceRecord& record : listRecord )
                {
                    SocialLink link;
                    const bool bParsed = ServiceKeyUtil::parseHex64( string_view( record._key ).substr( prefix.size() ), link._otherId ) &&
                                         SocialServiceInternal::decodeLink( record._bytes, link._state, link._sinceMs );
                    if ( bParsed )
                        _listLink.push_back( link );
                }
                _bReadOk = SW_TRUE;
            }

            void complete() override { _pService->applyLinksLoaded( _accountId, _requestTag, _bReadOk == SW_TRUE, std::move( _listLink ) ); }

        private:
            vector<SocialLink> _listLink;
            SocialService*     _pService;
            AccountId          _accountId;
            uint64             _requestTag;
            uint8              _bReadOk;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SocialService::SocialService()
        : _mapAccountToLocal{}
        , _mapCacheRequestToRead{}
        , _mapQuery{}
        , _completionBuffer{}
        , _notificationBuffer{}
        , _dependencies{}
        , _nextQueryId{ 1 }
        , _pendingCount{ 0 }
    {
    }

    SocialService::~SocialService() { shutdown(); }

    void SocialService::initialize( const SocialServiceDependencies& dependencies )
    {
        SW_ASSERT( dependencies._pStore != nullptr );
        _dependencies = dependencies;
    }

    void SocialService::shutdown()
    {
        if ( _dependencies._pRouter != nullptr )
        {
            for ( const auto& [cacheRequestId, read] : _mapCacheRequestToRead )
            {
                _dependencies._pRouter->cancel( cacheRequestId );
            }
        }
        _mapCacheRequestToRead.clear();
        _mapQuery.clear();
        _mapAccountToLocal.clear();
        _dependencies = SocialServiceDependencies{};
    }

    void SocialService::tick( int64 nowMs )
    {
        for ( auto& [accountId, local] : _mapAccountToLocal ) // 접속 상태 시한 연장
        {
            if ( local._status == SocialPresenceStatus::Offline || nowMs - local._presenceWrittenMs < SocialServiceInternal::kPresenceRefreshMs )
                continue;
            local._presenceWrittenMs = nowMs;
            writePresenceRecord( accountId, local );
        }
    }

    void SocialService::changeLink( SocialLinkOperation operation, AccountId accountId, AccountId otherId, int64 nowMs, uint64 requestTag )
    {
        if ( otherId == kInvalidAccountId || otherId == accountId )
        {
            pushCompletion( requestTag, SocialResult::Invalid );
            return;
        }
        submitLinkWork( operation, accountId, otherId, string_view{}, nowMs, requestTag );
    }

    void SocialService::requestFriendByName( AccountId accountId, string_view displayName, int64 nowMs, uint64 requestTag )
    {
        if ( displayName.empty() )
        {
            pushCompletion( requestTag, SocialResult::Invalid );
            return;
        }
        submitLinkWork( SocialLinkOperation::Request, accountId, kInvalidAccountId, displayName, nowMs, requestTag );
    }

    void SocialService::submitLinkWork( SocialLinkOperation operation, AccountId accountId, AccountId otherId, string_view displayName, int64 nowMs, uint64 requestTag )
    {
        (void)ensureLocal( accountId );
        ++_pendingCount;
        _dependencies._pStore->submit( sw::make_unique<SocialLinkWork>( this, _dependencies._pNameIndex, operation, accountId, otherId, displayName, nowMs, requestTag ) );
    }

    void SocialService::applyLinkChange( uint64 requestTag, AccountId accountId, AccountId otherId, const SocialLinkDecision& decision )
    {
        --_pendingCount;
        SocialCompletion completion;
        completion._requestTag = requestTag;
        completion._otherId    = otherId;
        completion._result     = decision._result;
        _completionBuffer.push( std::move( completion ) );
        if ( decision._result != SocialResult::Ok || decision._bWrite == SW_FALSE )
            return;

        // 이 서버 메모리 — 두 사람 모두(붙어 있으면) 다시 읽는다. 다른 서버는 버스로.
        reloadIfLocal( accountId );
        reloadIfLocal( otherId );
        if ( _dependencies._pBus != nullptr )
        {
            BitWriter body;
            body.writeVarUint( accountId );
            body.writeVarUint( otherId );
            _dependencies._pBus->publish( SocialBus::kLinksTopic, body.getBytes().data(), body.getByteCount() );
        }

        if ( decision._bNotifyOtherRequested == SW_TRUE )
            _notificationBuffer.push( SocialNotification{ SocialPresence{}, otherId, accountId, 0, SocialNotificationKind::FriendRequested } );
        if ( decision._bNotifyBothAdded == SW_TRUE )
        {
            _notificationBuffer.push( SocialNotification{ SocialPresence{}, otherId, accountId, 0, SocialNotificationKind::FriendAdded } );
            _notificationBuffer.push( SocialNotification{ SocialPresence{}, accountId, otherId, 0, SocialNotificationKind::FriendAdded } );
        }
        if ( decision._bNotifyOtherRemoved == SW_TRUE )
            _notificationBuffer.push( SocialNotification{ SocialPresence{}, otherId, accountId, 0, SocialNotificationKind::FriendRemoved } );
    }

    void SocialService::listLinks( AccountId accountId, uint64 requestTag )
    {
        (void)_mapAccountToLocal[accountId];
        startLoad( accountId, requestTag ); // 목록은 언제나 저장소에서(메모리는 그 김에 갈아 끼운다)
    }

    void SocialService::applyLinksLoaded( AccountId accountId, uint64 requestTag, bool bReadOk, vector<SocialLink>&& listLink )
    {
        --_pendingCount;
        const auto localIt = _mapAccountToLocal.find( accountId );
        if ( localIt != _mapAccountToLocal.end() )
        {
            localIt->second._bLoading = SW_FALSE;
            if ( bReadOk )
            {
                localIt->second._listLink = listLink;
                localIt->second._bLoaded  = SW_TRUE;
            }
        }
        if ( requestTag == 0 )
            return; // 안에서 건 읽기(첫 요청 · 버스 알림)
        SocialCompletion completion;
        completion._requestTag = requestTag;
        completion._result     = bReadOk ? SocialResult::Ok : SocialResult::Unavailable;
        completion._listLink   = std::move( listLink );
        _completionBuffer.push( std::move( completion ) );
    }

    bool SocialService::isBlockedLocal( AccountId ownerId, AccountId otherId ) const
    {
        const auto localIt = _mapAccountToLocal.find( ownerId );
        if ( localIt == _mapAccountToLocal.end() )
            return false;
        for ( const SocialLink& link : localIt->second._listLink )
        {
            if ( link._otherId == otherId )
                return link._state == SocialLinkState::Blocked;
        }
        return false;
    }

    void SocialService::setPresence( AccountId accountId, SocialPresenceStatus status, string_view activity, int64 nowMs, uint64 requestTag )
    {
        const bool bValid = status != SocialPresenceStatus::Offline && status < SocialPresenceStatus::Count &&
                            activity.size() <= static_cast<size_t>( SocialLimit::kMaxActivitySize );
        if ( bValid == false )
        {
            pushCompletion( requestTag, SocialResult::Invalid );
            return;
        }
        LocalAccount& local      = ensureLocal( accountId );
        local._status            = status;
        local._activity          = string( activity );
        local._presenceWrittenMs = nowMs;
        writePresenceRecord( accountId, local );
        publishPresence( accountId, local );
        pushCompletion( requestTag, SocialResult::Ok );
    }

    void SocialService::queryFriendPresence( AccountId accountId, uint64 requestTag )
    {
        const LocalAccount& local   = ensureLocal( accountId );
        const uint64        queryId = _nextQueryId++;
        PresenceQuery       query;
        query._requestTag = requestTag;
        for ( const SocialLink& link : local._listLink )
        {
            if ( link._state != SocialLinkState::Friend )
                continue;
            const auto friendIt = _mapAccountToLocal.find( link._otherId );
            if ( friendIt != _mapAccountToLocal.end() && friendIt->second._status != SocialPresenceStatus::Offline )
            {
                query._listPresence.push_back( SocialPresence{ friendIt->second._activity, link._otherId, friendIt->second._status } ); // 이 서버 — 캐시를 읽지 않는다
                continue;
            }
            if ( _dependencies._pRouter == nullptr )
            {
                query._listPresence.push_back( SocialPresence{ string{}, link._otherId, SocialPresenceStatus::Offline } );
                continue;
            }
            const uint64 cacheRequestId            = _dependencies._pRouter->submit( EphemeralRequest::makeGet( SocialServiceInternal::makePresenceKey( link._otherId ) ),
                                                                                     EphemeralStoreRouter::ReplyDelegate::create<&SocialService::onPresenceReply>( this ) );
            _mapCacheRequestToRead[cacheRequestId] = PresenceRead{ queryId, link._otherId };
            ++query._outstandingCount;
        }
        if ( query._outstandingCount > 0 )
        {
            _mapQuery[queryId] = std::move( query );
            return;
        }
        SocialCompletion completion;
        completion._requestTag   = requestTag;
        completion._listPresence = std::move( query._listPresence );
        _completionBuffer.push( std::move( completion ) );
    }

    void SocialService::removeAccount( AccountId accountId )
    {
        const auto localIt = _mapAccountToLocal.find( accountId );
        if ( localIt == _mapAccountToLocal.end() )
            return;
        if ( localIt->second._status != SocialPresenceStatus::Offline )
        {
            localIt->second._status = SocialPresenceStatus::Offline;
            localIt->second._activity.clear();
            publishPresence( accountId, localIt->second );
            if ( _dependencies._pRouter != nullptr )
                (void)_dependencies._pRouter->submit( EphemeralRequest::makeErase( SocialServiceInternal::makePresenceKey( accountId ) ), EphemeralStoreRouter::ReplyDelegate{} );
        }
        _mapAccountToLocal.erase( localIt );
    }

    void SocialService::handleBusMessage( string_view topic, const vector<uint8>& bytes )
    {
        BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        if ( topic == SocialBus::kLinksTopic )
        {
            const AccountId firstId  = reader.readVarUint();
            const AccountId secondId = reader.readVarUint();
            if ( reader.hasOverflowed() )
                return;
            reloadIfLocal( firstId );
            reloadIfLocal( secondId );
            return;
        }
        if ( topic == SocialBus::kPresenceTopic )
        {
            SocialPresence presence;
            if ( SocialProtocol::readPresence( reader, presence ) )
                notifyFriendsOfPresence( presence );
        }
    }

    SocialService::LocalAccount& SocialService::ensureLocal( AccountId accountId )
    {
        LocalAccount& local = _mapAccountToLocal[accountId];
        if ( local._bLoaded == SW_FALSE && local._bLoading == SW_FALSE )
            startLoad( accountId, 0 );
        return local;
    }

    void SocialService::startLoad( AccountId accountId, uint64 requestTag )
    {
        _mapAccountToLocal[accountId]._bLoading = SW_TRUE;
        ++_pendingCount;
        _dependencies._pStore->submit( sw::make_unique<SocialLoadWork>( this, accountId, requestTag ) );
    }

    void SocialService::reloadIfLocal( AccountId accountId )
    {
        const auto localIt = _mapAccountToLocal.find( accountId );
        if ( localIt != _mapAccountToLocal.end() && localIt->second._bLoading == SW_FALSE )
            startLoad( accountId, 0 );
    }

    void SocialService::writePresenceRecord( AccountId accountId, const LocalAccount& local )
    {
        if ( _dependencies._pRouter == nullptr )
            return;
        BitWriter            body;
        const SocialPresence presence{ local._activity, accountId, local._status };
        SocialProtocol::writePresence( body, presence );
        (void)_dependencies._pRouter->submit( EphemeralRequest::makeSet( SocialServiceInternal::makePresenceKey( accountId ), body.getBytes(), SocialServiceInternal::kPresenceTtlMs ),
                                              EphemeralStoreRouter::ReplyDelegate{} );
    }

    void SocialService::publishPresence( AccountId accountId, const LocalAccount& local )
    {
        const SocialPresence presence{ local._activity, accountId, local._status };
        notifyFriendsOfPresence( presence ); // 이 서버
        if ( _dependencies._pBus == nullptr )
            return;
        BitWriter body;
        SocialProtocol::writePresence( body, presence );
        _dependencies._pBus->publish( SocialBus::kPresenceTopic, body.getBytes().data(), body.getByteCount() );
    }

    void SocialService::notifyFriendsOfPresence( const SocialPresence& presence )
    {
        for ( const auto& [accountId, local] : _mapAccountToLocal )
        {
            if ( accountId == presence._accountId )
                continue;
            for ( const SocialLink& link : local._listLink )
            {
                if ( link._otherId == presence._accountId && link._state == SocialLinkState::Friend )
                {
                    _notificationBuffer.push( SocialNotification{ presence, accountId, presence._accountId, 0, SocialNotificationKind::PresenceChanged } );
                    break;
                }
            }
        }
    }

    void SocialService::onPresenceReply( const EphemeralReply& reply )
    {
        const auto readIt = _mapCacheRequestToRead.find( reply._requestId );
        if ( readIt == _mapCacheRequestToRead.end() )
            return;
        const PresenceRead read = readIt->second;
        _mapCacheRequestToRead.erase( readIt );
        const auto queryIt = _mapQuery.find( read._queryId );
        if ( queryIt == _mapQuery.end() )
            return;
        PresenceQuery& query = queryIt->second;
        SocialPresence presence{ string{}, read._friendId, SocialPresenceStatus::Offline };
        if ( reply._result == EphemeralResult::Ok )
        {
            BitReader reader( reply._value.data(), static_cast<int32>( reply._value.size() ) );
            if ( SocialProtocol::readPresence( reader, presence ) == false || presence._accountId != read._friendId )
                presence = SocialPresence{ string{}, read._friendId, SocialPresenceStatus::Offline };
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

    void SocialService::pushCompletion( uint64 requestTag, SocialResult result )
    {
        SocialCompletion completion;
        completion._requestTag = requestTag;
        completion._result     = result;
        _completionBuffer.push( std::move( completion ) );
    }
} // namespace sw
