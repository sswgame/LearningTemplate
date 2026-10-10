/**
 * @file EditorCurveProbeComponent.h
 * @brief 커브 편집기 시나리오가 다는 시험 컴포넌트입니다(에디터 안에만 있고 "Add Component" 메뉴에 나오지 않는다).
 */
#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Utility/FloatCurve.h"

namespace sw::editor
{
    /**
     * @class EditorCurveProbeComponent
     * @brief `FloatCurve` 프로퍼티 하나를 든 컴포넌트입니다. 시나리오 `curveedit` 가 개발 명령 `component.add` 로 달아 인스펙터의 커브 편집기를 누릅니다.
     * @details 기본 커브는 키 둘((0, 0) · (1, 1))입니다.
     */
    REFLECT( HideInMenu, DisplayName = "Curve Probe", Tooltip = "Automation: a curve property the curve editor scenario edits" )
    class EditorCurveProbeComponent : public Component
    {
    public:
        REFLECT_BODY();
        EditorCurveProbeComponent();
        virtual ~EditorCurveProbeComponent() override = default;

        EditorCurveProbeComponent( const EditorCurveProbeComponent& )            = delete;
        EditorCurveProbeComponent& operator=( const EditorCurveProbeComponent& ) = delete;

    private:
        PROPERTY( Tooltip = "Curve the scenario edits" )
        FloatCurve _curve;
    };
} // namespace sw::editor
