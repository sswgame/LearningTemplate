/**
 * @file Editor/Common/Workspace/Undo/EditorUndoHistory.h
 * @brief 편집기의 메인 되돌리기 기록입니다 — 스택을 소유해 범위에 걸고, 인스턴스가 내려갈 때 맡겼다가(리로드 · 백엔드 교체) 새 인스턴스가 되찾는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"

namespace sw::editor
{
    struct EditorToolDefaults;

    /**
     * @struct EditorUndoHistorySnapshot
     * @brief Ctrl+Z 가 가는 스택(포커스 창의 스택)의 값 사본입니다 — 게임 단계 끝이 짓고(`EditorUndoHistory::publishSnapshot`) UI(History 창)는 이것만 읽는다.
     * @details 포인터 · 참조를 두지 않는다(MVVM 계약). 언리얼 Undo History 창이 트랜잭션 목록을 값으로 받아 그리는 것과 같다.
     */
    struct EditorUndoHistorySnapshot
    {
        vector<string> _listLabel{};            ///< 명령 이름(1 번 단계부터). 이름이 없는 명령은 "Command"
        size_t         _currentIndex{ 0 };      ///< 지금 위치(0 은 처음 상태, n 은 n 번 단계까지 실행)
        uint8          _bAvailable{ SW_FALSE }; ///< 스택이 있다
        uint8          _bCanUndo{ SW_FALSE };
        uint8          _bCanRedo{ SW_FALSE };

        /** @brief @p pStack(없으면 nullptr)의 지금 값을 채웁니다(벡터 · 글은 재사용한다). */
        void captureFrom( const CommandStack* pStack );
    };

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

        /** @brief 게임 쪽: 포커스 창의 스택(`getFocusedCommandStack`) 값을 스냅샷으로 짓습니다. 모든 게임 단계 뒤에 돈다(`EditorGameStepOrder::kUndoHistory`). */
        void publishSnapshot();
        /** @brief UI 쪽: 마지막으로 지은 스냅샷입니다. History 창은 스택 대신 이것을 읽는다. */
        const EditorUndoHistorySnapshot& getSnapshot() const { return _snapshot; }

    private:
        CommandStack              _stack;
        EditorUndoHistorySnapshot _snapshot{};                   ///< 게임 단계 끝의 포커스 스택 값
        Subscription              _sceneInvalidatedSubscription; ///< 씬이 바뀌면 비운다
    };
} // namespace sw::editor
