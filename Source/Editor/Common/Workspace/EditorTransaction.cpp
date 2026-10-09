#include "pch.h"

#include "Editor/Common/Workspace/EditorTransaction.h"

#include "Core/Container/StringUtil.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/CommandStack.h"

namespace sw::editor
{
    namespace
    {
        struct EditorTransactionInternal
        {
            /** @brief Undo 스택입니다. 서비스가 풀린 뒤(종료 · 시험)에는 nullptr 이라 부르는 쪽이 확인합니다. */
            static CommandStack* getCommandStack()
            {
                return editor::getService<CommandStack>();
            }

            /**
             * @brief 활성 씬을 dirty 로 표시합니다. 기록할 때와 되돌리기 · 다시 하기가 씬을 바꿀 때 부릅니다.
             * @details 컨텍스트가 없으면(시험 · 도구) 지역 서비스로 건 워크스페이스에 표시합니다.
             */
            static void markActiveSceneDirty()
            {
                EditorContext*   pContext   = EditorContext::get();
                EditorWorkspace* pWorkspace = ( pContext != nullptr ) ? &pContext->getWorkspace() : editor::getService<EditorWorkspace>();
                if ( pWorkspace != nullptr )
                    pWorkspace->markSceneDirty();
            }

            /**
             * @brief 데이터 명령이 오브젝트에 한 일을 에디터 상태에 맞춥니다. 없앨 오브젝트는 선택에서 빼고(파괴는 지연이라 남겨 두면 인스펙터 · 기즈모가
             *        계속 잡는다), 되살린 오브젝트는 선택하고, 씬을 dirty 로 표시합니다.
             */
            static void onObjectEdit( const CommandStack::ObjectEditNotice& notice )
            {
                EditorContext* pContext = EditorContext::get();
                GameObject*    pTarget  = editor::findGameObject( GameObjectHandle::make( notice._objectId ) );
                if ( pContext != nullptr && pTarget != nullptr )
                {
                    EditorSelection& selection = pContext->getEditorSelection();
                    if ( notice._kind == ObjectEditKind::Destroyed && selection.hasObject( pTarget ) )
                        selection.selectObject( pTarget, SelectionMode::Remove );
                    else if ( notice._kind == ObjectEditKind::Recreated )
                        selection.selectObject( pTarget, SelectionMode::Replace );
                }
                markActiveSceneDirty();
            }

