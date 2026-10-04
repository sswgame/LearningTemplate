/**
 * @file FirstPersonCameraComponent.h
 * @brief 1인칭 카메라 — 마우스 시점(피치 한계), 마우스 잠금(Esc 로 풀고 다시 잠근다), 눈 자리, 손에 든 모델(뷰 모델)을 붙이는 자리.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Input/FirstPersonLook.h"

namespace sw
{
    class InputManager;
    class MeshComponent;

    /**
     * @struct FirstPersonCameraMath
     * @brief 1인칭 카메라의 계산입니다 — 씬 없이 시험합니다.
     * @details 시점(`FirstPersonLook`)은 피치가 위로 + 이고, 엔진 오일러(`SceneComponent::setLocalRotation`)는 피치가 아래로 + 입니다.
     */
    struct SW_GF_API FirstPersonCameraMath
    {
        /** @brief 눈 기준 오른쪽(수평) · 위(시점의 위) · 앞(시점) 오프셋(@p offset)에 둔 뷰 모델의 월드 자리입니다. */
        static float3 computeViewModelPosition( const float3& eyePosition, const FirstPersonLook& look, const float3& offset );
        /** @brief 마우스 이동(픽셀)을 감도로 돌린 시점입니다. 화면 아래(+y)로 끌면 아래를 보고, 피치는 시점의 한계에서 자른다. */
        static FirstPersonLook computeLookAfterMouse( const FirstPersonLook& look, float32 deltaX, float32 deltaY, float32 sensitivity );
    };
} // namespace sw

namespace sw
{
    /**
     * @class FirstPersonCameraComponent
     * @brief 같은 오브젝트의 `CameraComponent` 를 1인칭 원근 시점으로 둡니다. 슈터 · 복셀 샌드박스 · 걷는 시뮬레이터가 함께 씁니다.
     * @details `TickGroup::PrePhysics` 에서 마우스로 시점을 돌리고(잠겨 있을 때만) 카메라를 둡니다. `_lookAction` 이 있으면 원시 마우스 대신 입력 맵 액션의
     *          2D 값(마우스 이동량 · 스틱 바인딩)을 읽습니다. 몸을 움직이는 게임 컴포넌트는 **같은 오브젝트**에서
     *          뒤 그룹에 돌며 이 시점(`getLook`)으로 걷고 쏜 뒤 `setEyePosition` 으로 눈 자리를 넣습니다 — 카메라 · 뷰 모델은 그 자리에서 바로 다시 놓인다.
     *          시점을 코드로 정하면(자동 조준 · 반동) `setAngles` · `addRecoil` 입니다.
     *
     *          **마우스 잠금**: `_bLockMouse` 면 첫 틱에 커서를 창 가운데에 잠그고 숨기며, Esc 가 풀고 다시 잠급니다. 마우스 시점을 끄면(`setMouseLookEnabled`
     *          — 자동 플레이) 잠그지 않습니다. 잠금 · 커서는 틱 뒤 게임 스레드에서 바꾸고, 플레이가 끝나면 이 컴포넌트가 잠갔던 것을 풉니다.
     *
     *          **뷰 모델**: 같은 오브젝트의 메시 컴포넌트 중 컴포넌트 이름이 `_viewModelName` 인 것(손에 든 총)을 카메라의 자식으로 두고 로컬
     *          `_viewModelOffset`(오른쪽 · 위 · 앞) · 요 `_viewModelYawOffset` 에 놓습니다 — 시점을 따라 도는 것은 계층이 한다(피치까지). 같은 오브젝트의
     *          씬 컴포넌트는 기본으로 첫 씬 컴포넌트(카메라)에 붙는다. 붙어 있지 않으면 틱 밖에서 붙인다. 비우면 없다.
     */
    REFLECT( Category = "Camera", DisplayName = "First Person Camera", Tooltip = "First-person camera: mouse look with pitch limit, mouse lock, eye position and a view-model slot" )
    class SW_GF_API FirstPersonCameraComponent : public Component
    {
    public:
        REFLECT_BODY();

