#include "pch.h"

#include "Engine/Object/GameObject/Service/SceneNetLink.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Replication/NetClock.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneServiceRegistry.h"

namespace sw
{
    SW_SCENE_SERVICE( SceneNetLink, FlushEnd, SceneFrameAccess{} )

    bool NetLinkValue::isComplete() const
    {
        return _pPeers != nullptr && _pRouter != nullptr && _pRouter->getSessionSerial() == _sessionSerial;
    }

    SceneNetLink::SceneNetLink( GameObjectManager& manager )
        : _value{}
        , _bindSerial{ 0 }
        , _bServerWorld{ SW_FALSE }
    {
        (void)manager;
    }

    bool SceneNetLink::bind( const NetLinkValue& value, uint64 sceneID )
    {
        if ( value.isComplete() == false )
        {
            SW_LOG_ERROR_ID( "SceneNetLink.SceneNetLinkNeeds", "Scene net link needs both the peers and the router of an open session - the link stays unbound" );
            return false;
        }
        _value = value;
        if ( value._role == NetLinkRole::Server )
            value._pRouter->openWorld( *value._pPeers, sceneID );
        else
        {
            value._pRouter->setLocalScene( *value._pPeers, sceneID );
            markServerWorld();
        }
        _bindSerial = value._pRouter->getBindSerial();
        return true;
    }

    void SceneNetLink::update( float32 deltaTime )
    {
        const bool bOwnsClock = isBound() && _value._pRouter->getBindSerial() == _bindSerial; // 나가는 씬과 들어온 씬이 겹치는 동안 두 번 흘리지 않는다
        if ( bOwnsClock )
            _value._pRouter->advanceWorldClock( *_value._pPeers, GameSeconds{ deltaTime } ); // 씬 서비스의 update 는 씬의 게임 시간이다
    }

    NetClock& SceneNetLink::getServerClock()
    {
        SW_ASSERT( isBound() );
        return _value._pRouter->getWorldClock();
    }

    const NetClock& SceneNetLink::getServerClock() const
    {
        SW_ASSERT( isBound() );
        return _value._pRouter->getWorldClock();
    }

    uint64 SceneNetLink::computeSceneID( string_view scenePath )
    {
        return scenePath.empty() ? 0u : HashUtil::computeStringHash64( scenePath.data(), scenePath.size() );
    }

    uint32 SceneNetLink::getServerTick() const
    {
        return isBound() ? _value._pRouter->getWorldClock().getWholeTick() : 0u;
    }

    float32 SceneNetLink::computeSecondsSince( uint32 serverTick ) const
    {
        if ( isBound() == false || _value._pRouter->getWorldClock().hasServerTick() == false )
            return 0.0f;
        const NetClock&    clock = _value._pRouter->getWorldClock();
        NetConnectionStats stats;
        const bool         bClient = _value._role == NetLinkRole::Client && _value._pPeers->getConnectionStats( 0, stats );
        const float64      elapsed = ( clock.getReceivedTick() - static_cast<float64>( serverTick ) ) * static_cast<float64>( NetClock::kTickSeconds );
        return MathUtil::max( 0.0f, static_cast<float32>( elapsed ) + ( bClient ? stats._rtt * 0.5f : 0.0f ) );
    }

    uint8 SceneNetLink::getWorldGeneration() const
    {
        return isBound() ? _value._pRouter->getWorldGeneration() : uint8{ 0 };
    }

    const NetLinkValue* SceneNetLink::findBoundFor( const Component& component )
    {
        const GameObject*        pOwner   = component.getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const SceneNetLink*      pLink    = pManager != nullptr ? &pManager->getSubsystem<SceneNetLink>() : nullptr;
        return pLink != nullptr && pLink->isBound() ? &pLink->_value : nullptr;
    }
} // namespace sw
