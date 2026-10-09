/**
 * @file WrapPanel.h
 * @brief 줄이 차면 다음 줄로 넘기는 흐름 패널입니다(유니티 flex-wrap: wrap · Godot FlowContainer · WPF WrapPanel).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Layout/WidgetLayoutSlot.h"

namespace sw
{
    /**
     * @class WrapPanel
     * @brief 자식을 주축으로 늘어놓다가 남은 길이에 다음 자식이 안 들어가면 다음 줄로 넘깁니다.
     * @details 자식은 원하는 크기를 그대로 받고, 줄 높이(교차축)는 그 줄 자식들의 최대입니다 — 자식 슬롯의 교차축 정렬이 그 줄 안에 놓습니다.
     *          원하는 크기는 가용 주축 길이에 기대므로 measure 가 가용 길이로 줄을 나눕니다(무한이면 한 줄).
     */
    REFLECT( Category = "Layout", DisplayName = "Wrap Panel", Tooltip = "Flows children in rows and wraps to the next row when full" )
    class SW_API WrapPanel : public PanelWidget
    {
    public:
        REFLECT_BODY();

        WrapPanel();
        ~WrapPanel() override;

        const TypeInfo* getTypeInfo() const override;

        UiOrientation getOrientation() const { return _orientation; }
        /** @brief 흐름 방향을 바꿉니다(가로면 줄이 아래로 쌓인다). kLayout. */
        void    setOrientation( UiOrientation orientation );
        float32 getItemSpacing() const { return _itemSpacing; }
        /** @brief 한 줄 안 자식 사이 간격입니다. kLayout. */
        void    setItemSpacing( float32 itemSpacing );
        float32 getLineSpacing() const { return _lineSpacing; }
        /** @brief 줄 사이 간격입니다. kLayout. */
        void setLineSpacing( float32 lineSpacing );
        bool isChildOrderTopToBottom() const override { return _orientation == UiOrientation::Horizontal && _lineSpacing >= 0.0f; }

    protected:
        float2 computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const override;
        void   arrangeChildren( const UiLayoutContext& context, const float2& size ) override;

    private:
        PROPERTY( DisplayName = "Item Spacing", Meta = "Units=ui" )
        float32 _itemSpacing;
        PROPERTY( DisplayName = "Line Spacing", Meta = "Units=ui" )
        float32 _lineSpacing;
        PROPERTY( DisplayName = "Orientation" )
        UiOrientation _orientation;
    };
} // namespace sw