            /**
             * @brief 기록을 스택에 넣고 활성 씬을 dirty 로 표시합니다.
             * @details 스택이 없어도 씬은 이미 바뀌었습니다. 되돌리기 기록만 남기지 못할 뿐이므로 dirty 는 표시합니다.
             */
            static void pushCommand( CommandStack* pStack, CommandStack::Command&& cmd )
            {
                if ( pStack != nullptr )
                {
                    EditorTransaction::bindObjectEditListener( *pStack );
                    pStack->push( std::move( cmd ) );
                }
                markActiveSceneDirty();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorTransaction::beginTransaction( string_view label )
    {
        CommandStack* pStack = EditorTransactionInternal::getCommandStack();
        if ( pStack == nullptr )
            return;
        pStack->beginTransaction( label );
    }

    void EditorTransaction::endTransaction()
    {
        CommandStack* pStack = EditorTransactionInternal::getCommandStack();
        if ( pStack == nullptr )
            return;
        pStack->endTransaction();
    }

    void EditorTransaction::cancelTransaction()
    {
        CommandStack* pStack = EditorTransactionInternal::getCommandStack();
        if ( pStack == nullptr )
            return;
        pStack->cancelTransaction();
    }

    void EditorTransaction::bindObjectEditListener( CommandStack& stack )
    {
        stack.setObjectEditListener( SW_DELEGATE_FUNCTION( CommandStack::ObjectEditListener, EditorTransactionInternal::onObjectEdit ) );
    }

    ObjectSnapshot EditorTransaction::captureSnapshot( const GameObject* pObj )
    {
        return ObjectUndoUtil::captureSnapshot( pObj );
    }

    void EditorTransaction::recordModify( GameObject* pObj, const ObjectSnapshot& before, const ObjectSnapshot& after,
                                          string_view label )
    {
        if ( pObj == nullptr || before._xml == after._xml )
            return;

        CommandStack* pStack        = EditorTransactionInternal::getCommandStack();
        SceneManager* pSceneManager = editor::getService<SceneManager>();
        if ( pStack == nullptr || pSceneManager == nullptr )
        {
            EditorTransactionInternal::markActiveSceneDirty();
            return;
        }
        EditorTransactionInternal::pushCommand( pStack, ObjectUndoUtil::makeModify( *pStack, *pSceneManager, *pObj, before, after, label ) );
    }

    void EditorTransaction::recordObjectLifetime( GameObject* pObj, string_view label, ObjectLifetimeEdit edit )
    {
        if ( pObj == nullptr )
            return;

        CommandStack* pStack        = EditorTransactionInternal::getCommandStack();
        SceneManager* pSceneManager = editor::getService<SceneManager>();
        if ( pStack == nullptr || pSceneManager == nullptr )
        {
            EditorTransactionInternal::markActiveSceneDirty();
            return;
        }
        EditorTransactionInternal::pushCommand( pStack, ObjectUndoUtil::makeLifetime( *pStack, *pSceneManager, pObj, edit, label ) );
    }

    void EditorTransaction::recordCreation( GameObject* pObj, string_view label )
    {
        recordObjectLifetime( pObj, label, ObjectLifetimeEdit::Created );
    }

    void EditorTransaction::recordDestruction( GameObject* pObj, string_view label )
    {
        recordObjectLifetime( pObj, label, ObjectLifetimeEdit::Destroyed );
    }

    void EditorTransaction::push( Delegate<void()> undo, Delegate<void()> redo, string_view label,
                                  string_view coalesceKey )
    {
        CommandStack* pStack = editor::getService<CommandStack>();
        if ( pStack == nullptr )
            return;

        CommandStack::Command cmd;
        cmd._label = string{ label };
        cmd._undo  = std::move( undo );
        cmd._redo  = std::move( redo );
        if ( coalesceKey.empty() == false )
            pStack->pushCoalesce( coalesceKey, std::move( cmd ) );
        else
            pStack->push( std::move( cmd ) );
    }

    void EditorTransaction::recordDocumentText( string_view beforeText, string_view afterText, string_view label,
                                                const EditorDocumentRestoreDelegate& restore, string_view coalesceKey )
    {
        recordDocumentText( beforeText, afterText, label, restore, {}, coalesceKey );
    }

    void EditorTransaction::recordDocumentText( string_view beforeText, string_view afterText, string_view label,
                                                const EditorDocumentRestoreDelegate& restore, const EditorDocumentCaptureDelegate& capture,
                                                string_view coalesceKey )
    {
        if ( beforeText == afterText || restore.isBound() == false )
            return;

        const StringChangeSpan span = StringUtil::makeChangeSpan( beforeText, afterText );
        if ( capture.isBound() )
        {
            push( SW_DELEGATE_LAMBDA( Delegate<void()>, [restore, capture, span]()
            {
                restore( StringUtil::reconstructBefore( span, capture() ) );
            } ),
                  SW_DELEGATE_LAMBDA( Delegate<void()>, [restore, capture, span]()
            {
                restore( StringUtil::reconstructAfter( span, capture() ) );
            } ),
                  label, coalesceKey );
            return;
        }

        const string beforeStr{ beforeText };
        const string afterStr{ afterText };
        push( SW_DELEGATE_LAMBDA( Delegate<void()>, [restore, beforeStr]()
        {
            restore( beforeStr );
        } ),
              SW_DELEGATE_LAMBDA( Delegate<void()>, [restore, afterStr]()
        {
            restore( afterStr );
        } ),
              label, coalesceKey );
    }
} // namespace sw::editor
