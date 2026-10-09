/**
 * @file BoxPanel.h
 * @brief 가로 · 세로 상자 패널입니다(UMG HorizontalBox/VerticalBox · Godot HBox/VBox · 유니티 flex-direction).
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
     * @class BoxPanel
     * @brief 자식을 한 줄(가로) 또는 한 열(세로)로 놓습니다.
     * @details 주축: Auto 자식은 원하는 크기만큼, 남은 주축은 Fill 자식에게 채우기 비(`_fillWeight`)대로 나눕니다. 교차축: 패널 크기 전체가 슬롯이고
     *          자식 슬롯의 정렬이 그 안에 놓습니다. measure 는 Auto 자식을 주축 무한으로 잰 뒤 남은 주축을 Fill 자식 몫으로 나눠 그 크기로 잽니다 —
     *          줄 바꿈 글이 Fill 칸에 들어가도 같은 프레임에 높이가 맞습니다.
     */
    REFLECT( Category = "Layout", DisplayName = "Box Panel", Tooltip = "Lays children out in a row or a column" )
    class SW_API BoxPanel : public PanelWidget
    {
    public:
        REFLECT_BODY();

        BoxPanel();
        ~BoxPanel() override;

        const TypeInfo* getTypeInfo() const override;

        UiOrientation getOrientation() const { return _orientation; }
        /** @brief 주축 방향을 바꿉니다. 바뀌면 kLayout. */
        void    setOrientation( UiOrientation orientation );
        float32 getSpacing() const { return _spacing; }
        /** @brief 자식 사이 간격(UI 단위)을 바꿉니다. 바뀌면 kLayout. */
        void setSpacing( float32 spacing );
        bool isChildOrderTopToBottom() const override { return _orientation == UiOrientation::Vertical && _spacing >= 0.0f; }

    protected:
        float2 computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const override;
        void   arrangeChildren( const UiLayoutContext& context, const float2& size ) override;

    private:
        PROPERTY( DisplayName = "Spacing", Meta = "Units=ui" )
        float32 _spacing;
        PROPERTY( DisplayName = "Orientation" )
        UiOrientation _orientation;
    };
} // namespace sw
