/**
 * @file LocomotionWarpingComponent.h
 * @brief 이동 보정 — 보폭(실제 속도 ÷ 클립이 적은 속도로 재생 배율)과 방향(이동 방향과 몸 방향의 차이만큼 하체 뼈를 위 축으로 돌림)을 맞춥니다.
 * @details 언리얼 Stride Warping · Orientation Warping 의 단순형입니다. 모두 데이터(PROPERTY)입니다:
 *          - **보폭**: 클립 커브 `_speedCurve`(임포트 곁 데이터가 적은 클립의 이동 속도, m/s)와 실제 수평 속도(캐릭터 컨트롤러 → 없으면 트랜스폼 변화)로
 *            애니메이터 재생 배율을 [`_minPlayRate`, `_maxPlayRate`] 안에서 고릅니다. 배율로 못 맞춘 나머지는 `getStrideScale()` — 발 IK(PoseModifier)가
 *            보폭을 늘리고 줄일 자리입니다(여기서는 값만 낸다).
 *          - **방향**: 이동 방향과 몸 앞의 요 차이(뒤로 가면 반대로 접고, ±`_maxOrientationAngle`)를 `_listOrientationBone` 의 뼈마다 가중치만큼 캐릭터 위 축으로
 *            돌립니다(골반 +1, 가슴 -0.5 처럼 — 다리는 이동 방향을, 상체는 몸 방향을 본다). 후처리 단계(PostProcess)에서 돕니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class LocomotionWarpingComponent;

    /** @brief 방향 보정이 돌리는 뼈 하나와 그 몫입니다. */
    REFLECT()
    struct SW_API LocomotionOrientationBone
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Bone rotated around the character up axis" )
        hashed_string _bone{};
        PROPERTY( Tooltip = "Share of the orientation angle (1 = full, negative counter-rotates)" )
        float32 _weight{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @class LocomotionWarpingBinding @brief 유닛의 단계에 끼어드는 얼굴입니다(리플렉션 컴포넌트는 기반 하나). */
    class SW_API LocomotionWarpingBinding final : public IAnimationPhaseTask
    {
    public:
        explicit LocomotionWarpingBinding( LocomotionWarpingComponent& owner );

        bool isAnimationActive() const override { return false; }
        void runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context ) override;
        void finishAnimationFrame( SkeletalMeshComponent& unit ) override;
        void onAnimationUnitDetached( SkeletalMeshComponent& unit ) override;

    private:
        LocomotionWarpingComponent& _owner;
    };
} // namespace sw

namespace sw
{
    /** @class LocomotionWarpingComponent @brief 파일 머리말 참고. 같은 오브젝트의 유닛 · 애니메이터에 붙습니다. */
    REFLECT( Category = "Animation", DisplayName = "Locomotion Warping", Tooltip = "Stride (play rate) and orientation (lower body yaw) adaptation to the actual movement" )
    class SW_API LocomotionWarpingComponent : public Component
    {
        friend class LocomotionWarpingBinding;

    public:
        REFLECT_BODY();

        LocomotionWarpingComponent();
        virtual ~LocomotionWarpingComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;

        /** @brief 마지막 프레임의 방향 보정 각(라디안)입니다. */
        float32 getOrientationAngle() const { return _orientationAngle; }
        /** @brief 재생 배율로 못 맞춘 보폭 배율입니다(1 = 맞음). 발 IK 가 읽을 자리입니다. */
        float32 getStrideScale() const { return _strideScale; }
        /** @brief 마지막으로 잰 수평 속도(월드, m/s)입니다. */
        const float3& getMeasuredVelocity() const { return _measuredVelocity; }
        /** @brief 뼈 목록을 정합니다(코드 · 시험). */
        void setOrientationBones( const vector<LocomotionOrientationBone>& listBone ) { _listOrientationBone = listBone; }

    private:
        /** @brief 게임 스레드 — 속도를 재고 재생 배율 · 방향 각을 정합니다. */
        void updateFromMovement( SkeletalMeshComponent& unit );
        /** @brief 후처리 — 방향 각만큼 뼈를 돌립니다(워커). */
        void applyOrientation( SkeletalMeshComponent& unit ) const;
        void bindToUnit();
        void unbindFromUnit();

        PROPERTY( Category = "Stride", DisplayName = "Stride Warping", Tooltip = "Scale the play rate by actual speed / clip speed" )
        bool _bStrideWarping;
        PROPERTY( Category = "Stride", DisplayName = "Speed Curve", Tooltip = "Clip curve holding the authored movement speed (m/s)" )
        hashed_string _speedCurve;
        PROPERTY( Category = "Stride", DisplayName = "Min Play Rate", Min = 0.1, Max = 4.0 )
        float32 _minPlayRate;
        PROPERTY( Category = "Stride", DisplayName = "Max Play Rate", Min = 0.1, Max = 4.0 )
        float32 _maxPlayRate;
        PROPERTY( Category = "Orientation", DisplayName = "Orientation Warping", Tooltip = "Turn the lower body toward the movement direction" )
        bool _bOrientationWarping;
        PROPERTY( Category = "Orientation", DisplayName = "Max Angle", Min = 0.0, Max = 3.14, Units = rad )
        float32 _maxOrientationAngle;
        PROPERTY( Category = "Orientation", DisplayName = "Bones", Tooltip = "Bones turned around the character up axis and their share" )
        vector<LocomotionOrientationBone> _listOrientationBone;

        LocomotionWarpingBinding _binding;
        SkeletalMeshComponent*   _pUnit; ///< 같은 오브젝트의 유닛(일을 건 곳 — 유닛이 사라지면 알려 준다)
        float3                   _previousPosition;
        float3                   _measuredVelocity;
        float32                  _orientationAngle;
        float32                  _strideScale;
        float32                  _lastDeltaSeconds;
        bool                     _bHasPrevious;
    };
} // namespace sw
