/**
 * @file Editor/Common/Workspace/Undo/EditorUndoHistory.h
 * @brief 편집기의 메인 되돌리기 기록입니다 — 스택을 소유해 범위에 걸고, 인스턴스가 내려갈 때 맡겼다가(리로드 · 백엔드 교체) 새 인스턴스가 되찾는다.
 */
#pragma once
#include "Core/Delegate/Subscription.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"

namespace sw::editor
{
    /**
     * @class EditorUndoHistory
     * @brief 메인 `CommandStack` 의 주인입니다. 편집기 범위에 스택을 걸어 `findSubsystem<CommandStack>()` 이 찾게 한다.
     * @details 기록은 데이터라(`UndoStep`) 호스트의 맡김 칸(`IModuleStateStore`)에 바이트로 맡긴다. 맡길 때의 씬 세대와 다르면 되찾지 않는다 — 다른 씬의
     *          오브젝트 id 를 가리키는 기록이다. 씬이 바뀌면(`SceneManager::onSceneObjectsInvalidated`) 비운다.
     */
    class SW_EDITOR_API EditorUndoHistory final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorUndoHistory";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        SubsystemInitResult          initializeSubsystem() override;
        void                         shutdownSubsystem() override;

        /** @brief 메인 스택입니다. */
        CommandStack& getStack() { return _stack; }

    private:
        CommandStack _stack;
        Subscription _sceneInvalidatedSubscription; ///< 씬이 바뀌면 비운다
    };
} // namespace sw::editor
