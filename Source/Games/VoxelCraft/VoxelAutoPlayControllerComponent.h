/**
 * @file VoxelAutoPlayControllerComponent.h
 * @brief VoxelCraft 의 자동 플레이를 맡는 AI 조종자입니다. 플레이어 폰에 빙의해 걷다가 막히면 점프하고, 앞 블록을 부수고 놓습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Character/Control/Controller/AIControllerComponent.h"

namespace sw
{
    /**
     * @class VoxelAutoPlayControllerComponent
     * @brief 자동 플레이가 켜지면 디렉터가 스폰해서 플레이어 폰에 빙의시키는 조종자입니다.
     *        `gv_voxelAutoPlay` 와 에디터 툴바의 스위치가 같은 스위치를 켜고 끕니다. 끄면 플레이어 조종자가 폰을 다시 가져갑니다.
     * @details 몸(`VoxelPlayerComponent`)에는 자동 플레이용 분기가 없습니다. 이 조종자가 플레이어와 같은 의도를 냅니다.
     *          의도는 이동, 조종 회전, `Voxel.Jump`, `Voxel.Break`, `Voxel.Place` 입니다.
     *          한 주기(기본 12초) 동안 앞으로 걸으면서 천천히 돕니다. 주기의 앞 절반은 오른쪽으로, 뒤 절반은 왼쪽으로 돕니다.
     *          아래를 조금 보며, 주기 안의 정해진 구간에 블록을 부수고 한 번 놓습니다. 구간의 시각은 PROPERTY 로 정합니다.
     *          앞이 막혔거나 물 속이면 점프 버튼을 누릅니다. 월드는 이 판단을 위해서만 읽습니다.
     *          판단 상태(`setStateName`, 감시 값 `AI.State`)는 `Walk`, `Break`, `Place` 중 하나입니다.
     */
    REFLECT( Category = "VoxelCraft", DisplayName = "Voxel Auto Play Controller", Tooltip = "AI controller that walks, breaks and places blocks with the player pawn when auto play is on" )
    class VoxelAutoPlayControllerComponent : public AIControllerComponent
    {
    public:
        REFLECT_BODY();

        VoxelAutoPlayControllerComponent();
        ~VoxelAutoPlayControllerComponent() override = default;

    protected:
        void think( const ControlFrameContext& context, const PawnComponent& pawn ) override;

    private:
        PROPERTY( Category = "Auto Play", DisplayName = "Cycle Seconds", Tooltip = "Length of one walk, break and place cycle", Min = 0.1, Units = s )
        float32 _cycleSeconds;
        PROPERTY( Category = "Auto Play", DisplayName = "Turn Right Rate", Tooltip = "Yaw speed in the first half of the cycle", Units = "rad/s" )
        float32 _turnRightRate;
        PROPERTY( Category = "Auto Play", DisplayName = "Turn Left Rate", Tooltip = "Yaw speed in the second half of the cycle", Units = "rad/s" )
        float32 _turnLeftRate;
        PROPERTY( Category = "Auto Play", DisplayName = "Look Down Pitch", Tooltip = "Pitch held while walking (negative looks down)", Units = rad )
        float32 _lookDownPitch;
        PROPERTY( Category = "Auto Play", DisplayName = "Break Start", Tooltip = "Time in the cycle at which Break is held", Min = 0.0, Units = s )
        float32 _breakStart;
        PROPERTY( Category = "Auto Play", DisplayName = "Break End", Tooltip = "Time in the cycle at which Break is released", Min = 0.0, Units = s )
        float32 _breakEnd;
        PROPERTY( Category = "Auto Play", DisplayName = "Place At", Tooltip = "Time in the cycle at which one block is placed", Min = 0.0, Units = s )
        float32 _placeAt;
        PROPERTY( Category = "Auto Play", DisplayName = "Focus Distance", Tooltip = "Distance to the point looked at (turns the control rotation toward it)", Min = 0.0, Units = m )
        float32 _focusDistance;
        PROPERTY( Category = "Auto Play", DisplayName = "Walk Look Ahead", Tooltip = "Distance to the walk target, set again ahead every tick", Min = 0.0, Units = m )
        float32 _walkLookAhead;
        PROPERTY( Category = "Auto Play", DisplayName = "Block Look Ahead", Tooltip = "Distance ahead checked for a blocking block", Min = 0.0, Units = m )
        float32 _blockLookAhead;

        float32 _timer; ///< 행동 주기를 측정하는 시간
    };
} // namespace sw
