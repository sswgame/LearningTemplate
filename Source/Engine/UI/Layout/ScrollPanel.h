/**
 * @file ScrollPanel.h
 * @brief 내용 하나를 스크롤 축으로 원하는 만큼 재서 오프셋만큼 밀어 보여 주는 패널입니다(UMG ScrollBox · Godot ScrollContainer · 유니티 ScrollView).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Core/PanelWidget.h"

namespace sw
{
    /**
     * @class ScrollPanel
     * @brief 첫 자식(내용)을 스크롤 축으로는 무한 가용 크기로 재고, `-오프셋` 자리에 놓고 자릅니다(`clipsChildren`).
     * @details 오프셋은 `[0, 내용 − 보이는 크기]` 로 묶입니다. 오프셋이 바뀌면 `kArrange` 만 — 크기는 그대로라 위로 번지지 않고 이 패널만 다시 놓습니다
     *          (measure 0). 휠 · 막대 끌기 · 패드 오른쪽 스틱(`UI.Scroll`)은 사건 경로(2-2 · 2-4)가 `scrollBy` · `setScrollOffset` 을 부르게 연결합니다.
     *          두 번째 자식부터는 놓지 않습니다.
     */
    REFLECT( Category = "Layout", DisplayName = "Scroll Panel", Tooltip = "Scrolls one content child inside a clipped viewport" )
    class SW_API ScrollPanel : public PanelWidget
    {
    public:
        REFLECT_BODY();

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
        bool scrollIntoView( const Widget& widget );

        float32 getNavigationMargin() const { return _navigationMargin; }
        void    setNavigationMargin( float32 navigationMargin ) { _navigationMargin = navigationMargin; }

    protected:
        float2 computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const override;
        void   arrangeChildren( const UiLayoutContext& context, const float2& size ) override;

    private:
        /** @brief 오프셋을 지난 배치의 범위로 묶습니다. */
        float2 clampOffset( const float2& scrollOffset ) const;

    private:
        float2 _scrollOffset; ///< 지금 오프셋(UI 단위, 내용이 위 · 왼쪽으로 밀린 길이)
        float2 _contentSize;  ///< 지난 배치의 내용 크기
        float2 _viewportSize; ///< 지난 배치의 보이는 크기(이 패널 크기)
        PROPERTY( DisplayName = "Navigation Margin", Tooltip = "Gap kept between a focused child and the viewport edge", Meta = "Units=ui" )
        float32 _navigationMargin;
        PROPERTY( DisplayName = "Scroll Horizontal" )
        bool _bScrollHorizontal;
        PROPERTY( DisplayName = "Scroll Vertical" )
        bool _bScrollVertical;
    };
} // namespace sw
