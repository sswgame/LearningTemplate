/**
 * @file EditorMultiEdit.h
 * @brief 여러 오브젝트를 한 번에 고치는 판단입니다(ImGui 없음 — EditorTest 가 시험합니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Editor/Common/EditorExports.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;

    class Component;
    class GameObject;
} // namespace sw

namespace sw::editor
{
    /** @brief 다중 편집의 공통 컴포넌트 하나입니다. 오브젝트마다 그 타입의 첫 컴포넌트를 선택 순서로 담습니다. */
    struct EditorMultiEditComponent
    {
        const TypeInfo*    _pType{ nullptr };
        vector<Component*> _listComponent; ///< 선택 순서 — 첫 원소가 주 선택의 것
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorMultiEditUtil
     * @brief 언리얼 · 유니티의 다중 선택 편집 판단입니다. 공통 컴포넌트만 그리고, 다른 값은 혼합으로 보이고, 고친 프로퍼티 하나만 나머지에 입힙니다.
     */
    struct SW_EDITOR_API EditorMultiEditUtil
    {
        /** @brief 모든 오브젝트가 가진 컴포넌트 타입을 주 선택의 컴포넌트 순서대로 모읍니다(먼저 비운다). 오브젝트가 하나면 그 오브젝트의 컴포넌트 전부입니다. */
        static void collectCommonComponents( const vector<GameObject*>& listObject, vector<EditorMultiEditComponent>& outListCommon );
        /** @brief 프로퍼티 @p prop 의 값이 @p listInstance 사이에서 다르면 true 입니다(프로퍼티 글로 비교). */
        static bool hasMixedValues( const PropertyInfo& prop, const vector<const void*>& listInstance );
        /**
         * @brief 첫 컴포넌트의 @p prop 값을 나머지에 입힙니다. 프로퍼티 글 하나만 옮기므로 다른 프로퍼티의 값은 그대로입니다. 입힌 수입니다.
         * @details 입힌 컴포넌트마다 `onPropertyChanged` 를 부릅니다. Undo 기록은 부르는 쪽(인스펙터)이 한 트랜잭션으로 감쌉니다.
         */
        static uint32 copyPropertyToOthers( const PropertyInfo& prop, const vector<Component*>& listComponent );
    };
} // namespace sw::editor
