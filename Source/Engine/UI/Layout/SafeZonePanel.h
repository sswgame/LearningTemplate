/**
 * @file SafeZonePanel.h
 * @brief 화면의 안전 영역 밖을 피해 자식을 놓는 패널입니다(UMG SafeZone · 유니티 Screen.safeArea).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Core/PanelWidget.h"

namespace sw
{
    /**
     * @class SafeZonePanel
     * @brief 자식들을 겹침 패널처럼 놓되, 뷰포트 안전 영역(`UiLayoutContext::_safeInsets`)과 겹치는 만큼 안쪽으로 줄인 사각형에 놓습니다.
     * @details TV 가장자리 · 휴대 기기 노치에 HUD 가 잘리지 않게 합니다. PC 는 안전 영역이 0 이고, `gv_uiDebugSafeZone` 으로 흉내 냅니다.
     *          화면 문서의 기본 루트는 `SafeZonePanel > CanvasPanel` 입니다.
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
        float2 computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const override;
        void   arrangeChildren( const UiLayoutContext& context, const float2& size ) override;
    };
} // namespace sw
