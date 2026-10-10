/**
 * @file CanvasPainter.h
 * @brief 캔버스 칠하기 도구입니다 — UI 단위의 모양을 물리 픽셀 사각형(`CanvasQuad`)으로 바꿔 그리기 목록에 쌓습니다.
 * @details 언리얼 Slate 의 `FSlateDrawElement::MakeBox` · `MakeText` 와 `FSlateElementBatcher` 의 일을 한 자리에서 합니다(유니티 UIR 의
 *          `MeshGenerationContext`, Godot `CanvasItem::draw_*` 자리). 변환 · 자르기 · 불투명도는 스택이고, 사각 자르기는 일괄의 **가위**로,
 *          둥근 자르기만 셰이더의 부호 거리로 합니다. 같은 텍스처(최대 넷) · 같은 가위가 이어지면 한 일괄입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Text/TextTypes.h"

namespace sw
{
    class GlyphCache;
    class Texture2D;

    /** @brief 칠할 모양 하나의 겉모습입니다(스타일이 위젯마다 계산해 줄 값). 길이는 UI 단위입니다. */
    struct CanvasBrush
    {
        shared_ptr<const Texture2D> _image{};                          ///< 그림 객체. `_imagePath` 와 둘 다 비면 단색 사각형
        hashed_string               _imagePath{};                      ///< 그림 경로(DDS) — 객체가 없을 때 렌더 스레드가 `TextureCache` 로 푼다
        float4                      _color{ 1.0f, 1.0f, 1.0f, 1.0f };  ///< 곧은 RGBA. 그림이면 그림에 곱한다
        float4                      _borderColor{};                    ///< 테두리 색(단색 사각형만)
        float4                      _cornerRadius{};                   ///< 왼위 · 오위 · 오아 · 왼아
        float4                      _nineSliceMargin{};                ///< 그림 크기의 비율(왼 · 위 · 오 · 아, 0..1) — 0 이면 늘이기(Slate `FSlateBrush::Margin`)
        float4                      _uvRect{ 0.0f, 0.0f, 1.0f, 1.0f }; ///< 그림에서 쓸 구간(u0, v0, u1, v1)
        float2                      _imageSize{};                      ///< 9-슬라이스 여백의 기준 크기(Slate `ImageSize`). 0 이면 텍스처 픽셀 크기(경로 그림이면 칠할 크기)

        /** @brief 그림(객체 또는 경로)이 있으면 true 입니다. */
        bool    hasImage() const { return _image != nullptr || _imagePath.empty() == false; }
        float32 _borderWidth{ 0.0f }; ///< 테두리 두께(단색 사각형만)
    };
} // namespace sw

namespace sw
{
    /** @brief 글리프 하나를 칠할 값입니다. 길이는 UI 단위입니다. */
    struct CanvasGlyphStyle
    {
        float4  _color{ 1.0f, 1.0f, 1.0f, 1.0f }; ///< 곧은 RGBA
        float4  _outlineColor{};                  ///< 외곽선 색(`_outlineWidth` > 0 일 때)
        float32 _fontSize{ 16.0f };               ///< em 하나의 크기
        float32 _outlineWidth{ 0.0f };            ///< 외곽선 두께
        uint8   _bFauxBold{ SW_FALSE };           ///< 굵은 면이 없어 SDF 문턱을 밀어 굵게
        uint8   _bFauxItalic{ SW_FALSE };         ///< 기운 면이 없어 기울여 그린다
    };
} // namespace sw

namespace sw
{
    /** @brief 2×3 아핀 변환입니다(UI 단위) — 점 p 는 `_axisX * p.x + _axisY * p.y + _translation` 으로 갑니다. */
    struct CanvasTransform
    {
        float2 _axisX{ 1.0f, 0.0f };
        float2 _axisY{ 0.0f, 1.0f };
        float2 _translation{};

