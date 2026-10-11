/**
 * @file BoxPanel.h
 * @brief 가로와 세로 상자 패널입니다(UMG 의 HorizontalBox 와 VerticalBox, Godot 의 HBox 와 VBox, 유니티의 flex-direction 에 해당).
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
     * @details 주축에서는 Auto 자식이 원하는 크기만큼 차지하고, 남은 주축은 Fill 자식에게 채우기 비율(`_fillWeight`)대로 나눈다. 교차축에서는 패널 크기 전체가 슬롯이고
     *          자식 슬롯의 정렬이 그 안에 배치한다. measure 는 Auto 자식을 주축 무한으로 측정한 뒤 남은 주축을 Fill 자식 몫으로 나눠 그 크기로 측정한다.
     *          그래서 줄 바꿈 텍스트가 Fill 슬롯에 들어가도 같은 프레임에 높이가 맞는다.
     */
    REFLECT( Category = "Layout", DisplayName = "Box Panel", Tooltip = "Lays children out in a row or a column" )
    class SW_API BoxPanel : public PanelWidget
    {
    public:
        REFLECT_BODY();

        BoxPanel();
        ~BoxPanel() override;

        const TypeInfo* getTypeInfo() const override;

        UIOrientation getOrientation() const { return _orientation; }
        /** @brief 주축 방향을 바꿉니다. 바뀌면 kLayout. */
        void    setOrientation( UIOrientation orientation );
        float32 getSpacing() const { return _spacing; }
        /** @brief 자식 사이 간격(UI 단위)을 바꿉니다. 바뀌면 kLayout. */
        void setSpacing( float32 spacing );
        bool isChildOrderTopToBottom() const override { return _orientation == UIOrientation::Vertical && _spacing >= 0.0f; }

    protected:
        float2 computeDesiredSize( const UILayoutContext& context, const float2& availableSize ) const override;
        void   arrangeChildren( const UILayoutContext& context, const float2& size ) override;

    private:
        PROPERTY( DisplayName = "Spacing", Meta = "Units=ui" )
        float32 _spacing;
        PROPERTY( DisplayName = "Orientation" )
        UIOrientation _orientation;
    };
} // namespace sw
