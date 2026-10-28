/**
 * @file EditorSceneReferenceTestComponent.h
 * @brief 씬 오브젝트 참조 선택기 시나리오가 다는 테스트 컴포넌트입니다(에디터 안에만 있고 "Add Component" 메뉴에 나오지 않는다).
 */
#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Scene/SceneObjectReference.h"

namespace sw::editor
{
    /**
     * @class EditorSceneReferenceTestComponent
     * @brief `SceneObjectReference` 프로퍼티 하나를 가진 컴포넌트입니다. 시나리오 `scenereference` 가 개발 명령 `component.add` 로 달아 인스펙터의 선택기를 누릅니다.
     */
    REFLECT( HideInMenu, DisplayName = "Scene Reference Probe", Tooltip = "Automation: a scene object reference the picker scenario edits" )
    class EditorSceneReferenceTestComponent : public Component
    {
    public:
        REFLECT_BODY();
        EditorSceneReferenceTestComponent()           = default;
        ~EditorSceneReferenceTestComponent() override = default;

        EditorSceneReferenceTestComponent( const EditorSceneReferenceTestComponent& )            = delete;
        EditorSceneReferenceTestComponent& operator=( const EditorSceneReferenceTestComponent& ) = delete;

    private:
        PROPERTY( Tooltip = "Reference the scenario picks" )
        SceneObjectReference _target;
    };
} // namespace sw::editor
