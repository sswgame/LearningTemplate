/**
 * @file CanvasDrawList.h
 * @brief 화면 2D 그리기 목록입니다 — 사각형(셰이더 원소와 같은 배치) · 일괄(텍스처 넷 + 가위) · 글리프 아틀라스 업로드.
 * @details 게임 스레드가 채우고 렌더 프레임 패킷에 실려 렌더 스레드가 읽습니다(렌더 스레드는 위젯을 보지 못한다 — 씬을 못 보는 것과 같은 규칙).
 *          좌표는 **물리 픽셀**(대상 왼쪽 위 원점)입니다 — UI 단위 → 픽셀 변환은 칠하는 쪽(`CanvasPainter`)이 합니다.
 *          `CanvasQuad` 의 배치는 `canvas.hlsl` 의 `SwCanvasQuad` 와 같아야 합니다(static_assert 크기 + `CanvasDrawListTest` 의 오프셋 단언).
 *          언리얼 `FSlateWindowElementList` · Godot canvas item 명령 목록의 자리입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Text/GlyphAtlas.h"

namespace sw
{
    class Texture2D;

    /** @brief 사각형 종류입니다(셰이더 `kind`). 숫자는 `canvas.hlsl` 의 `kCanvasQuad*` 와 같아야 합니다. */
    enum class CanvasQuadKind : uint32
    {
        Rect   = 0, ///< 단색 · 둥근 모서리 · 테두리(SDF)
        Image  = 1, ///< 텍스처 × 색(둥근 모서리 마스크 가능)
        Glyph  = 2, ///< SDF 글리프(아틀라스 R 채널) — 굵게 · 외곽선은 문턱 이동
        Shadow = 3  ///< 둥근 상자의 흐린 그림자(SDF)
    };
} // namespace sw

namespace sw
{
    /** @brief `CanvasQuad::_flags` 의 비트입니다. 숫자는 `canvas.hlsl` 의 `kCanvasFlag*` 와 같아야 합니다. */
    struct CanvasQuadFlag
    {
        static constexpr uint32 kRoundedClip = SW_BIT( 0 ); ///< `_clipRect` · `_params._w`(반지름)로 둥근 자르기를 셰이더가 한다. 사각 자르기는 일괄의 가위가 한다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 셰이더가 읽는 사각형 하나입니다(144 바이트 — float4 여덟 + uint 넷, HLSL · std430 구조버퍼 정렬).
     * @details 정점 셰이더가 `SV_VertexID` 로 모서리 여섯을 만들고 `_axis` 로 돌린다(동적 정점 버퍼가 없다). 모양 · 둥근 자르기는 픽셀 셰이더의 부호 거리다.
     */
    struct CanvasQuad
    {
        float4 _rect{};                                ///< x, y(변환 전 왼쪽 위의 화면 위치) · 너비, 높이(변환 전 픽셀)
        float4 _uvRect{};                              ///< u0, v0, u1, v1
        float4 _color{};                               ///< 곧은 RGBA(셰이더가 프리멀티플라이한다)
        float4 _borderColor{};                         ///< Rect: 테두리 색 · Glyph: 외곽선 색
        float4 _cornerRadius{};                        ///< 왼위 · 오위 · 오아 · 왼아(픽셀)
        float4 _clipRect{};                            ///< x0, y0, x1, y1(픽셀) — `CanvasQuadFlag::kRoundedClip` 일 때만 읽는다
        float4 _axis{ 1.0f, 0.0f, 0.0f, 1.0f };        ///< 2×2 변환(축 X = xy · 축 Y = zw) — 사각형 왼쪽 위 기준. 회전 · 기울임 · 크기
        float4 _params{};                              ///< Rect: x 테두리 두께 · Glyph: x 거리 배율(px) · y 외곽선 두께 · z 굵게 이동 · Shadow: y 흐림 · 모두: w 둥근 자르기 반지름
        uint32 _kind{ 0 };                             ///< `CanvasQuadKind`
        uint32 _textureSlot{ invalid_index::kUint32 }; ///< 일괄 텍스처의 번호(0..3), 없음 = invalid_index::kUint32
        uint32 _flags{ 0 };                            ///< `CanvasQuadFlag`
        uint32 _padding0{ 0 };
    };
    static_assert( sizeof( CanvasQuad ) == 144, "CanvasQuad must match SwCanvasQuad in canvas.hlsl" );
} // namespace sw

namespace sw
{
    /** @brief 일괄이 거는 텍스처 하나입니다 — 글리프 아틀라스 페이지 번호 또는 텍스처 에셋. */
    struct SW_API CanvasTextureRef
    {
        shared_ptr<const Texture2D> _texture{};                           ///< 그림 · 9-슬라이스(패킷이 수명을 쥔다 — 머티리얼과 같은 규칙)
        uint16                      _atlasPage{ invalid_index::kUint16 }; ///< 글리프 아틀라스 페이지(텍스처가 없을 때)

