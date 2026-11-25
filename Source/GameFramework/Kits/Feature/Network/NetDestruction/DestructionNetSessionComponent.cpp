#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionNetSessionComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Network/GameNetSession.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Scene/SceneObjectHash.h"

#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    DestructionNetSessionComponent::DestructionNetSessionComponent()
        : _hashInterval{ 1.0f }
        , _interpolationDelay{ 0.1f }
        , _session{}
        , _hashSubscription{}
    {
        setCanEverTick( true );
    }

    void DestructionNetSessionComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 넷 링크는 이 컴포넌트의 월드(오브젝트 매니저)에 올라 있다 - 전역 세션을 찾지 않는다(`SceneNetLink`). 링크가 비면 혼자 하는 게임이다.
        GameObject*         pOwner   = getOwner();
        GameObjectManager*  pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        const NetLinkValue* pLink    = SceneNetLink::findBoundFor( *this );
        if ( pLink == nullptr )
            return; // 혼자 하는 게임 - 복제할 상대가 없다
        const NetLinkValue& link = *pLink;

        DestructionReplicationConfig settings;
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

    void DestructionNetSessionComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _session.isActive() == false )
            return;
        // 복제는 남의 파괴 컴포넌트를 쓴다 — 틱 뒤에 돈다.
        context.deferPostTickOnComponent( *this, []( DestructionNetSessionComponent& component, GameObjectManager& manager )
        {
            (void)manager;
            component.updateSession();
        } );
    }

    void DestructionNetSessionComponent::updateSession()
    {
        if ( _session.isActive() == false )
            return;
        _session.update();
        reportReplicaStats();
    }

    void DestructionNetSessionComponent::reportReplicaStats()
    {
        // 복제 표(프로파일러의 `replicas=`)는 전역 세션의 것이다 - 이 월드의 링크가 전역 세션의 창구일 때만 올린다(가상 서버 하니스의 월드는 올리지 않는다).
        GameNetSession* pNetSession = game::findSubsystem<GameNetSession>();
        if ( pNetSession == nullptr )
            return;
        // 누적 셈을 틱마다 올린다(같은 이름은 덮는다). 서버는 보낸 양, 클라이언트는 받아 적용한 자세 · 사건 수가 갱신 수이다.
        const DestructionReplicationStats& stats       = _session.getStats();
        const uint64                       updateCount = stats._eventMessageCount + stats._poseMessageCount + stats._restPoseCount + stats._snapshotCount + stats._hashMessageCount;
        (void)pNetSession->setWorldReplicaStats( *this, "destruction", updateCount, stats._sentBytes ); // 다른 월드(하니스 · 에디터의 가상 서버)면 올리지 않는다
    }
} // namespace sw
