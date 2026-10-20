/**
 * @file RenderView.h
 * @brief "어느 눈으로 보는가" 를 한 곳에 모은 타입을 정의합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Math/Frustum.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHIConstantBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Object/Component/CameraPostProcessValues.h"
#include "Engine/Renderer/Light/ShadowSetup.h"

namespace sw
{
    /**
     * @enum RenderViewType
     * @brief 프레임이 그리는 시점입니다. 컬링과 정렬 산출물이 이 단위로 나뉩니다.
     * @details 언리얼이 뷰마다 `FViewInfo` 와 `FInstanceCullingContext` 를 두는 것과 같다.
     */
    enum class RenderViewType : uint32
    {
        Main   = 0, ///< 게임 카메라
        Shadow = 1, ///< 그림자 라이트
        Count  = 2  ///< 고정 뷰의 수. 추가 뷰(CCTV, PiP)의 컬링 슬롯은 이 뒤에 붙는다(`kFirstExtraCullView`).
    };

    /// @brief 추가 뷰의 첫 컬링 슬롯입니다. 슬롯 0은 주 시점이고 1은 주 시점의 그림자입니다.
    inline constexpr uint32 kFirstExtraCullView = static_cast<uint32>( RenderViewType::Count );
    /// @brief 추가 뷰 하나가 쓰는 컬링 슬롯 수입니다 — 자기 시점 하나, 자기 그림자 패스 하나(뷰마다 그림자 볼륨이 다르다).
    inline constexpr uint32 kCullSlotsPerExtraView = 2;
    /// @brief 한 프레임에 가질 수 있는 추가 뷰의 최대 수입니다(컬링 슬롯, 상수 버퍼, 트랜지언트 풀이 뷰마다 하나씩 있습니다).
    inline constexpr uint32 kMaxExtraRenderView = 8;

    /** @brief 추가 뷰의 렌더링 결과가 가는 곳입니다. */
    enum class RenderViewOutputKind : uint8
    {
        ScreenRect    = 0, ///< 주 출력(백버퍼, 게임 뷰)의 사각형이며 주 시점 위에 겹친다(분할 화면, PiP).
        RenderTexture = 1, ///< 렌더 텍스처(`rendertarget/<이름>`)이며 머티리얼이 읽는다(CCTV 모니터, 백미러).
        HostTarget    = 2, ///< 호스트가 만든 렌더 타깃(에디터 씬 뷰)이다. 핸들(`RenderViewRequest::_hostTarget`)로 받고, 그린 뒤 셰이더 읽기 상태로 둔다.
    };

    /**
     * @enum RenderViewMode
     * @brief 씬 지오메트리를 어떻게 보여 줄지 정합니다. 에디터 씬 뷰 툴바의 보기 모드입니다.
     * @details 렌더 상태(채우기, 블렌드, 깊이)와 셰이더 퍼뮤테이션(조명 항)을 함께 가르는 값이라 어느 한쪽에만 둘 수 없다.
     *          뷰마다 갖는 설정(`RenderViewSettings::_viewMode`)이고, 배치 PSO 를 고를 때 머티리얼 퍼뮤테이션과 **같은 단계**에서 적용된다.
     *          그래서 와이어프레임이 머티리얼 변형을 잃지 않는다. 언리얼 뷰포트(뷰 패밀리)마다의 View Mode, 유니티 Scene 뷰의 Draw Mode 와 같다.
     * @note 에디터 전용이 아니다. 헤드리스에서도 `-gv_viewMode=<n>` 로 주 출력의 모드를 골라 스크린샷으로 검증한다.
     */
    enum class RenderViewMode : uint8
    {
        Lit = 0,      ///< 조명과 그림자를 모두 계산한 기본 화면
        Unlit,        ///< 알베도만 보인다. 조명 항이 셰이더에서 컴파일 아웃된다.
        Wireframe,    ///< 삼각형 외곽선만 보인다(RHIFillMode::Wireframe).
        Normals,      ///< 월드 노멀을 색으로 보인다(0.5 + 0.5 n).
        Depth,        ///< 카메라에서의 거리(뷰 공간 깊이)를 보이며 `kViewModeDepthRange` 에서 흰색이 된다.
        Overdraw,     ///< 겹쳐 그린 횟수를 보인다. 단색을 깊이 쓰기 없이 더한다(유니티 Overdraw).
        LightingOnly, ///< 알베도를 회색 하나로 바꾸고 조명만 보인다(언리얼 Lighting Only). 빛 · 그림자 배치를 텍스처 없이 본다.
        Roughness,    ///< 표면의 거칠기를 회색으로 보인다(언리얼 Buffer Visualization Roughness). PBR 칸이 없는 셰이더는 자기 정의값(셰이더의 kSurfaceRoughness)을 보인다.
        Metallic,     ///< 표면의 금속도를 회색으로 보인다(언리얼 Buffer Visualization Metallic). 금속도 칸이 아직 없어 모든 표면이 0 이다.
        Mipmaps,      ///< 머티리얼 텍스처를 읽는 밉 단계를 색으로 보인다(유니티 Scene 뷰 Mipmaps). 확대는 파랑, 밉 0 초록, 1 노랑, 2 주황, 3 이상 빨강.

        Count
    };

    /** @brief 보기 모드 하나의 테이블 항목입니다. 셰이더 define, PSO 조정, 후처리, 이름이 한 항목에 들어 있습니다. */
    struct RenderViewModeInfo
    {
        const utf8* _pName;
        const utf8* _pShaderDefine;    ///< nullptr 이면 셰이더를 바꾸지 않는다(Lit, Wireframe). `.hlsl` 의 `#if defined(...)` 와 이름이 같아야 한다.
        bool        _bWireframe;       ///< 채우기 모드를 와이어프레임으로 하고 컬링을 끈다.
        bool        _bAdditiveNoDepth; ///< Overdraw 용이다. 더하기 블렌드(알파 0 의 프리멀티플라이), 깊이 쓰기 끔(클리어 값 1이 남아 모든 면이 통과), 컬링 끔
        bool        _bShowsBuffer;     ///< 표면 값을 그대로 보인다(Normals, Depth, Overdraw). 후처리, 톤맵, 외곽선 껍질을 건너뛴다.
    };

    /** @brief 보기 모드 테이블입니다. 쿠커, PSO 변형, 툴바, 로그가 모두 이것을 읽습니다. 항목 순서가 열거값입니다. */
    inline constexpr RenderViewModeInfo kArrRenderViewModeInfo[] = {
        {         "Lit",                      nullptr, false, false, false},
        {       "Unlit",        "SW_VIEWMODE_UNLIT=1", false, false, false},
        {   "Wireframe",                      nullptr,  true, false, false},
        {     "Normals",      "SW_VIEWMODE_NORMALS=1", false, false,  true},
        {       "Depth",        "SW_VIEWMODE_DEPTH=1", false, false,  true},
        {    "Overdraw",     "SW_VIEWMODE_OVERDRAW=1", false,  true,  true},
        {"LightingOnly", "SW_VIEWMODE_LIGHTINGONLY=1", false, false, false},
        {   "Roughness",    "SW_VIEWMODE_ROUGHNESS=1", false, false,  true},
        {    "Metallic",     "SW_VIEWMODE_METALLIC=1", false, false,  true},
        {     "Mipmaps",      "SW_VIEWMODE_MIPMAPS=1", false, false,  true},
    };
    static_assert( sizeof( kArrRenderViewModeInfo ) / sizeof( kArrRenderViewModeInfo[0] ) == static_cast<size_t>( RenderViewMode::Count ),
                   "보기 모드 표와 열거형의 개수가 다릅니다" );

    /** @brief 보기 모드의 테이블 항목을 반환합니다. 범위 밖이면 Lit 항목을 반환합니다. */
    inline const RenderViewModeInfo& getRenderViewModeInfo( RenderViewMode viewMode )
    {
        const uint8 index = static_cast<uint8>( viewMode );
        return kArrRenderViewModeInfo[index < static_cast<uint8>( RenderViewMode::Count ) ? index : 0];
    }
} // namespace sw

