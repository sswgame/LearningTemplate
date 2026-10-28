/**
 * @file CanvasDrawList.h
 * @brief 화면 2D 그리기 목록을 정의합니다. 사각형(셰이더 원소와 같은 배치), 일괄(텍스처 4개와 가위), 글리프 아틀라스 업로드로 이루어집니다.
 * @details 게임 스레드가 채우고 렌더 프레임 패킷에 실려 렌더 스레드가 읽는다(렌더 스레드는 위젯을 보지 못한다. 씬을 못 보는 것과 같은 규칙이다).
 *          좌표는 **물리 픽셀**이다(대상 왼쪽 위 원점). UI 단위에서 픽셀로의 변환은 칠하는 쪽(`CanvasPainter`)이 한다.
 *          `CanvasQuad` 의 배치는 `canvas.hlsl` 의 `SwCanvasQuad` 와 같아야 한다(static_assert 크기와 `CanvasDrawListTest` 의 오프셋 단언).
 *          언리얼 `FSlateWindowElementList`, Godot canvas item 명령 목록에 해당한다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Text/GlyphAtlas.h"

namespace sw
{
    class Texture2D;

    /** @brief 사각형 종류입니다(셰이더 `kind`). 숫자는 `canvas.hlsl` 의 `kCanvasQuad*` 와 같아야 합니다. */
    enum class CanvasQuadKind : uint32
    {
        Rect   = 0, ///< 단색, 둥근 모서리, 테두리(SDF)
        Image  = 1, ///< 텍스처 × 색(둥근 모서리 마스크 가능)
        Glyph  = 2, ///< SDF 글리프(아틀라스 R 채널). 굵게 표시와 외곽선은 임계값을 이동해 만든다.
        Shadow = 3  ///< 둥근 상자의 흐린 그림자(SDF)
    };
} // namespace sw

namespace sw
{
    /** @brief `CanvasQuad::_flags` 의 비트입니다. 숫자는 `canvas.hlsl` 의 `kCanvasFlag*` 와 같아야 합니다. */
    struct CanvasQuadFlag
    {
        static constexpr uint32 kRoundedClip       = SW_BIT( 0 ); ///< `_clipRect` 와 `_params._w`(반지름)로 둥근 자르기를 셰이더가 한다. 사각 자르기는 일괄의 가위가 한다.
        static constexpr uint32 kMultiChannelGlyph = SW_BIT( 1 ); ///< 글리프 사각형의 아틀라스 페이지가 MSDF(RGBA)다 — 셰이더가 RGB 의 중앙값을 거리값으로 쓴다(아니면 R 채널 하나).
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 셰이더가 읽는 사각형 하나입니다(144바이트이며 float4 여덟 개와 uint 네 개입니다. HLSL과 std430 구조 버퍼 정렬을 따릅니다).
     * @details 정점 셰이더가 `SV_VertexID` 로 모서리 여섯 개를 만들고 `_axis` 로 돌린다(동적 정점 버퍼가 없다). 모양과 둥근 자르기는 픽셀 셰이더의 부호 거리로 처리한다.
     */
    struct CanvasQuad
    {
        float4 _rect{};                                ///< x, y(변환 전 왼쪽 위의 화면 위치), 너비, 높이(변환 전 픽셀)
        float4 _uvRect{};                              ///< u0, v0, u1, v1
        float4 _color{};                               ///< 곧은 RGBA(셰이더가 프리멀티플라이한다)
        float4 _borderColor{};                         ///< Rect 는 테두리 색, Glyph 는 외곽선 색
        float4 _cornerRadius{};                        ///< 왼쪽 위, 오른쪽 위, 오른쪽 아래, 왼쪽 아래 순서(픽셀)
        float4 _clipRect{};                            ///< x0, y0, x1, y1(픽셀). `CanvasQuadFlag::kRoundedClip` 일 때만 읽는다.
        float4 _axis{ 1.0f, 0.0f, 0.0f, 1.0f };        ///< 2×2 변환(축 X는 xy, 축 Y는 zw). 사각형 왼쪽 위 기준이며 회전, 기울임, 크기를 나타낸다.
        float4 _params{};                              ///< Rect 는 x 테두리 두께. Glyph 는 x 거리 배율(px), y 외곽선 두께, z 굵게 이동. Shadow 는 y 흐림. 모두 w 가 둥근 자르기 반지름
        uint32 _kind{ 0 };                             ///< `CanvasQuadKind`
        uint32 _textureSlot{ invalid_index::kUint32 }; ///< 일괄 텍스처의 번호(0..3). 없으면 invalid_index::kUint32
        uint32 _flags{ 0 };                            ///< `CanvasQuadFlag`
        uint32 _anchor{ invalid_index::kUint32 };      ///< 화면 앵커 번호(`CanvasFrameData::_listAnchor`). 있으면 `_rect` 위치는 앵커 기준이다. 없으면 invalid_index::kUint32
    };
    static_assert( sizeof( CanvasQuad ) == 144, "CanvasQuad must match SwCanvasQuad in canvas.hlsl" );
} // namespace sw

namespace sw
{
    /**
     * @brief 일괄이 바인딩하는 텍스처 하나입니다. 글리프 아틀라스 페이지 번호, 텍스처 객체, 텍스처 경로 중 하나로 가리킵니다.
     * @details 경로(`_texturePath`)는 게임 스레드가 디바이스 없이 이미지를 가리키는 방법이다. 렌더 스레드의 캔버스 렌더러가 `TextureCache` 로 찾는다
     *          (머티리얼의 텍스처 프로퍼티와 같은 방식). 셋 중 하나만 쓴다(객체, 경로, 아틀라스 페이지 순서로 우선한다).
     */
    struct SW_API CanvasTextureRef
    {
        shared_ptr<const Texture2D> _texture{};                           ///< 이미지와 9-슬라이스. 패킷이 수명을 잡는다(머티리얼과 같은 규칙).
        hashed_string               _texturePath{};                       ///< 이미지의 리소스 경로(DDS). 렌더 스레드가 찾는다. 텍스처 객체가 없을 때 쓴다.
        uint16                      _atlasPage{ invalid_index::kUint16 }; ///< 글리프 아틀라스 페이지(텍스처와 경로가 없을 때)

