/**
 * @file ArenaEnemyAiComponent.h
 * @brief 적 유닛의 AI 조종자 — 근접(Grunt)은 다가와 때리고, 원거리(Caster)는 거리를 지키며 쏩니다. 폰(`ArenaUnitComponent`)과 따로 살고 의도만 냅니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Control/AiControllerComponent.h"

#include "Games/AbilityArena/ArenaUnitComponent.h"

namespace sw
{
    /**
     * @class ArenaEnemyAiComponent
     * @brief 디렉터가 적 유닛을 세울 때 그 종류의 AI 프리팹(`gruntai` · `casterai`)으로 함께 세워 빙의시킵니다. 대상은 디렉터의 유닛 모습에서 가장 가까운 적대 유닛입니다.
     * @details 판단은 조종 시스템이 틱 전에 게임 스레드에서 부릅니다(`think`) — 대상의 자리는 그 메시의 지금 월드 자리(이번 프레임 시작)를 읽습니다.
     *          언리얼 `AAIController` 처럼 목표를 쫓는 쪽은 한 프레임 앞 자리를 보고, 움직임은 플레이어와 같은 폰 이동(`ArenaUnitComponent`)이 합니다.
     *          바라보기는 대상을 초점으로(`setFocus`) — 적 유닛은 조종 요를 바라본다(`PawnFacingMode::ControlYaw`), 물러나도 대상을 본다.
     */
    REFLECT( Category = "AbilityArena", DisplayName = "Arena Enemy AI", Tooltip = "Grunt / caster AI controller of an arena enemy" )
    class ArenaEnemyAiComponent : public AiControllerComponent
    {
    public:
        REFLECT_BODY();

        ArenaEnemyAiComponent();
        virtual ~ArenaEnemyAiComponent() override = default;

    protected:
        void think( const ControlFrameContext& context, const PawnComponent& pawn ) override;

    private:
        PROPERTY( Category = "AI", DisplayName = "Kind", Tooltip = "Grunt walks up and strikes; Caster keeps its distance and shoots" )
        ArenaUnitKind _kind;
        PROPERTY( Category = "AI", DisplayName = "Melee Reach", Tooltip = "Grunt stops and strikes inside this distance", Min = 0.0, Units = m )
        float32 _meleeReach;
        PROPERTY( Category = "AI", DisplayName = "Preferred Min", Tooltip = "Caster backs off inside this distance", Min = 0.0, Units = m )
        float32 _preferredMin;
        PROPERTY( Category = "AI", DisplayName = "Preferred Max", Tooltip = "Caster closes in beyond this distance", Min = 0.0, Units = m )
        float32 _preferredMax;
        PROPERTY( Category = "AI", DisplayName = "Fire Range", Tooltip = "Caster shoots inside this distance", Min = 0.0, Units = m )
        float32 _fireRange;
    };
} // namespace sw
