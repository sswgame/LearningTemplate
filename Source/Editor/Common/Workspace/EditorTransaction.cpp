#include "pch.h"

#include "Editor/Common/Workspace/EditorTransaction.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

SW_LOG_CALLER( "EditorTransaction" );
namespace sw::editor
{
    namespace
    {
        struct EditorTransactionInternal
        {
            /** @brief 두 문자열의 공통 접두 · 접미를 뺀 변경 구간입니다(텍스트 Undo). */
            struct StringChangeSpan
            {
                string _removed;
                string _added;
                uint32 _prefixLength{ 0 };
                uint32 _suffixLength{ 0 };
            };

            /** @brief 편집 전후 텍스트의 공통 접두 · 접미를 빼고 바뀐 가운데 부분만 남깁니다. */
            static StringChangeSpan makeChangeSpan( string_view before, string_view after )
            {
                StringChangeSpan span{};
                const size_t     beforeSize = before.size();
                const size_t     afterSize  = after.size();
                const size_t     minSize    = ( beforeSize < afterSize ) ? beforeSize : afterSize;
                size_t           prefix     = 0;
                while ( prefix < minSize && before[prefix] == after[prefix] )
                {
                    ++prefix;
                }

                size_t suffix = 0;
                while ( true )
                {
                    const bool bSuffixMatches = suffix < ( beforeSize - prefix ) && suffix < ( afterSize - prefix ) &&
                                                before[beforeSize - 1 - suffix] == after[afterSize - 1 - suffix];
                    if ( bSuffixMatches == false )
                        break;
                    ++suffix;
                }

                span._prefixLength = static_cast<uint32>( prefix );
                span._suffixLength = static_cast<uint32>( suffix );
                span._removed      = string{ before.substr( prefix, beforeSize - prefix - suffix ) };
                span._added        = string{ after.substr( prefix, afterSize - prefix - suffix ) };
                return span;
            }

            /** @brief 편집 후 텍스트와 변경 구간으로 편집 전 본문을 만듭니다. */
            static string reconstructBefore( const StringChangeSpan& span, string_view afterText )
            {
                const size_t prefixLength = static_cast<size_t>( span._prefixLength );
                const size_t suffixLength = static_cast<size_t>( span._suffixLength );
                if ( afterText.size() < prefixLength + suffixLength )
                    return string{ afterText };
                string result;
                result.reserve( prefixLength + span._removed.size() + suffixLength );
                // `data()` 를 넘기지만 **길이도 함께** 넘기므로, 널 종료를 기대하는 호출이 아니다.
                // 검사기가 커스텀 string 의 (ptr, count) 오버로드를 알아보지 못한다.
                // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
                result.append( afterText.data(), prefixLength );
                result.append( span._removed );
                if ( suffixLength > 0 )
                    result.append( afterText.data() + ( afterText.size() - suffixLength ), suffixLength );
                return result;
            }

            /** @brief 편집 전 텍스트와 변경 구간으로 편집 후 본문을 만듭니다. */
            static string reconstructAfter( const StringChangeSpan& span, string_view beforeText )
            {
                const size_t prefixLength = static_cast<size_t>( span._prefixLength );
                const size_t suffixLength = static_cast<size_t>( span._suffixLength );
                if ( beforeText.size() < prefixLength + suffixLength )
                    return string{ beforeText };
                string result;
                result.reserve( prefixLength + span._added.size() + suffixLength );
                // 위와 같다. 길이를 함께 넘긴다.
                // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
                result.append( beforeText.data(), prefixLength );
                result.append( span._added );
                if ( suffixLength > 0 )
                    result.append( beforeText.data() + ( beforeText.size() - suffixLength ), suffixLength );
                return result;
            }

            /** @brief Undo 스택입니다. 서비스가 풀린 뒤(종료 · 테스트)에는 nullptr 이라 부르는 쪽이 확인합니다. */
            static CommandStack* getCommandStack()
            {
                return editor::findSubsystem<CommandStack>();
            }

            /**
             * @brief 활성 씬을 dirty 로 표시합니다. 기록할 때와 되돌리기 · 다시 하기가 씬을 바꿀 때 부릅니다.
             * @details 컨텍스트가 없으면(테스트 · 도구) 지역 서비스로 건 워크스페이스에 표시합니다.
             */
            static void markActiveSceneDirty()
            {
                EditorContext*   pContext   = editor::findSubsystem<EditorContext>();
                EditorWorkspace* pWorkspace = ( pContext != nullptr ) ? &pContext->getSubsystem<EditorWorkspace>() : editor::findSubsystem<EditorWorkspace>();
                if ( pWorkspace != nullptr )
                    pWorkspace->markSceneDirty();
            }

