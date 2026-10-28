#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetPairedAnimation/PairedAnimationReplication.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Character/PairedAnimation/PairedAnimationSystem.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/NetObjectTable.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "PairedAnimationReplication" );

    namespace
    {
        /** @brief 애니메이터를 가진 오브젝트의 번호(`NetObjectTable`)입니다. 핸들이 풀리지 않거나 번호가 없으면 0 입니다. */
        uint32 findAnimatorNetID( GameObjectManager& manager, const TComponentHandle<SkeletalAnimatorComponent>& animator )
        {
            const SkeletalAnimatorComponent* pAnimator = manager.resolveComponent( animator );
            const GameObject*                pOwner    = pAnimator != nullptr ? pAnimator->getOwner() : nullptr;
            return pOwner != nullptr ? manager.getSubsystem<NetObjectTable>().findID( *pOwner ) : 0u;
        }

        /** @brief 번호의 오브젝트에 있는 애니메이터입니다. 오브젝트나 애니메이터가 없으면 무효 핸들입니다. */
        TComponentHandle<SkeletalAnimatorComponent> findNetAnimator( GameObjectManager& manager, uint32 netID )
        {
            GameObject*                      pObject   = manager.getSubsystem<NetObjectTable>().findObject( netID );
            const SkeletalAnimatorComponent* pAnimator = pObject != nullptr ? pObject->getComponent<SkeletalAnimatorComponent>() : nullptr;
            return pAnimator != nullptr ? TComponentHandle<SkeletalAnimatorComponent>( pAnimator ) : TComponentHandle<SkeletalAnimatorComponent>{};
        }

        /**
         * @brief 두 프로세스 확인이 로그를 맞대는 사건 줄입니다: `[PairedAnimationNet] side=<server|client> event=<start|join|end|restore> session=<번호> anchor=<앵커 시각 초>`.
         * @details 줄 형식의 정본은 이 함수이고 `Scripts/common/PairedAnimationNetLog.py` 가 읽는다. `start` 는 서버가 새 세션을 모두에게 보냈고, `join` 은 늦게 붙은 연결에 재생 중인 세션을
         *          보냈고, `end` 는 끝 메시지(서버가 보냈다 · 클라이언트가 받았다), `restore` 는 클라이언트가 상태를 이어 받았다. 끝 줄의 앵커 시각은 뜻이 없어 0 이다.
         */
        string formatEventLine( const utf8* pSide, const utf8* pEvent, uint32 sessionID, float32 anchorTime )
        {
            StringBuilder<constant::kMaxBuffer256> text;
            text.appendFormat( "[PairedAnimationNet] side=%# event=%# session=%# anchor=%#", pSide, pEvent, sessionID, anchorTime );
            return string( text.c_str(), text.size() );
        }
    } // namespace
} // namespace sw

namespace sw
{
    bool PairedAnimationNetState::write( BitWriter& writer ) const
    {
        const bool bPathFits  = _assetPath.empty() == false && _assetPath.size() < constant::kMaxPathSize;
        const bool bRoleCount = _listRoleNetID.size() <= kMaxRoleCount;
        if ( bPathFits == false || bRoleCount == false )
            return false;
        vector<uint8> snapshotBytes;
        _snapshot.writeBytes( snapshotBytes );
        if ( snapshotBytes.size() > static_cast<size_t>( kMaxSnapshotBytes ) )
            return false;
        writer.writeBlob( reinterpret_cast<const uint8*>( _assetPath.data() ), static_cast<int32>( _assetPath.size() ) );
        writer.writeVarUint( _listRoleNetID.size() );
        for ( const uint32 netID : _listRoleNetID )
        {
            writer.writeVarUint( netID );
        }
        writer.writeBlob( snapshotBytes.data(), static_cast<int32>( snapshotBytes.size() ) );
        return true;
    }

    bool PairedAnimationNetState::read( BitReader& reader, PairedAnimationNetState& outState )
    {
        outState = PairedAnimationNetState{};
        PairedAnimationNetState state{};
        vector<uint8>           pathBytes;
        if ( reader.readBlob( pathBytes, static_cast<int32>( constant::kMaxPathSize ) - 1 ) == false || pathBytes.empty() )
            return false;
        state._assetPath.assign( reinterpret_cast<const utf8*>( pathBytes.data() ), pathBytes.size() );
        const uint64 roleCount = reader.readVarUint();
        if ( reader.hasOverflowed() || roleCount > kMaxRoleCount )
            return false;
        state._listRoleNetID.resize( static_cast<size_t>( roleCount ) );
        for ( uint32& netID : state._listRoleNetID )
        {
            netID = reader.readVarUint32();
        }
        vector<uint8> snapshotBytes;
        if ( reader.readBlob( snapshotBytes, kMaxSnapshotBytes ) == false )
            return false;
        if ( PairedAnimationSessionSnapshot::readBytes( snapshotBytes, state._snapshot ) != PairedAnimationSessionSnapshot::ReadResult::Success )
            return false;
        if ( reader.hasOverflowed() )
            return false;
        outState = std::move( state );
        return true;
    }
} // namespace sw

