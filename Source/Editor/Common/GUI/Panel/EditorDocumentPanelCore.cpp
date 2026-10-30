// ImGui 를 쓰지 않는 문서 패널의 상태 · 읽기 · 되돌리기입니다. 그리는 쪽은 EditorDocumentPanel.cpp 에 있다.
// 나눈 이유: EditorTest 는 ImGui 를 링크하지 않아, 이 파일이 아니면 문서 패널의 가짜 파생이 링크되지 않는다(되돌리기 기본의 테스트).
#include "pch.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/GUI/Panel/EditorDocumentPanel.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"

#include "Engine/FileSystem/AssetPaths.h"

SW_LOG_CALLER( "EditorDocumentPanelCore" );
namespace sw::editor
{
    EditorDocumentPanel::EditorDocumentPanel( const EditorAssetType& kind, bool bLoadOnOpen )
        : IEditorPanel{ false }
        , _kind{ kind }
        , _loadedAssetPath{}
        , _pendingFocusPath{}
        , _documentUndoBaseline{}
        , _lastSavedDocumentText{}
        , _pUndoTarget{ make_shared<EditorDocumentPanel*>( this ) }
        , _listRecentPath{}
        , _listOpenCandidate{}
        , _openFilter{}
        , _bLoaded{ SW_FALSE }
        , _bConfirmSwitch{ SW_FALSE }
        , _bLoadFailed{ SW_FALSE }
        , _reserved{ 0 }
    {
        if ( bLoadOnOpen == false )
            _bLoaded = SW_TRUE;
    }

    EditorDocumentPanel::~EditorDocumentPanel()
    {
        *_pUndoTarget = nullptr;
    }

    std::optional<AbsolutePath> EditorDocumentPanel::getDocumentDiskPath() const
    {
        if ( _loadedAssetPath.empty() )
            return std::nullopt;
        return editor::getSubsystem<AssetPaths>().toDiskPath( _loadedAssetPath );
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
        const string_view focused = EditorAssetTypeRegistry::matchingFocusedPath( _kind );
        return focused.empty() == false && acceptsDocumentPath( focused ) ? focused : string_view{};
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
        rememberRecentDocument( _loadedAssetPath );
        _pendingFocusPath.clear();
        _bLoaded        = SW_FALSE;
        _bConfirmSwitch = SW_FALSE;
        _bLoadFailed    = SW_FALSE;
        clearDocumentDirty();
    }

    void EditorDocumentPanel::rememberRecentDocument( string_view assetPath )
    {
        if ( assetPath.empty() )
            return;
        for ( auto it = _listRecentPath.begin(); it != _listRecentPath.end(); ++it )
        {
            if ( *it == assetPath )
            {
                _listRecentPath.erase( it );
                break;
            }
        }
        _listRecentPath.insert( _listRecentPath.begin(), string{ assetPath } );
        if ( _listRecentPath.size() > kMaxRecentDocumentCount )
            _listRecentPath.resize( kMaxRecentDocumentCount );
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
        const string after = captureDocumentText();
        if ( after == _documentUndoBaseline )
            return;
        // 이 편집이 속한 문서를 적어 둔다 — 되돌리기는 **그 문서가 아직 열려 있을 때만** 적용한다. 주의: `this` 와 바뀐 구간만 잡으면
        // 다른 문서로 바꾼 뒤 Ctrl+Z 할 때 앞 문서의 구간을 지금 문서의 텍스트에 붙여, 지금 문서가 깨진 채 dirty 가 되고 저장하면
        // 그대로 쓰인다. (구간 다시 짜기는 길이가 맞지 않아도 메모리를 넘지 않으므로, 막는 곳은 적용하는 쪽 하나면 된다.)
        // 패널은 `this` 가 아니라 표지로 잡는다 — 백엔드 교체는 패널을 다시 만들지만 되돌리기 스택은 남긴다. 사라진 패널의 항목은 건너뛴다.
        const string                           documentPath = _loadedAssetPath;
        const shared_ptr<EditorDocumentPanel*> pTarget      = _pUndoTarget;
        EditorTransaction::recordDocumentText(
            _documentUndoBaseline, after, label, _undoOwner,
            SW_DELEGATE_LAMBDA( EditorDocumentRestoreDelegate, [pTarget, documentPath]( string_view snapshot )
        {
            EditorDocumentPanel* pPanel = *pTarget;
            if ( pPanel == nullptr || documentPath != pPanel->_loadedAssetPath )
            {
                SW_LOG_WARNING( "Undo skipped: '%#' is no longer open in its panel", documentPath );
                return;
            }
            pPanel->restoreDocumentFromUndo( snapshot );
        } ),
            SW_DELEGATE_LAMBDA( EditorDocumentCaptureDelegate, [pTarget]()
        {
            EditorDocumentPanel* pPanel = *pTarget;
            return pPanel != nullptr ? pPanel->captureDocumentText() : string{};
        } ),
            coalesceKey );
        _documentUndoBaseline = after;
        markDocumentDirty();
    }
} // namespace sw::editor
