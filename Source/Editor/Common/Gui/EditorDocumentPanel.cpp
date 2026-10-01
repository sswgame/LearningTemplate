#include "pch.h"

#include "Editor/Common/Gui/EditorDocumentPanel.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include <imgui.h>

SW_LOG_CALLER( "EditorDocumentPanel" );
namespace sw::editor
{
    EditorDocumentPanel::EditorDocumentPanel( EditorAssetKind kind, bool bLoadOnOpen )
        : IEditorPanel{ false }
        , _kind{ kind }
        , _loadedAssetPath{}
        , _pendingFocusPath{}
        , _documentUndoBaseline{}
        , _lastSavedDocumentText{}
        , _bLoaded{ SW_FALSE }
        , _bConfirmSwitch{ SW_FALSE }
        , _bLoadFailed{ SW_FALSE }
        , _reserved{ 0 }
    {
        if ( bLoadOnOpen == false )
            _bLoaded = SW_TRUE;
    }

    bool EditorDocumentPanel::hasNewFocusedDocument() const
    {
        const string_view focused = getMatchingFocusedPath();
        if ( focused.empty() )
            return false;
        return focused != _loadedAssetPath;
    }

    string_view EditorDocumentPanel::getMatchingFocusedPath() const
    {
        return EditorAssetTypeRegistry::matchingFocusedPath( _kind );
    }

    const utf8* EditorDocumentPanel::getPanelTitle() const
    {
        return EditorAssetTypeRegistry::getPanelTitle( _kind );
    }

    void EditorDocumentPanel::acceptFocusedDocument()
    {
        const string_view focused = _pendingFocusPath.empty() ? getMatchingFocusedPath() : string_view{ _pendingFocusPath };
        if ( focused.empty() )
            return;
        _loadedAssetPath = string{ focused };
        _pendingFocusPath.clear();
        _bLoaded        = SW_FALSE;
        _bConfirmSwitch = SW_FALSE;
        _bLoadFailed    = SW_FALSE;
        clearDocumentDirty();
    }

    void EditorDocumentPanel::updateFocusedDocument()
    {
        if ( _bConfirmSwitch == SW_TRUE )
        {
            drawUnsavedDocumentPopup();
            return;
        }

        if ( hasNewFocusedDocument() == false )
            return;

        if ( isDocumentDirty() )
        {
            _pendingFocusPath = string{ getMatchingFocusedPath() };
            _bConfirmSwitch   = SW_TRUE;
            drawUnsavedDocumentPopup();
            return;
        }

        acceptFocusedDocument();
    }

    void EditorDocumentPanel::markDocumentLoaded()
    {
        _bLoaded     = SW_TRUE;
        _bLoadFailed = SW_FALSE;
        clearDocumentDirty();
        syncDocumentUndoBaseline();
    }

    void EditorDocumentPanel::markDocumentLoadFailed( string_view reason )
    {
        _bLoaded     = SW_TRUE;
        _bLoadFailed = SW_TRUE;
        clearDocumentDirty();
        SW_LOG_WARNING( "%# could not load '%#' (%#) - saving is disabled so the file is not overwritten", getPanelTitle(), _loadedAssetPath,
                        reason );
    }

    bool EditorDocumentPanel::isDocumentLoaded() const
    {
        return _bLoaded == SW_TRUE;
    }

    void EditorDocumentPanel::syncDocumentUndoBaseline()
    {
        _documentUndoBaseline  = captureDocumentText();
        _lastSavedDocumentText = _documentUndoBaseline;
    }

    void EditorDocumentPanel::restoreDocumentFromUndo( string_view text )
    {
        applyDocumentText( text );
        _documentUndoBaseline = string{ text };
        if ( EditorSessionPolicy::shouldClearDocumentDirtyOnRestore( text == _lastSavedDocumentText ) )
            clearDocumentDirty();
        else
            markDocumentDirty();
    }

    void EditorDocumentPanel::notifyDocumentEdited( string_view label, string_view coalesceKey )
    {
        const string after = captureDocumentText();
        if ( after == _documentUndoBaseline )
            return;
        // 이 편집이 속한 문서를 적어 둔다 — 되돌리기는 **그 문서가 아직 열려 있을 때만** 적용한다. 예전에는 `this` 와 바뀐 구간만 잡아,
        // 다른 문서로 바꾼 뒤 Ctrl+Z 하면 앞 문서의 구간을 지금 문서의 텍스트에 붙였다 — 지금 문서가 깨진 채 dirty 가 되고 저장하면
        // 그대로 쓰였다. (구간 다시 짜기는 길이가 맞지 않아도 메모리를 넘지 않으므로, 막는 자리는 적용하는 쪽 하나면 된다.)
        const string documentPath = _loadedAssetPath;
        EditorTransaction::recordDocumentText(
            _documentUndoBaseline, after, label,
            SW_DELEGATE_LAMBDA( EditorDocumentRestoreDelegate, [this, documentPath]( string_view snapshot )
        {
            if ( documentPath != _loadedAssetPath )
            {
                SW_LOG_WARNING( "Undo skipped: '%#' is no longer open in %# (now '%#')", documentPath, getPanelTitle(), _loadedAssetPath );
                return;
            }
            restoreDocumentFromUndo( snapshot );
        } ),
            SW_DELEGATE_LAMBDA( EditorDocumentCaptureDelegate, [this]()
        {
            return captureDocumentText();
        } ),
            coalesceKey );
        _documentUndoBaseline = after;
        markDocumentDirty();
    }

    void EditorDocumentPanel::drawUnsavedDocumentPopup()
    {
        if ( ImGui::IsPopupOpen( "##UnsavedDocumentSwitch" ) == false )
            ImGui::OpenPopup( "##UnsavedDocumentSwitch" );
        const EditorUnsavedChoice choice =
            EditorWidgets::drawUnsavedChangesModal( "##UnsavedDocumentSwitch",
                                                    "This document has unsaved changes. Switch anyway?" );
        if ( choice == EditorUnsavedChoice::None )
            return;

        if ( EditorSessionPolicy::shouldSaveBeforeAction( choice ) )
        {
            // 저장 순서는 기반 클래스가 정한다. 성공했을 때만 dirty 가 지워진다.
            if ( saveDocumentAndClearDirty() == false )
            {
                _bConfirmSwitch = SW_FALSE;
                _pendingFocusPath.clear();
                return;
            }
        }
        if ( EditorSessionPolicy::shouldClearDirtyWithoutSave( choice ) )
            clearDocumentDirty();

        if ( EditorSessionPolicy::shouldProceedWithAction( choice ) )
        {
            acceptFocusedDocument();
            return;
        }

        _bConfirmSwitch = SW_FALSE;
        _pendingFocusPath.clear();
        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr && _loadedAssetPath.empty() == false )
            pContext->getWorkspace().setFocusedAssetPath( _loadedAssetPath.c_str() );
    }
} // namespace sw::editor
