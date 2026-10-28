/**
 * @file CameraBlend.h
 * @brief 두 카메라 포즈를 섞습니다(`blendPoses`). 곡선, 길이, 가중치는 엔진의 `BlendCurveDef` 와 `evaluateBlendWeight`(`Engine/Animation/Graph/BlendCurve.h`)를 씁니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Animation/Graph/BlendCurve.h"

#include "GameFramework/Base/Character/Camera/CameraPose.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @brief 두 포즈를 @p weight(0 = @p from, 1 = @p to)로 섞습니다.
     * @details 위치, 시야각, 직교 높이, 근평면과 원평면은 선형 보간이고, 회전은 짧은 쪽 구면 보간입니다. 직교와 원근은 섞지 않고 가중치 0.5 에서 바꿉니다.
     *          Cinemachine 도 투영을 섞지 않습니다. 두 투영 사이의 행렬은 어느 쪽 화면과도 닮지 않기 때문입니다.
     */
    SW_GF_API CameraPose blendPoses( const CameraPose& from, const CameraPose& to, float32 weight );
} // namespace sw

namespace sw
{
    /**
     * @class CameraPoseBlender
     * @brief 두 시점 사이 전환의 진행 상태입니다. **지금 내보내는 포즈에서** 들어오는 포즈로 곡선을 따라 섞습니다. 프리셋을 바꾸는 디렉터와 뷰 타깃을 바꾸는
     *        카메라 매니저가 같은 규칙을 씁니다(Cinemachine Brain · 언리얼 `SetViewTargetWithBlend`).
     * @details 나가는 쪽은 둘 중 하나입니다. 블렌드가 없던 때 시작하면 나가는 쪽이 **살아 있어**(대상을 계속 따라가는 프리셋 · 움직이는 카메라) 부르는 쪽이
     *          `step` 마다 그 포즈를 줍니다. 블렌드 도중에 다시 시작하면 그 순간의 섞인 포즈를 **고정**해 출발점으로 둡니다. 어느 쪽이든 시작한 순간
     *          화면이 튀지 않습니다. 처음 포즈(아직 낸 포즈가 없다)는 블렌드 없이 바로 붙습니다.
     */
    class SW_GF_API CameraPoseBlender
    {
    public:
        CameraPoseBlender();

        /**
         * @brief 새 전환을 시작합니다. 컷(곡선 `Cut`, 길이 0, 낸 포즈 없음)이면 다음 `step` 이 들어오는 포즈를 그대로 냅니다.
         * @return 나가는 쪽을 살려 두어야 하면(블렌드가 없던 때 시작한 블렌드) true 입니다. 그때 부르는 쪽이 다음 `step` 부터 나가는 포즈를 줍니다.
         */
        bool start( const BlendCurveDef& blend );
        /**
         * @brief 화면이 지금 @p from 이라고 보고 새 전환을 시작합니다. 나가는 쪽은 고정입니다. 다른 카메라가 그리던 화면(시네마틱의 마지막 컷)에서 넘어올 때 씁니다.
         * @details 컷 블렌드면 다음 `step` 이 들어오는 포즈를 그대로 낸다. 이 카메라는 그동안 화면에 없었으므로 컷 신호는 내지 않는다.
         */
        void startFromPose( const CameraPose& from, const BlendCurveDef& blend );
        /** @brief 시간을 흘려 이번 포즈를 냅니다. @p pLiveFrom 은 살아 있는 나가는 쪽의 이번 포즈입니다(없으면 고정한 출발점을 씁니다). */
        const CameraPose& step( float32 deltaTime, const CameraPose& incoming, const CameraPose* pLiveFrom );

        bool isBlending() const { return _bBlending == SW_TRUE; }
        /** @brief 나가는 쪽을 살려 두는 블렌드인지입니다(`start` 의 반환값과 같다). 블렌드가 끝나면 false 입니다. */
        bool isFromLive() const { return _bFromLive == SW_TRUE; }
        /** @brief 지금 블렌드의 가중치(0 = 나가는 쪽, 1 = 들어오는 쪽)입니다. 블렌드 중이 아니면 1 입니다. */
        float32           getWeight() const { return _weight; }
        const CameraPose& getPose() const { return _outputPose; }
        bool              hasPose() const { return _bHasPose == SW_TRUE; }
        /** @brief 마지막 `start` 가 이미 포즈를 내던 중의 컷이었는지입니다. 그러면 화면이 한 번에 바뀝니다(렌더러의 컷 신호). 읽으면 지웁니다. */
        bool consumeCut();

    private:
        BlendCurveDef          _blend;
        CameraPose             _fromPose; ///< 고정한 출발점 · 살아 있으면 마지막으로 받은 나가는 포즈
        CameraPose             _outputPose;
        float32                _elapsed;
        float32                _weight;
        uint8                  _bBlending  : 1;
        uint8                  _bFromLive  : 1;
        uint8                  _bHasPose   : 1;
        uint8                  _bCutQueued : 1;
        [[maybe_unused]] uint8 _reserved   : 4;
    };
} // namespace sw
