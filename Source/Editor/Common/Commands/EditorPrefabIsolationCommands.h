/**
 * @file EditorPrefabIsolationCommands.h
 * @brief 씬 안에서 프리팹 하나만 보이게 해 편집하는 프리팹 격리(Isolation) 동작입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw::editor
{
    /**
     * @class EditorPrefabIsolationCommands
     * @brief 프리팹 격리에 들어가고 나옵니다. 격리 스택은 워크스페이스(`PrefabIsolationFrame`)가 들고, 여기서는 오브젝트를 숨기고 되살립니다.
     */
    class EditorPrefabIsolationCommands
    {
    public:
        /** @brief 현재 씬에서 해당 프리팹만 보이게 합니다. 중첩이면 스택에 쌓습니다. */
        static bool enterPrefabIsolation( string_view prefabPath );
        /** @brief 프리팹 Isolation 을 종료하고 숨겼던 오브젝트를 복원합니다. bSaveToPrefab 이면 루트를 템플릿에 씁니다. */
        static bool exitPrefabIsolation( bool bSaveToPrefab );
    };
} // namespace sw::editor
