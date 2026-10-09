/**
 * @file OrthoCameraRigComponent.h
 * @brief 비스듬히 내려다보는 직교 카메라 리그 — 초점 둘레를 도는 시점, 이동(범위 제한) · 확대 · 90° 회전(입력 맵 `Camera.Pan` · `Camera.Zoom` · `Camera.Rotate`), 외부 시점 덮어쓰기,
 *        화면 점 → 땅 점(마우스 고르기).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/Camera/CameraBlend.h"
#include "GameFramework/Base/Actor/Camera/CameraDirector.h"
#include "GameFramework/Base/Actor/Camera/CameraPreset.h"
#include "GameFramework/Base/Foundation/Utility/Math/RayMath.h"
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
        /** @brief 초점의 X · Z 를 [@p focusMin, @p focusMax] 안에 묶습니다(Y 는 그대로). */
        static float3 clampFocus( const float3& focus, const float3& focusMin, const float3& focusMax );
        /**
         * @brief 화면 점(@p mouseNormalized — 왼쪽 위 (0, 0) · 오른쪽 아래 (1, 1))을 지나는 직교 광선입니다. 방향은 카메라 앞 방향이고 시작은 카메라 평면 위입니다.
         * @param aspect 화면 너비 ÷ 높이. 화면 높이는 @p orthoHeight, 너비는 높이 × @p aspect 입니다.
         */
        static GameRay computeScreenRay( const float3& focus, float32 yaw, float32 pitch, float32 distance, float32 orthoHeight, float32 aspect,
                                         const float2& mouseNormalized );
    };
} // namespace sw

namespace sw
{
    /**
     * @class OrthoCameraRigComponent
     * @brief 같은 오브젝트의 `CameraComponent` 를 롤러코스터 타이쿤 · 도시 건설처럼 비스듬히 내려다보는 직교 시점으로 둡니다.
     * @details **직교 시점은 카메라 모드(`CameraPresetMode::OrthoTopDown`)이고 리그는 그 입력 앞단입니다.** 리그의 값(초점 · 요 · 피치 · 거리 · 화면
     *          높이)으로 프리셋을 지어 `CameraDirector` 가 풀고 블렌드하므로, 데이터 프리셋과 같은 계산 · 같은 블렌드를 탑니다.
     *          `TickGroup::PostUpdate` 에서 자기 오브젝트의 카메라만 씁니다. 입력은 `_bInputEnabled` 일 때만 읽습니다.
     *          다른 컴포넌트(게임 디렉터)가 앞선 그룹에서 `setViewOverride` 로 시점을 넣으면 그 원근 시점(`Fixed` 모드)으로 `_overrideBlend` 를 따라
     *          블렌드해 들어가고(코스터 탑승 · 컷신), 풀면 같은 블렌드로 직교 시점에 돌아옵니다. Q/E 회전은 `_rotateTime` 으로 부드럽게 돈다.
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
        /** @brief 덮어쓴 시점을 풀고 직교 시점으로 돌아갑니다(`_overrideBlend` 로). */
        void clearViewOverride() { _bOverride = SW_FALSE; }
        bool isViewOverridden() const { return _bOverride == SW_TRUE; }

        const float3& getFocus() const { return _focus; }
        void          setFocus( const float3& focus ) { _focus = focus; }
        float32       getYaw() const { return _yaw; }
        /** @brief 화면에 보이는 요입니다 — Q/E 로 돈 요를 `_rotateTime` 으로 따라간다(마우스 고르기는 이 요로 쏜다). */
        float32 getShownYaw() const;
        float32 getOrthoHeight() const { return _orthoHeight; }
        /** @brief 직교 화면 높이를 바꿉니다(확대 범위 안으로 묶는다 — 휠과 같은 규칙). */
        void setOrthoHeight( float32 orthoHeight );
        /** @brief 초점을 미는 입력 맵 액션(2D 벡터)을 바꿉니다 — 같은 게임에서 모드마다 다른 키로 밀 때(관전은 WASD 도). */
        void setPanAction( const hashed_string& action ) { _panAction = action; }
        /**
         * @brief 화면 점 아래의 땅(y = @p groundHeight) 점입니다. 지금 직교 시점(초점 · 요 · 피치 · 거리 · 화면 높이)으로 광선을 쏩니다. 안 만나면 false 입니다.
         * @details 다른 오브젝트가 읽어도 됩니다 — 리그는 PostUpdate 에서 쓰므로 앞 그룹(PrePhysics)에서 부르면 지난 프레임의 시점입니다.
         */
        [[nodiscard]] bool findGroundPoint( const float2& mouseNormalized, float32 aspect, float32 groundHeight, float3& outPoint ) const;
        /** @brief 지금 값으로 같은 오브젝트의 카메라를 둡니다(시간을 흘리지 않는다 — 틱 밖에서 부르면 바로 보인다). */
        void applyToCamera() { updateCamera( 0.0f ); }
        /** @brief 시간을 흘려 블렌드 · 회전 따라가기를 진행하고 카메라를 둡니다. */
        void updateCamera( float32 deltaTime );
        /** @brief 덮어쓰기 · 직교로 오가는 블렌드입니다. */
        void setOverrideBlend( const BlendCurveSpec& blend ) { _overrideBlend = blend; }