            /**
             * @brief 데이터 명령이 오브젝트에 한 일을 에디터 상태에 맞춥니다. 없앨 오브젝트는 선택에서 빼고(파괴는 지연이라 남겨 두면 인스펙터 · 기즈모가
             *        계속 잡는다), 복원한 오브젝트는 선택하고, 씬을 dirty 로 표시합니다.
             */
            static void onObjectEdit( const CommandStack::ObjectEditNotice& notice )
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                GameObject*    pTarget  = editor::findGameObject( GameObjectHandle::make( notice._objectID ) );
                if ( pContext != nullptr && pTarget != nullptr )
                {
                    // 이 처리기는 전역 스택에만 걸린다 — 전역 스택은 메인 씬 오브젝트만 받는다(미리보기 씬 것은 창의 스택, `findPreviewScopeOf`).
                    EditorSelection& selection = editor::getMainSceneSelection();
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
                    // 선택 조회: 처리기 주인은 에디터 범위의 서브시스템이다 — 범위에 없으면(서비스를 일부만 건 테스트) 알림 없이 기록만 한다.
                    EditorObjectEditBindings* const pBindings = editor::findSubsystem<EditorObjectEditBindings>();
                    if ( pBindings != nullptr )
                        pBindings->ensureBound( *pStack );
                    pStack->push( std::move( cmd ) );
                }
                markActiveSceneDirty();
            }

            /**
             * @brief @p obj 가 미리보기 씬에 살면 그 씬에 묶인 선택(창의 범위)입니다. 메인 씬 오브젝트면 nullptr 입니다.
             * @details 기록이 갈 스택은 부른 쪽의 포커스가 아니라 **오브젝트가 사는 씬**이 정한다 — 명령이 창 선택의 오브젝트를 고쳐도 전역 스택 ·
             *          메인 씬 dirty 로 새지 않는다(창을 닫으면 그 기록도 함께 사라진다, 계획의 되돌리기 어려운 결정 4).
             */
            static EditorSelection* findPreviewScopeOf( const GameObject& obj )
            {
                const GameObjectManager* pManager = obj.getManager();
                if ( pManager == nullptr )
                    return nullptr;
                for ( EditorSelection* pSelection : editor::getPreviewSelections() )
                {
                    const Scene* pScene = pSelection->getScene();
                    if ( pScene != nullptr && pScene->getObjectManager() == pManager )
                        return pSelection;
                }
                return nullptr;
            }

