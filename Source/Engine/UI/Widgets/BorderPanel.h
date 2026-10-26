/**
 * @file BorderPanel.h
 * @brief 자식 하나를 배경, 테두리, 그림자, 안쪽 여백으로 감싸는 패널입니다(UMG 의 Border, Godot 의 PanelContainer 와 StyleBox 에 해당).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Widgets/UIBrush.h"

namespace sw
{
    /**
     * @class BorderPanel
     * @brief 배경 브러시(색, 테두리, 둥근 모서리)와 그림자 위에 첫 자식을 안쪽 여백만큼 들여 놓습니다. 버튼과 패널 바탕은 모두 이 클래스를 씁니다.
     * @details 원하는 크기는 자식 크기에 여백을 더한 값이다. 두 번째 자식부터는 배치하지 않는다. 파생 클래스(버튼)는 `selectBackgroundBrush` 로 상태별 브러시를 고른다.
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
        void           setBackground( const UIBrush& brush );
        const UIBrush& getBackground() const { return _background; }
        /** @brief 안쪽 여백(왼쪽, 위, 오른쪽, 아래, UI 단위)을 바꿉니다. kLayout 입니다. */
        void          setContentPadding( const float4& padding );
        const float4& getContentPadding() const { return _contentPadding; }
        /** @brief 그림자(색, 흐림, 밀림)를 바꿉니다. 알파가 0 이면 그리지 않습니다. kPaint 입니다. */
        void setShadow( const float4& color, float32 blur, const float2& offset );
        /** @brief 지금 쓰는 안쪽 여백입니다. 계산된 스타일이 `_padding` 을 정했으면 그 값이고, 아니면 자기 필드 값입니다. */
        float4 computeEffectivePadding() const;
        /** @brief 지금 칠할 배경입니다. 상태 브러시(`getBackgroundBrush`) 위에 계산된 스타일이 정한 필드(배경색, 모서리, 테두리)를 덧씌웁니다. */
        UIBrush computeEffectiveBrush() const;

    protected:
        float2 computeDesiredSize( const UILayoutContext& context, const float2& availableSize ) const override;
        void   arrangeChildren( const UILayoutContext& context, const float2& size ) override;
        void   paint( CanvasPainter& painter, const UIPaintContext& context ) const override;
        /** @brief 지금 칠할 배경입니다 — 지금 상태(`computeStyleStates`)의 `selectBackgroundBrush` 입니다. */
        const UIBrush& getBackgroundBrush() const { return selectBackgroundBrush( computeStyleStates() ); }
        /** @brief 상태 @p styleStates(`UIStyleState` 비트)에서 칠할 배경입니다. 기본은 `_background` 이며, 버튼은 호버 · 누름 · 꺼짐 브러시를 고릅니다. */
        virtual const UIBrush& selectBackgroundBrush( [[maybe_unused]] uint32 styleStates ) const { return _background; }
        /** @brief 상태 @p styleStates 의 배경 브러시 색 · 모서리 · 테두리, 여백, 그림자를 자기 값으로 씁니다(스타일 전환의 "한쪽만 정한 필드"). */
        void fillOwnStyle( uint32 styleStates, UIOwnStyle& outStyle ) const override;

    private:
        PROPERTY( DisplayName = "Background" )
        UIBrush _background;
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
