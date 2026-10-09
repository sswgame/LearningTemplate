/**
 * @file BorderPanel.h
 * @brief 자식 하나를 배경 · 테두리 · 그림자 · 안쪽 여백으로 감싸는 패널입니다(UMG Border · Godot PanelContainer + StyleBox).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Widgets/UiBrush.h"

namespace sw
{
    /**
     * @class BorderPanel
     * @brief 배경 브러시(색 · 테두리 · 둥근 모서리)와 그림자 위에 첫 자식을 안쪽 여백만큼 들여 놓습니다. 버튼 · 패널 바탕은 모두 이것입니다.
     * @details 원하는 크기 = 자식 + 여백. 두 번째 자식부터는 놓지 않습니다. 파생(버튼)은 `getBackgroundBrush` 로 상태별 브러시를 고릅니다.
     */
    REFLECT( Category = "UI", DisplayName = "Border", Tooltip = "Background, border and padding around one child" )
    class SW_API BorderPanel : public PanelWidget
    {
    public:
        REFLECT_BODY();

        BorderPanel();
        ~BorderPanel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 배경 브러시를 바꿉니다. kPaint. */
        void           setBackground( const UiBrush& brush );
        const UiBrush& getBackground() const { return _background; }
        /** @brief 안쪽 여백(왼 · 위 · 오른 · 아래, UI 단위)을 바꿉니다. kLayout. */
        void          setContentPadding( const float4& padding );
        const float4& getContentPadding() const { return _contentPadding; }
        /** @brief 그림자(색 · 흐림 · 밀림)를 바꿉니다. 알파 0 이면 그리지 않는다. kPaint. */
        void setShadow( const float4& color, float32 blur, const float2& offset );
        /** @brief 지금 쓰는 안쪽 여백입니다 — 계산된 스타일이 `_padding` 을 정했으면 그것, 아니면 자기 칸. */
        float4 computeEffectivePadding() const;
        /** @brief 지금 칠할 배경입니다 — 상태 브러시(`getBackgroundBrush`) 위에 계산된 스타일이 정한 칸(배경색 · 모서리 · 테두리)을 얹는다. */
        UiBrush computeEffectiveBrush() const;

    protected:
        float2 computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const override;
        void   arrangeChildren( const UiLayoutContext& context, const float2& size ) override;
        void   paint( CanvasPainter& painter, const UiPaintContext& context ) const override;
        /** @brief 지금 칠할 배경입니다. 기본은 `_background` — 버튼은 상태(호버 · 누름 · 꺼짐)별 브러시를 돌려준다. */
        virtual const UiBrush& getBackgroundBrush() const { return _background; }

    private:
        PROPERTY( DisplayName = "Background" )
        UiBrush _background;
        PROPERTY( DisplayName = "Content Padding", Tooltip = "Left, top, right, bottom", Meta = "Units=ui" )
        float4 _contentPadding;
        PROPERTY( DisplayName = "Shadow Color" )
        float4 _shadowColor;
        PROPERTY( DisplayName = "Shadow Offset", Meta = "Units=ui" )
        float2 _shadowOffset;
        PROPERTY( DisplayName = "Shadow Blur", Min = 0.0, Meta = "Units=ui" )
        float32 _shadowBlur;
    };
} // namespace sw
