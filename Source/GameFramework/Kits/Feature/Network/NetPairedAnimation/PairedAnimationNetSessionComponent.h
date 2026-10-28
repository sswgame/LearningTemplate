/**
 * @file PairedAnimationNetSessionComponent.h
 * @brief 게임 인스턴스의 네트워크 세션(`GameNetSession`)이 열려 있으면 그 씬의 페어드 애니메이션 세션을 복제합니다.
 * @details 씬에 오브젝트 하나로 둔다. 역할은 실행 인자가 정한다(`GameNetSession` 머리말): 전용 서버 · `-gv_netListenPort` 면 서버, `-gv_netConnectAddress` 면
 *          클라이언트, 세션이 없으면(혼자 하는 게임) 아무것도 하지 않는다. 복제는 `PairedAnimationReplicationServer` · `PairedAnimationReplicationClient` 가 맡는다.
 *          오브젝트 번호표는 플레이 시작 때 만든다 — 이 컴포넌트는 씬 문서의 참가자가 모두 선 뒤에 시작해야 한다(같은 씬 문서에 둔다).
 *
 *          서버는 마지막 틱 그룹(`PostUpdate`)에서 보낸다 — 세션은 프레임 시스템 단계(틱 앞)에서 갱신되므로 이번 프레임에 시작 · 끝난 세션을 같은 프레임에 보낸다.
 *          클라이언트는 메시지를 게임 프레임 앞(`GameNetSession::update`)에서 받아 바로 복원한다. 참가자가 클립을 아직 읽는 중이면 상태를 보관해 두고 이 컴포넌트의
 *          틱이 준비된 뒤(틱 뒤에) 복원한다(`PairedAnimationReplicationClient::retryPending`, 상한 안에 못 하면 오류).
 *          클라이언트의 게임플레이(상호작용)가 세션을 따로 시작하지 않게 하는 일은 게임이 권한으로 막는다(서버가 시작을 결정한다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetPairedAnimation/PairedAnimationReplication.h"

namespace sw
{
    class NetMessageRouter;

    /**
     * @class PairedAnimationNetSessionComponent
     * @brief 씬 하나의 페어드 애니메이션 복제를 게임 네트워크 세션에 잇습니다. 자세한 계약은 파일 머리말을 보세요.
     */
    REFLECT( Category = "Network", DisplayName = "Paired Animation Net Session", Tooltip = "Replicates this scene's paired animation sessions over the game's network session" )
    class SW_KIT_API PairedAnimationNetSessionComponent : public Component
    {
    public:
        REFLECT_BODY();

        PairedAnimationNetSessionComponent();
        ~PairedAnimationNetSessionComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        const PairedAnimationReplicationServer& getServer() const { return _server; }
        const PairedAnimationReplicationClient& getClient() const { return _client; }

    private:
        PairedAnimationReplicationServer _server;
        PairedAnimationReplicationClient _client;
        Subscription                     _routerSubscription; ///< 세션 라우터에 단 처리기의 등록. 없으면 시작하지 않았다(처리기보다 뒤에 선언 — 먼저 풀린다)
        uint8                            _bServer;            ///< 서버로 시작했다
    };
} // namespace sw
