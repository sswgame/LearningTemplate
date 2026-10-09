/**
 * @file InspectorPropertyManager.h
 * @brief 프로퍼티 타입명 → IInspectorProperty (EditorContext 소유)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"
#include "Core/String/RegistrationList.h"

namespace sw::editor
{
    struct InspectorMethodArgSlot;

    class IInspectorProperty;

    /** @brief 타입별 인스펙터 프로퍼티 UI 관리자 (EditorContext 소유) */
    class InspectorPropertyManager
    {
    public:
        InspectorPropertyManager()  = default;
        ~InspectorPropertyManager() = default;

        /** @brief 타입 이름에 위젯을 겁니다. 같은 이름이 있으면 바꿉니다. */
        void                registerType( string_view typeName, unique_ptr<IInspectorProperty> pProperty );
        IInspectorProperty* find( string_view typeName ) const;
        /** @brief `ReflectBuiltins.xxx` 의 내장 타입마다 위젯을 등록합니다(`InspectorWidgetFor<T>` 가 갈래를 정합니다). */
        void registerDefaults();

        /** @brief CallInEditor 인자 칸 하나를 그 C++ 타입의 위젯으로 그립니다. 값이 바뀌었으면 true 입니다. */
        static bool drawMethodArg( const utf8* pLabel, InspectorMethodArgSlot& slot );

    private:
        NameRegistry<unique_ptr<IInspectorProperty>> _registry; ///< 프로퍼티 타입 이름 → 위젯
    };
} // namespace sw::editor