namespace sw
{
    /**
     * @struct RenderViewSettings
     * @brief 뷰 하나의 출력 사각형, 해상도 배율, 끌 기능, 컷 표시, 보기 모드를 담습니다. 주 시점과 추가 뷰가 같은 묶음을 씁니다(값 타입이며 게임 스레드에서 렌더 스레드로 넘어갑니다).
     */
    struct RenderViewSettings
    {
        float4  _screenRect{ 0.0f, 0.0f, 1.0f, 1.0f }; ///< 출력 안의 사각형(x, y, 너비, 높이. 0..1, 왼쪽 위 원점). 렌더 텍스처는 항상 전체다.
        float32 _resolutionScale{ 1.0f };              ///< 출력 크기에 곱하는 내부 해상도
        /// @brief 이 뷰를 그리는 카메라의 후처리 값(노출 · 블룸 배율 · 비네트 · 채도 · 대비 · 레터박스)입니다. 후처리를 끈 뷰와 표면 값을 보이는 보기 모드는 읽지 않습니다.
        CameraPostProcessValues _postProcess{};
        uint8                   _bShadows{ SW_TRUE };             ///< 그림자를 그린다(끄면 그림자 맵을 지우기만 하므로 모두 밝다).
        uint8                   _bPostProcess{ SW_TRUE };         ///< 후처리(블룸, 외곽선, 톤맵, TAA)를 적용한다.
        uint8                   _bCut{ SW_FALSE };                ///< 이번 프레임에 화면이 끊겼다(언리얼 `bCameraCut`). TAA 히스토리를 버린다.
        RenderViewMode          _viewMode{ RenderViewMode::Lit }; ///< 이 뷰의 보기 모드(에디터 씬 뷰만 바꾸고, 게임 뷰는 Lit 이다).
        /**
         * @brief 정수 업스케일 배율입니다(0 = 끔). N 이면 내부 해상도가 사각형 픽셀 / N(내림)이고 Present 가 점 샘플로 N 배 키워 사각형 가운데에 놓는다.
         * @details 픽셀 아트의 Upscale Render Texture(유니티 Pixel Perfect Camera) — 낮은 해상도에서 그려 자산 픽셀이 화면 픽셀 N × N 으로 고르게 커진다.
         *          해상도 배율(`_resolutionScale`)은 보지 않는다. 주 시점은 남는 띠를 검게 지우고, 화면 사각형 뷰는 아래 그림을 남긴다.
         */
        uint8 _upscaleZoom{ 0 };

