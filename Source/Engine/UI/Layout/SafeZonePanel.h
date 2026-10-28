/**
 * @file SafeZonePanel.h
 * @brief 화면의 안전 영역 밖을 피해 자식을 놓는 패널입니다(UMG 의 SafeZone, 유니티의 Screen.safeArea 에 해당).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/WidgetTree/PanelWidget.h"

namespace sw
{
    /**
     * @class SafeZonePanel
     * @brief 자식들을 겹침 패널처럼 놓되, 뷰포트 안전 영역(`UILayoutContext::_safeInsets`)과 겹치는 만큼 안쪽으로 줄인 사각형에 놓습니다.
     * @details TV 가장자리나 휴대 기기 노치에 HUD 가 잘리지 않게 한다. PC 는 안전 영역이 0 이고, `gv_uiDebugSafeZone` 으로 흉내 낸다.
     *          화면 문서의 기본 루트는 `SafeZonePanel > CanvasPanel` 이다.
     */
    REFLECT( Category = "Layout", DisplayName = "Safe Zone Panel", Tooltip = "Keeps children inside the screen safe area" )
    class SW_API SafeZonePanel : public PanelWidget
    {
    public:
        REFLECT_BODY();

        SafeZonePanel();
        ~SafeZonePanel() override;

        const TypeInfo* getTypeInfo() const override;

    protected:
        float2 computeDesiredSize( const UILayoutContext& context, const float2& availableSize ) const override;
        void   arrangeChildren( const UILayoutContext& context, const float2& size ) override;
    };
} // namespace sw
