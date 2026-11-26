#include "pch.h"

#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Network/GameNetSession.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Subsystem/SceneNetLink.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetPairedAnimation/PairedAnimationReplication.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    class NetMessageRouter;

    /**
     * @class PairedAnimationNetSessionComponent
     * @brief 씬 하나의 페어드 애니메이션 복제를 게임 네트워크 세션에 잇습니다. 자세한 계약은 파일 머리말을 보세요.
     */
    REFLECT( Category = "Network", DisplayName = "Paired Animation Net Session", Tooltip = "Replicates this scene's paired animation sessions over the game's network session" )
    class PairedAnimationNetSessionComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate; ///< 세션 갱신(프레임 시스템 단계) 뒤에 보낸다 — 마지막 그룹에서 돈다.

        PairedAnimationNetSessionComponent();
        ~PairedAnimationNetSessionComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;

        const PairedAnimationReplicationServer& getServer() const { return _server; }
        const PairedAnimationReplicationClient& getClient() const { return _client; }

    private:
        /** @brief 누적 셈을 `GameNetSession::setReplicaStats( "paired_animation" … )` 로 올립니다(Profiler 탭의 `replicas=` 표). */
        void reportReplicaStats();

        SW_TRANSIENT PairedAnimationReplicationServer _server;
        SW_TRANSIENT PairedAnimationReplicationClient _client;
        Subscription                                  _routerSubscription; ///< 세션 라우터에 단 처리기의 등록. 없으면 시작하지 않았다(처리기보다 뒤에 선언 — 먼저 풀린다)
        SW_TRANSIENT uint8                            _bServer;            ///< 서버로 시작했다
    };

    PairedAnimationNetSessionComponent::PairedAnimationNetSessionComponent()
        : _server{}
        , _client{}
        , _routerSubscription{}
        , _bServer{ SW_FALSE }
    {
        setCanEverTick( true );
    }

    void PairedAnimationNetSessionComponent::onBeginPlay()
    {
        Component::onBeginPlay();

        // 넷 링크는 이 컴포넌트의 월드(오브젝트 매니저)에 올라 있다 - 전역 세션을 찾지 않는다(`SceneNetLink`). 링크가 비면 혼자 하는 게임이다.
        GameObject*         pOwner   = getOwner();
        GameObjectManager*  pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        const NetLinkValue* pLink    = SceneNetLink::findBoundFor( *this );
        if ( pLink == nullptr )
            return; // 혼자 하는 게임 - 복제할 상대가 없다
        const NetLinkValue& link = *pLink;

        _bServer                     = link._role == NetLinkRole::Server ? SW_TRUE : SW_FALSE;
        INetMessageHandler* pHandler = nullptr;
        if ( _bServer == SW_TRUE )
        {
            _server.initialize( link._pPeers, pManager );
            pHandler = &_server;
        }
        else
        {
            _client.initialize( pManager );
            pHandler = &_client;
        }
        _routerSubscription = link._pRouter->registerHandler( pHandler );
        if ( _routerSubscription.isActive() == false )
        {
            SW_LOG_ERROR_ID( "PairedAnimationNetSessionComponent.PairedAnimationNetSession", "Paired animation net session not started: another handler owns the paired animation message range" );
            return;
        }
        SW_LOG_INFO_ID( "PairedAnimationNetSessionComponent.PairedAnimationNetSession2", "Paired animation net session started as %#", _bServer == SW_TRUE ? "server" : "client" );
    }

    void PairedAnimationNetSessionComponent::onEndPlay()
    {
        _routerSubscription.reset();
        Component::onEndPlay();
    }

    void PairedAnimationNetSessionComponent::reportReplicaStats()
    {
        // 복제 표(프로파일러의 `replicas=`)는 전역 세션의 것이다 - 이 월드의 링크가 전역 세션의 창구일 때만 올린다(가상 서버 하니스의 월드는 올리지 않는다).
        GameNetSession* pNetSession = game::findSubsystem<GameNetSession>();
        if ( pNetSession == nullptr )
            return;
        // 이 키트는 바이트를 세지 않는다(0) — 갱신 수만 오른다. 서버는 보낸 상태 · 끝 메시지, 클라이언트는 복원 · 끝으로 중단한 수이다.
        const PairedAnimationReplicationStats& stats       = _bServer == SW_TRUE ? _server.getStats() : _client.getStats();
        const uint64                           updateCount = _bServer == SW_TRUE ? static_cast<uint64>( stats._stateSentCount ) + stats._endSentCount
                                                                                 : static_cast<uint64>( stats._appliedCount ) + stats._endAppliedCount;
        (void)pNetSession->setWorldReplicaStats( *this, "paired_animation", updateCount, 0 ); // 다른 월드(하니스 · 에디터의 가상 서버)면 올리지 않는다
    }

    void PairedAnimationNetSessionComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const bool bSends = _routerSubscription.isActive() && _bServer == SW_TRUE;
        if ( bSends )
            _server.update();
        if ( _routerSubscription.isActive() )
            reportReplicaStats();
    }
} // namespace sw

#include "PairedAnimationNetSessionComponent.cpp.gen.inl"
