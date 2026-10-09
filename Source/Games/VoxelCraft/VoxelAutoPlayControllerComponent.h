/**
 * @file VoxelAutoPlayControllerComponent.h
 * @brief VoxelCraft 의 자동 플레이 — 플레이어 폰에 빙의하는 AI 조종자입니다(걷다가 막히면 뛰고, 앞 블록을 부수고 놓는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/Control/AiControllerComponent.h"

namespace sw
{
    /**
     * @class VoxelAutoPlayControllerComponent
     * @brief 자동 플레이가 켜지면 디렉터가 세워 플레이어 폰에 빙의시킵니다(`gv_voxelAutoPlay` · 에디터 툴바 스위치 하나 — 끄면 플레이어 조종자가 다시 쥔다).
     * @details 몸(`VoxelPlayerComponent`)에는 자동 플레이 분기가 없습니다 — 이 조종자가 플레이어와 같은 의도(이동 · 조종 회전 · `Voxel.Jump` · `Voxel.Break` ·
     *          `Voxel.Place`)를 냅니다. 12 초 주기로 앞으로 걸으며 천천히 돌고(앞 6 초는 오른쪽, 뒤는 왼쪽), 아래를 조금 보며 3 ~ 6.5 초에 부수고 9 초에 하나 놓는다.
     *          앞이 막혔거나 물 속이면 점프를 누른다(월드는 판단만 위해 읽는다).
     */
    REFLECT( Category = "VoxelCraft", DisplayName = "Voxel Auto Play Controller", Tooltip = "AI controller that walks, breaks and places blocks with the player pawn when auto play is on" )
    class VoxelAutoPlayControllerComponent : public AiControllerComponent
    {
    public:
        REFLECT_BODY();

        VoxelAutoPlayControllerComponent();
        ~VoxelAutoPlayControllerComponent() override = default;

    protected:
        void think( const ControlFrameContext& context, const PawnComponent& pawn ) override;

    private:
        float32 _timer; ///< 행동 주기(12 초)를 재는 시간
    };
} // namespace sw
