/**
 * @file FirstPersonCameraComponent.h
 * @brief 1인칭 카메라와, 마우스로 요 · 피치를 돌리는 1인칭 시점(`FirstPersonLook` — 슈터 · 복셀 샌드박스 · 걷는 시뮬레이터가 함께 쓴다)을 정의합니다.
 * @details 시점(폰의 조종 회전, 피치 한계), 눈 위치, 손에 든 모델(뷰 모델)을 붙이는 위치를 다룬다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/OrientationUtil.h"
#include "Core/Math/Radians.h"
#include "Core/Math/Ray.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Camera/CameraMode.h"
#include "GameFramework/Runtime/Character/Camera/CameraPreset.h"

namespace sw
{
    /**
     * @class FirstPersonLook
     * @brief 요 · 피치 시점입니다. 피치는 ±최대각에서 자르고(뒤로 넘어가지 않는다) 요는 [-π, π) 로 감습니다.
     * @details 엔진 오일러(`SceneComponent::setLocalRotation`)는 피치가 아래로 + 입니다. 카메라에 넣을 값은 `computeCameraEuler` 가 돌려줍니다.
     *          장르의 규칙이 아니라 기반이다. 1 인칭 시점이 필요한 키트와 게임은 슈터 키트를 링크하지 않고 이것을 쓴다.
     */
    class SW_GF_API FirstPersonLook
    {
    public:
        FirstPersonLook();

        /** @brief 마우스 이동(픽셀)을 감도(라디안/픽셀)로 돌립니다. 화면 아래(+y)로 끌면 아래를 본다. */
        void addMouseDelta( float32 deltaX, float32 deltaY, float32 sensitivity );
        /** @brief 반동을 줍니다. 피치를 올리고(위로 튄다) 요를 조금 흔듭니다(라디안). */
        void addRecoil( float32 pitchKick, float32 yawKick );
        void setAngles( float32 yaw, float32 pitch );
        /** @brief 피치 한계(라디안)입니다. 기본 85°. */
        /** @details 0 ~ 90° 밖이면 오류를 남기고 false 입니다(조용히 맞추지 않습니다). */
        [[nodiscard]] bool setMaxPitch( float32 maxPitch );

        float32 getYaw() const { return _yaw; }
        float32 getPitch() const { return _pitch; }
        float3  getForward() const { return OrientationUtil::computeLookDirection( _yaw, _pitch ); }
        /** @brief 수평 앞(이동용, 피치 없음)입니다. */
        float3 getFlatForward() const { return OrientationUtil::computeLookDirection( _yaw, 0.0f ); }
        /** @brief 수평 오른쪽입니다. */
        float3 getFlatRight() const;
        /** @brief 카메라와 무기에 넣을 엔진 오일러(피치, 요, 롤 0)입니다. */
        float3 computeCameraEuler() const { return float3{ -_pitch, _yaw, 0.0f }; }
        /**
         * @brief 눈 @p eyePosition 기준 오프셋(@p offset)에 둔 뷰 모델의 월드 위치입니다. 오프셋은 오른쪽(수평), 위(시점의 위), 앞(시점) 순입니다. 씬 없이 테스트할 수 있습니다.
         * @details 시점은 피치가 위로 +이고, 엔진 오일러(`SceneComponent::setLocalRotation`)는 피치가 아래로 +이다.
         */
        float3 computeViewModelPosition( const float3& eyePosition, const float3& offset ) const;
        /** @brief 앞 · 오른쪽 축 입력(-1..1)을 수평 이동 방향으로 바꿉니다(길이 1 이하). */
        float3 computeMoveDirection( float32 forwardAxis, float32 rightAxis ) const;

    private:
        float32 _yaw;
        float32 _pitch;
        float32 _maxPitch;
    };

    class MeshComponent;
    class PawnComponent;

} // namespace sw

namespace sw
{
    /**
     * @class FirstPersonCameraComponent
     * @brief 같은 오브젝트의 `CameraComponent` 를 1인칭 원근 시점으로 둡니다. 슈터, 복셀 샌드박스, 걷는 시뮬레이터가 함께 씁니다.
     * @details **시점은 폰의 조종 회전이다.** 같은 오브젝트에 `PawnComponent` 가 있으면 `TickGroup::PrePhysics` 에서 그 의도의 조종 요와 피치를 시점으로 둔다.
     *          마우스와 스틱은 플레이어 조종자가, 바라볼 곳은 AI 조종자가 조종 회전으로 낸다. 이 컴포넌트는 입력을 읽지 않는다.
     *          폰이 없으면(관전 카메라) `setAngles` 로 정한 시점을 지킨다.
     *          마우스 잠금과 Esc 는 플레이어 조종자의 일이다(`PawnComponent::needsMouseLock`).
     *          몸을 움직이는 게임 컴포넌트는 **같은 오브젝트**에서 뒤 그룹에 돈다. 이 시점(`getLook`)으로 걷고 쏜 뒤 `setEyePosition` 으로 눈 위치를 넣으면,
     *          카메라와 뷰 모델이 그 위치에 바로 다시 놓인다.
     *          코드가 시점을 정할 때(시작 시점, 반동)는 `setAngles` 와 `addRecoil` 을 쓴다. 폰이 있으면 그 값이 조종 회전 요청과 오프셋으로 넘어가서 다음 틱부터 조종자의 값이 된다.
     *
     *          **뷰 모델**: 같은 오브젝트의 메시 컴포넌트 중 컴포넌트 이름이 `_viewModelName` 인 것(손에 든 총)을 카메라의 자식으로 둔다.
     *          위치는 로컬 `_viewModelOffset`(오른쪽, 위, 앞)이고 요는 `_viewModelYawOffset` 이다. 시점을 따라 도는 것은 계층이 한다(피치까지).
     *          같은 오브젝트의 씬 컴포넌트는 기본으로 첫 씬 컴포넌트(카메라)에 붙는다. 붙어 있지 않으면 틱 밖에서 붙인다. 이름을 비우면 뷰 모델이 없다.
     *
     *          **사용자 설정**: 옵션 메뉴의 시야각은 `_fieldOfViewY` 를 기본 시야각 대비 비율로 넓히고 좁히며, 머리 흔들림(`_headBob`, 진폭 0 이면 없음)은
     *          `gameplay.headBob` 이 끄면 멈춘다(`CameraUserSettings`). 흔들림의 위상은 눈 위치가 간 거리로 정하므로 `setEyePosition` 이 틱 밖에서 불려도 맞는다.
     */
    REFLECT( Category = "Camera", DisplayName = "First Person Camera", Tooltip = "First-person camera: follows the pawn's control rotation, pitch limit, eye position and a view-model slot" )
    class SW_GF_API FirstPersonCameraComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PrePhysics; ///< 조종 회전(조종 시스템 단계 — 틱 앞)을 몸을 움직이는 컴포넌트(같은 오브젝트, 뒤 그룹)가 같은 프레임에 쓴다.

