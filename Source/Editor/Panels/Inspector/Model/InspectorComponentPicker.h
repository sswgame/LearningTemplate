/**
 * @file InspectorComponentPicker.h
 * @brief 타입 핸들(`TComponentHandle<T>`) 프로퍼티의 선택기가 쓰는 판단입니다. ImGui 에 의존하지 않으므로 테스트를 붙일 수 있습니다.
 * @details 선택기가 필드에 쓰는 값은 **열거한 컴포넌트의 `getHandle()`** 뿐이라 대상 타입이 틀린 값이 형식상 들어갈 수 없다.
 *          그리는 쪽은 `InspectorPropertyManager.cpp` 의 `ComponentHandleProperty` 이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;

    class Component;
    class GameObjectManager;
    class TypeRegistry;
} // namespace sw

namespace sw::editor
{
    /** @brief 선택기 목록의 항목 하나입니다. */
    struct InspectorComponentChoice
    {
        ComponentHandle _handle; ///< 열거한 컴포넌트의 `getHandle()`. 다른 길로 만들지 않는다
        string          _label;  ///< "오브젝트 이름 / 컴포넌트 키(이름#n)"
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 필드에 있는 현재 값의 상태입니다. 미리보기 글과 색을 정합니다. */
    enum class InspectorHandleState : uint8
    {
        Empty,     ///< 무효 핸들 — "대상 없음", 정상 값
        Valid,     ///< 대상이 있고 대상 타입이거나 그 파생
        Missing,   ///< id 는 있지만 찾지 못한다(파괴됨 · 삭제 대기). 약한 참조의 정상 값
        WrongType, ///< 대상이 있지만 대상 타입이 아니다(손으로 고친 파일, 옛 값). 오류
    };

    struct InspectorComponentPickerUtil
    {
        /** @brief 필드의 대상 타입입니다. 타입 핸들이 아니거나(`_handleTargetTypeName` 이 빔) 레지스트리에 없으면 nullptr 입니다. */
        static const TypeInfo* findTargetType( const TypeRegistry& registry, const PropertyInfo& prop );
        /** @brief 대상 타입 이름(`PropertyInfo` · `FunctionParameterInfo` 의 `_handleTargetTypeName`)의 타입입니다. 비었거나 레지스트리에 없으면 nullptr 입니다. */
        static const TypeInfo* findTargetType( const TypeRegistry& registry, const hashed_string& targetTypeName );
        /** @brief @p manager 에서 @p targetType 이나 그 파생 컴포넌트를 모아 라벨 순으로 정렬해 채웁니다. 비우고 채웁니다. */
        static void collectChoices( const GameObjectManager& manager, const TypeInfo& targetType, vector<InspectorComponentChoice>& outListChoice );
        /** @brief 컴포넌트 하나의 라벨("오브젝트 이름 / 이름#n")입니다. */
        static string makeLabel( const Component& component );
        /** @brief @p handle 의 상태입니다. */
        static InspectorHandleState computeHandleState( const GameObjectManager& manager, const TypeInfo& targetType, ComponentHandle handle );
    };
} // namespace sw::editor
