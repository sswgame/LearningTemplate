// 문서 패널(문서 상태 · 읽기 · 되돌리기와 열기 막대 · 문서 바꾸기 확인 그리기)입니다.
#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorDocumentPanel.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/Asset/EditorAssetIndex.h"
#include "Editor/Common/GUI/Panel/Model/EditorDocumentOpenUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
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

    void EditorDocumentPanel::drawDocumentOpenBar( const utf8* pMarkPrefix )
    {
        const string markOpen = string{ pMarkPrefix } + ".open";
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Open" ) ) )
        {
            // 열 때 한 번 모은다 — 리소스 트리를 훑으므로 그리기마다 하지 않는다.
            vector<EditorAssetIndexEntry> listEntry;
            EditorAssetIndex::collectEntries( editor::getSubsystem<AssetPaths>(), listEntry );
            EditorDocumentOpenUtil::collectCandidates( listEntry, _kind, [this]( string_view path )
            { return canAcceptDocumentPath( path ); }, _listOpenCandidate );
            _openFilter.clear();
            EditorInputGUI::openPopup( "##DocumentOpen" );
        }
        EditorSelfTestMarks::record( markOpen.c_str() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "DocumentOpen" ) );
        drawOpenDocumentPopup( pMarkPrefix );

        ImGui::SameLine();
        const utf8* pPreview = _loadedAssetPath.empty() ? SW_LOCKEY( "EdCommon", "NoDocument" ) : _loadedAssetPath.c_str();
        ImGui::SetNextItemWidth( -1.0f );
        if ( ImGui::BeginCombo( "##RecentDocuments", pPreview ) )
        {
            if ( _listRecentPath.empty() )
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdCommon", "NoRecentDocuments" ) );
            string openPath;
            for ( const string& path : _listRecentPath )
            {
                if ( ImGui::Selectable( path.c_str(), path == _loadedAssetPath ) )
                    openPath = path;
            }
            ImGui::EndCombo();
            if ( openPath.empty() == false )
                requestOpenDocument( openPath );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "RecentDocuments" ) );
    }

    void EditorDocumentPanel::drawOpenDocumentPopup( const utf8* pMarkPrefix )
    {
        if ( ImGui::BeginPopup( "##DocumentOpen" ) == false )
            return;
        const string markPrefix = string{ pMarkPrefix } + ".open.";
        if ( ImGui::IsWindowAppearing() )
            ImGui::SetKeyboardFocusHere();
        EditorWidgets::drawSearchField( "##DocumentOpenSearch", _openFilter, SW_LOCKEY( "EdCommon", "SearchAssetsHint" ), 360.0f * EditorThemeUtil::getDPIScale(), false, ( markPrefix + "search" ).c_str() );

        const EditorListFilter filter{ _openFilter.c_str() };
        string                 openPath;
        uint32                 shownCount{ 0 };
        for ( const string& path : _listOpenCandidate )
        {
            if ( filter.matchesAny( { string_view{ path } } ) == false )
                continue;
            ++shownCount;
            if ( ImGui::Selectable( path.c_str(), path == _loadedAssetPath ) )
                openPath = path;
            EditorSelfTestMarks::record( ( markPrefix + FileUtil::getFileNamePart( path ) ).c_str() );
        }
        if ( shownCount == 0 )
        {
            if ( filter.isActive() )
                EditorWidgets::drawNoSearchResultHint( filter.getText() );
            else
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdCommon", "NoAssetsOfKind" ) );
        }
        if ( openPath.empty() == false )
        {
            requestOpenDocument( openPath );
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void EditorDocumentPanel::requestOpenDocument( string_view assetPath )
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        editorContext.getSubsystem<EditorWorkspace>().setFocusedAssetPath( string{ assetPath }.c_str() );
    }

    void EditorDocumentPanel::drawUnsavedDocumentPopup()
    {
        if ( ImGui::IsPopupOpen( "##UnsavedDocumentSwitch" ) == false )
            EditorInputGUI::openPopup( "##UnsavedDocumentSwitch" );
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
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        EditorDocumentOpenUtil::restoreFocusAfterCancel( editorContext.getSubsystem<EditorWorkspace>(), _loadedAssetPath );
    }

    EditorDocumentPanel::EditorDocumentPanel( const EditorAssetType& kind, bool bLoadOnOpen )
        : IEditorDocumentPanel{ false }
        , _kind{ kind }
        , _loadedAssetPath{}
        , _pendingFocusPath{}
        , _documentUndoBaseline{}
        , _lastSavedDocumentText{}
        , _pUndoTarget{ make_shared<EditorDocumentPanel*>( this ) }
        , _pUndoOwner{ make_unique<UndoStepOwner>() }
        , _listRecentPath{}
        , _listOpenCandidate{}
        , _openFilter{}
        , _documentLoadEpoch{ 0 }
        , _bLoaded{ SW_FALSE }
        , _bConfirmSwitch{ SW_FALSE }
        , _bLoadFailed{ SW_FALSE }
        , _reserved{ 0 }
    {
        if ( bLoadOnOpen == false )
            _bLoaded = SW_TRUE;
    }

    void EditorDocumentPanel::onDocumentSaved()
    {
        EditorContext* pContext = editor::findSubsystem<EditorContext>(); // 컨텍스트 없는 패널 테스트는 감시도 없다
        if ( _loadedAssetPath.empty() == false && pContext != nullptr )
            pContext->getSubsystem<AssetHotReload>().applySavedAsset( _loadedAssetPath );
    }

    EditorDocumentPanel::~EditorDocumentPanel()
    {
        *_pUndoTarget = nullptr;
    }

    optional<AbsolutePath> EditorDocumentPanel::getDocumentDiskPath() const
    {
        if ( _loadedAssetPath.empty() )
            return nullopt;
        return editor::getSubsystem<AssetPaths>().toDiskPath( _loadedAssetPath );
    }

    bool EditorDocumentPanel::hasNewFocusedDocument() const
    {
        return EditorDocumentOpenUtil::isNewFocus( getMatchingFocusedPath(), _loadedAssetPath );
    }

    string_view EditorDocumentPanel::getMatchingFocusedPath() const
    {
        const string_view focused = EditorAssetTypeRegistry::matchingFocusedPath( _kind );
        return focused.empty() == false && canAcceptDocumentPath( focused ) ? focused : string_view{};
    }

    const utf8* EditorDocumentPanel::getPanelTitle() const
    {
        return EditorAssetTypeRegistry::getPanelTitle( _kind );
    }

    string EditorDocumentPanel::acceptDocumentToLoad()
    {
        if ( _loadedAssetPath.empty() )
            acceptFocusedDocument();
        return _loadedAssetPath;
    }

    void EditorDocumentPanel::acceptFocusedDocument()
    {
        const string_view focused = EditorDocumentOpenUtil::chooseDocumentToAccept( _pendingFocusPath, getMatchingFocusedPath() );
        if ( focused.empty() )
            return;
        _loadedAssetPath = string{ focused };
        EditorDocumentOpenUtil::rememberRecent( _listRecentPath, _loadedAssetPath, kMaxRecentDocumentCount );
        _pendingFocusPath.clear();
        _bLoaded        = SW_FALSE;
        _bConfirmSwitch = SW_FALSE;
        _bLoadFailed    = SW_FALSE;
        clearDocumentDirty();
    }

    void EditorDocumentPanel::markDocumentLoaded()
    {
        _bLoaded     = SW_TRUE;
        _bLoadFailed = SW_FALSE;
        ++_documentLoadEpoch; // 다시 읽은 뒤의 편집을 읽기 전 되돌리기 항목에 합치지 않는다
        clearDocumentDirty();
        syncDocumentUndoBaseline();
    }

    void EditorDocumentPanel::markDocumentLoadFailed( string_view reason )
    {
        _bLoaded     = SW_TRUE;
        _bLoadFailed = SW_TRUE;
        clearDocumentDirty();
        SW_LOG_WARNING_ID( "EditorDocumentPanel.CouldNotLoadSaving", "%# could not load '%#' (%#) - saving is disabled so the file is not overwritten", getPanelTitle(), _loadedAssetPath,
                           reason );
    }

    bool EditorDocumentPanel::isDocumentLoaded() const
    {
        return _bLoaded == SW_TRUE;
    }

    void EditorDocumentPanel::reloadDocument()
    {
        const ToolAssetLoadResult result = loadDocument();
        if ( result == ToolAssetLoadResult::Malformed )
            markDocumentLoadFailed( "malformed or newer format" );
        else
            markDocumentLoaded();
    }

    void EditorDocumentPanel::ensureDocumentLoaded()
    {
        if ( isDocumentLoaded() == false )
            reloadDocument();
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
        EditorTransaction transaction; // 씬을 바꾸는 기록은 거래 구간 안에서만 한다(`EditorTransaction` 의 기록 함수는 인스턴스 멤버다)
        const string      after = captureDocumentText();
        if ( after == _documentUndoBaseline )
            return;
        // 이 편집이 속한 문서를 적어 둔다 — 되돌리기는 **그 문서가 아직 열려 있을 때만** 적용한다. 주의: `this` 와 바뀐 구간만 잡으면
        // 다른 문서로 바꾼 뒤 Ctrl+Z 할 때 앞 문서의 구간을 지금 문서의 텍스트에 붙여, 지금 문서가 깨진 채 dirty 가 되고 저장하면
        // 그대로 쓰인다. (구간 다시 짜기는 길이가 맞지 않아도 메모리를 넘지 않으므로, 막는 곳은 적용하는 쪽 하나면 된다.)
        // 패널은 `this` 가 아니라 표지로 잡는다 — 백엔드 교체는 패널을 다시 만들지만 되돌리기 스택은 남긴다. 사라진 패널의 항목은 건너뛴다.
        const string                           documentPath = _loadedAssetPath;
        const shared_ptr<EditorDocumentPanel*> pTarget      = _pUndoTarget;
        transaction.recordDocumentText(
            _documentUndoBaseline, after, label, *_pUndoOwner,
            SW_DELEGATE_LAMBDA( EditorDocumentRestoreDelegate, [pTarget, documentPath]( string_view snapshot )
        {
            EditorDocumentPanel* pPanel = *pTarget;
            if ( pPanel == nullptr || documentPath != pPanel->_loadedAssetPath )
            {
                SW_LOG_WARNING_ID( "EditorDocumentPanel.UndoSkippedNoLonger", "Undo skipped: '%#' is no longer open in its panel", documentPath );
                return;
            }
            pPanel->restoreDocumentFromUndo( snapshot );
        } ),
            SW_DELEGATE_LAMBDA( EditorDocumentCaptureDelegate, [pTarget]()
        {
            EditorDocumentPanel* pPanel = *pTarget;
            return pPanel != nullptr ? pPanel->captureDocumentText() : string{};
        } ),
            EditorSessionPolicy::makeDocumentCoalesceKey( coalesceKey, reinterpret_cast<uintptr_t>( pTarget.get() ), documentPath, _documentLoadEpoch ) );
        _documentUndoBaseline = after;
        markDocumentDirty();
    }
} // namespace sw::editor
