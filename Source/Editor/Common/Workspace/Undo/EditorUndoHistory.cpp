#include "pch.h"

#include "Editor/Common/Workspace/Undo/EditorUndoHistory.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "ModuleAPI/Service/IModuleStateStore.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorUndoHistory" );

    namespace
    {
        struct EditorUndoHistoryInternal
        {
            /** @brief 맡김 칸의 키입니다. */
            static constexpr string_view kStoreKey = "editor.undoHistory";
        };
    } // namespace

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
            SW_LOG_WARNING( "The undo history from the previous editor instance could not be read - starting empty" );
        else
            SW_LOG_INFO( "Resumed the undo history: %# command(s)", static_cast<uint32>( _stack.getCommandCount() ) );
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
