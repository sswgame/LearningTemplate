/**
 * @file UIPaintPass.h
 * @brief 그리기 순회를 정의합니다. 그리기 더티인 위젯만 자기 그리기 캐시를 다시 칠하고, 프레임 목록은 그리기 순서로 캐시를 이어 붙입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/Canvas/CanvasPainter.h"

namespace sw
{
    struct UIActionGlyphSource;
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
     * @struct UIPaintContext
     * @brief 위젯이 칠할 때 필요한 것입니다. 텍스트 배치, 글리프 캐시, 프레임 번호, 배율이 있습니다.
     */
    struct UIPaintContext
    {
        TextLayoutEngine*          _pTextLayout{ nullptr };   ///< 텍스트 배치입니다(없으면 텍스트 위젯은 칠하지 않습니다).
        GlyphCache*                _pGlyphCache{ nullptr };   ///< 글리프 아틀라스입니다(없으면 텍스트 위젯은 칠하지 않습니다).
        uint64                     _frameIndex{ 0 };          ///< 글리프 캐시의 최근 사용 프레임입니다.
        float32                    _uiScale{ 1.0f };          ///< UI 단위에서 물리 픽셀로의 배율입니다.
        float32                    _textScale{ 1.0f };        ///< 글자 배율(gv_uiTextScale)입니다. 레이아웃 컨텍스트와 같은 값입니다.
        uint32                     _atlasGeneration{ 0 };     ///< 글리프 아틀라스 세대입니다. 바뀌면 텍스트 위젯을 다시 칠합니다.
        const LocalizationManager* _pLocalization{ nullptr }; ///< 텍스트 위젯이 키를 해석하는 문화권입니다(레이아웃 컨텍스트와 같은 값).
        uint32                     _textRevision{ 0 };        ///< 그 문화권의 텍스트 버전입니다.
        const UIActionGlyphSource* _pActionGlyphs{ nullptr }; ///< `[action=이름]` 태그의 글리프 출처입니다(레이아웃 컨텍스트와 같은 값).
    };
} // namespace sw

namespace sw
{
    /** @brief 위젯 하나의 그리기 캐시입니다(물리 픽셀 사각형이며, 조상의 자르기와 불투명도가 반영되어 있습니다). */
    struct WidgetPaintCache
    {
        CanvasDrawList _under{}; ///< 자식 아래에 그리는 목록입니다(`Widget::paint`).
        CanvasDrawList _over{};  ///< 자식 위에 그리는 목록입니다(`Widget::paintOverChildren`).
    };
} // namespace sw

namespace sw
{
    /**
     * @class UIPaintPass
     * @brief 트리 하나를 그리기 목록으로 냅니다(언리얼 Slate 의 캐시된 요소 목록과 전역 무효화, 유니티 UIR 의 더티 요소 다시 칠하기에 해당).
     * @details 그리기 순서(자식 순서이며, z 순서 패널은 `collectPaintOrder`)로 트리를 순회하면서 위젯마다 두 가지를 한다.
     *          (1) 그리기 더티이면 자기 캐시를 다시 칠한다. 조건은 `kPaint`, `kStyle`, `kVisibility`, 처음 그리는 경우, 조상의 `kTransform`(조상의 불투명도와 자르기가 캐시에 반영되어 있다),
     *          배율 변화, 텍스트 위젯의 아틀라스 세대 변화다.
     *          (2) 캐시를 프레임 목록에 이어 붙인다. 자르는 패널은 자기 사각형을 가위 영역으로 쌓는다. 순회가 끝나면 그리기와 스타일 무효화를 비운다
     *          (스타일 순회(5-2) 전까지 `kStyle` 은 그 위젯을 다시 칠하라는 뜻이다). Collapsed 와 Hidden 위젯, 그 아래는 칠하지 않는다. 게임 스레드에서만 쓴다.
     */
    class SW_API UIPaintPass
    {
    public:
        /**
         * @brief @p tree 를 칠해 @p outCanvas 뒤에 이어 붙입니다. @p painter 는 칠할 목록을 위젯 캐시마다 바꿔 쓰고, 끝나면 @p outCanvas 로 되돌려 놓습니다.
         * @return 이번에 다시 칠한 위젯 수입니다(`Widget::paint` 호출 수이며 프로파일 카운터 UI.PaintWidgets).
         */
        static uint32 paint( WidgetTree& tree, const UIPaintContext& context, CanvasPainter& painter, CanvasDrawList& outCanvas );
        /** @brief 포커스 테두리를 @p widget 둘레에 칠합니다(캐시하지 않고 지금 목록에 바로 칠합니다). 탐색 입력 방식일 때 `UISystem` 이 부릅니다. */
        static void paintFocusRing( const Widget& widget, CanvasPainter& painter );
        /** @brief 위젯 기하(로컬에서 화면 UI 단위)를 칠하기 변환으로 바꿉니다. */
        static CanvasTransform makeWidgetTransform( const WidgetGeometry& geometry );
        /** @brief 이 스레드의 그리기 순서 스크래치를 비웁니다. 그리는 스레드가 끝나기 전(`UISystem::shutdown`)에 부릅니다. */
        static void releaseScratch();

    private:
        /** @brief 위젯 하나와 그 아래를 칠합니다. @p bForce 면 이 위젯과 자손을 모두 다시 칠하며, 다시 칠한 수를 반환합니다. */
        static uint32 paintWidget( Widget& widget, const UIPaintContext& context, CanvasPainter& painter, CanvasDrawList& outCanvas, bool bForce,
                                   bool bAtlasChanged );
        /** @brief 패널의 자식 하나를 처리합니다. 지금 자르기 영역 밖에 통째로 있으면 순회하지 않고(강제 칠하기는 비트로 남깁니다), 아니면 `paintWidget` 을 부릅니다. */
        static uint32 paintChild( Widget& widget, const UIPaintContext& context, CanvasPainter& painter, CanvasDrawList& outCanvas, bool bForce,
                                  bool bAtlasChanged );
        /** @brief 위젯 하나의 캐시(@p outCache)를 다시 칠합니다. 목록을 비우고 위젯 변환 아래에서 @p bOver 에 따라 `paint` 또는 `paintOverChildren` 을 부릅니다. */
        static void repaintCache( const Widget& widget, const UIPaintContext& context, CanvasPainter& painter, const CanvasDrawList& outCanvas,
                                  CanvasDrawList& outCache, bool bOver );
    };
} // namespace sw