        FirstPersonCameraComponent();
        virtual ~FirstPersonCameraComponent() override = default;

        void onBeginPlay() override;
        void onTick( TickContext& context ) override;
        /** @brief 루트 카메라의 눈 자리를 옮깁니다(머리 흔들림이 옮긴 만큼 걸은 것으로 재지 않게). */
        void onWorldOriginShifted( const float3& delta ) override;

        /** @brief 눈 위치를 넣고 카메라와 뷰 모델을 바로 다시 둡니다. 눈 위치는 카메라의 부모 공간 기준이며 루트 카메라면 월드입니다. 같은 오브젝트의 게임 컴포넌트가 걷고 난 뒤 부릅니다. */
        void          setEyePosition( const float3& eyePosition );
        const float3& getEyePosition() const { return _eyePosition; }
        /** @brief 시점을 정합니다. 라디안이며 피치는 위가 +입니다. 카메라를 바로 다시 두고, 폰이 있으면 조종 회전을 이 값으로 요청합니다. */
        void setAngles( float32 yaw, float32 pitch );
        /** @brief 반동을 줍니다. 피치를 올리고 요를 흔들며 단위는 라디안입니다. 폰이 있으면 같은 양을 조종 회전 오프셋으로 쌓습니다. */
        void                   addRecoil( float32 pitchKick, float32 yawKick );
        const FirstPersonLook& getLook() const { return _look; }
        /** @brief 뷰 모델(같은 오브젝트의 메시 컴포넌트 이름)과 눈 기준 오프셋(오른쪽, 위, 앞), 앞을 맞추는 요를 정합니다. */
        void setViewModel( const hashed_string& componentName, const float3& offset, float32 yawOffset );
        /** @brief 지금 값으로 같은 오브젝트의 카메라와 뷰 모델을 둡니다. 틱 밖에서 부르면 바로 보입니다. */
        void applyToCamera();

    private:
        PawnComponent* findPawn() const;
        MeshComponent* findViewModel() const;

    private:
        PROPERTY( Category = "Look", DisplayName = "Yaw", Tooltip = "Starting yaw from +Z towards +X" )
        Radians _yaw;
        PROPERTY( Category = "Look", DisplayName = "Pitch", Tooltip = "Starting pitch, up is positive" )
        Radians _pitch;
        PROPERTY( Category = "Look", DisplayName = "Max Pitch", Tooltip = "Pitch limit up and down (keep it equal to the pawn's Max Pitch)", Min = 0.0, Max = 90.0 )
        Radians _maxPitch;
        PROPERTY( Category = "Lens", DisplayName = "Field Of View", Tooltip = "Vertical field of view", Min = 5.7, Max = 171.9 )
        Radians _fieldOfViewY;
        PROPERTY( Category = "Lens", DisplayName = "Near Plane", Min = 0.001, Units = m )
        float32 _nearPlane;
        PROPERTY( Category = "Lens", DisplayName = "Far Plane", Min = 0.01, Units = m )
        float32 _farPlane;
        PROPERTY( Category = "View Model", DisplayName = "View Model", Tooltip = "Component name of the mesh held in front of the eye (empty: none)" )
        hashed_string _viewModelName;
        PROPERTY( Category = "View Model", DisplayName = "Offset", Tooltip = "Right, up and forward of the eye", Units = m )
        float3 _viewModelOffset;
        PROPERTY( Category = "View Model", DisplayName = "Yaw Offset", Tooltip = "Turns the model so its front faces the view (180 when the muzzle is -Z)" )
        Radians _viewModelYawOffset;
        PROPERTY( Category = "Head Bob", DisplayName = "Head Bob", Tooltip = "Walking bob of the view; zero amplitude turns it off" )
        CameraHeadBobDef _headBob;

        SW_TRANSIENT FirstPersonLook _look;
        SW_TRANSIENT float3          _eyePosition;
        SW_TRANSIENT CameraModeState _modeState;        ///< 머리 흔들림의 위상 · 크기
        SW_TRANSIENT float32         _pendingDeltaTime; ///< 틱이 흘린 시간. 다음 `applyToCamera` 가 머리 흔들림에 쓰고 비운다
    };
} // namespace sw
