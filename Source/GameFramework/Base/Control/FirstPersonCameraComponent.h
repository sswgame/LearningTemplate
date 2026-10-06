/**
 * @file FirstPersonCameraComponent.h
 * @brief 1인칭 카메라 — 시점(폰의 조종 회전, 피치 한계), 눈 자리, 손에 든 모델(뷰 모델)을 붙이는 자리.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Input/FirstPersonLook.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class MeshComponent;
    class PawnComponent;

    /**
     * @struct FirstPersonCameraMath
     * @brief 1인칭 카메라의 계산입니다 — 씬 없이 시험합니다.
     * @details 시점(`FirstPersonLook`)은 피치가 위로 + 이고, 엔진 오일러(`SceneComponent::setLocalRotation`)는 피치가 아래로 + 입니다.
     */
    struct SW_GF_API FirstPersonCameraMath
    {
        /** @brief 눈 기준 오른쪽(수평) · 위(시점의 위) · 앞(시점) 오프셋(@p offset)에 둔 뷰 모델의 월드 자리입니다. */
        static float3 computeViewModelPosition( const float3& eyePosition, const FirstPersonLook& look, const float3& offset );
    };
} // namespace sw

namespace sw
{
    /**
     * @class FirstPersonCameraComponent
     * @brief 같은 오브젝트의 `CameraComponent` 를 1인칭 원근 시점으로 둡니다. 슈터 · 복셀 샌드박스 · 걷는 시뮬레이터가 함께 씁니다.
     * @details **시점은 폰의 조종 회전입니다** — 같은 오브젝트에 `PawnComponent` 가 있으면 `TickGroup::PrePhysics` 에서 그 의도의 조종 요 · 피치를 시점으로 둡니다
     *          (마우스 · 스틱은 플레이어 조종자가, 바라볼 곳은 AI 조종자가 조종 회전으로 낸다 — 이 컴포넌트는 입력을 읽지 않는다). 폰이 없으면(관전 카메라)
     *          `setAngles` 로 정한 시점을 지킵니다. 마우스 잠금 · Esc 는 플레이어 조종자의 일입니다(`PawnComponent::wantsMouseLock`).
     *          몸을 움직이는 게임 컴포넌트는 **같은 오브젝트**에서 뒤 그룹에 돌며 이 시점(`getLook`)으로 걷고 쏜 뒤 `setEyePosition` 으로 눈 자리를 넣습니다 —
     *          카메라 · 뷰 모델은 그 자리에서 바로 다시 놓인다. 코드가 시점을 정하면(시작 시점 · 반동) `setAngles` · `addRecoil` 이고, 폰이 있으면 그 값이
     *          조종 회전 요청 · 오프셋으로 넘어가 다음 틱부터 조종자의 값이 됩니다.
     *
     *          **뷰 모델**: 같은 오브젝트의 메시 컴포넌트 중 컴포넌트 이름이 `_viewModelName` 인 것(손에 든 총)을 카메라의 자식으로 두고 로컬
     *          `_viewModelOffset`(오른쪽 · 위 · 앞) · 요 `_viewModelYawOffset` 에 놓습니다 — 시점을 따라 도는 것은 계층이 한다(피치까지). 같은 오브젝트의
     *          씬 컴포넌트는 기본으로 첫 씬 컴포넌트(카메라)에 붙는다. 붙어 있지 않으면 틱 밖에서 붙인다. 비우면 없다.
     */
    REFLECT( Category = "Camera", DisplayName = "First Person Camera", Tooltip = "First-person camera: follows the pawn's control rotation, pitch limit, eye position and a view-model slot" )
    class SW_GF_API FirstPersonCameraComponent : public Component
    {
    public:
        REFLECT_BODY();

        FirstPersonCameraComponent();
        virtual ~FirstPersonCameraComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 눈 자리(카메라의 부모 공간 — 루트 카메라면 월드)를 넣고 카메라 · 뷰 모델을 바로 다시 둡니다(같은 오브젝트의 게임 컴포넌트가 걷고 난 뒤 부른다). */
        void          setEyePosition( const float3& eyePosition );
        const float3& getEyePosition() const { return _eyePosition; }
        /** @brief 시점을 정합니다(라디안, 피치는 위가 +). 카메라를 바로 다시 두고, 폰이 있으면 조종 회전을 이 값으로 요청한다. */
        void setAngles( float32 yaw, float32 pitch );
        /** @brief 반동 — 피치를 올리고 요를 흔듭니다(라디안). 폰이 있으면 같은 양을 조종 회전 오프셋으로 쌓는다. */
        void                   addRecoil( float32 pitchKick, float32 yawKick );
        const FirstPersonLook& getLook() const { return _look; }
        /** @brief 뷰 모델(같은 오브젝트의 메시 컴포넌트 이름)과 눈 기준 오프셋(오른쪽 · 위 · 앞) · 앞을 맞추는 요를 정합니다. */
        void setViewModel( const hashed_string& componentName, const float3& offset, float32 yawOffset );
        /** @brief 지금 값으로 같은 오브젝트의 카메라와 뷰 모델을 둡니다(틱 밖에서 부르면 바로 보인다). */
        void applyToCamera();

    private:
        PawnComponent* findPawn() const;
        MeshComponent* findViewModel() const;

    private:
        PROPERTY( Category = "Look", DisplayName = "Yaw", Tooltip = "Starting yaw from +Z towards +X", Units = rad )
        float32 _yaw;
        PROPERTY( Category = "Look", DisplayName = "Pitch", Tooltip = "Starting pitch, up is positive", Units = rad )
        float32 _pitch;
        PROPERTY( Category = "Look", DisplayName = "Max Pitch", Tooltip = "Pitch limit up and down (keep it equal to the pawn's Max Pitch)", Min = 0.0, Max = 1.5707963, Units = rad )
        float32 _maxPitch;
        PROPERTY( Category = "Lens", DisplayName = "Field Of View", Tooltip = "Vertical field of view", Min = 0.1, Max = 3.0, Units = rad )
        float32 _fieldOfViewY;
        PROPERTY( Category = "Lens", DisplayName = "Near Plane", Min = 0.001, Units = m )
        float32 _nearPlane;
        PROPERTY( Category = "Lens", DisplayName = "Far Plane", Min = 0.01, Units = m )
        float32 _farPlane;
        PROPERTY( Category = "View Model", DisplayName = "View Model", Tooltip = "Component name of the mesh held in front of the eye (empty: none)" )
        hashed_string _viewModelName;
        PROPERTY( Category = "View Model", DisplayName = "Offset", Tooltip = "Right, up and forward of the eye", Units = m )
        float3 _viewModelOffset;
        PROPERTY( Category = "View Model", DisplayName = "Yaw Offset", Tooltip = "Turns the model so its front faces the view (pi when the muzzle is -Z)", Units = rad )
        float32 _viewModelYawOffset;

        FirstPersonLook _look;
        float3          _eyePosition;
    };
} // namespace sw
