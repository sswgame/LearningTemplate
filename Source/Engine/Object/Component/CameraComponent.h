/**
 * @file CameraComponent.h
 * @brief 렌더용 뷰/투영 행렬을 만드는 SceneComponent 입니다.
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"

#include "Engine/Object/Component/CameraPostProcessValues.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @brief 카메라 역할(용도)입니다. `Game` 은 플레이어 시점 후보이고, `Custom` 은 직접 고르거나 뷰 타깃으로 바꿀 때만 쓰는 보조 카메라입니다.
     *        `Capture` 는 자기 출력(렌더 텍스처 또는 화면 사각형)으로만 그리며 플레이어 시점으로 뽑히지 않습니다. `Viewport` 는 엔진 밖의 도구가 소유한 뷰포트 카메라입니다(유니티의 `CameraType.SceneView` 에 해당).
     *        `Viewport` 카메라는 게임 시점 후보가 아니고, 씬 파일에 저장되지 않으며, 추가 뷰 수집에서 빠집니다. 그 역할을 주는 것은 도구 쪽입니다.
     */
    ENUM()
    enum class CameraRole : uint8
    {
        Game     = 0, ///< 플레이 중에 활성(플레이어 시점 후보)
        Viewport = 1, ///< 도구 뷰포트 카메라다. 씬에 저장하지 않고 추가 뷰로 수집하지 않는다
        Custom   = 2, ///< 보조 카메라다. 직접 고르거나 뷰 타깃으로 바꿀 때만 쓴다
        Capture  = 3, ///< 캡처 카메라다. 자기 출력(`CameraRenderOutput`)으로만 그린다(CCTV, 백미러, 미니맵)
    };

    /** @brief 카메라가 그린 이미지가 가는 곳입니다. */
    ENUM()
    enum class CameraOutputTarget : uint8
    {
        MainView = 0,  ///< 주 시점이다. 활성 게임 카메라면 화면 전체(사각형을 주면 그 안)에 그리고, 아니면 그리지 않는다
        ScreenRect,    ///< 화면의 사각형이다(분할 화면, PiP). 주 시점 위에 겹쳐 그린다
        RenderTexture, ///< 렌더 텍스처(`rendertarget/<이름>`)다. 머티리얼이 텍스처로 읽는다(CCTV 모니터, 백미러, 미니맵)
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 카메라의 출력 설정입니다. 어디로(화면 사각형, 렌더 텍스처), 얼마나 자주(갱신 주기), 얼마나 크게(해상도 배율), 무엇을 끄고(그림자, 후처리),
     *        언제만(모니터가 화면에 충분히 크게 보일 때) 그릴지 정합니다. 언리얼의 SceneCapture2D, 유니티의 `Camera.targetTexture` 와 `Camera.rect` 에 해당합니다.
     * @details 렌더 텍스처 경로는 `rendertarget/` 로 시작한다(`TextureCache` 가 그 경로를 렌더 타깃으로 만든다). 머티리얼의 텍스처 프로퍼티나
     *          `MaterialInstance::setTextureParameter` 에 같은 경로를 적으면 그 이미지를 읽는다.
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
        /**
         * @brief 정수 업스케일 배율입니다(0 = 끔). N 이면 화면 사각형 픽셀 / N 으로 그리고 점 샘플로 N 배 키워 가운데에 놓습니다(남는 띠는 검다).
         * @details 픽셀 아트의 Upscale Render Texture(유니티 Pixel Perfect Camera)입니다. 켜면 해상도 배율은 보지 않습니다. `PixelPerfectCameraComponent` 가
         *          고른 배율을 여기에 적습니다. MainView · ScreenRect 만 받습니다(렌더 텍스처는 자기 크기에 그린다).
         */
        PROPERTY( Min = 0, Max = 16, Tooltip = "Integer upscale: render at output / N and enlarge N times with point sampling (0 = off)" )
        uint32 _upscaleZoom{ 0 };
        PROPERTY( Min = 0.0, Tooltip = "Renders per second; 0 renders every frame", Units = Hz )
        float32 _updateRate{ 0.0f };
        PROPERTY( Tooltip = "Object whose bounds must be seen on screen (main view or a screen-rect view) for this view to render (a CCTV monitor); empty always renders, a destroyed object never" )
        GameObjectHandle _visibilityObject{};
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Smallest on-screen size of the visibility object (bounds diameter / screen height) for this view to render; 0 renders whenever it is in view" )
        float32 _minScreenSize{ 0.0f };
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
     * @brief GameObject 에 붙는 카메라입니다. 트랜스폼은 SceneComponent 에서 가져옵니다.
     */
    REFLECT( Category = "Camera", DisplayName = "Camera Component", Tooltip = "Perspective / Orthographic Viewport Camera", Meta = "Icon=Camera, IconColor=Camera, Billboard" )
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
         * @brief 그 이름의 오브젝트에 카메라가 있게 하고(없으면 오브젝트와 컴포넌트를 만듭니다) 역할, 위치, 시선, 기본 렌즈를 맞춥니다.
         * @details 엔진의 기본 게임 카메라(`Scene::ensureDefaultCameras`)와 도구의 뷰포트 카메라가 함께 쓴다(기본 렌즈 값
         *          `kDefaultFovY`, `kDefaultNearZ`, `kDefaultFarZ` 를 리터럴로 다시 적지 않게 하려는 것이다).
         * @return 매니저가 없거나 만들 수 없으면(틱 중에는 `addComponent` 가 지연된다) nullptr 입니다.
         */
        static CameraComponent* findOrCreateNamed( GameObjectManager* pObjectManager, hashed_string objectName, CameraRole role,
                                                   const float3& position, const float3& lookTarget );
        /** @brief 기본 소멸자입니다. */
        virtual ~CameraComponent() override = default;

        /** @brief 씬에 붙을 때 카메라 레지스트리에 자기를 등록합니다. */
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

        /** @brief 카메라가 그리는 눈의 월드 위치입니다. 월드 위치에 그리기 전용 오프셋(`setViewOffset`)을 더한 것이며, 뷰 행렬이 이 점을 씁니다. */
        float3 getCameraPosition() const { return getWorldPosition() + _viewOffset; }
        /**
         * @brief 그리기에만 쓰는 눈 위치 오프셋(월드)입니다. 트랜스폼은 그대로 둡니다. 저장하지 않습니다.
         * @details 픽셀 퍼펙트 카메라가 눈을 화면 픽셀 격자에 붙일 때 쓴다(`PixelPerfectCameraComponent`). 트랜스폼을 반올림하면 감쇠로 따라가는
         *          카메라가 반 픽셀 아래의 움직임을 영영 잃어 멈춘다. 그래서 그리는 눈만 옮긴다(유니티도 렌더 직전에만 붙인다).
         */
        void setViewOffset( const float3& offset ) { _viewOffset = offset; }
        /** @brief 그리기 전용 눈 위치 오프셋입니다. */
        const float3& getViewOffset() const { return _viewOffset; }
        /** @brief 카메라가 보는 방향(월드, 정규화)입니다. 로컬 +Z 를 월드 회전으로 돌린 것이며 뷰 행렬과 같은 축입니다. */
        float3 getCameraForward() const;

        /** @brief 출력 설정(렌더 텍스처, 화면 사각형, 갱신 주기, 끌 기능)입니다. */
        const CameraRenderOutput& getRenderOutput() const { return _renderOutput; }
        /** @brief 출력 설정을 바꿉니다. 렌더 텍스처면 그 크기를 텍스처 캐시에 알립니다. */
        void setRenderOutput( const CameraRenderOutput& output );

        /** @brief 이 카메라의 후처리 값(노출 · 블룸 배율 · 비네트 · 채도 · 대비 · 레터박스)입니다. 렌더 뷰가 이 값을 렌더러로 나릅니다. */
        const CameraPostProcessValues& getPostProcess() const { return _postProcess; }
        /** @brief 후처리 값을 바꿉니다. 카메라 프리셋 · 블렌드를 쓰는 게임은 포즈가 매 프레임 씁니다(`CameraPoseUtil::applyToCamera`). */
        void setPostProcess( const CameraPostProcessValues& values ) { _postProcess = values; }

        /**
         * @brief 이 카메라의 화면이 이번 프레임에 **끊어** 바뀌었다고 표시합니다(언리얼의 `bCameraCut`). 렌더러가 시간 누적(TAA 기록)을 버립니다.
         * @details 블렌드 없는 프리셋과 뷰 타깃 전환이 부른다. 게임 스레드에서 쓰고 패킷을 만드는 쪽이 `consumeCut` 으로 한 번 읽어 지운다.
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
        /** @brief 렌더 텍스처 출력이면 그 크기를 텍스처 캐시에 알립니다(`engine::declareRenderTarget` — 텍스처 캐시가 묶는다). */
        void declareRenderTexture() const;

    private:
        PROPERTY( Category = "Projection", DisplayName = "Field Of View", Tooltip = "Vertical FOV (radians)", Min = 0.1, Max = 3.14, Units = rad )
        float32 _fovY;
        PROPERTY( Category = "Clipping", DisplayName = "Near Plane", Tooltip = "Near clipping distance", Min = 0.01, Max = 1000.0, Units = m )
        float32 _nearZ;
        PROPERTY( Category = "Clipping", DisplayName = "Far Plane", Tooltip = "Far clipping distance", Min = 1.0, Max = 100000.0, Units = m )
        float32 _farZ;
        PROPERTY( Category = "Projection", DisplayName = "Ortho Height", Tooltip = "Orthographic view height", Min = 0.1, Max = 1000.0 )
        float32 _orthoHeight;
        float3  _viewOffset; ///< 그리기 전용 눈 위치 오프셋(월드). 저장하지 않습니다
        PROPERTY( Category = "General", DisplayName = "Priority", Tooltip = "Camera selection priority" )
        int32 _priority;
        PROPERTY( Category = "General", DisplayName = "Role", Tooltip = "Camera usage role" )
        CameraRole _role;
        PROPERTY( Category = "Output", DisplayName = "Render Output", Tooltip = "Render texture / screen rectangle, update rate and feature switches" )
        CameraRenderOutput _renderOutput;
        PROPERTY( Category = "Post Process", DisplayName = "Post Process", Tooltip = "Per-camera exposure, bloom, vignette, saturation, contrast and letterbox" )
        CameraPostProcessValues _postProcess;
        PROPERTY( Category = "Projection", DisplayName = "Orthographic", Tooltip = "Toggle orthographic projection" )
        bool  _bOrthographic;
        uint8 _bCutPending; ///< `markCut` 이 켜고 `consumeCut` 이 지운다(저장하지 않는다)
    };
} // namespace sw
