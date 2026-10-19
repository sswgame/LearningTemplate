/**
 * @file EditorSceneGenerationSync.h
 * @brief 씬이 바뀌었을 때(새 세대) 에디터가 옛 씬을 가리키던 상태를 버립니다 — 선택 · dirty · Undo 스택.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    class CommandStack;
} // namespace sw

namespace sw::editor
{
    class EditorWorkspace;
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 씬 세대가 바뀐 뒤의 정리입니다. ImGui 를 쓰지 않아 EditorTest 가 바로 부릅니다. */
    struct EditorSceneGenerationSync
    {
        /**
         * @brief @p generation 이 워크스페이스가 본 세대와 다르면 옛 씬 상태를 버리고 true 입니다. 같으면 아무것도 하지 않습니다.
         * @details Undo 스택: 명령의 XML 스냅숏은 사라진 씬의 것이다 — 남기면 Edit 메뉴가 Undo 를 켜 둔 채로 두고, 눌러도 아무 일도 없거나
         *          이름이 같은 새 씬의 오브젝트를 덮어쓴다.
         *          프리팹 격리는 여기 없다 — 격리 스택은 프리팹 창(`PrefabEditorPanel`)이 자기 미리보기 씬과 함께 들어, 메인 씬이 바뀌어도 영향이 없다.
         * @param pCommandStack 없으면(nullptr) Undo 스택은 건너뛴다.
         */
        [[nodiscard]] static bool apply( EditorWorkspace& workspace, uint64 generation, CommandStack* pCommandStack );
    };
} // namespace sw::editor
