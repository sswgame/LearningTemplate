/**
 * @file EditorSceneObjectCommands.h
 * @brief 씬 오브젝트 · 컴포넌트를 대상으로 받는 명령(만들기 · 복제 · 복사 · 붙여 넣기 · 지우기 · 부모 바꾸기 · 형제 순서)의 대상 풀기입니다.
 * @details 명령 등록 줄은 `EditorSceneObjectCommands.cpp` 에 있다 — 계층 창 · 씬 뷰 맥락 메뉴 · 단축키가 같은 명령을 부른다(EditorUX 10-3 R4 · R5).
 *          대상(`EditorCommandTarget`)에 선택 스냅숏이 있으면 그것, 없으면(메뉴바 · 단축키) 메인 씬의 지금 선택이 대상이다. ImGui 를 쓰지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Commands/EditorCommandTarget.h"
#include "Editor/Common/EditorExports.h"

namespace sw
{
    class Component;
    class GameObject;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct EditorSceneObjectCommands
     * @brief 씬 오브젝트 명령의 대상을 메인 씬의 오브젝트로 바꿉니다.
     */
    struct SW_EDITOR_API EditorSceneObjectCommands
    {
        /** @brief 대상의 선택 스냅숏(없으면 메인 선택)을 메인 씬 오브젝트로 @p outListObject 에 담습니다. 사라진 id 는 건너뜁니다. */
        static void collectTargetObjects( const EditorCommandTarget& target, vector<GameObject*>& outListObject );
        /** @brief 누른 항목(없으면 주 선택)의 오브젝트입니다. 없으면 nullptr 입니다. */
        static GameObject* findTargetObject( const EditorCommandTarget& target );
        /** @brief 컴포넌트 대상(누른 항목의 컴포넌트 id)의 컴포넌트와 주인입니다. 없으면 nullptr 입니다. */
        static Component* findTargetComponent( const EditorCommandTarget& target, GameObject*& pOutOwner );
        /** @brief 대상 오브젝트가 하나 이상이면 true 입니다(복제 · 복사 · 지우기의 켜짐). */
        static bool hasTargetObjects( const EditorCommandTarget& target );
    };
} // namespace sw::editor
