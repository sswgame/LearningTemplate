#include "pch.h"

#include "Engine/Curve/FloatCurve.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    /**
     * @class EditorCurveTestComponent
     * @brief `FloatCurve` 프로퍼티 하나를 가진 테스트 컴포넌트입니다. 시나리오 `curveedit` 가 개발 명령 `component.add` 로 달아 인스펙터의 커브 편집기를 누릅니다.
     * @details 에디터 안에만 있고 "Add Component" 메뉴에 나오지 않는다. 기본 커브는 키 둘((0, 0) · (1, 1))입니다.
     *          다른 파일이 이 형식을 쓰지 않아 헤더 없이 여기 둔다. 리플렉션 등록은 파일 끝의 `EditorCurveTestComponent.cpp.gen.inl` 이 한다.
     */
    REFLECT( HideInMenu, DisplayName = "Curve Probe", Tooltip = "Automation: a curve property the curve editor scenario edits" )
    class EditorCurveTestComponent : public Component
    {
    public:
        REFLECT_BODY();
        EditorCurveTestComponent();
        virtual ~EditorCurveTestComponent() override = default;

        EditorCurveTestComponent( const EditorCurveTestComponent& )            = delete;
        EditorCurveTestComponent& operator=( const EditorCurveTestComponent& ) = delete;

    private:
        PROPERTY( Tooltip = "Curve the scenario edits" )
        FloatCurve _curve;
    };

    EditorCurveTestComponent::EditorCurveTestComponent()
        : _curve{}
    {
        _curve.addKey( 0.0f, 0.0f );
        _curve.addKey( 1.0f, 1.0f );
    }
} // namespace sw::editor

#include "EditorCurveTestComponent.cpp.gen.inl"
