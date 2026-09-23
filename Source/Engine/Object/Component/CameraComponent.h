/**
 * @file CameraComponent.h
 * @brief 렌더용 뷰/투영 행렬을 만드는 SceneComponent 입니다.
 */
#pragma once
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;

    /// @brief 카메라 역할입니다.
    ENUM()
    enum class CameraRole : uint8
    {
        Game   = 0, ///< 플레이 중에 활성
        Editor = 1, ///< 에디터 뷰포트에서 활성
        Custom = 2, ///< 직접 골라야만 쓰임
    };

    /**
     * @class CameraComponent
     * @brief GameObject 에 붙는 카메라입니다. 트랜스폼은 SceneComponent 에서 옵니다.
     */
    REFLECT( Category = "Camera", DisplayName = "Camera Component", Tooltip = "Perspective / Orthographic Viewport Camera" )
    class SW_API CameraComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        static constexpr float32 kDefaultFovY        = 0.70f;
        static constexpr float32 kDefaultNearZ       = 0.1f;
        static constexpr float32 kDefaultFarZ        = 100.0f;
        static constexpr float32 kDefaultOrthoHeight = 10.0f;

        /** @brief 기본 카메라를 만듭니다. */
        CameraComponent();

        /**
         * @brief 그 이름의 오브젝트에 카메라가 있게 하고(없으면 오브젝트 · 컴포넌트를 만듭니다) 역할 · 위치 · 시선 · 기본 렌즈를 맞춥니다.
         * @details 엔진의 기본 게임 카메라(`Scene::ensureDefaultCameras`)와 에디터 카메라가 이 스무 줄을 각자 들고, 기본 렌즈
         *          값(`kDefaultFovY` · `kDefaultNearZ` · `kDefaultFarZ`)을 리터럴로 다시 적고 있었습니다.
         * @return 매니저가 없거나 만들 수 없으면(틱 중에는 `addComponent` 가 지연됩니다) nullptr 입니다.
         */
        static CameraComponent* findOrCreateNamed( GameObjectManager* pObjectManager, hashed_string objectName, CameraRole role,
                                                   const float3& position, const float3& lookTarget );
        /** @brief 기본 소멸자입니다. */
        virtual ~CameraComponent() override = default;

        void onBeginPlay() override;

        /** @brief 카메라 역할을 설정합니다. */
        void setRole( CameraRole role ) { _role = role; }
        /** @brief 카메라 역할을 반환합니다. */
        CameraRole getRole() const { return _role; }

        /** @brief 수직 시야각(라디안)을 설정합니다. */
        void setFieldOfViewY( float32 fovRadians ) { _fovY = fovRadians; }
        /** @brief 수직 시야각(라디안)을 반환합니다. */
        float32 getFieldOfViewY() const { return _fovY; }

        /** @brief 근평면을 설정합니다. */
        void setNearPlane( float32 nearZ ) { _nearZ = nearZ; }
        /** @brief 근평면을 반환합니다. */
        float32 getNearPlane() const { return _nearZ; }

        /** @brief 원평면을 설정합니다. */
        void setFarPlane( float32 farZ ) { _farZ = farZ; }
        /** @brief 원평면을 반환합니다. */
        float32 getFarPlane() const { return _farZ; }

        /** @brief 직교 투영 높이를 설정합니다. */
        void setOrthoHeight( float32 height ) { _orthoHeight = height; }
        /** @brief 직교 투영 높이를 반환합니다. */
        float32 getOrthoHeight() const { return _orthoHeight; }

        /** @brief 직교 투영 여부를 설정합니다. */
        void setOrthographic( bool bOrtho ) { _bOrthographic = bOrtho; }
        /** @brief 직교 투영인지 반환합니다. */
        bool isOrthographic() const { return _bOrthographic; }

        /** @brief 우선순위를 설정합니다. */
        void setPriority( int32 priority ) { _priority = priority; }
        /** @brief 우선순위를 반환합니다. */
        int32 getPriority() const { return _priority; }

        /** @brief 월드 공간 타깃을 바라봅니다(로컬 회전을 갱신합니다). */
        void lookAt( const float3& target, const float3& up = float3( 0.0f, 1.0f, 0.0f ) );

        /** @brief 뷰 행렬을 반환합니다. */
        float4x4 getViewMatrix() const;
        /** @brief 투영 행렬을 반환합니다. */
        float4x4 getProjectionMatrix( float32 aspectRatio ) const;
        /** @brief 뷰-투영 행렬을 반환합니다. */
        float4x4 getViewProjectionMatrix( float32 aspectRatio ) const;

        /** @brief 카메라 월드 위치를 반환합니다. */
        float3 getCameraPosition() const { return getWorldPosition(); }

    private:
        PROPERTY( Category = "Projection", DisplayName = "Field Of View", Tooltip = "Vertical FOV (radians)", Min = 0.1, Max = 3.14, Meta = "Units=rad" )
        float32 _fovY;
        PROPERTY( Category = "Clipping", DisplayName = "Near Plane", Tooltip = "Near clipping distance", Min = 0.01, Max = 1000.0, Meta = "Units=m" )
        float32 _nearZ;
        PROPERTY( Category = "Clipping", DisplayName = "Far Plane", Tooltip = "Far clipping distance", Min = 1.0, Max = 100000.0, Meta = "Units=m" )
        float32 _farZ;
        PROPERTY( Category = "Projection", DisplayName = "Ortho Height", Tooltip = "Orthographic view height", Min = 0.1, Max = 1000.0 )
        float32 _orthoHeight;
        PROPERTY( Category = "General", DisplayName = "Priority", Tooltip = "Camera selection priority" )
        int32 _priority;
        PROPERTY( Category = "General", DisplayName = "Role", Tooltip = "Camera usage role" )
        CameraRole _role;
        PROPERTY( Category = "Projection", DisplayName = "Orthographic", Tooltip = "Toggle orthographic projection" )
        bool _bOrthographic;
    };
} // namespace sw
