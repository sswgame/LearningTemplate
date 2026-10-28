/**
 * @file EditorNavigationCommands.h
 * @brief 활성 씬의 내비메시를 지금 베이크하는 동작입니다(`navigation.bake`, ImGui 없음 — EditorTest 가 본다).
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct EditorNavigationCommands
     * @brief 언리얼 Build Paths · 유니티 Navigation > Bake 에 해당합니다. 편집 중인 씬의 기하(표면 컴포넌트의 규칙)로 에이전트 종류마다 내비메시를 다시 짓습니다.
     * @details 플레이 중에는 부르지 않는다 — 설정을 다시 걸면 군중과 에이전트의 내비메시를 놓는다(`SceneNavigation::setSettings`). 커맨드는 편집 상태에서만 켜진다.
     */
    struct EditorNavigationCommands
    {
        /**
         * @brief 이미 마련한 내비메시를 버리고 설정 테이블의 에이전트 종류마다 다시 베이크합니다.
         * @return 내비메시가 선 에이전트 종류 수입니다. 기하가 없는 종류는 세지 않으며 씬에 기하가 없으면 0 입니다.
         */
        static uint32 bakeAll( GameObjectManager& manager );
        /** @brief 지금 베이크해 둔 에이전트 종류 수입니다(감시 값 `Editor.NavMeshCount`). */
        static uint32 countBakedMeshes( GameObjectManager& manager );
    };
} // namespace sw::editor
