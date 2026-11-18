/**
 * @file BehaviorTreeLiveRunner.h
 * @brief 행동 트리 창이 플레이 중 "running" 을 표시할 실행기를 선택한 오브젝트에서 찾습니다. 씬 · 컴포넌트는 이 파일의 `.cpp` 만 본다(창 View 는 작업 공간 · 씬 헤더를 보지 않는다 — `docs/agents/EditorUI.md` MVVM).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class BehaviorTreeRunner;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct BehaviorTreeLiveRunner
     * @brief 선택한 오브젝트의 활성 노드를 내놓는 실행기 찾기입니다.
     */
    struct BehaviorTreeLiveRunner
    {
        /**
         * @brief 포커스된 선택의 오브젝트에서 `AIControllerComponent` 가 쥔 실행기를 찾습니다. 컴포넌트에 안 붙은 실행기(`IBehaviorTreeDebugSource`)는 @p treePath 와 같은 트리일 때만 잡고 @p outName 에 이름을 채웁니다.
         * @return 없으면 nullptr 입니다. 포인터는 이번 프레임 안에서만 쓴다.
         */
        [[nodiscard]] static const BehaviorTreeRunner* findForFocusedSelection( string_view treePath, string& outName );
    };
} // namespace sw::editor