        /** @brief 같은 텍스처를 가리키면 true 입니다. */
        bool isEqual( const CanvasTextureRef& other ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 같은 텍스처 그룹(최대 4개)과 같은 가위로 그리는 연속 사각형입니다. 드로우 하나(`drawInstanced( 6, _quadCount )`)로 그립니다.
     * @details 텍스처 수 한도는 에뮬레이션 백엔드(DX11, GL)가 머티리얼 텍스처 슬롯 t16..t19 에 서수로 바인딩하는 수와 같다(`shaderslot::kCanvasTextureCount`).
     *          가위가 바뀌는 지점에서 일괄이 끊긴다(Slate, Dear ImGui, Godot 의 사각 클리핑과 같다).
     */
    struct CanvasBatch
    {
        CanvasTextureRef _arrTexture[shaderslot::kCanvasTextureCount]{};
        RHIScissorRect   _scissor{}; ///< `_bScissor` 일 때만 쓴다(렌더 타깃 픽셀).
        uint32           _firstQuad{ 0 };
        uint32           _quadCount{ 0 };
        uint8            _textureCount{ 0 };
        uint8            _bScissor{ SW_FALSE }; ///< 0이면 대상 전체
    };
} // namespace sw

namespace sw
{
    /** @brief 대상 하나에 그리는 그리기 목록입니다. */
    struct SW_API CanvasDrawList
    {
        vector<CanvasQuad>  _listQuad{};
        vector<CanvasBatch> _listBatch{};
        float2              _targetSize{}; ///< 대상 픽셀 크기(칠하는 쪽이 가위를 대상 안으로 자른다. 0이면 자르지 않는다).

