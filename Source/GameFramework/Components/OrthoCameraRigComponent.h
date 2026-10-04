/**
 * @file OrthoCameraRigComponent.h
 * @brief 비스듬히 내려다보는 직교 카메라 리그 — 초점 둘레를 도는 시점, WASD · 방향키 이동, 휠 확대, Q/E 90° 회전, 외부 시점 덮어쓰기.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 카메라 하나의 자리와 오일러 회전(피치 · 요 · 롤, 라디안)입니다. */
    struct OrthoCameraView
    {
        float3 _position{};
        float3 _euler{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct OrthoCameraRigMath
     * @brief 리그의 계산입니다 — 씬 없이 시험합니다.
     * @details 요는 +Z 에서 +X 쪽으로 도는 각, 피치는 아래로 내려다보는 각입니다(라디안). 오일러는 `CameraComponent::lookAt` 과 같은 배치(피치 · 요 · 0)입니다.
     */
    struct SW_GF_API OrthoCameraRigMath
    {
        /** @brief 초점에서 카메라 쪽을 가리키는 단위 벡터입니다. */
        static float3 computeToCamera( float32 yaw, float32 pitch );
        /** @brief 초점 둘레 @p distance 에 놓인 카메라의 자리 · 회전입니다. 카메라는 초점을 봅니다. */
        static OrthoCameraView computeView( const float3& focus, float32 yaw, float32 pitch, float32 distance );
        /** @brief 화면 기준 입력(앞 · 오른쪽, 각 −1..1)을 땅 위 이동 방향으로 바꿉니다. 길이는 정규화하지 않습니다(대각선이 빠르다 — 타이쿤과 같다). */
        static float3 computePanDirection( float32 yaw, float32 forwardInput, float32 rightInput );
        /** @brief 휠 한 칸만큼 화면 높이를 바꿉니다. 휠이 위(+)면 @p zoomStep 배로 줄이고(확대) 아래면 늘립니다. [min, max] 로 묶습니다. */
        static float32 computeZoomedHeight( float32 orthoHeight, float32 wheel, float32 zoomStep, float32 minHeight, float32 maxHeight );
    };
} // namespace sw

namespace sw
{
    /**
     * @class OrthoCameraRigComponent
     * @brief 같은 오브젝트의 `CameraComponent` 를 롤러코스터 타이쿤 · 도시 건설처럼 비스듬히 내려다보는 직교 시점으로 둡니다.
     * @details `TickGroup::PostUpdate` 에서 자기 오브젝트의 카메라만 씁니다. 입력은 `_bInputEnabled` 일 때만 읽습니다.
     *          다른 컴포넌트(게임 디렉터)가 앞선 그룹에서 `setViewOverride` 로 시점을 넣으면 그 프레임은 그 원근 시점을 씁니다(코스터 탑승 · 컷신).
     *          덮어쓰기는 틱 그룹 순서로만 안전합니다 — 리그와 같은 그룹(PostUpdate)에서 부르면 리그가 도는 워커와 겹칩니다.
     */
    REFLECT( Category = "Camera", DisplayName = "Ortho Camera Rig", Tooltip = "Isometric orthographic camera rig: pan, zoom, 90-degree rotate, external view override" )
    class SW_GF_API OrthoCameraRigComponent : public Component
    {
    public:
        REFLECT_BODY();

        OrthoCameraRigComponent();
        virtual ~OrthoCameraRigComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 다음 틱부터 이 원근 시점을 씁니다(직교 · 입력을 쉰다). @p euler 는 피치 · 요 · 롤(라디안)입니다. */
        void setViewOverride( const float3& position, const float3& euler, float32 fieldOfViewY, float32 farPlane );
        /** @brief 덮어쓴 시점을 풀고 직교 시점으로 돌아갑니다. */
        void clearViewOverride() { _bOverride = SW_FALSE; }
        bool isViewOverridden() const { return _bOverride == SW_TRUE; }

        const float3& getFocus() const { return _focus; }
        void          setFocus( const float3& focus ) { _focus = focus; }
        float32       getYaw() const { return _yaw; }
        float32       getOrthoHeight() const { return _orthoHeight; }
        /** @brief 지금 값으로 같은 오브젝트의 카메라를 둡니다(틱 밖에서 부르면 바로 보인다). */
        void applyToCamera();

    private:
        void applyInput( float32 deltaTime );

    private:
        PROPERTY( Category = "Rig", DisplayName = "Focus", Tooltip = "Ground point the camera orbits and looks at", Meta = "Units=m" )
        float3 _focus;
        PROPERTY( Category = "Rig", DisplayName = "Yaw", Tooltip = "Orbit angle from +Z towards +X", Meta = "Units=rad" )
        float32 _yaw;
        PROPERTY( Category = "Rig", DisplayName = "Pitch", Tooltip = "Downward viewing angle", Min = 0.01, Max = 1.56, Meta = "Units=rad" )
        float32 _pitch;
        PROPERTY( Category = "Rig", DisplayName = "Distance", Tooltip = "Distance from the focus to the camera", Min = 1.0, Meta = "Units=m" )
        float32 _distance;
        PROPERTY( Category = "Rig", DisplayName = "Ortho Height", Tooltip = "Visible height of the orthographic view", Min = 0.1, Meta = "Units=m" )
        float32 _orthoHeight;
        PROPERTY( Category = "Rig", DisplayName = "Min Ortho Height", Tooltip = "Closest zoom", Min = 0.1, Meta = "Units=m" )
        float32 _minOrthoHeight;
        PROPERTY( Category = "Rig", DisplayName = "Max Ortho Height", Tooltip = "Farthest zoom", Min = 0.1, Meta = "Units=m" )
        float32 _maxOrthoHeight;
        PROPERTY( Category = "Rig", DisplayName = "Zoom Step", Tooltip = "Height factor per wheel notch (below 1)", Min = 0.1, Max = 0.99 )
        float32 _zoomStep;
        PROPERTY( Category = "Rig", DisplayName = "Pan Speed", Tooltip = "Pan speed at the reference height; scales with the zoom", Meta = "Units=m/s" )
        float32 _panSpeed;
        PROPERTY( Category = "Rig", DisplayName = "Pan Reference Height", Tooltip = "Ortho height at which the pan speed holds", Min = 0.1, Meta = "Units=m" )
        float32 _panReferenceHeight;
        PROPERTY( Category = "Rig", DisplayName = "Rotate Step", Tooltip = "Yaw change per Q/E press", Meta = "Units=rad" )
        float32 _rotateStep;
        PROPERTY( Category = "Rig", DisplayName = "Far Plane Scale", Tooltip = "Far plane as a multiple of the distance", Min = 1.0 )
        float32 _farPlaneScale;
        PROPERTY( Category = "Rig", DisplayName = "Input Enabled", Tooltip = "Read WASD / arrows, wheel and Q/E" )
        bool _bInputEnabled;

        float3  _overridePosition;
        float3  _overrideEuler;
        float32 _overrideFieldOfViewY;
        float32 _overrideFarPlane;
        uint8   _bOverride : 1;
        uint8   _reserved  : 7;
    };
} // namespace sw