        FirstPersonCameraComponent();
        virtual ~FirstPersonCameraComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 눈 자리(카메라의 부모 공간 — 루트 카메라면 월드)를 넣고 카메라 · 뷰 모델을 바로 다시 둡니다(같은 오브젝트의 게임 컴포넌트가 걷고 난 뒤 부른다). */
        void          setEyePosition( const float3& eyePosition );
        const float3& getEyePosition() const { return _eyePosition; }
        /** @brief 시점을 정합니다(라디안, 피치는 위가 +). 카메라를 바로 다시 둔다. */
        void setAngles( float32 yaw, float32 pitch );
        /** @brief 반동 — 피치를 올리고 요를 흔듭니다(라디안). */
        void addRecoil( float32 pitchKick, float32 yawKick );
        /** @brief 마우스 이동(픽셀)을 감도로 시점에 더합니다. 틱이 잠긴 마우스로 부르고, 시험도 이것을 부른다. */
        void                   addMouseDelta( float32 deltaX, float32 deltaY );
        const FirstPersonLook& getLook() const { return _look; }
        /** @brief 마우스로 시점을 돌릴지입니다(자동 플레이는 끈다 — 잠금도 풀린다). */
        void setMouseLookEnabled( bool bEnabled );
        /** @brief 시점을 돌릴 입력 맵 액션(2D — 마우스 이동량 · 스틱)입니다. 비우면 원시 마우스입니다. */
        void setLookAction( const hashed_string& action ) { _lookAction = action; }
        bool isMouseLookEnabled() const { return _bMouseLook; }
        /** @brief 뷰 모델(같은 오브젝트의 메시 컴포넌트 이름)과 눈 기준 오프셋(오른쪽 · 위 · 앞) · 앞을 맞추는 요를 정합니다. */
        void setViewModel( const hashed_string& componentName, const float3& offset, float32 yawOffset );
        /** @brief 지금 값으로 같은 오브젝트의 카메라와 뷰 모델을 둡니다(틱 밖에서 부르면 바로 보인다). */
        void applyToCamera();

    private:
        void updateMouseLock( const InputManager& input );
        /** @brief 잠금 상태를 입력 매니저에 넣습니다 — 틱 안이면 틱 뒤 게임 스레드로 미룬다. */
        void           scheduleMouseLockApply();
        void           applyMouseLock( bool bLocked );
        MeshComponent* findViewModel() const;

    private:
        PROPERTY( Category = "Look", DisplayName = "Yaw", Tooltip = "Starting yaw from +Z towards +X", Meta = "Units=rad" )
        float32 _yaw;
        PROPERTY( Category = "Look", DisplayName = "Pitch", Tooltip = "Starting pitch, up is positive", Meta = "Units=rad" )
        float32 _pitch;
        PROPERTY( Category = "Look", DisplayName = "Max Pitch", Tooltip = "Pitch limit up and down", Min = 0.0, Max = 1.5707963, Meta = "Units=rad" )
        float32 _maxPitch;
        PROPERTY( Category = "Look", DisplayName = "Mouse Sensitivity", Tooltip = "Radians per pixel of mouse movement", Min = 0.0 )
        float32 _mouseSensitivity;
        PROPERTY( Category = "Look", DisplayName = "Look Action", Tooltip = "InputMap action whose 2D value turns the view (mouse delta binding); empty reads the raw mouse" )
        hashed_string _lookAction;
        PROPERTY( Category = "Look", DisplayName = "Mouse Look", Tooltip = "Turn the view with the mouse (auto play turns it off)" )
        bool _bMouseLook;
        PROPERTY( Category = "Look", DisplayName = "Lock Mouse", Tooltip = "Lock and hide the cursor while mouse look is on; Esc toggles" )
        bool _bLockMouse;
        PROPERTY( Category = "Lens", DisplayName = "Field Of View", Tooltip = "Vertical field of view", Min = 0.1, Max = 3.0, Meta = "Units=rad" )
        float32 _fieldOfViewY;
        PROPERTY( Category = "Lens", DisplayName = "Near Plane", Min = 0.001, Meta = "Units=m" )
        float32 _nearPlane;
        PROPERTY( Category = "Lens", DisplayName = "Far Plane", Min = 0.01, Meta = "Units=m" )
        float32 _farPlane;
        PROPERTY( Category = "View Model", DisplayName = "View Model", Tooltip = "Component name of the mesh held in front of the eye (empty: none)" )
        hashed_string _viewModelName;
        PROPERTY( Category = "View Model", DisplayName = "Offset", Tooltip = "Right, up and forward of the eye", Meta = "Units=m" )
        float3 _viewModelOffset;
        PROPERTY( Category = "View Model", DisplayName = "Yaw Offset", Tooltip = "Turns the model so its front faces the view (pi when the muzzle is -Z)", Meta = "Units=rad" )
        float32 _viewModelYawOffset;

        FirstPersonLook _look;
        float3          _eyePosition;
        uint8           _bMouseLocked      : 1; ///< 지금 잠가 두어야 한다(Esc 가 뒤집는다)
        uint8           _bLockApplied      : 1; ///< 입력 매니저에 잠금이 들어가 있다(끝날 때 푼다)
        uint8           _bLockApplyPending : 1;
        uint8           _bLockInitialized  : 1; ///< 첫 틱에 잠금을 정했다
        uint8           _reserved          : 4;
    };
} // namespace sw