            /**
             * @brief 그룹(`beginTransaction` …)을 걸 스택들 — 전역 스택과 미리보기 창의 스택입니다.
             * @details 그룹은 어느 씬의 기록이 들어올지 모른다(기록마다 오브젝트의 씬이 스택을 고른다). 모두에 걸고, 기록이 들지 않은 빈 그룹은 스택이 버린다.
             */
            static void collectRecordStacks( vector<CommandStack*>& outListStack )
            {
                outListStack.clear();
                CommandStack* pGlobal = getCommandStack();
                if ( pGlobal != nullptr )
                    outListStack.push_back( pGlobal );
                for ( EditorSelection* pSelection : editor::getPreviewSelections() )
                {
                    CommandStack* pStack = pSelection->findPreviewCommandStack();
                    if ( pStack != nullptr && pStack != pGlobal )
                        outListStack.push_back( pStack );
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorTransaction::beginTransaction( string_view label )
    {
        vector<CommandStack*> listStack;
        EditorTransactionInternal::collectRecordStacks( listStack );
        for ( CommandStack* pStack : listStack )
        {
            pStack->beginTransaction( label );
        }
    }

    void EditorTransaction::endTransaction()
    {
        vector<CommandStack*> listStack;
        EditorTransactionInternal::collectRecordStacks( listStack );
        for ( CommandStack* pStack : listStack )
        {
            pStack->endTransaction();
        }
    }

    void EditorTransaction::cancelTransaction()
    {
        vector<CommandStack*> listStack;
        EditorTransactionInternal::collectRecordStacks( listStack );
        for ( CommandStack* pStack : listStack )
        {
            pStack->cancelTransaction();
        }
    }

    void EditorObjectEditBindings::ensureBound( CommandStack& stack )
    {
        if ( stack.hasObjectEditListener() )
            return;
        // 사라진 스택의 구독(무효)은 걷는다 — 미리보기 창을 여닫아도 목록이 자라지 않는다.
        (void)VectorUtil::removeIf( _listSubscription, []( const Subscription& subscription )
        { return subscription.isActive() == false; } );
        _listSubscription.push_back( stack.bindObjectEditListener( SW_DELEGATE_FUNCTION( CommandStack::ObjectEditListener, EditorTransactionInternal::onObjectEdit ) ) );
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

        const EditorSelection* pPreviewScope = EditorTransactionInternal::findPreviewScopeOf( *pObj );
        if ( pPreviewScope != nullptr )
        {
            pPreviewScope->recordModify( *pObj, before, after, label ); // 창의 스택 — 메인 씬은 dirty 가 아니다
            return;
        }

        // 선택 조회(②): 스택이나 씬 관리자가 없는 곳은 종료 뒤 · 테스트 · 도구뿐이다. 기록은 못 남겨도 dirty 는 표시한다.
        CommandStack* pStack        = EditorTransactionInternal::getCommandStack();
        SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
        if ( pStack == nullptr || pSceneManager == nullptr )
        {
            EditorTransactionInternal::markActiveSceneDirty();
            return;
        }
        EditorTransactionInternal::pushCommand( pStack, ObjectUndoUtil::makeModify( *pSceneManager, *pObj, before, after, label ) );
    }

    void EditorTransaction::recordObjectLifetime( GameObject* pObj, string_view label, ObjectLifetimeEdit edit )
    {
        if ( pObj == nullptr )
            return;

        SceneManager*          pSceneManager = editor::findSubsystem<SceneManager>();
        const EditorSelection* pPreviewScope = EditorTransactionInternal::findPreviewScopeOf( *pObj );
        if ( pPreviewScope != nullptr )
        {
            // 창의 스택에 남긴다 — 메인 씬은 dirty 가 아니다. 미리보기 스택에는 메인 선택을 고치는 처리기(`onObjectEdit`)를 걸지 않는다.
            CommandStack* pPreviewStack = pPreviewScope->findPreviewCommandStack();
            if ( pSceneManager == nullptr || pPreviewStack == nullptr )
            {
                SW_LOG_ERROR( "No SceneManager - the lifetime edit of '%#' in the preview scene is not recorded", pObj->getName().c_str() );
                return;
            }
            pPreviewStack->push( ObjectUndoUtil::makeLifetime( *pSceneManager, pObj, edit, label ) );
            return;
        }

        // 선택 조회(②): `recordModify` 와 같다.
        CommandStack* pStack = EditorTransactionInternal::getCommandStack();
        if ( pStack == nullptr || pSceneManager == nullptr )
        {
            EditorTransactionInternal::markActiveSceneDirty();
            return;
        }
        EditorTransactionInternal::pushCommand( pStack, ObjectUndoUtil::makeLifetime( *pSceneManager, pObj, edit, label ) );
    }

    void EditorTransaction::recordCreation( GameObject* pObj, string_view label )
    {
        recordObjectLifetime( pObj, label, ObjectLifetimeEdit::Created );
    }

    void EditorTransaction::recordDestruction( GameObject* pObj, string_view label )
    {
        recordObjectLifetime( pObj, label, ObjectLifetimeEdit::Destroyed );
    }

    void EditorTransaction::push( UndoStep undo, UndoStep redo, string_view label, string_view coalesceKey )
    {
        // 선택 조회(②): 스택이 없는 곳은 종료 뒤 · 테스트 · 도구뿐이다. 되돌리기 기록은 남기지 못하지만 편집 자체는 이미 끝났으므로 로그만 남기고 넘어간다.
        CommandStack* pStack = editor::findSubsystem<CommandStack>();
        if ( pStack == nullptr )
        {
            SW_LOG_TRACE( "No CommandStack - the undo record '%#' is not kept", string{ label }.c_str() );
            return;
        }

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

        const EditorTransactionInternal::StringChangeSpan span = EditorTransactionInternal::makeChangeSpan( beforeText, afterText );
        if ( capture.isBound() )
        {
            push( UndoStep::makeTransient( SW_DELEGATE_LAMBDA( Delegate<void()>, [restore, capture, span]()
            {
                restore( EditorTransactionInternal::reconstructBefore( span, capture() ) );
            } ) ),
                  UndoStep::makeTransient( SW_DELEGATE_LAMBDA( Delegate<void()>, [restore, capture, span]()
            {
                restore( EditorTransactionInternal::reconstructAfter( span, capture() ) );
            } ) ),
                  label, coalesceKey );
            return;
        }

        const string beforeStr{ beforeText };
        const string afterStr{ afterText };
        push( UndoStep::makeTransient( SW_DELEGATE_LAMBDA( Delegate<void()>, [restore, beforeStr]()
        {
            restore( beforeStr );
        } ) ),
              UndoStep::makeTransient( SW_DELEGATE_LAMBDA( Delegate<void()>, [restore, afterStr]()
        {
            restore( afterStr );
        } ) ),
              label, coalesceKey );
    }
} // namespace sw::editor
