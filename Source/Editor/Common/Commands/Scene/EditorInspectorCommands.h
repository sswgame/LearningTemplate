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
    };
} // namespace sw::editor
