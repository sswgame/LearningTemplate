/**
 * @file DestructionNetSessionComponent.h
 * @brief 게임 인스턴스의 네트워크 세션(`GameNetSession`)이 열려 있으면 그 씬의 파괴 오브젝트를 복제합니다 — 전용 서버(`Server`)가 파괴를 UDP 서버로 돌리는 자리입니다.
 * @details 씬에 오브젝트 하나로 둔다. 역할은 실행 인자가 정한다(`GameNetSession` 머리말): 전용 서버 · `-gv_netListenPort` 면 서버,
 *          `-gv_netConnectAddress` 면 클라이언트, 세션이 없으면(혼자 하는 게임) 아무것도 하지 않는다. 복제와 해시 로그는 `DestructionNetSession` 이 맡는다.
 *          예: 서버 `Server -gv_firstScene=<씬> -gv_netListenPort=7777`, 클라이언트 `App -gv_firstScene=<같은 씬> -gv_netConnectAddress=127.0.0.1:7777`.
 *          두 로그를 `Scripts/dev/CompareDestructionHashLogs.py` 로 맞대면 컴파일러 · 플랫폼을 넘어 구조 해시가 같은지 본다.
 *
 *          틱은 마지막 그룹(`PostUpdate`)이다 — 서버는 이번 틱의 물리가 만든 사건을 같은 틱에 보낸다. 서버의 사건 · 자세 시각은 틱 번호 × `_tickInterval` 이므로
 *          서버 고정 틱과 같아야 한다. 서버는 첫 틱의 델타가 다르면 오류를 남기고 복제를 멈춘다(다른 값으로 대신 돌지 않는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

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

        DestructionNetSessionComponent();
        ~DestructionNetSessionComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        const DestructionNetSession& getSession() const { return _session; }

    private:
        /** @brief 파괴 복제의 누적 셈을 `GameNetSession::setReplicaStats( "destruction" … )` 로 올립니다(Profiler 탭의 `replicas=` 표). */
        void reportReplicaStats();

        PROPERTY( Category = "Network", DisplayName = "Tick Interval", Min = 0.001, Units = s, Tooltip = "Server fixed step; pose times are tick x interval, so it must equal the server tick" )
        float32 _tickInterval;
        PROPERTY( Category = "Network", DisplayName = "Hash Interval", Min = 0.1, Units = s, Tooltip = "How often the server sends its structure hash for the clients to compare" )
        float32 _hashInterval;
        PROPERTY( Category = "Network", DisplayName = "Interpolation Delay", Min = 0.0, Units = s, Tooltip = "Least time clients draw moving chunks in the past" )
        float32 _interpolationDelay;

        DestructionNetSession _session;
        uint8                 _bTickChecked; ///< 서버 — 첫 틱의 델타를 `_tickInterval` 과 견줬다
    };
} // namespace sw