        /** @brief 평행 이동만 있는 변환을 만듭니다. */
        static CanvasTransform makeTranslation( const float2& offset );
        /** @brief 점을 변환합니다. */
        float2 transformPoint( const float2& point ) const;
        /** @brief 방향(평행 이동 없이)을 변환합니다. */
        float2 transformVector( const float2& vector ) const;
        /** @brief 이 변환 다음에 @p child 를 거는 변환(부모 ∘ 자식)을 만듭니다. */
        CanvasTransform makeConcatenated( const CanvasTransform& child ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class CanvasPainter
     * @brief 그리기 목록 하나에 칠합니다(게임 스레드 · 위젯 그리기 단계). 목록을 소유하지 않습니다.
     */
    class SW_API CanvasPainter
    {
    public:
        /** @brief 가짜 굵게의 문턱 이동입니다(글꼴 픽셀 크기의 비율 — 윤곽이 양쪽으로 이만큼 밀린다). */
        static constexpr float32 kFauxBoldEmFraction = 0.025f;
        /** @brief 가짜 기울임의 기울기입니다(아래로 1 내려갈 때 왼쪽으로 가는 양 — 약 11 도, 언리얼 Slate 의 가짜 이탤릭과 같은 값). */
        static constexpr float32 kFauxItalicShear = 0.2f;

        /**
         * @param outCanvas 칠할 목록(대상 크기 `_targetSize` 를 먼저 채워 두면 가위를 그 안으로 자른다)
         * @param uiScale     UI 단위 → 물리 픽셀 배율
         */
        CanvasPainter( CanvasDrawList& outCanvas, float32 uiScale );

        CanvasPainter( const CanvasPainter& )            = delete;
        CanvasPainter& operator=( const CanvasPainter& ) = delete;

        /** @brief 지금 변환 · 자르기 · 불투명도 아래에서 사각형을 칠합니다 — 그림이 있으면 그림(9-슬라이스는 `drawImage`), 없으면 단색 · 둥근 · 테두리. */
        void fillRect( const float2& position, const float2& size, const CanvasBrush& brush );
        /**
         * @brief 그림을 칠합니다. 여백(`_nineSliceMargin`)이 있으면 조각 아홉(가운데는 늘인다), 사각형이 여백 합보다 작으면 여백을 비율대로 줄입니다.
         * @details 줄이는 규칙은 `SpriteMeshBuilder` 의 월드 9-슬라이스와 같습니다. 크기가 0 이 된 조각은 내지 않습니다.
         */
        void drawImage( const float2& position, const float2& size, const CanvasBrush& brush );
        /**
         * @brief 글리프 하나를 칠합니다 — 아틀라스 사각형을 캐시에서 찾고(없으면 래스터화) SDF 사각형 하나를 냅니다. 빈 글리프(공백)는 내지 않습니다.
         * @param origin 기준선 위의 글리프 원점(UI 단위)
         * @return 사각형을 냈으면 true(빈 글리프 · 캐시 실패 · 다 잘림은 false)
         */
        bool drawGlyph( const float2& origin, FontFaceID face, uint32 glyphIndex, const CanvasGlyphStyle& style, GlyphCache& glyphCache, uint64 frameIndex );
        /** @brief 둥근 상자의 흐린 그림자를 칠합니다(상자는 @p offset 만큼 밀린 자리). */
        void drawShadow( const float2& position, const float2& size, const float4& cornerRadius, const float4& color, float32 blur, const float2& offset );

        /**
         * @brief 자르기를 쌓습니다 — 지금 자르기와의 교집합입니다. 사각 부분은 일괄의 가위가, 둥근 모서리(@p cornerRadius > 0)는 셰이더가 합니다.
         * @details 회전된 자르기는 그 축 정렬 경계 상자로 자릅니다(Slate 의 스텐실 자르기는 없다). 둥근 자르기는 가장 안쪽 하나만 셰이더가 본다.
         */
        void pushClip( const float2& position, const float2& size, float32 cornerRadius );
        void popClip();
        /**
         * @brief 지금 변환 뒤에 @p transform 을 건 원점 사각형(크기 @p size, UI 단위)이 지금 자르기 밖에 통째로 있으면 true 입니다. 자르기가 없으면 늘 false 입니다.
         * @details 위젯 그리기가 스크롤 밖 위젯을 걷지 않는 데 씁니다(Slate 의 자식 컬링과 같다 — 위젯 사각형 기준, 그림자 · 넘친 자손은 보지 않는다).
         *          변환 스택을 건드리지 않는다 — 1 만 칸 목록에서 칸마다 부르는 값싼 길이다.
         */
        bool isOutsideClip( const CanvasTransform& transform, const float2& size ) const;
        /**
         * @brief 지금 자르기의 세로 범위(위 · 아래, 화면 UI 단위)를 돌려줍니다. 자르기가 없거나 변환이 쌓여 있으면 false 입니다.
         * @details 자식이 세로로 차례대로 놓이는 패널(세로 상자 · 가로 흐름)이 보이는 자식 범위를 이분 탐색으로 찾는 데 씁니다.
         */
        [[nodiscard]] bool findClipVerticalRange( float32& outTop, float32& outBottom ) const;
        /** @brief 변환을 쌓습니다(지금 변환 뒤에 건다). */
        void pushTransform( const CanvasTransform& transform );
        void popTransform();
        /** @brief 불투명도를 쌓습니다(곱한다). */
        void pushOpacity( float32 opacity );
        void popOpacity();

        /** @brief UI 단위 → 물리 픽셀 배율입니다. */
        float32 getUiScale() const { return _uiScale; }
        /**
         * @brief 칠할 목록을 바꿉니다 — 변환 · 자르기 · 불투명도 스택은 그대로입니다(위젯 그리기가 위젯마다 자기 그림 캐시로 바꾼다).
         * @details 가위를 대상 안으로 자르는 크기(`_targetSize`)는 새 목록의 것을 씁니다.
         */
        void            setDrawList( CanvasDrawList& outCanvas ) { _pDrawList = &outCanvas; }
        CanvasDrawList& getDrawList() const { return *_pDrawList; }

    private:
        /** @brief 자르기 스택 한 칸입니다(물리 픽셀). */
        struct ClipState
        {
            float4  _bounds{};              ///< x0, y0, x1, y1 — 가위가 되는 축 정렬 사각형
            float4  _roundedBounds{};       ///< 둥근 자르기의 사각형(x0, y0, x1, y1)
            float32 _roundedRadius{ 0.0f }; ///< 0 이면 둥근 자르기 없음
            uint8   _bClipped{ SW_FALSE };  ///< 0 이면 자르기 없음(대상 전체)
        };

        /** @brief 사각형 하나에 지금 변환 · 자르기 · 불투명도를 얹어 목록에 넣고 일괄을 고릅니다. 다 잘리면 넣지 않습니다. */
        void appendQuad( CanvasQuad& quad, const CanvasTextureRef* pTexture );
        /** @brief 지금 일괄에 그 텍스처 · 가위로 이어 붙일 수 있으면 그 일괄, 아니면 새 일괄을 엽니다. 사각형의 텍스처 번호를 돌려줍니다. */
        uint32 selectBatch( const CanvasTextureRef* pTexture );
        /** @brief 지금 가위 사각형을 만듭니다(픽셀 정수, 대상 안으로 자른다). 다 잘렸으면 false. */
        [[nodiscard]] bool computeScissor( RHIScissorRect& outScissor, bool& outHasScissor ) const;
        /** @brief UI 단위 사각형을 지금 변환으로 옮겨 물리 픽셀 사각형(위치 · 크기 · 축)을 채웁니다. */
        void placeRect( const float2& position, const float2& size, CanvasQuad& outQuad ) const;

        CanvasDrawList*         _pDrawList; ///< 칠하는 목록(소유하지 않는다)
        vector<CanvasTransform> _listTransform;
        vector<ClipState>       _listClip;
        vector<float32>         _listOpacity;
        float32                 _uiScale;
    };
} // namespace sw
