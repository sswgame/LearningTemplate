/**
 * @file OverlayPanel.h
 * @brief 자식을 겹쳐 놓는 패널입니다(UMG 의 Overlay, Godot 의 기본 Container 에 해당).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/WidgetTree/PanelWidget.h"

namespace sw
{
    /**
     * @class OverlayPanel
     * @brief 모든 자식이 패널 사각형 전체를 슬롯으로 받습니다. 자식 슬롯의 여백과 정렬이 그 안에 배치하며, 뒤 자식이 위에 그려집니다.
     * @details 원하는 크기는 자식들의 (원하는 크기 + 여백) 최댓값이다.
     */
    REFLECT( Category = "Layout", DisplayName = "Overlay Panel", Tooltip = "Stacks children on top of each other" )
    class SW_API OverlayPanel : public PanelWidget
    {
    public:
        REFLECT_BODY();

        OverlayPanel();
        ~OverlayPanel() override;

        const TypeInfo* getTypeInfo() const override;

    protected:
        float2 computeDesiredSize( const UILayoutContext& context, const float2& availableSize ) const override;
        void   arrangeChildren( const UILayoutContext& context, const float2& size ) override;
    };
} // namespace sw
