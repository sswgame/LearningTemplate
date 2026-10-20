#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionNetSessionComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Net/GameNetSession.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "DestructionNetSessionComponent" );

    namespace
    {
        /** @brief 틱 간격을 정수 틱 빈도(Hz)로 바꿉니다. 서버 틱 빈도는 정수(`ServerConfig::_tickRateHz`)이므로 실수 반올림 차이 없이 비교한다. */
        int32 computeDestructionNetTickRateHz( float32 interval )
        {
            return static_cast<int32>( MathUtil::round( 1.0f / interval ) );
        }
    } // namespace
} // namespace sw

namespace sw
{
    DestructionNetSessionComponent::DestructionNetSessionComponent()
        : _tickInterval{ 1.0f / 60.0f }
        , _hashInterval{ 1.0f }
        , _interpolationDelay{ 0.1f }
        , _session{}
        , _bTickChecked{ SW_FALSE }
    {
        setCanEverTick( true );
    }

    void DestructionNetSessionComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 서버는 이번 틱의 물리가 만든 사건을 같은 틱에 보낸다 — 마지막 그룹에서 돈다.
        setTickGroup( TickGroup::PostUpdate );
        _bTickChecked = SW_FALSE;

        GameNetSession*    pNetSession = game::findSubsystem<GameNetSession>();
        GameObject*        pOwner      = getOwner();
        GameObjectManager* pManager    = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const bool         bNoSession  = pNetSession == nullptr || pNetSession->isActive() == false || pManager == nullptr;
        if ( bNoSession )
            return; // 혼자 하는 게임 — 복제할 상대가 없다

        DestructionReplicationConfig settings;
        settings._tickInterval        = _tickInterval;
        settings._hashInterval        = _hashInterval;
        settings._interpolationDelay  = _interpolationDelay;
        const DestructionNetSide side = pNetSession->getRole() == GameNetRole::Server ? DestructionNetSide::Server : DestructionNetSide::Client;
        _session.initialize( pNetSession->getHost(), pNetSession->getRouter(), *pManager, side, settings );
    }

    void DestructionNetSessionComponent::onEndPlay()
    {
        _session.shutdown();
        Component::onEndPlay();
    }

    void DestructionNetSessionComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _session.isActive() == false )
            return;
        // 서버의 첫 틱(델타가 있는)에서 틱 빈도를 한 번 비교한다.
        const bool bCheckServerTick = _session.getSide() == DestructionNetSide::Server && _bTickChecked == SW_FALSE && deltaTime > 0.0f;
        if ( bCheckServerTick )
        {
            _bTickChecked = SW_TRUE;
            if ( computeDestructionNetTickRateHz( deltaTime ) != computeDestructionNetTickRateHz( _tickInterval ) )
            {
                SW_LOG_ERROR( "Destruction net session stopped: the server ticks every %# s but Tick Interval is %# s - pose times would drift; set Tick Interval to the server fixed step",
                              deltaTime, _tickInterval );
                _session.shutdown();
                return;
            }
        }
        _session.update( deltaTime );
    }
} // namespace sw
