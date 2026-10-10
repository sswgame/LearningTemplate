/**
 * @file ScrollPanel.h
 * @brief 내용 하나를 스크롤 축으로 원하는 만큼 재서 오프셋만큼 밀어 보여 주는 패널입니다(UMG ScrollBox · Godot ScrollContainer · 유니티 ScrollView).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Base/PanelWidget.h"

namespace sw
{
    /**
     * @class ScrollPanel
     * @brief 첫 자식(내용)을 스크롤 축으로는 무한 가용 크기로 재고, `-오프셋` 자리에 놓고 자릅니다(`clipsChildren`).
     * @details 오프셋은 `[0, 내용 − 보이는 크기]` 로 묶입니다. 오프셋이 바뀌면 `kArrange` 만 — 크기는 그대로라 위로 번지지 않고 이 패널만 다시 놓습니다
     *          (measure 0). 입력: 휠(버블 — 안쪽 스크롤이 끝에 닿으면 바깥으로 간다) · 막대 끌기(터널 — 막대 위 누름은 내용보다 먼저, 포인터를 잡는다) ·
     *          패드 오른쪽 스틱(`UI.Scroll` 행동 — 포커스 경로) · 포커스 탐색 뒤 `scrollIntoView`(`UIFocusManager::navigate`).
     *          막대는 넘치는 스크롤 축의 안쪽 끝(세로는 오른쪽 · 가로는 아래)에 겹쳐 그립니다. 두 번째 자식부터는 놓지 않습니다.
     */
    REFLECT( Category = "Layout", DisplayName = "Scroll Panel", Tooltip = "Scrolls one content child inside a clipped viewport" )
    class SW_API ScrollPanel : public PanelWidget
    {
    public:
        REFLECT_BODY();

        /** @brief 스크롤 막대 하나의 사각형입니다(패널 로컬, UI 단위). */
        struct ScrollBarLayout
        {
            float2 _trackPosition{};
            float2 _trackSize{};
            float2 _thumbPosition{};
            float2 _thumbSize{};
        };

        ScrollPanel();
        ~ScrollPanel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 스크롤 축을 정합니다(기본 세로만). kLayout. */
        void setScrollAxes( bool bHorizontal, bool bVertical );
        bool isScrollHorizontal() const { return _bScrollHorizontal; }
        bool isScrollVertical() const { return _bScrollVertical; }

        const float2& getScrollOffset() const { return _scrollOffset; }
        /** @brief 오프셋을 바꿉니다 — 지난 배치의 `[0, 최대]` 로 묶고, 바뀌면 kArrange. 스크롤 축이 아닌 성분은 0 입니다. */
        void setScrollOffset( const float2& scrollOffset );
        /** @brief 오프셋에 @p delta 를 더합니다(휠 · 스틱). */
        void scrollBy( const float2& delta );
        /** @brief 지난 배치의 최대 오프셋(내용 − 보이는 크기, 음수는 0)입니다. */
        float2 getMaxScrollOffset() const;
        /** @brief 지난 배치의 내용 크기(여백 포함)입니다. */
        const float2& getContentSize() const { return _contentSize; }

        /**
         * @brief @p widget(이 패널의 자손)의 사각형이 보이도록 오프셋을 최소한만 옮깁니다(포커스 이동 — 가장자리에서 `_navigationMargin` 만큼 띄운다).
         * @details 지난 배치의 기하로 셉니다(회전 없는 축). 옮겼으면 true, 이미 보이거나 자손이 아니면 false 입니다.
         */
        bool scrollIntoView( const Widget& widget ) override;
        bool canScrollIntoView() const override { return _bScrollHorizontal || _bScrollVertical; }

        /**
         * @brief @p axis 의 스크롤 막대 사각형을 셉니다(지난 배치 기준). 그 축으로 스크롤하지 않거나 넘치지 않으면 막대가 없어 false 입니다.
         * @details 엄지 길이 = 트랙 × 보이는 크기 / 내용 크기(최소 `_minThumbLength`), 자리 = (트랙 − 엄지) × 오프셋 / 최대 오프셋.
         */
        [[nodiscard]] bool computeScrollBar( UIOrientation axis, ScrollBarLayout& outLayout ) const;
        /** @brief 막대를 끄는 중이면 true 입니다. */
        bool isDraggingScrollBar() const { return _bDraggingBar; }

        UIReply onPointerEvent( const UIPointerEvent& event, UIRoutePhase phase ) override;
        UIReply onActionEvent( const UIActionEvent& event, UIRoutePhase phase ) override;

        float32 getNavigationMargin() const { return _navigationMargin; }
        void    setNavigationMargin( float32 navigationMargin ) { _navigationMargin = navigationMargin; }

    protected:
        float2 computeDesiredSize( const UILayoutContext& context, const float2& availableSize ) const override;
        void   arrangeChildren( const UILayoutContext& context, const float2& size ) override;
        /** @brief 스크롤 막대 엄지를 내용 위에 칠합니다(끄는 중이면 더 밝게). */
        void paintOverChildren( CanvasPainter& painter, const UIPaintContext& context ) const override;
        /** @brief 내용 자리는 스크롤 오프셋이다 — 거울로 놓지 않는다(내용 안의 패널은 자기 방향으로 거울한다). 가로 스크롤의 RTL 시작점은 왼쪽이다. */
        bool mirrorsChildrenInRightToLeft() const override { return false; }

    private:
        /** @brief 오프셋을 지난 배치의 범위로 묶습니다. */
        float2 clampOffset( const float2& scrollOffset ) const;
        /** @brief 휠 · 스틱 스크롤을 오프셋 변화로 적용합니다. 옮겼으면 true(안 옮겼으면 바깥 스크롤 패널이 받는다). */
        [[nodiscard]] bool applyScrollDelta( const float2& delta );
        /** @brief 막대 위 누름 — 엄지면 끌기를 시작하고(포인터를 잡는다), 트랙이면 한 화면만큼 옮깁니다. 막대 밖이면 처리하지 않습니다. */
        UIReply beginScrollBarDrag( const float2& local );

    private:
        float2        _scrollOffset;    ///< 지금 오프셋(UI 단위, 내용이 위 · 왼쪽으로 밀린 길이)
        float2        _contentSize;     ///< 지난 배치의 내용 크기
        float2        _viewportSize;    ///< 지난 배치의 보이는 크기(이 패널 크기)
        float2        _dragStartLocal;  ///< 막대 끌기를 시작한 로컬 점
        float32       _dragStartOffset; ///< 막대 끌기를 시작할 때 그 축의 오프셋
        UIOrientation _dragAxis;        ///< 끄는 막대의 축
        bool          _bDraggingBar;    ///< 막대를 끄는 중이다(포인터를 잡았다)
        PROPERTY( DisplayName = "Navigation Margin", Tooltip = "Gap kept between a focused child and the viewport edge", Meta = "Units=ui" )
        float32 _navigationMargin;
        PROPERTY( DisplayName = "Wheel Step", Tooltip = "Scroll distance per mouse wheel notch", Min = 0.0, Meta = "Units=ui" )
        float32 _wheelStep;
        PROPERTY( DisplayName = "Stick Speed", Tooltip = "Scroll speed at full right stick tilt (UI.Scroll), per second", Min = 0.0, Meta = "Units=ui" )
        float32 _stickSpeed;
        PROPERTY( DisplayName = "Scroll Bar Thickness", Min = 0.0, Meta = "Units=ui" )
        float32 _scrollBarThickness;
        PROPERTY( DisplayName = "Min Thumb Length", Min = 0.0, Meta = "Units=ui" )
        float32 _minThumbLength;
        PROPERTY( DisplayName = "Scroll Horizontal" )
        bool _bScrollHorizontal;
        PROPERTY( DisplayName = "Scroll Vertical" )
        bool _bScrollVertical;
    };
} // namespace sw