    private:
        void applyInput( float32 deltaTime );
        /** @brief 리그 값으로 직교 프리셋(`OrthoTopDown`)을 짓습니다. 요는 모드 상태의 회전 요가 준다. */
        CameraPresetDef makeOrthoPreset() const;
        /** @brief 덮어쓴 시점으로 고정 프리셋(`Fixed`)을 짓습니다. */
        CameraPresetDef makeOverridePreset() const;

    private:
        PROPERTY( Category = "Rig", DisplayName = "Focus", Tooltip = "Ground point the camera orbits and looks at", Units = m )
        float3 _focus;
        PROPERTY( Category = "Rig", DisplayName = "Yaw", Tooltip = "Orbit angle from +Z towards +X", Units = rad )
        float32 _yaw;
        PROPERTY( Category = "Rig", DisplayName = "Pitch", Tooltip = "Downward viewing angle", Min = 0.01, Max = 1.56, Units = rad )
        float32 _pitch;
        PROPERTY( Category = "Rig", DisplayName = "Distance", Tooltip = "Distance from the focus to the camera", Min = 1.0, Units = m )
        float32 _distance;
        PROPERTY( Category = "Rig", DisplayName = "Ortho Height", Tooltip = "Visible height of the orthographic view", Min = 0.1, Units = m )
        float32 _orthoHeight;
        PROPERTY( Category = "Rig", DisplayName = "Min Ortho Height", Tooltip = "Closest zoom", Min = 0.1, Units = m )
        float32 _minOrthoHeight;
        PROPERTY( Category = "Rig", DisplayName = "Max Ortho Height", Tooltip = "Farthest zoom", Min = 0.1, Units = m )
        float32 _maxOrthoHeight;
        PROPERTY( Category = "Rig", DisplayName = "Zoom Step", Tooltip = "Height factor per wheel notch (below 1)", Min = 0.1, Max = 0.99 )
        float32 _zoomStep;
        PROPERTY( Category = "Rig", DisplayName = "Pan Speed", Tooltip = "Pan speed at the reference height; scales with the zoom", Units = "m/s" )
        float32 _panSpeed;
        PROPERTY( Category = "Rig", DisplayName = "Pan Reference Height", Tooltip = "Ortho height at which the pan speed holds", Min = 0.1, Units = m )
        float32 _panReferenceHeight;
        PROPERTY( Category = "Rig", DisplayName = "Rotate Step", Tooltip = "Yaw change per Q/E press", Units = rad )
        float32 _rotateStep;
        PROPERTY( Category = "Rig", DisplayName = "Far Plane Scale", Tooltip = "Far plane as a multiple of the distance", Min = 1.0 )
        float32 _farPlaneScale;
        PROPERTY( Category = "Rig", DisplayName = "Focus Min", Tooltip = "Lowest focus X and Z when Clamp Focus is on", Units = m )
        float3 _focusMin;
        PROPERTY( Category = "Rig", DisplayName = "Focus Max", Tooltip = "Highest focus X and Z when Clamp Focus is on", Units = m )
        float3 _focusMax;
        PROPERTY( Category = "Rig", DisplayName = "Pan Action", Tooltip = "InputMap action (2D vector) that pans the focus; missing from the map: no pan" )
        hashed_string _panAction;
        PROPERTY( Category = "Rig", DisplayName = "Rotate Action", Tooltip = "InputMap action (1D axis, Pressed) whose sign steps the yaw; missing from the map: no rotation" )
        hashed_string _rotateAction;
        PROPERTY( Category = "Rig", DisplayName = "Zoom Action", Tooltip = "InputMap action (1D axis, wheel notches; up zooms in); missing from the map: no zoom" )
        hashed_string _zoomAction;
        PROPERTY( Category = "Rig", DisplayName = "Input Enabled", Tooltip = "Read the pan / rotate / zoom actions" )
        bool _bInputEnabled;
        PROPERTY( Category = "Rig", DisplayName = "Clamp Focus", Tooltip = "Keep the panned focus inside Focus Min / Max (X and Z)" )
        bool _bClampFocus;
        PROPERTY( Category = "Rig", DisplayName = "Rotate Time", Tooltip = "Time constant the shown yaw follows a Q/E step with", Min = 0.0, Units = s )
        float32 _rotateTime;
        PROPERTY( Category = "Rig", DisplayName = "Override Blend", Tooltip = "Blend into and out of a view override (ride camera)" )
        BlendCurveSpec _overrideBlend;

        float3         _overridePosition;
        float3         _overrideEuler;
        float32        _overrideFieldOfViewY;
        float32        _overrideFarPlane;
        CameraDirector _director;
        uint8          _bOverride        : 1;
        uint8          _bOverrideApplied : 1; ///< 디렉터가 지금 덮어쓴 시점 프리셋을 켜 두었다
        uint8          _reserved         : 6;
    };
} // namespace sw