        /** @brief 같은 텍스처를 가리키면 true 입니다. */
        bool isEqual( const CanvasTextureRef& other ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 같은 텍스처 묶음(최대 넷)과 같은 가위로 그리는 연속 사각형입니다 — 드로우 하나(`drawInstanced( 6, _quadCount )`).
     * @details 텍스처 수 한도는 에뮬 백엔드(DX11 · GL)가 머티리얼 텍스처 슬롯 t5..t8 에 서수로 거는 수와 같습니다(`shaderslot::kMaterialTextureCount`).
     *          가위가 바뀌는 자리에서 일괄이 끊깁니다(Slate · Dear ImGui · Godot 의 사각 클리핑과 같다).
     */
    struct CanvasBatch
    {
        CanvasTextureRef _arrTexture[shaderslot::kMaterialTextureCount]{};
        RHIScissorRect   _scissor{}; ///< `_bScissor` 일 때만 — 렌더 타깃 픽셀
        uint32           _firstQuad{ 0 };
        uint32           _quadCount{ 0 };
        uint8            _textureCount{ 0 };
        uint8            _bScissor{ SW_FALSE }; ///< 0 이면 대상 전체
    };
} // namespace sw

namespace sw
{
    /** @brief 대상 하나에 그리는 그리기 목록입니다. */
    struct SW_API CanvasDrawList
    {
        vector<CanvasQuad>  _listQuad{};
        vector<CanvasBatch> _listBatch{};
        float2              _targetSize{}; ///< 대상 픽셀 크기(칠하는 쪽이 가위를 대상 안으로 자른다 — 0 이면 자르지 않는다)

        /** @brief 사각형 · 일괄을 비웁니다(용량은 남긴다). */
        void clear();
        /** @brief 그릴 것이 없으면 true 입니다. */
        bool isEmpty() const { return _listQuad.empty(); }
    };
} // namespace sw

namespace sw
{
    /** @brief 한 프레임의 캔버스 전부입니다 — 렌더 프레임 패킷이 듭니다. */
    struct SW_API CanvasFrameData
    {
        CanvasDrawList           _mainOutput{};      ///< 주 출력(백버퍼 · 게임 뷰 RT · 스크린샷 캡처)에 Present 뒤 그리는 목록
        vector<GlyphAtlasUpload> _listAtlasUpload{}; ///< 지난 프레임 뒤 바뀐 글리프 아틀라스 구간(`GlyphAtlas::takeUploads`)
        /**
         * @brief 사각형 · 일괄 내용이 바뀔 때만 오르는 번호입니다. 렌더러는 지난 프레임과 같으면 사각형 버퍼를 다시 올리지 않습니다.
         * @details 0 은 "모른다" 라 늘 올립니다(시험 · 개발 시험 그림).
         */
        uint64 _contentRevision{ 0 };

        /** @brief 비웁니다(용량은 남긴다 — 패킷 재사용, `RenderFramePacket::resetForFrame`). */
        void clear();
    };
} // namespace sw
