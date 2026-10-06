/**
 * @file UiPaintPass.h
 * @brief 그리기 걷기입니다 — 그리기 더러운 위젯만 자기 그림 캐시를 다시 칠하고, 프레임 목록은 그리기 순서로 캐시를 이어 붙입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/Canvas/CanvasPainter.h"

namespace sw
{
    struct UiActionGlyphSource;
    struct WidgetGeometry;

    class GlyphCache;
    class LocalizationManager;
    class TextLayoutEngine;
    class Widget;
    class WidgetTree;
} // namespace sw

namespace sw
{
    /**
     * @struct UiPaintContext
     * @brief 위젯이 칠할 때 묻는 것 — 글 배치 · 글리프 캐시 · 프레임 번호 · 배율입니다.
     */
    struct UiPaintContext
    {
        TextLayoutEngine*          _pTextLayout{ nullptr };   ///< 글 배치(없으면 글 위젯은 칠하지 않는다)
        GlyphCache*                _pGlyphCache{ nullptr };   ///< 글리프 아틀라스(없으면 글 위젯은 칠하지 않는다)
        uint64                     _frameIndex{ 0 };          ///< 글리프 캐시의 최근 사용 프레임
        float32                    _uiScale{ 1.0f };          ///< UI 단위 → 물리 픽셀
        float32                    _textScale{ 1.0f };        ///< 글자 배율(gv_uiTextScale) — 레이아웃 문맥과 같은 값
        uint32                     _atlasGeneration{ 0 };     ///< 글리프 아틀라스 세대 — 바뀌면 글 위젯을 다시 칠한다
        const LocalizationManager* _pLocalization{ nullptr }; ///< 글 위젯이 키를 푸는 문화권(레이아웃 문맥과 같은 값)
        uint32                     _textRevision{ 0 };        ///< 그 문화권의 글 판
        const UiActionGlyphSource* _pActionGlyphs{ nullptr }; ///< `[action=이름]` 태그의 글리프 출처(레이아웃 문맥과 같은 값)
    };
} // namespace sw

namespace sw
{
    /** @brief 위젯 하나의 그림 캐시입니다(물리 픽셀 사각형 — 조상의 자르기 · 불투명도가 구워져 있다). */
    struct WidgetPaintCache
    {
        CanvasDrawList _under{}; ///< 자식 아래(`Widget::paint`)
        CanvasDrawList _over{};  ///< 자식 위(`Widget::paintOverChildren`)
    };
} // namespace sw

namespace sw
{
    /**
     * @class UiPaintPass
     * @brief 트리 하나를 그리기 목록으로 냅니다(언리얼 Slate 의 캐시된 요소 목록 · 전역 무효화, 유니티 UIR 의 더러운 요소 다시 칠하기 자리).
     * @details 그리기 순서(자식 순서 — z 순서 패널은 `collectPaintOrder`)로 트리를 걸으며 위젯마다 (1) 그리기 더러우면(`kPaint` · `kStyle` · `kVisibility` ·
     *          처음 · 조상의 `kTransform` — 조상의 불투명도 · 자르기가 캐시에 구워져 있다 · 배율 변화 · 글 위젯이면 아틀라스 세대 변화) 자기 캐시를 다시
     *          칠하고 (2) 캐시를 프레임 목록에 이어 붙입니다. 자르는 패널은 자기 사각형을 가위로 쌓습니다. 걷기가 끝나면 그리기 · 스타일 무효화를 비웁니다
     *          (스타일 걷기(5-2) 전까지 `kStyle` 은 그 위젯의 다시 칠하기다). Collapsed · Hidden 위젯과 그 아래는 칠하지 않습니다. 게임 스레드만.
     */
    class SW_API UiPaintPass
    {
    public:
        /**
         * @brief @p tree 를 칠해 @p outCanvas 뒤에 이어 붙입니다. @p painter 는 칠할 목록을 위젯 캐시마다 바꿔 쓰고, 끝나면 @p outCanvas 로 돌려 놓습니다.
         * @return 이번에 다시 칠한 위젯 수입니다(`Widget::paint` 호출 수 — 프로파일 카운터 Ui.PaintWidgets).
         */
        static uint32 paint( WidgetTree& tree, const UiPaintContext& context, CanvasPainter& painter, CanvasDrawList& outCanvas );
        /** @brief 포커스 테두리를 @p widget 둘레에 칠합니다(지금 목록에 바로 — 캐시하지 않는다). 탐색 입력 방식일 때 `UiSystem` 이 부른다. */
        static void paintFocusRing( const Widget& widget, CanvasPainter& painter );
        /** @brief 위젯 기하(로컬 → 화면 UI 단위)를 칠하기 변환으로 바꿉니다. */
        static CanvasTransform makeWidgetTransform( const WidgetGeometry& geometry );

    private:
        /** @brief 위젯 하나와 그 아래를 칠합니다. @p bForce 면 이 위젯과 자손을 모두 다시 칠합니다. 다시 칠한 수를 돌려준다. */
        static uint32 paintWidget( Widget& widget, const UiPaintContext& context, CanvasPainter& painter, CanvasDrawList& outCanvas, bool bForce,
                                   bool bAtlasChanged );
        /** @brief 패널의 자식 하나 — 지금 자르기 밖에 통째로 있으면 걷지 않고(강제 칠하기는 비트로 남긴다) 아니면 `paintWidget`. */
        static uint32 paintChild( Widget& widget, const UiPaintContext& context, CanvasPainter& painter, CanvasDrawList& outCanvas, bool bForce,
                                  bool bAtlasChanged );
        /** @brief 위젯 하나의 캐시(@p outCache)를 다시 칠합니다 — 목록을 비우고 위젯 변환 아래에서 @p bOver 에 따라 `paint` 또는 `paintOverChildren`. */
        static void repaintCache( const Widget& widget, const UiPaintContext& context, CanvasPainter& painter, const CanvasDrawList& outCanvas,
                                  CanvasDrawList& outCache, bool bOver );
    };
} // namespace sw
