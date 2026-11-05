/**
 * @file InspectorMultiSelection.h
 * @brief 인스펙터가 여러 오브젝트를 고른 상태에서 화면 없이 정하는 일입니다(ImGui 없음). 그리기는 `InspectorPanel::drawMultiSelection` 입니다.
 * @details 공통 컴포넌트 모으기와 프로퍼티 입히기는 `EditorMultiEditComponent` 가 맡고, 여기는 그 위의 활성 칸 · 공통 아닌 컴포넌트 수 · 혼합 판정입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"

namespace sw
{
    struct PropertyInfo;

    class Component;
    class GameObject;
} // namespace sw

namespace sw::editor
{
    struct EditorMultiEditComponent;

    /** @brief 다중 선택 판단 묶음입니다. @p listObject 는 선택 순서이고 첫 원소가 주 선택이며 널이 없습니다(`EditorSelection::getSelectedObjects`). */
    struct InspectorMultiSelection
    {
        /** @brief 다중 선택의 Active 칸에 보일 값입니다. */
        struct ActiveState
        {
            bool _bActive{ false }; ///< NOLINT(sw-bool-flag): 집합체다. 체크 표시 — 주 선택의 값이다
            bool _bMixed{ false };  ///< NOLINT(sw-bool-flag): 집합체다. 오브젝트마다 값이 달라 혼합으로 보인다
        };

        /** @brief 활성 칸의 값을 읽습니다. 자기 비트(`GameObject::isActive`)를 봅니다 — 꺼진 부모 아래의 자식은 켜져 있습니다(단일 선택의 칸과 같다). */
        static ActiveState readActiveState( const vector<GameObject*>& listObject );
        /** @brief 모든 오브젝트의 활성 비트를 @p bActive 로 바꿉니다. 바뀐 오브젝트들을 되돌리기 한 번으로 묶고, 이미 그 값인 오브젝트는 기록하지 않습니다. */
        static void applyActive( const vector<GameObject*>& listObject, bool bActive );
        /**
         * @brief 주 선택의 컴포넌트 중 공통 목록(@p listCommon)에 타입이 없는 것의 수입니다.
         * @details 공통 타입의 두 번째 컴포넌트는 세지 않는다 — "모든 오브젝트에 있지 않은 컴포넌트" 를 보이는 수라서, 같은 타입이 둘이어도 그 타입은 모두에게 있다.
         */
        static size_t countUnsharedComponents( const GameObject& primary, const vector<EditorMultiEditComponent>& listCommon );
        /** @brief 프로퍼티 @p prop 의 값이 @p listComponent 사이에서 다르면 true 입니다. */
        static bool hasMixedValues( const PropertyInfo& prop, const vector<Component*>& listComponent );
    };
} // namespace sw::editor
