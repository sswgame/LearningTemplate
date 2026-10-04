/**
 * @file CameraComponent.h
 * @brief 렌더용 뷰/투영 행렬을 만드는 SceneComponent 입니다.
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @brief 카메라 역할(용도)입니다. 플레이어 시점 후보(`Game`) · 보조(`Custom` — 직접 고르거나 뷰 타깃으로 바꿀 때만) · 캡처(`Capture` — 자기 출력
     *        (렌더 텍스처 · 화면 사각형)으로만 그리고 플레이어 시점으로 뽑히지 않는다) · 에디터 뷰포트입니다.
     */
    ENUM()
    enum class CameraRole : uint8
    {
        Game    = 0, ///< 플레이 중에 활성(플레이어 시점 후보)
        Editor  = 1, ///< 에디터 뷰포트에서 활성
        Custom  = 2, ///< 보조 — 직접 고르거나 뷰 타깃으로 바꿀 때만 쓰임
        Capture = 3, ///< 캡처 — 자기 출력(`CameraRenderOutput`)으로만 그린다(CCTV · 백미러 · 미니맵)
    };

    /** @brief 카메라가 그린 그림이 가는 곳입니다. */
    ENUM()
    enum class CameraOutputTarget : uint8
    {
        MainView = 0,  ///< 주 시점 — 활성 게임 카메라면 화면 전체(사각형을 주면 그 안)에 그린다. 아니면 그리지 않는다
        ScreenRect,    ///< 화면의 사각형(분할 화면 · PiP) — 주 시점 위에 겹쳐 그린다
        RenderTexture, ///< 렌더 텍스처(`rendertarget/<이름>`) — 머티리얼이 텍스처로 읽는다(CCTV 모니터 · 백미러 · 미니맵)
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 카메라의 출력 설정입니다 — 어디로(화면 사각형 · 렌더 텍스처), 얼마나 자주(갱신 주기), 얼마나 크게(해상도 배율), 무엇을 끄고(그림자 · 후처리),
     *        언제만(보일 때) 그릴지입니다. 언리얼 SceneCapture2D · 유니티 `Camera.targetTexture` · `Camera.rect` 의 자리입니다.
     * @details 렌더 텍스처 경로는 `rendertarget/` 로 시작합니다(`TextureCache` 가 그 경로를 렌더 타깃으로 만든다). 머티리얼의 텍스처 프로퍼티 ·
     *          `MaterialInstance::setTextureParameter` 에 같은 경로를 적으면 그 그림을 읽는다.
     */
    REFLECT()
    struct SW_API CameraRenderOutput
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Screen rectangle (x, y, width, height as 0..1 of the output, top-left origin) for MainView / ScreenRect" )
        float4 _screenRect{ 0.0f, 0.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "Render texture path (rendertarget/<name>) for RenderTexture" )
        string _renderTexture{};
        PROPERTY( Min = 1, Max = 8192, Tooltip = "Render texture width", Meta = "Units=px" )
        uint32 _renderTextureWidth{ 256 };
        PROPERTY( Min = 1, Max = 8192, Tooltip = "Render texture height", Meta = "Units=px" )
        uint32 _renderTextureHeight{ 256 };
        PROPERTY( Min = 0.1, Max = 2.0, Tooltip = "Internal resolution as a multiple of the output size" )
        float32 _resolutionScale{ 1.0f };
        PROPERTY( Min = 0.0, Tooltip = "Renders per second; 0 renders every frame", Meta = "Units=Hz" )
        float32 _updateRate{ 0.0f };
        PROPERTY( Tooltip = "Object whose bounds must be in the main view for this view to render (a CCTV monitor); empty always renders" )
        GameObjectHandle _visibilityObject{};
        PROPERTY( Tooltip = "Where the picture goes" )
        CameraOutputTarget _target{ CameraOutputTarget::MainView };
        PROPERTY( Tooltip = "Draw shadows in this view" )
        bool _bShadows{ true };
        PROPERTY( Tooltip = "Apply post effects (bloom, outline, tonemap, TAA) in this view" )
        bool _bPostProcess{ true };
    };
} // namespace sw

namespace sw
{
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
         * @details 엔진의 기본 게임 카메라(`Scene::ensureDefaultCameras`)와 에디터 카메라가 함께 씁니다(기본 렌즈 값
         *          `kDefaultFovY` · `kDefaultNearZ` · `kDefaultFarZ` 를 리터럴로 다시 적지 않게).
         * @return 매니저가 없거나 만들 수 없으면(틱 중에는 `addComponent` 가 지연됩니다) nullptr 입니다.
         */
        static CameraComponent* findOrCreateNamed( GameObjectManager* pObjectManager, hashed_string objectName, CameraRole role,
                                                   const float3& position, const float3& lookTarget );
        /** @brief 기본 소멸자입니다. */
        virtual ~CameraComponent() override = default;

        /** @brief 씬에 붙을 때 카메라 등록부에 자기를 등록합니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 씬에서 떨어질 때 등록을 해제합니다. */
        void onUnregister( GameObjectManager& manager ) override;

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

        /** @brief 출력 설정(렌더 텍스처 · 화면 사각형 · 갱신 주기 · 끌 기능)입니다. */
        const CameraRenderOutput& getRenderOutput() const { return _renderOutput; }
        /** @brief 출력 설정을 바꿉니다. 렌더 텍스처면 그 크기를 텍스처 캐시에 알립니다. */
        void setRenderOutput( const CameraRenderOutput& output );

        /**
         * @brief 이 카메라의 화면이 이번 프레임에 **끊어** 바뀌었다고 표시합니다(언리얼 `bCameraCut`). 렌더러가 시간 누적(TAA 기록)을 버린다.
         * @details 블렌드 없는 프리셋 · 뷰 타깃 전환이 부릅니다. 게임 스레드에서 쓰고 패킷을 만드는 쪽이 `consumeCut` 으로 한 번 읽어 지웁니다.
         */
        void markCut() { _bCutPending = SW_TRUE; }
        /** @brief 컷 표시를 읽고 지웁니다. */
        bool consumeCut()
        {
            const bool bCut = _bCutPending == SW_TRUE;
            _bCutPending    = SW_FALSE;
            return bCut;
        }

    private:
        /** @brief 렌더 텍스처 출력이면 그 크기를 텍스처 캐시에 알립니다(`TextureCache::declareRenderTarget`). */
        void declareRenderTexture() const;

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
        PROPERTY( Category = "Output", DisplayName = "Render Output", Tooltip = "Render texture / screen rectangle, update rate and feature switches" )
        CameraRenderOutput _renderOutput;
        PROPERTY( Category = "Projection", DisplayName = "Orthographic", Tooltip = "Toggle orthographic projection" )
        bool  _bOrthographic;
        uint8 _bCutPending; ///< `markCut` 이 세우고 `consumeCut` 이 지운다(저장하지 않는다)
    };
} // namespace sw
