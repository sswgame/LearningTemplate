/**
 * @file DestructionNetSessionComponent.h
 * @brief 게임 인스턴스의 네트워크 세션(`GameNetSession`)이 열려 있으면 그 씬의 파괴 오브젝트를 복제합니다 — 전용 서버(`Server`)가 파괴를 UDP 서버로 돌리는 자리입니다.
 * @details 씬에 오브젝트 하나로 둔다. 역할은 실행 인자가 정한다(`GameNetSession` 머리말): 전용 서버 · `-gv_netListenPort` 면 서버,
 *          `-gv_netConnectAddress` 면 클라이언트, 세션이 없으면(혼자 하는 게임) 아무것도 하지 않는다. 복제와 해시 로그는 `DestructionNetSession` 이 맡는다.
 *          예: 서버 `Server -gv_firstScene=<씬> -gv_netListenPort=7777`, 클라이언트 `App -gv_firstScene=<같은 씬> -gv_netConnectAddress=127.0.0.1:7777`.
 *          두 로그를 `py -3 -m Scripts destruction-net compare` 로(두 프로세스를 띄우는 것까지는 `run`) 맞대면 컴파일러 · 플랫폼을 넘어 구조 해시가 같은지 본다.
 *
 *          틱은 마지막 그룹(`PostUpdate`)이다 — 서버는 이번 틱의 물리가 만든 사건을 같은 틱에 보낸다. 사건 · 자세의 시각은 월드의 서버 틱(`NetClock`)이라
 *          서버의 프레임 길이와 무관하다(예전에는 프레임마다 하나씩 세어 서버 틱 빈도가 설정과 다르면 복제를 멈췄다).
 *          틱은 걸기만 하고 복제는 틱 뒤 큐 한 번에 건다 — 복제가 남의 오브젝트(파괴 컴포넌트)의 상태를 쓰므로 틱 안(워커)에서 하지 않는다
 *          (`Gameplay.md` 규칙 6 — 씬에 하나라 프레임에 한 일이다. 하우징 `HousingNetSessionComponent` 와 같은 자리).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionNetSession.h"

namespace sw
{
    /**
     * @class DestructionNetSessionComponent
     * @brief 씬 하나의 파괴 복제를 게임 네트워크 세션에 잇습니다. 자세한 계약은 파일 머리말을 보세요.
     */
    REFLECT( Category = "Network", DisplayName = "Destruction Net Session", Tooltip = "Replicates this scene's fracture objects over the game's network session and logs structure hashes" )
    class SW_KIT_API DestructionNetSessionComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate; ///< 서버는 이번 틱의 물리가 만든 사건을 같은 틱에 보낸다 — 마지막 그룹에서 돈다.

        DestructionNetSessionComponent();
        ~DestructionNetSessionComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;

        const DestructionNetSession& getSession() const { return _session; }

    private:
        /** @brief 틱 뒤(게임 스레드): 복제를 한 번 돌린 뒤 셈을 올립니다. */
        void updateSession();
        /** @brief 파괴 복제의 누적 셈을 `GameNetSession::setReplicaStats( "destruction" … )` 로 올립니다(Profiler 탭의 `replicas=` 표). */
        void reportReplicaStats();

        PROPERTY( Category = "Network", DisplayName = "Hash Interval", Min = 0.1, Units = s, Tooltip = "How often the server sends its structure hash for the clients to compare" )
        float32 _hashInterval;
        PROPERTY( Category = "Network", DisplayName = "Interpolation Delay", Min = 0.0, Units = s, Tooltip = "Least time clients draw moving chunks in the past" )
        float32 _interpolationDelay;

        DestructionNetSession _session;
        Subscription          _hashSubscription; ///< 해시 이력의 `destruction` 줄 등록. `_session` 보다 뒤에 선언한다 — 먼저 풀린다
    };
} // namespace sw