namespace sw
{
    PairedAnimationReplicationServer::PairedAnimationReplicationServer()
        : _listAnnouncedID{}
        , _listRefusedID{}
        , _listJoiningConnection{}
        , _stats{}
        , _writer{}
        , _pHost{ nullptr }
        , _pManager{ nullptr }
    {
    }

    void PairedAnimationReplicationServer::initialize( INetPeers* pHost, GameObjectManager* pManager )
    {
        _pHost    = pHost;
        _pManager = pManager;
        _listAnnouncedID.clear();
        _listRefusedID.clear();
        _listJoiningConnection.clear();
        _stats = PairedAnimationReplicationStats{};
        if ( pManager != nullptr )
            pManager->getSubsystem<NetObjectTable>().captureSceneObjects();
    }

    void PairedAnimationReplicationServer::update()
    {
        if ( _pHost == nullptr || _pManager == nullptr )
            return;
        const PairedAnimationSystem* pSystem = PairedAnimationSystem::find( *_pManager );
        // 끝을 먼저 보낸다 — 같은 갱신에 새 세션이 같은 참가자를 쓰면 클라이언트가 옛 세션을 먼저 놓아야 한다.
        sendEnds( pSystem );
        if ( pSystem != nullptr )
        {
            sendToJoining( *pSystem );
            sendNewSessions( *pSystem );
        }
        _listJoiningConnection.clear();
    }