        /** @brief 사각형이 출력 전체인지 나타냅니다. */
        bool isFullRect() const { return _screenRect._x <= 0.0f && _screenRect._y <= 0.0f && _screenRect._z >= 1.0f && _screenRect._w >= 1.0f; }
        /** @brief 효과 후처리(블룸, 외곽선, SSAO, TAA)를 적용하는지 나타냅니다. 끈 뷰와 표면 값을 보이는 보기 모드는 원본을 넘깁니다. */
        bool usesPostProcess() const { return _bPostProcess == SW_TRUE && getRenderViewModeInfo( _viewMode )._bShowsBuffer == false; }
        /** @brief 톤맵을 적용하는지 나타냅니다. 표면 값을 보이는 보기 모드(Normals, Depth, Overdraw)는 색을 바꾸지 않습니다. */
        bool usesTonemap() const { return getRenderViewModeInfo( _viewMode )._bShowsBuffer == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RenderViewRequest
     * @brief 이번 프레임의 추가 뷰 하나입니다. 게임 스레드가 카메라에서 만들어 패킷에 싣습니다. 값만 가지며 씬을 가리키지 않습니다.
     * @details 살아 있는 추가 뷰는 갱신 주기 때문에 쉬는 프레임에도 실린다(`_bRender` = 0). 렌더러가 그 뷰의 풀, 텍스처, 컬링 슬롯을 계속 가지고 있게 하려는 것이다.
     */
    struct RenderViewRequest
    {
        float4x4             _viewProj{};
        float3               _position{};
        float3               _transparentSortAxis{}; ///< 이 뷰의 투명 정렬 축(직교 카메라의 시선). 0이면 눈까지의 거리로 정렬한다(주 뷰와 같은 규칙).
        RenderViewSettings   _settings{};
        hashed_string        _renderTexture{};  ///< `RenderTexture` 의 경로(`rendertarget/<이름>`)
        RHITextureHandle     _hostTarget{ 0 };  ///< `HostTarget` 의 렌더 타깃 핸들
        uint64               _viewID{ 0 };      ///< 카메라 컴포넌트 id. 렌더러가 이것으로 뷰마다의 상태를 찾는다.
        uint32               _outputWidth{ 0 }; ///< 출력 크기(렌더 텍스처 크기이며, 화면 사각형이면 그 픽셀 크기)
        uint32               _outputHeight{ 0 };
        RenderViewOutputKind _outputKind{ RenderViewOutputKind::RenderTexture };
        uint8                _bRender{ SW_TRUE }; ///< 이번 프레임에 그린다(갱신 주기, 가시 여부, 예산이 정한다).
        /// @brief 이 뷰의 카메라에 맞춘 방향광 그림자입니다(그리는 뷰만 채운다). 비어 있으면 주 시점의 것을 쓴다.
        ViewShadowData _shadow{};
    };
} // namespace sw

namespace sw
{
    /** @brief 호스트(에디터)가 만든 오프스크린 출력 하나입니다. 렌더 타깃이 0이면 이번 프레임에 그 뷰를 그리지 않습니다. */
    struct HostViewTarget
    {
        RHITextureHandle _renderTarget{ 0 };
        uint32           _width{ 0 };
        uint32           _height{ 0 };

