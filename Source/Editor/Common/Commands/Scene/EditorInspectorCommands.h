/**
 * @file EditorInspectorCommands.h
 * @brief 인스펙터 프로퍼티 Undo / 프리팹 적용·복원 커맨드
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    class GameObject;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorInspectorCommands
     * @brief 프리팹을 적용 · 복원 · 연결 해제합니다. 프로퍼티 편집의 Undo 는 오브젝트 스냅샷(`EditorTransaction::recordModify`)이 맡습니다.
     */
    class EditorInspectorCommands
    {
    public:
        /** @brief 선택 오브젝트 상태를 프리팹 XML로 저장합니다. */
        [[nodiscard]] static bool applyToPrefab( GameObject* pObj, string_view prefabPath );
        /** @brief 프리팹 상태로 되돌리고 Undo에 기록합니다. */
        [[nodiscard]] static bool revertToPrefab( GameObject* pObj, string_view prefabPath );
        /** @brief 오브젝트와 프리팹 연결을 끊습니다. */
        static void unlinkPrefab( GameObject* pObj );
        /**
         * @brief @p pRoot 아래의 프리팹 인스턴스 자식을 @p pRoot 의 중첩 프리팹 부품(`PrefabInstanceRegistry`)으로 옮기고 옮긴 수를 반환합니다.
         * @details 새 프리팹을 만들기 직전에 부른다 — 프리팹 본문은 루트 오브젝트 하나와 중첩 인스턴스 노드뿐이라, 옮기지 않은 자식 인스턴스는
         *          프리팹에 실리지 않는다(유니티: 프리팹 인스턴스를 자식으로 둔 계층으로 프리팹을 만들면 중첩 프리팹이 된다). 옮긴 자식은 씬이
         *          엔티티로 저장하지 않고 루트 인스턴스가 다시 스폰하므로 작업 공간의 인스턴스 경로를 지운다. 루트의 바로 아래와, 옮긴 자식 아래로만
         *          내려간다(부품의 부모는 루트나 다른 부품이어야 한다). 되돌리기는 남기지 않는다 — 기존 프리팹에 쓰는 `applyToPrefab` 은 부르지 않는다.
         */
        static uint32 adoptChildPrefabInstances( GameObject* pRoot );
    };
} // namespace sw::editor