    NetHandleResult PairedAnimationReplicationServer::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        (void)context;
        (void)body;
        return NetHandleResult::NotMine; // 종류 마스크가 0 이라 라우터가 넘기지 않는다
    }

    void PairedAnimationReplicationServer::onConnectionOpened( int32 connectionID )
    {
        _listJoiningConnection.push_back( connectionID );
    }

    void PairedAnimationReplicationServer::onConnectionClosed( int32 connectionID, NetDisconnectReason reason )
    {
        (void)reason;
        VectorUtil::removeFirst( _listJoiningConnection, connectionID );
    }

    bool PairedAnimationReplicationServer::makeSessionState( const PairedAnimationSession& session, PairedAnimationNetState& outState ) const
    {
        const PairedAnimationDef& def = session.getDef();
        if ( _pManager == nullptr )
        {
            SW_LOG_ERROR( "Paired animation session %# is not replicated: the replication server was not initialized with a scene", session.getID() );
            return false;
        }
        if ( def.getSourcePath().empty() )
        {
            SW_LOG_ERROR( "Paired animation session %# is not replicated: its asset was not loaded from a resource path, so clients cannot load the same asset", session.getID() );
            return false;
        }
        outState._assetPath = def.getSourcePath();
        outState._listRoleNetID.assign( def.getRoles().size(), 0u );
        for ( uint32 roleIndex = 0; roleIndex < static_cast<uint32>( def.getRoles().size() ); ++roleIndex )
        {
            const TComponentHandle<SkeletalAnimatorComponent>& animator = session.getRoleAnimator( roleIndex );
            if ( animator.isValid() == false )
                continue; // 바인딩 없는 선택 역할
            const uint32 netID = findAnimatorNetID( *_pManager, animator );
            if ( netID == 0 )
            {
                SW_LOG_ERROR( "Paired animation session %# is not replicated: role '%#' of '%#' is bound to an object without a net id (not a captured scene object and not a registered spawned object)", session.getID(),
                              def.getRoles()[roleIndex]._name.c_str(), def.getSourcePath().c_str() );
                return false;
            }
            outState._listRoleNetID[roleIndex] = netID;
        }
        if ( session.captureSnapshot( outState._snapshot ) == false )
            return false; // 재생 중이 아니다 — 부르는 쪽이 재생 중인 세션만 넘긴다
        return true;
    }

    bool PairedAnimationReplicationServer::writeStateMessage( const PairedAnimationNetState& state )
    {
        BitWriter& writer = _writer.begin( NetPairedAnimationMessage::kSessionState );
        if ( state.write( writer ) )
            return true;
        SW_LOG_ERROR( "Paired animation session %# is not replicated: path '%#' or %# roles exceed the message limits", state._snapshot._sessionID, state._assetPath.c_str(),
                      static_cast<uint32>( state._listRoleNetID.size() ) );
        return false;
    }

    void PairedAnimationReplicationServer::sendEnds( const PairedAnimationSystem* pSystem )
    {
        for ( size_t index = _listAnnouncedID.size(); index > 0; --index )
        {
            const uint32                  sessionID = _listAnnouncedID[index - 1];
            const PairedAnimationSession* pSession  = pSystem != nullptr ? pSystem->findSession( sessionID ) : nullptr;
            const bool                    bEnded    = pSession == nullptr || pSession->isPlaying() == false;
            if ( bEnded == false )
                continue;
            BitWriter& writer = _writer.begin( NetPairedAnimationMessage::kSessionEnd );
            writer.writeVarUint( sessionID );
            (void)_writer.broadcast( *_pHost, NetChannelType::ReliableOrdered ); // 연결이 없으면 보낼 곳이 없다 — 늦게 들어온 쪽은 재생 중인 세션만 받는다
            ++_stats._endSentCount;
            SW_LOG_INFO( "%#", formatEventLine( "server", "end", sessionID, 0.0f ).c_str() );
            _listAnnouncedID.erase( _listAnnouncedID.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
        // 복제하지 못한 세션은 끝나면 잊는다.
        for ( size_t index = _listRefusedID.size(); index > 0; --index )
        {
            const PairedAnimationSession* pSession = pSystem != nullptr ? pSystem->findSession( _listRefusedID[index - 1] ) : nullptr;
            const bool                    bEnded   = pSession == nullptr || pSession->isPlaying() == false;
            if ( bEnded )
                _listRefusedID.erase( _listRefusedID.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
    }

    void PairedAnimationReplicationServer::sendNewSessions( const PairedAnimationSystem& system )
    {
        for ( uint32 index = 0; index < system.getSessionCount(); ++index )
        {
            const PairedAnimationSession& session  = system.getSessionAt( index );
            const bool                    bIsKnown = hasAnnounced( session.getID() ) || hasRefused( session.getID() );
            if ( session.isPlaying() == false || bIsKnown )
                continue;
            PairedAnimationNetState state{};
            const bool              bWritten = makeSessionState( session, state ) && writeStateMessage( state );
            if ( bWritten == false )
            {
                _listRefusedID.push_back( session.getID() );
                ++_stats._unreplicableCount;
                continue;
            }
            (void)_writer.broadcast( *_pHost, NetChannelType::ReliableOrdered ); // 연결이 없어도 기록한다 — 나중에 들어온 쪽은 늦은 참가로 받는다
            ++_stats._stateSentCount;
            SW_LOG_INFO( "%#", formatEventLine( "server", "start", session.getID(), state._snapshot._anchorTime ).c_str() );
            _listAnnouncedID.push_back( session.getID() );
        }
    }

    void PairedAnimationReplicationServer::sendToJoining( const PairedAnimationSystem& system )
    {
        if ( _listJoiningConnection.empty() )
            return;
        for ( const uint32 sessionID : _listAnnouncedID )
        {
            const PairedAnimationSession* pSession = system.findSession( sessionID );
            if ( pSession == nullptr || pSession->isPlaying() == false )
                continue;
            PairedAnimationNetState state{};
            if ( makeSessionState( *pSession, state ) == false || writeStateMessage( state ) == false )
                continue; // 처음에 복제한 세션이라 여기서 실패하지 않는다 — 실패하면 오류는 남겼다
            for ( const int32 connectionID : _listJoiningConnection )
            {
                if ( _writer.send( *_pHost, connectionID, NetChannelType::ReliableOrdered ) )
                {
                    ++_stats._stateSentCount;
                    SW_LOG_INFO( "%#", formatEventLine( "server", "join", sessionID, state._snapshot._anchorTime ).c_str() );
                }
                else
                    ++_stats._sendRejectedCount;
            }
        }
    }

    bool PairedAnimationReplicationServer::hasAnnounced( uint32 sessionID ) const
    {
        return VectorUtil::contains( _listAnnouncedID, sessionID );
    }

    bool PairedAnimationReplicationServer::hasRefused( uint32 sessionID ) const
    {
        return VectorUtil::contains( _listRefusedID, sessionID );
    }
} // namespace sw

namespace sw
{
    PairedAnimationReplicationClient::PairedAnimationReplicationClient()
        : _stats{}
        , _pManager{ nullptr }
    {
    }

    void PairedAnimationReplicationClient::initialize( GameObjectManager* pManager )
    {
        _pManager = pManager;
        _stats    = PairedAnimationReplicationStats{};
        if ( pManager != nullptr )
            pManager->getSubsystem<NetObjectTable>().captureSceneObjects();
    }

    PairedAnimationStartResult PairedAnimationReplicationClient::applyState( const PairedAnimationNetState& state )
    {
        if ( _pManager == nullptr )
        {
            SW_LOG_ERROR( "Replicated paired animation session %# rejected: the replication client was not initialized with a scene", state._snapshot._sessionID );
            ++_stats._rejectedCount;
            return PairedAnimationStartResult::ParticipantMissing;
        }
        const shared_ptr<const PairedAnimationDef> def = SharedAssetCache<PairedAnimationDef>::acquire( state._assetPath );
        if ( def == nullptr )
        {
            SW_LOG_ERROR( "Replicated paired animation session %# rejected: asset '%#' could not be loaded", state._snapshot._sessionID, state._assetPath.c_str() );
            ++_stats._rejectedCount;
            return PairedAnimationStartResult::AssetNotLoaded;
        }
        if ( state._listRoleNetID.size() != def->getRoles().size() )
        {
            SW_LOG_ERROR( "Replicated paired animation session %# rejected: %# roles received but '%#' has %#", state._snapshot._sessionID,
                          static_cast<uint32>( state._listRoleNetID.size() ), state._assetPath.c_str(), static_cast<uint32>( def->getRoles().size() ) );
            ++_stats._rejectedCount;
            return PairedAnimationStartResult::InvalidSnapshot;
        }
        vector<PairedAnimationBinding> listBinding;
        for ( uint32 roleIndex = 0; roleIndex < static_cast<uint32>( state._listRoleNetID.size() ); ++roleIndex )
        {
            const uint32 netID = state._listRoleNetID[roleIndex];
            if ( netID == 0 )
                continue; // 서버에서도 바인딩 없는 선택 역할
            const TComponentHandle<SkeletalAnimatorComponent> animator = findNetAnimator( *_pManager, netID );
            if ( animator.isValid() == false )
            {
                SW_LOG_ERROR( "Replicated paired animation session %# rejected: role '%#' names net object %# but no animated object here has that id", state._snapshot._sessionID,
                              def->getRoles()[roleIndex]._name.c_str(), netID );
                ++_stats._rejectedCount;
                return PairedAnimationStartResult::ParticipantMissing;
            }
            listBinding.push_back( PairedAnimationBinding{ def->getRoles()[roleIndex]._name, animator } );
        }

        PairedAnimationSystem&        system   = PairedAnimationSystem::getOrCreate( *_pManager );
        const PairedAnimationSession* pPlaying = system.findSession( state._snapshot._sessionID );
        if ( pPlaying != nullptr && pPlaying->isPlaying() )
            system.abortSession( *_pManager, state._snapshot._sessionID ); // 서버 상태가 기준이다 — 보정 · 롤백은 옛 세션을 놓고 다시 받는다
        const PairedAnimationStartResult result = system.restoreSession( *_pManager, def, listBinding, state._snapshot );
        if ( result == PairedAnimationStartResult::Started )
        {
            ++_stats._appliedCount;
            SW_LOG_INFO( "%#", formatEventLine( "client", "restore", state._snapshot._sessionID, state._snapshot._anchorTime ).c_str() );
        }
        else
        {
            SW_LOG_ERROR( "Replicated paired animation session %# of '%#' was not restored (result %#)", state._snapshot._sessionID, state._assetPath.c_str(), static_cast<uint32>( result ) );
            ++_stats._rejectedCount;
        }
        return result;
    }

    NetHandleResult PairedAnimationReplicationClient::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        if ( _pManager == nullptr )
            return NetHandleResult::Handled; // 시작하지 않은 클라이언트 — 받을 씬이 없다
        if ( context._kind == NetPairedAnimationMessage::kSessionEnd )
        {
            const uint32 sessionID = body.readVarUint32();
            if ( body.hasOverflowed() )
                return NetHandleResult::Malformed;
            PairedAnimationSystem*        pSystem  = PairedAnimationSystem::find( *_pManager );
            const PairedAnimationSession* pSession = pSystem != nullptr ? pSystem->findSession( sessionID ) : nullptr;
            if ( pSession != nullptr && pSession->isPlaying() )
            {
                pSystem->abortSession( *_pManager, sessionID );
                ++_stats._endAppliedCount;
            }
            // 클라이언트 쪽 세션이 먼저 끝났어도(같은 시계로 따라 돈다) 끝 메시지를 받은 것은 남긴다.
            SW_LOG_INFO( "%#", formatEventLine( "client", "end", sessionID, 0.0f ).c_str() );
            return NetHandleResult::Handled;
        }
        PairedAnimationNetState state{};
        if ( PairedAnimationNetState::read( body, state ) == false )
            return NetHandleResult::Malformed;
        (void)applyState( state ); // 거절은 applyState 가 오류로 남기고 셌다
        return NetHandleResult::Handled;
    }
} // namespace sw