        /** @brief 그릴 수 있는 출력인지 나타냅니다. */
        bool isValid() const { return _renderTarget != 0 && _width > 0 && _height > 0; }
    };
} // namespace sw

namespace sw
{
    /// @brief 호스트가 한 프레임에 요청할 수 있는 미리보기 씬 뷰의 최대 수입니다(`HostViewTargets::_arrSceneView`).
    inline constexpr uint32 kMaxHostSceneView = 8;

    /**
     * @struct HostSceneView
     * @brief 호스트(에디터 툴 창)가 이번 프레임에 그려 달라고 요청한 **미리보기 씬** 뷰 하나입니다. 값 타입이며 C ABI 를 넘어갑니다(`EditorAPI::getSceneViews`).
     * @details 어느 씬(`_sceneKey` — `SceneManager::getPreviewSceneKey`), 어느 눈(`_viewProj` · `_cameraPos`), 어느 렌더 타깃(`_target`)인지를 고른다.
     *          씬마다 GT 의 스냅샷 빌더 하나 · RT 의 렌더러 하나가 따로 돌므로 메인 씬과 섞이지 않는다(`RenderPacketBuilder::populateScenePreviews`).
     *          안 보이는 창은 요청하지 않는다 — 요청이 없는 씬은 그리지 않는다.
     */
    struct HostSceneView
    {
        float4x4           _viewProj{};
        float3             _cameraPos{};
        RenderViewSettings _settings{};
        HostViewTarget     _target{};
        uint32             _sceneKey{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct HostViewTargets
     * @brief 호스트가 이번 프레임에 그려 달라고 요청한 뷰들입니다(`Engine::update`) — 활성 씬의 두 슬롯과 미리보기 씬 목록입니다.
     * @details 게임 뷰는 게임 카메라, 화면 UI, 화면 사각형 뷰를 담는 주 출력이고, 씬 뷰는 호스트 카메라의 추가 뷰(`RenderViewOutputKind::HostTarget`)다.
     *          게임 뷰가 없고 씬 뷰만 있으면 씬 뷰가 주 출력이 된다(화면 UI와 화면 사각형 뷰 없이). 둘 다 없으면 백버퍼에 게임 카메라로 그린다(에디터 없는 실행).
     *          미리보기 씬 뷰(`_arrSceneView`)는 활성 씬이 아닌 씬을 자기 타깃에 그린다. 고정 배열인 까닭은 호스트가 프레임마다 이 값을 새로 채우기 때문이다(힙을 건드리지 않는다).
     */
    struct HostViewTargets
    {
        HostViewTarget _game;
        HostViewTarget _scene;
        HostSceneView  _arrSceneView[kMaxHostSceneView]{};
        uint32         _sceneViewCount{ 0 };

        /** @brief 씬 뷰가 주 출력인지 나타냅니다(게임 뷰가 없는 경우입니다). */
        bool isSceneViewMain() const { return _game.isValid() == false && _scene.isValid(); }
        /** @brief 이번 프레임의 주 출력입니다. 둘 다 없으면 빈 값이며 백버퍼를 뜻합니다. */
        const HostViewTarget& getMainOutput() const { return isSceneViewMain() ? _scene : _game; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RenderView
     * @brief 뷰 하나가 가지는 상태입니다. 행렬, 절두체, 그리고 **자기 상수 버퍼**를 담습니다.
     * @details 뷰마다 달라지는 것을 모두 여기 모은다. 뷰를 얻으면 그 뷰의 버퍼가 따라오고, 다른 뷰의 것을 집으려면
     *          일부러 다른 인덱스를 써야 해서 눈에 띈다. 주의: "이 값은 누구 것인가" 가 타입에 없으면 컬링 상수 버퍼를
     *          뷰들이 나눠 써서 **메인 패스가 그림자 라이트의 절두체로 걸러진다**(화면 절반이 사라진다).
     *
     * @note 컬링 **산출물**(간접 인자와 가시 인스턴스 목록)은 인스턴스 수에 맞춰 커지므로 GPUScene 이
     *       소유한다(`GPUScene::getCullView`). 여기 있는 것은 그 산출물을 **만들 때 넣는 입력**이다.
     */
    struct SW_API RenderView
    {
        /// @brief 이 뷰의 뷰 × 프로젝션 행렬입니다. 절두체는 여기서만 뽑습니다(둘이 어긋날 여지를 없앱니다).
        float4x4 _viewProj{};
        /// @brief 이 뷰의 눈 위치입니다. 투명 정렬 키(카메라까지의 거리)가 이 값을 씁니다.
        float3 _position{};
        /// @brief `_viewProj` 에서 뽑은 절두체 평면 여섯 개입니다(왼쪽, 오른쪽, 아래, 위, 근평면, 원평면이며 정규화되어 있습니다). 식은 `Frustum` 하나로, CPU 공간 질의(`BVHTree3D`)와 같습니다.
        Frustum _frustum{};
        /**
         * @brief 이 뷰 전용 컬링 상수 버퍼입니다.
         * @details **뷰마다 하나여야 한다.** 하나를 나눠 쓰면 두 번째 업로드가 첫 번째 디스패치가 읽을
         *          내용을 덮어쓴다. CPU 는 디스패치 사이에 쓰지만 GPU 는 제출 뒤에 읽기 때문이다.
         */
        RHIConstantBufferSlot _cullCb;
        /**
         * @brief 이 뷰 전용 인스턴스 정렬 상수 버퍼입니다(투명 깊이 정렬이며, 정렬 키가 이 뷰의 눈까지의 거리입니다).
         * @details 컬링 상수 버퍼와 같은 이유로 뷰마다 하나다. 하나를 나눠 쓰면 모든 뷰가 마지막 뷰의 눈 위치로 정렬한다.
         */
        RHIConstantBufferSlot _sortCb;

        /** @brief 컬링을 돌릴 준비가 되었는지(상수 버퍼가 있는지) 반환합니다. */
        bool isReadyForCulling() const { return _cullCb.isValid(); }

        /**
         * @brief 뷰 행렬을 정하고 절두체를 **함께** 갱신합니다.
         * @details 행렬만 바꾸고 평면을 바꾸지 않으면 컬링이 지난 프레임의 시점으로 판정한다. 둘을 한
         *          함수로 묶어 그런 상태가 존재할 수 없게 한다.
         */
        void setViewProjection( const float4x4& viewProj );
    };
} // namespace sw
