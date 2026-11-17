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
    struct EditorToolDefaults;

    /**
     * @class EditorUndoHistory
     * @brief 메인 `CommandStack` 의 주인입니다. 편집기 범위에 스택을 걸어 `findSubsystem<CommandStack>()` 이 찾게 한다.
     * @details 기록은 데이터라(`UndoStep`) 호스트의 맡김 칸(`IModuleStateStore`)에 바이트로 맡긴다. 맡길 때의 씬 세대와 다르면 되찾지 않는다 — 다른 씬의
     *          오브젝트 id 를 가리키는 기록이다. 씬이 바뀌면(`SceneManager::onSceneObjectsInvalidated`) 비운다.
     */
    class SW_EDITOR_API EditorUndoHistory final : public EditorSubsystem
    {
    public:
        /** @brief 메인 스택을 @p limit 한도로 만듭니다(`readConfiguredLimit`). */
        explicit EditorUndoHistory( const UndoBufferLimit& limit )
            : _stack{ limit }
            , _sceneInvalidatedSubscription{}
        {
        }

        /**
         * @brief 설정(`EditorToolDefaults` — editortooldefaults.json 의 `_undoMaxCommandCount` · `_undoBufferMegabytes`)이 정한 스택 하나의 한도입니다.
         * @details 메인 스택과 문서 창 스택(`EditorDocumentPanel`)이 같은 값을 쓴다. 도구 기본값은 에디터 컨텍스트가 서기 전에 걸린다 — 없으면 `getSubsystem` 이 멈춘다(대신 쓸 값이 없다).
         */
        [[nodiscard]] static UndoBufferLimit readConfiguredLimit( const EditorToolDefaults& defaults );

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