        /** @brief 사각형과 일괄을 비웁니다(용량은 남깁니다). */
        void clear();
        /** @brief 그릴 것이 없으면 true 입니다. */
        bool isEmpty() const { return _listQuad.empty(); }
        /**
         * @brief @p source 의 사각형과 일괄을 뒤에 이어 붙입니다(위젯 이미지 캐시에서 프레임 목록으로).
         * @details 첫 일괄은 지금 마지막 일괄과 가위가 같고 텍스처 합이 4개 안이면 합치고(사각형의 텍스처 번호를 다시 매긴다), 아니면 새 일괄로 둔다.
         *          칠하기 도구가 한 목록에 바로 칠한 것과 같은 일괄 수가 된다.
         */
        void appendDrawList( const CanvasDrawList& source );
        /** @brief 사각형 바이트와 일괄(텍스처, 가위, 범위)이 같으면 true 입니다. 내용 번호를 증가시킬지 정하는 데 씁니다. */
        bool isSameContent( const CanvasDrawList& other ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 렌더 텍스처 하나에 그리는 목록입니다(월드 공간 UI인 `WidgetComponent` World). 렌더러가 장면 앞에서 그 텍스처를 지우고 그립니다.
     * @details 텍스처는 `TextureCache` 의 렌더 타깃 경로(`rendertarget/...`)라 머티리얼이 같은 경로로 읽는다. 크기는 `_list._targetSize` 다.
     */
    struct SW_API CanvasTargetDrawList
    {
        hashed_string  _targetPath{};         ///< 렌더 텍스처 경로
        CanvasDrawList _list{};               ///< 대상 픽셀 크기는 `_list._targetSize` 다.
        float4         _clearColor{};         ///< 그리기 전에 지우는 색(프리멀티플라이이며 투명이면 0)
        uint64         _contentRevision{ 0 }; ///< 내용이 바뀔 때만 오른다. 같으면 렌더러가 텍스처를 다시 그리지 않는다(0이면 항상 그린다).
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 화면 앵커 하나입니다. 앵커를 가리키는 사각형(`CanvasQuad::_anchor`)은 셰이더가 `_position + 배율 × 사각형 위치` 에 놓습니다.
     * @details 월드 점을 따라가는 화면 마커(`WidgetComponent` Screen)가 쓴다. 마커가 움직여도 사각형은 그대로이고 앵커만 바뀐다.
     *          그래서 게임 스레드가 마커마다 다시 배치하고 칠할 필요가 없고, 렌더러도 사각형 버퍼를 다시 업로드하지 않는다.
     */
    struct CanvasAnchor
    {
        float2  _position{};     ///< 앵커 점(대상 물리 픽셀, 정수로 맞춘다)
        float32 _scale{ 1.0f };  ///< 앵커 점을 기준으로 한 배율(거리 배율)
        float32 _unused{ 0.0f }; ///< 셰이더 float4 의 w(쓰지 않는다)
    };
} // namespace sw
namespace sw
{
    /** @brief 마커 기록의 `_flags` 비트입니다. 숫자는 `canvasmarker.hlsl` 의 `kMarkerFlag*` 와 같아야 합니다. */
    enum class CanvasMarkerFlag : uint32
    {
        None  = 0,
        Clamp = SW_BIT( 0 ), ///< 화면 밖이면 가장자리에 붙인다(끄면 숨긴다)
    };

    /**
     * @brief 컴퓨트가 투영하는 화면 마커 하나의 입력입니다(앵커 번호가 인덱스). 월드 점과 보이기 규칙만 싣는다 — 화면 좌표 · 가시 판정은 `canvasmarker.hlsl` 이 계산한다.
     * @details 기록 하나가 렌더러 쪽 마커 인스턴스 하나다: 마커 ↔ GPU 인스턴스 대응은 앵커 번호(= 구조 버퍼의 앞 원소 번호)이므로 따로 표를 두지 않는다.
     *          `_bActive` 가 0 이면 컴퓨트는 그 앵커를 건드리지 않는다(CPU 가 투영하는 마커이거나 숨긴 마커). 배치는 float4 둘이다(`CanvasMarkerView` 앞 머리 행 뒤에 이어 올린다).
     */
    struct CanvasMarkerRecord
    {
        float3  _worldPosition{};                          ///< 마커의 월드 기준점입니다(주 씬 컴포넌트 위치 + 월드 오프셋).
        float32 _maxDistance{ 0.0f };                      ///< 이보다 멀면 숨깁니다(0 이면 제한 없음).
        float2  _screenOffset{};                           ///< 투영한 점에 더하는 UI 단위 오프셋입니다.
        float32 _referenceDistance{ 0.0f };                ///< 0 보다 크면 거리 배율(`_referenceDistance / 거리`, 뷰의 최소 · 최대로 자른다)을 씁니다.
        uint32  _flags{ 0 };                               ///< `CanvasMarkerFlag` 비트입니다.
        uint32  _bActive{ 0 };                             ///< 1 이면 컴퓨트가 이 앵커를 씁니다.
        float3  _worldOffset{};                            ///< 주 씬 컴포넌트 위치에서 기준점까지의 월드 오프셋입니다. 인스턴스 칸이 있으면 컴퓨트가 인스턴스 변환의 위치에 이것을 더합니다.
        uint32  _primitiveIndex{ invalid_index::kUint32 }; ///< 게임 스레드가 적은 마커 오브젝트의 렌더 프리미티브 번호(`PrimitiveRegistry`)입니다. 메시가 주 씬 컴포넌트일 때만 있습니다.
        uint32  _instanceSlot{ invalid_index::kUint32 };   ///< 패킷을 채울 때 빌더가 푼 GPUScene 인스턴스 칸입니다(`GPUSceneBuilder::findOpaqueInstanceSlot`). 없으면 `_worldPosition` 을 씁니다.
    };

    /** @brief 마커 투영이 한 프레임에 쓰는 공용 값입니다(게임 카메라의 뷰 · 투영과 UI 뷰포트). */
    struct CanvasMarkerView
    {
        float4x4 _viewProjection{};   ///< 행 벡터 규약(`float4::transform`)의 게임 카메라 뷰 · 투영 행렬입니다.
        float3   _cameraPosition{};   ///< 거리 계산용 카메라 월드 위치입니다.
        float2   _viewportSize{};     ///< UI 뷰포트 크기(UI 단위)입니다.
        float32  _uiScale{ 1.0f };    ///< UI 단위 → 물리 픽셀 배율입니다(앵커를 이 배율로 곱해 정수로 맞춘다 — `UIScreenMarkers::publishCanvasAnchors` 와 같은 규칙).
        float32  _edgeMargin{ 0.0f }; ///< 가장자리에 붙일 때 남기는 안쪽 여백(UI 단위)입니다.
        float32  _minClipW{ 0.0f };   ///< 클립 w 가 이보다 작으면 카메라 뒤입니다.
        float32  _minDistanceScale{ 1.0f };
        float32  _maxDistanceScale{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 한 프레임의 캔버스 전부입니다. 렌더 프레임 패킷이 가지고 있습니다. */
    struct SW_API CanvasFrameData
    {
        CanvasDrawList               _mainOutput{};      ///< 주 출력(백버퍼, 게임 뷰 RT, 스크린샷 캡처)에 Present 뒤 그리는 목록
        vector<CanvasTargetDrawList> _listTarget{};      ///< 렌더 텍스처 대상 목록(장면 앞에서 그리며 월드 공간 UI 에 해당한다)
        vector<GlyphAtlasUpload>     _listAtlasUpload{}; ///< 지난 프레임 뒤 바뀐 글리프 아틀라스 구간(`GlyphAtlas::takeUploads`)
        /** @brief 주 출력 사각형의 `_anchor` 가 가리키는 앵커입니다. 프레임마다 바뀌어도 `_contentRevision` 을 증가시키지 않습니다. 렌더러가 앵커 구간만 업로드합니다. */
        vector<CanvasAnchor> _listAnchor{};
        /**
         * @brief 컴퓨트가 투영할 화면 마커 입력입니다(앵커 번호가 인덱스, 비어 있으면 컴퓨트를 돌리지 않는다). 렌더러가 업로드한 뒤 `canvasmarker.hlsl` 이 활성 기록의 앵커 원소를 덮어쓴다.
         * @details 게임 스레드가 월드 점만 싣고(`_listAnchor` 의 CPU 투영을 하지 않는다) 화면 좌표 · 가시 판정은 GPU 가 낸다. 사각형 버퍼는 그대로라 `_contentRevision` 도 올리지 않는다.
         */
        vector<CanvasMarkerRecord> _listMarker{};
        CanvasMarkerView           _markerView{}; ///< `_listMarker` 가 쓰는 공용 값입니다.
        /**
         * @brief 사각형이나 일괄 내용이 바뀔 때만 증가하는 번호입니다. 렌더러는 지난 프레임과 같으면 사각형 버퍼를 다시 업로드하지 않습니다.
         * @details 0은 "모른다" 는 뜻이라 항상 업로드합니다(테스트와 개발 테스트 이미지).
         */
        uint64 _contentRevision{ 0 };
        /**
         * @brief 주 출력에 적용하는 색각 보정 방식입니다(`gv_colorVisionMode`: 0 끔, 1 적색약, 2 녹색약, 3 청색약, colorvision.hlsli).
         * @details UI 는 톤맵 뒤에 그려 톤맵 쪽 보정이 닿지 않으므로 캔버스 셰이더가 직접 적용한다. 렌더 텍스처 대상(월드 위젯)에는 적용하지 않는다.
         *          그 이미지는 장면을 지나 톤맵 쪽 보정을 받는다.
         */
        uint32 _colorVisionMode{ 0 };

        /** @brief 비웁니다(용량은 남깁니다. 패킷 재사용이며 `RenderFramePacket::resetForFrame` 이 부릅니다). */
        void clear();
    };
} // namespace sw
