/**
 * @file ShooterAutoAimControllerComponent.h
 * @brief Shooter3D 의 자동 플레이 — 플레이어 폰에 빙의하는 AI 조종자입니다(가까운 적을 겨눠 쏘고, 아레나 가운데를 돌며, 거리로 무기를 고른다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/Control/Controller/AiControllerComponent.h"

namespace sw
{
    /**
     * @class ShooterAutoAimControllerComponent
     * @brief 자동 플레이가 켜지면 디렉터가 세워 플레이어 폰에 빙의시킵니다(`gv_shooterAutoPlay` · 에디터 툴바 스위치 하나 — 끄면 플레이어 조종자가 다시 쥔다).
     * @details 몸(`ShooterPlayerComponent`)에는 자동 플레이 분기가 없습니다 — 이 조종자가 사람과 같은 의도(이동 · 조종 회전 · `Fire` · `Weapon1` · `Weapon2`)를 냅니다.
     *          가장 가까운 적의 가슴에 초점을 두고(조종 회전은 `_turnRate` 로 돈다 — 표적이 바뀌어도 시점이 튀지 않게), 조준 오차가 3° 안이고 `_engageDistance`
     *          안이면 쏩니다. `_closeRangeDistance` 안이면 산탄총, 밖이면 소총(탄이 있을 때). 이동은 아레나 가운데를 도는 나선(접선 + 안쪽)의 앞 점으로 곧장 갑니다 — 가운데 가까이에서는 그 점이 도착 거리 안이라 선다.
     *          적 목록 · 상자는 디렉터가 앞 프레임에 적은 것을 읽습니다(조종 단계 — 틱 전, 게임 스레드).
     */
    REFLECT( Category = "Shooter3D", DisplayName = "Shooter Auto Aim Controller", Tooltip = "AI controller that aims, shoots, picks weapons and circles the arena with the player pawn when auto play is on" )
    class ShooterAutoAimControllerComponent : public AiControllerComponent
    {
    public:
        REFLECT_BODY();

        ShooterAutoAimControllerComponent();
        ~ShooterAutoAimControllerComponent() override = default;

    protected:
        void think( const ControlFrameContext& context, const PawnComponent& pawn ) override;

    private:
        PROPERTY( Category = "Auto Play", DisplayName = "Engage Distance", Tooltip = "Shoots at enemies inside this distance", Min = 0.0, Units = m )
        float32 _engageDistance;
        PROPERTY( Category = "Auto Play", DisplayName = "Close Range Distance", Tooltip = "Inside this distance the shotgun, outside it the rifle", Min = 0.0, Units = m )
        float32 _closeRangeDistance;
    };
} // namespace sw
