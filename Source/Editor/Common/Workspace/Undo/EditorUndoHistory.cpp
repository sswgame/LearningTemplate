#include "pch.h"

#include "Editor/Common/Workspace/Undo/EditorUndoHistory.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorFrameStep.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "ModuleAPI/Service/IModuleStateStore.h"

namespace sw::editor
{
    namespace
    {
        struct EditorUndoHistoryInternal
        {
            /** @brief 맡김 칸의 키입니다. */
            static constexpr string_view kStoreKey = "editor.undoHistory";
            /** @brief 설정의 MB 를 바이트로 바꾸는 배수입니다. */
            static constexpr uint64 kBytesPerMegabyte = uint64{ 1 } << 20;
        };
    } // namespace

    void EditorUndoHistorySnapshot::captureFrom( const CommandStack* pStack )
    {
        // 같은 값이면 쓰지 않는다 — 게임 단계마다 부르므로 바뀐 프레임에만 세대가 오르고 글을 복사한다.
        const uint8  bAvailable   = pStack != nullptr ? SW_TRUE : SW_FALSE;
        const size_t count        = pStack != nullptr ? pStack->getCommandCount() : 0;
        const size_t currentIndex = pStack != nullptr ? pStack->getCurrentIndex() : 0;
        const uint8  bCanUndo     = pStack != nullptr && pStack->canUndo() ? SW_TRUE : SW_FALSE;
        const uint8  bCanRedo     = pStack != nullptr && pStack->canRedo() ? SW_TRUE : SW_FALSE;
        bool         bChanged     = bAvailable != _bAvailable || count != _listLabel.size() || currentIndex != _currentIndex || bCanUndo != _bCanUndo || bCanRedo != _bCanRedo;
        _listLabel.resize( count );
        for ( size_t index = 0; index < count; ++index )
        {
            const string&     label = pStack->getCommand( index )._label;
            const string_view shown = label.empty() == false ? string_view( label ) : string_view( "Command" );
            if ( _listLabel[index] == shown )
                continue;
            _listLabel[index] = string{ shown };
            bChanged          = true;
        }
        _bAvailable   = bAvailable;
        _currentIndex = currentIndex;
        _bCanUndo     = bCanUndo;
        _bCanRedo     = bCanRedo;
        if ( bChanged )
            ++_generation;
    }

    void EditorUndoHistory::publishSnapshot()
    {
        // Ctrl+Z 가 가는 스택(포커스한 문서 패널 · 미리보기 창의 스택, 없으면 메인) — 문서마다 스택이라(감사 E-3) 메인만 보이면 문서 기록이 보이지 않는다.
        _snapshot.captureFrom( getFocusedCommandStack() );
    }

    UndoBufferLimit EditorUndoHistory::readConfiguredLimit( const EditorToolDefaults& defaults )
    {
        return UndoBufferLimit{ defaults._undoMaxCommandCount, static_cast<uint64>( defaults._undoBufferMegabytes ) * EditorUndoHistoryInternal::kBytesPerMegabyte };
    }

    SubsystemInitResult EditorUndoHistory::initializeSubsystem()
    {
        // 선택 조회(②): 에디터 컨텍스트만 세우는 테스트 · 도구에는 씬 관리자 · 맡김 칸이 없다 — 그때는 비운 채로 시작한다.
        SceneManager* const pSceneManager = editor::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
            return SubsystemInitResult::Succeeded;
        _sceneInvalidatedSubscription = pSceneManager->onSceneObjectsInvalidated().add( SW_DELEGATE_METHOD( Delegate<void()>, &CommandStack::clear, &_stack ) );

        IModuleStateStore* const pStore = editor::findSubsystem<IModuleStateStore>();
        vector<uint8>            bytes;
        if ( pStore == nullptr || pStore->take( EditorUndoHistoryInternal::kStoreKey, bytes ) == false )
            return SubsystemInitResult::Succeeded;
        Archive archive( bytes.data(), bytes.size() );
        uint64  sceneGeneration{ 0 };
        archive >> sceneGeneration;
        if ( archive.isOk() == false || sceneGeneration != pSceneManager->getSceneGeneration() )
            return SubsystemInitResult::Succeeded; // 다른 씬의 기록이다
        if ( _stack.load( archive ) == false )
            SW_LOG_WARNING_ID( "EditorUndoHistory.UndoHistoryPreviousEditor", "The undo history from the previous editor instance could not be read - starting empty" );
        else
            SW_LOG_INFO_ID( "EditorUndoHistory.ResumedUndoHistoryCommand", "Resumed the undo history: %# command(s)", static_cast<uint32>( _stack.getCommandCount() ) );
        return SubsystemInitResult::Succeeded;
    }

    void EditorUndoHistory::shutdownSubsystem()
    {
        _sceneInvalidatedSubscription.reset();
        // 선택 조회(②): 종료 경로라 호스트가 서비스 결합을 먼저 풀었을 수 있다. 그때는 다음 인스턴스가 이어받을 기록도 없다.
        IModuleStateStore* const pStore        = editor::findSubsystem<IModuleStateStore>();
        SceneManager* const      pSceneManager = editor::findSubsystem<SceneManager>();
        if ( pStore == nullptr || pSceneManager == nullptr )
            return;
        Archive archive;
        archive << pSceneManager->getSceneGeneration();
        _stack.save( archive );
        pStore->store( EditorUndoHistoryInternal::kStoreKey, vector<uint8>( archive.getData(), archive.getData() + archive.getSize() ) );
        _stack.clear();
    }
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct EditorUndoHistoryStepInternal
        {
            /** @brief 게임 단계 끝 — 이번 프레임의 편집 · 되돌리기가 끝난 스택 값을 다음 UI 프레임이 읽을 스냅샷으로 짓습니다. */
            static void run( EditorContext& context, EditorPlaySessionWriteKey /*key*/ ) { context.getSubsystem<EditorUndoHistory>().publishSnapshot(); }
        };
    } // namespace

    SW_EDITOR_GAME_STEP( UndoHistory, "undoHistory", EditorGameStepOrder::kUndoHistory, &EditorUndoHistoryStepInternal::run );
} // namespace sw::editor
