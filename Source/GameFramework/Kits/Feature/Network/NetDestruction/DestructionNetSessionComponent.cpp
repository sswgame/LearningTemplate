#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionNetSessionComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Net/GameNetSession.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Scene/SceneStateHash.h"

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
        , _hashSubscription{}
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

        // 넷 링크는 이 컴포넌트의 월드(오브젝트 매니저)에 올라 있다 - 전역 세션을 찾지 않는다(`SceneNetLink`). 링크가 비면 혼자 하는 게임이다.
        GameObject*         pOwner     = getOwner();
        GameObjectManager*  pManager   = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const SceneNetLink* pLink      = pManager != nullptr ? &pManager->getSubsystem<SceneNetLink>() : nullptr;
        const bool          bNoSession = pLink == nullptr || pLink->isBound() == false;
        if ( bNoSession )
            return; // 혼자 하는 게임 - 복제할 상대가 없다
        const NetLinkValue& link = pLink->get();

        DestructionReplicationConfig settings;
        settings._tickInterval        = _tickInterval;
        settings._hashInterval        = _hashInterval;
        settings._interpolationDelay  = _interpolationDelay;
        const DestructionNetSide side = link._role == NetLinkRole::Server ? DestructionNetSide::Server : DestructionNetSide::Client;
        _session.initialize( *link._pPeers, *link._pRouter, *pManager, side, settings );
        // 해시 이력(`-gv_stateHashHistory`)이 켜져 있으면 파괴 구조 해시를 `destruction` 줄로 같은 틱에 남긴다 — 씬 해시가 같아도 구조가 갈린 틱을 찾는다.
        StateHashHistory* pHistory       = game::findSubsystem<StateHashHistory>();
        const bool        bRecordsHashes = pHistory != nullptr && pHistory->isRecording() && _session.isActive();
        if ( bRecordsHashes )
            _hashSubscription = pHistory->registerChannel( "destruction", &_session );
    }

    void DestructionNetSessionComponent::onEndPlay()
    {
        _hashSubscription.reset();
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
        reportReplicaStats();
    }

    void DestructionNetSessionComponent::reportReplicaStats()
    {
        // 복제 표(프로파일러의 `replicas=`)는 전역 세션의 것이다 - 이 월드의 링크가 전역 세션의 창구일 때만 올린다(가상 서버 하니스의 월드는 올리지 않는다).
        GameNetSession*     pNetSession  = game::findSubsystem<GameNetSession>();
        GameObject*         pOwner       = getOwner();
        GameObjectManager*  pManager     = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const SceneNetLink* pLink        = pManager != nullptr ? &pManager->getSubsystem<SceneNetLink>() : nullptr;
        const bool          bGlobalWorld = pNetSession != nullptr && pLink != nullptr && pLink->isBound() && pLink->get()._pPeers == &pNetSession->getPeers();
        if ( bGlobalWorld == false )
            return;
        // 누적 셈을 틱마다 올린다(같은 이름은 덮는다). 서버는 보낸 양, 클라이언트는 받아 적용한 자세 · 사건 수가 갱신 수이다.
        const DestructionReplicationStats& stats       = _session.getStats();
        const uint64                       updateCount = stats._eventMessageCount + stats._poseMessageCount + stats._restPoseCount + stats._snapshotCount + stats._hashMessageCount;
        (void)pNetSession->setReplicaStats( "destruction", updateCount, stats._sentBytes );
    }
} // namespace sw
