/**
 * @file Engine/Graphics/View/RenderViewSettings.h
 * @brief 뷰 하나의 설정 값(보기 모드 · 출력 사각형 · 후처리 값)입니다 — 렌더 스레드 · RHI 형식이 없는 값이라 렌더러를 짓지 않는 구성(전용 서버)의 호스트도 읽습니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/View/CameraDepthOfFieldValues.h"
#include "Engine/Graphics/View/PostProcessValues.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @enum RenderViewMode
     * @brief 씬 지오메트리를 어떻게 보여 줄지 정합니다. 에디터 씬 뷰 툴바의 보기 모드입니다.
     * @details 렌더 상태(채우기, 블렌드, 깊이)와 셰이더 퍼뮤테이션(조명 항)을 함께 가르는 값이라 어느 한쪽에만 둘 수 없다.
     *          뷰마다 갖는 설정(`RenderViewSettings::_viewMode`)이고, 배치 PSO 를 고를 때 머티리얼 퍼뮤테이션과 **같은 단계**에서 적용된다.
     *          그래서 와이어프레임이 머티리얼 변형을 잃지 않는다. 언리얼 뷰포트(뷰 패밀리)마다의 View Mode, 유니티 Scene 뷰의 Draw Mode 와 같다.
     * @note 에디터 전용이 아니다. 헤드리스에서도 `-gv_viewMode=<이름>`(예: `Wireframe`)으로 주 출력의 모드를 골라 스크린샷으로 검증한다. 리플렉션 enum 이라
     *       이름으로 고르고 숫자는 받지 않는다. `Count` 는 개수 표식이라 모드가 아니다(`isRenderViewModeIndex` 가 거른다).
     */
    ENUM( Count = Count )
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
        Metallic,     ///< 표면의 금속도를 회색으로 보인다(언리얼 Buffer Visualization Metallic). PBR 칸이 없는 셰이더는 자기 정의값(셰이더의 kSurfaceMetallic)을 보인다.
        Mipmaps,      ///< 머티리얼 텍스처를 읽는 밉 단계를 색으로 보인다(유니티 Scene 뷰 Mipmaps). 확대는 파랑, 밉 0 초록, 1 노랑, 2 주황, 3 이상 빨강.
        /**
         * @brief 픽셀마다 조명 식이 도는 라이트 수(라이트 그리드의 전역 구간 + 그 클러스터 목록)를 색 띠로 보인다(언리얼 Light Complexity, 유니티 Forward+ 디버그 보기).
         * @details 0 은 검정, 그 뒤로 파랑 · 초록 · 노랑 · 빨강(binding.hlsli kViewModeLightComplexityMax 이상)이다. 그리드가 없으면(gv_lightCulling=None) 목록 전체의 수다.
         *          G버퍼가 표면에서 셈하므로 디퍼드 조명 패스에 보기 모드 변형이 필요 없다.
         */
        LightComplexity,
        /**
         * @brief Lit 으로 그린 위에 같은 배치의 삼각형 외곽선을 단색으로 겹친다(언리얼 Lighting + Wireframe, 유니티 Shaded Wireframe).
         * @details 지오메트리 패스가 배치를 두 번 그린다 — Lit PSO 로 한 번, 이 모드의 PSO(와이어프레임 채우기 · 깊이 바이어스 · 깊이 쓰기 끔 · 단색 define)로 한 번.
         *          G버퍼에는 선을 셰이딩 모델 Unlit 으로 적어 디퍼드 조명 패스가 그대로 낸다.
         */
        ShadedWireframe,
        /**
         * @brief 그리는 인스턴스의 LOD 단계를 색으로 보인다(언리얼 Visualize LOD Coloration, 유니티 Scene 뷰 LOD 색). 0 초록 · 1 연두 · 2 노랑 · 3 주황 · 4 빨강 · 5 보라 · 6 파랑 · 7 청록이다.
         * @details 컬링이 가시 목록 항목에 쓴 단계 번호(`gpu_cull.hlsl`)를 정점 단계가 픽셀로 넘긴다(binding.hlsli `SW_LODDEBUG_*`). 머티리얼 셰이더 경로의 메시만 읽고 그 밖의 셰이더는 회색이다.
         */
        LODLevel,
        /**
         * @brief LOD 크로스페이드의 디더 코드를 색으로 보인다 — 전환 없음은 어두운 회색, 새 단계는 초록 · 직전 단계는 빨강이고 진행이 클수록 밝다(상보 디더라 두 색이 겹치지도 비지도 않으면 맞다).
         * @details 가시 항목의 위쪽 5비트(`Appearance::computeLODFadeAppearance` 가 읽는 값)를 보인다. 읽는 경로는 `LODLevel` 과 같다.
         */
        LODFadeCode,
        /**
         * @brief 화면 마커(`gv_uiMarkerGpuProjection`)가 GPUScene 인스턴스 칸을 가졌는지를 마커 위젯의 색조로 보인다 — 칸이 있으면 초록, 없으면(월드 점을 게임 스레드가 보낸다) 빨강, 칸이 인스턴스 버퍼 밖이면 자홍이다.
         * @details 장면 지오메트리는 Lit 그대로 그린다(셰이더 define 이 없다). 마커 투영 컴퓨트가 앵커의 네 번째 값에 코드를 쓰고(canvas_marker.hlsl) 캔버스 셰이더가 그 위젯을 칠한다(canvas.hlsl). 주 출력 모드만 본다.
         */
        MarkerSlots,
        /**
         * @brief 표면이 어느 셰이딩 경로로 그려졌는지 색으로 보인다 — 디퍼드 G버퍼 조명(파랑) · G버퍼 조명 없음(청회색) · 포워드 전용(주황, `MATERIAL_FORWARD_ONLY`) ·
         *        반투명(보라) · 포워드 파이프라인(초록) · 포워드 조명 없음(짙은 초록). 언리얼 Buffer Visualization 의 Shading Model 과 같은 자리다.
         * @details 디퍼드에서 기본 PBR 머티리얼이 실수로 포워드 전용으로 분류되면(디퍼드 · 타일 라이트 이득을 잃는다) 주황으로 드러난다. 색은 binding_output.hlsli `ViewMode::computeShadingPathColor`.
         */
        ShadingPath,

        Count
    };

    /** @brief 보기 모드 하나의 테이블 항목입니다. 셰이더 define, PSO 조정, 후처리, 이름이 한 항목에 들어 있습니다. */
    struct RenderViewModeInfo
    {
        const utf8* _pName;
        const utf8* _pShaderDefine;    ///< nullptr 이면 셰이더를 바꾸지 않는다(Lit, Wireframe). `.hlsl` 의 `#if defined(...)` 와 이름이 같아야 한다.
        bool        _bWireframe;       ///< 채우기 모드를 와이어프레임으로 하고 컬링을 끈다. NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool        _bAdditiveNoDepth; ///< Overdraw 용이다. 더하기 블렌드(알파 0 의 프리멀티플라이), 깊이 쓰기 끔(클리어 값 1이 남아 모든 면이 통과), 컬링 끔 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool        _bShowsBuffer;     ///< 표면 값을 그대로 보인다(Normals, Depth, Overdraw). 후처리, 톤맵, 외곽선 껍질을 건너뛴다. NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        /**
         * @brief 겹침 모드다(ShadedWireframe). 배치를 Lit PSO 로 먼저 그리고, 이 모드의 PSO 로 한 번 더 그린다.
         * @details 이 모드의 PSO 는 깊이 쓰기를 끄고 카메라 쪽 깊이 바이어스를 걸어(`FramePSOManager.cpp` applyViewModeToDesc) 같은 삼각형 위에서 선이 LessEqual 을 통과한다.
         */
        bool _bOverlay; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /** @brief 보기 모드 테이블입니다. 쿠커, PSO 변형, 툴바, 로그가 모두 이것을 읽습니다. 항목 순서가 열거값입니다. */
    inline constexpr RenderViewModeInfo kArrRenderViewModeInfo[] = {
        {            "Lit",                         nullptr, false, false, false, false},
        {          "Unlit",           "SW_VIEWMODE_UNLIT=1", false, false, false, false},
        {      "Wireframe",                         nullptr,  true, false, false, false},
        {        "Normals",         "SW_VIEWMODE_NORMALS=1", false, false,  true, false},
        {          "Depth",           "SW_VIEWMODE_DEPTH=1", false, false,  true, false},
        {       "Overdraw",        "SW_VIEWMODE_OVERDRAW=1", false,  true,  true, false},
        {   "LightingOnly",    "SW_VIEWMODE_LIGHTINGONLY=1", false, false, false, false},
        {      "Roughness",       "SW_VIEWMODE_ROUGHNESS=1", false, false,  true, false},
        {       "Metallic",        "SW_VIEWMODE_METALLIC=1", false, false,  true, false},
        {        "Mipmaps",         "SW_VIEWMODE_MIPMAPS=1", false, false,  true, false},
        {"LightComplexity", "SW_VIEWMODE_LIGHTCOMPLEXITY=1", false, false,  true, false},
        {"ShadedWireframe",     "SW_VIEWMODE_WIREOVERLAY=1",  true, false, false,  true},
        {       "LODLevel",        "SW_VIEWMODE_LODLEVEL=1", false, false,  true, false},
        {    "LODFadeCode",         "SW_VIEWMODE_LODFADE=1", false, false,  true, false},
        {    "MarkerSlots",                         nullptr, false, false, false, false},
        {    "ShadingPath",     "SW_VIEWMODE_SHADINGPATH=1", false, false,  true, false},
    };
    static_assert( sizeof( kArrRenderViewModeInfo ) / sizeof( kArrRenderViewModeInfo[0] ) == static_cast<size_t>( RenderViewMode::Count ),
                   "보기 모드 표와 열거형의 개수가 다릅니다" );

    /**
     * @brief 배치 PSO 를 고르는 데 쓰는 보기 모드입니다. 셰이더 define 도 채우기 · 블렌드 변경도 없는 모드(`MarkerSlots`)는 Lit 과 같은 PSO 이므로 Lit 으로 접습니다.
     * @details 접지 않으면 같은 PSO 를 키만 달리해 하나 더 만들었다(`FrameRenderer::collectFrameViewModeMask` · `psoForBatch` 가 이 값으로 키를 만든다).
     */
    inline RenderViewMode getRenderViewModePipelineMode( RenderViewMode viewMode );

    /** @brief 정수가 보기 모드 번호(0 .. Count - 1)인지 봅니다. 번호로 들어온 값(`Count` 표식, 에디터 · 콘솔이 넣은 `gv_viewMode`)은 이것으로 거른 뒤에만 `RenderViewMode` 로 바꿉니다. */
    inline bool isRenderViewModeIndex( int32 value )
    {
        return value >= 0 && value < static_cast<int32>( RenderViewMode::Count );
    }

    /**
     * @brief 보기 모드의 테이블 항목을 반환합니다.
     * @details 범위 밖 값을 Lit 으로 바꾸지 않는다 — 들어오는 곳(`isRenderViewModeIndex`, `FrameRenderer::setViewMode`)이 거르므로 여기 닿으면 결함이다.
     */
    inline const RenderViewModeInfo& getRenderViewModeInfo( RenderViewMode viewMode )
    {
        const uint8 index = static_cast<uint8>( viewMode );
        SW_ASSERT( index < static_cast<uint8>( RenderViewMode::Count ) );
        return kArrRenderViewModeInfo[index];
    }

    inline RenderViewMode getRenderViewModePipelineMode( RenderViewMode viewMode )
    {
        const RenderViewModeInfo& info               = getRenderViewModeInfo( viewMode );
        const bool                bSamePipelineAsLit = info._pShaderDefine == nullptr && info._bWireframe == false && info._bAdditiveNoDepth == false && info._bOverlay == false;
        return bSamePipelineAsLit ? RenderViewMode::Lit : viewMode;
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
        PostProcessValues _postProcess{};
        /// @brief 이 뷰를 그리는 카메라의 피사계 심도 렌즈 값(초점 거리 · 조리개 · 센서 높이)입니다. 피사계 심도 패스가 `g_CameraLens` 로 읽습니다(`FramePassExecutor::applyViewPassConstants`).
        CameraDepthOfFieldValues _depthOfField{};
        /**
         * @brief 원근 투영의 세로 배율 `1 / tan( fovY / 2 )` 입니다. 직교 카메라는 0 이며 피사계 심도가 없습니다.
         * @details 착란원의 화면 크기는 초점 길이에 달렸고, 초점 길이는 센서 높이와 시야각에서 나온다. 뷰-투영 행렬에서 되짚지 않고 카메라에서 받는다.
         */
        float32        _projectionScaleY{ 0.0f };
        uint8          _bShadows{ SW_TRUE };             ///< 그림자를 그린다(끄면 그림자 맵을 지우기만 하므로 모두 밝다).
        uint8          _bPostProcess{ SW_TRUE };         ///< 후처리(블룸, 외곽선, 톤맵, TAA)를 적용한다.
        uint8          _bCut{ SW_FALSE };                ///< 이번 프레임에 화면이 끊겼다(언리얼 `bCameraCut`). 시간 누적 기록(TAA · 모션 블러 · 자동 노출)을 버린다(`FrameViewTarget::_bHistoryReset`).
        RenderViewMode _viewMode{ RenderViewMode::Lit }; ///< 이 뷰의 보기 모드(에디터 씬 뷰만 바꾸고, 게임 뷰는 Lit 이다).
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
