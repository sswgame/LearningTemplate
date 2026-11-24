// 문서 패널의 문서 상태 · 읽기 · 저장 · 되돌리기 · 자동 저장입니다(ImGui 없음). 그리기는 `EditorDocumentPanel.cpp`(MVVM View)다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Commands/Asset/EditorAssetCatalog.h"
#include "Editor/Common/Commands/Asset/EditorDocumentAutosave.h"
#include "Editor/Common/Commands/Scene/EditorAutosaveCommands.h"
#include "Editor/Common/GUI/Panel/EditorDocumentPanel.h"
#include "Editor/Common/GUI/Panel/Model/EditorDocumentOpenUtil.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"
#include "Editor/Common/Workspace/Undo/DocumentEditSpan.h"
#include "Editor/Common/Workspace/Undo/EditorUndoHistory.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/ContentHash.h"

namespace sw::editor
{
    void EditorDocumentPanel::requestOpenDocument( string_view assetPath )
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        editorContext.getSubsystem<EditorWorkspace>().setFocusedAssetPath( string{ assetPath }.c_str() );
    }

    void EditorDocumentPanel::collectOpenCandidates()
    {
        // 열 때 한 번 모은다 — 에디터의 에셋 색인에서 읽는다(디스크를 걷지 않는다).
        vector<EditorAssetIndexEntry> listEntry;
        EditorAssetCatalog::collectEntriesOnUIThread( editor::getSubsystem<AssetPaths>(), listEntry );
        EditorDocumentOpenUtil::collectCandidates( listEntry, _kind, [this]( string_view path )
        { return canAcceptDocumentPath( path ); }, _listOpenCandidate );
    }

    void EditorDocumentPanel::restoreFocusAfterCancel()
    {
        EditorDocumentOpenUtil::restoreFocusAfterCancel( editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>(), _loadedAssetPath );
    }

    EditorDocumentPanel::EditorDocumentPanel( const EditorAssetType& kind, bool bLoadOnOpen )
        : EditorDocumentPanelBase{ false }
        , _kind{ kind }
        , _loadedAssetPath{}
        , _pendingFocusPath{}
        , _lastSavedDocumentText{}
        , _assetsMovedSubscription{}
        , _pUndoTarget{ make_shared<EditorDocumentPanel*>( this ) }
        , _pEditRecorder{ make_unique<DocumentEditRecorder>( SW_DELEGATE_METHOD( DocumentEditRecorder::RecordDelegate, &EditorDocumentPanel::notifyDocumentEdited, this ) ) }
        , _pEditSpan{ make_unique<DocumentEditSpan>() }
        , _pUndoOwner{ make_unique<UndoStepOwner>() }
        , _pDocumentStack{ make_unique<CommandStack>( EditorUndoHistory::readConfiguredLimit( getEditorToolDefaults() ) ) }
        , _pStackInteraction{ make_unique<CommandStackInteractionScope>() }
        , _listRecentPath{}
        , _listOpenCandidate{}
        , _openFilter{}
        , _movedUnsavedText{}
        , _documentSerial{ 0 }
        , _lastCopyTextHash{ 0 }
        , _bLoaded{ SW_FALSE }
        , _bConfirmSwitch{ SW_FALSE }
        , _bLoadFailed{ SW_FALSE }
        , _reserved{ 0 }
    {
        if ( bLoadOnOpen == false )
            _bLoaded = SW_TRUE;
        // 열린 문서의 에셋을 옮기면 경로가 따라가고, 글을 고친 참조 문서는 다시 읽는다 — 옛 id 로 남으면 다음 저장이 옛 자리에 파일을 다시 만들거나 고친 참조를 덮는다(감사 P0-4).
        EditorWorkspace* const pWorkspace = editor::findSubsystem<EditorWorkspace>(); // 작업 공간 없는 패널 테스트는 옮김도 없다
        if ( pWorkspace != nullptr )
        {
            _assetsMovedSubscription = pWorkspace->onAssetsMoved().add(
                SW_DELEGATE_LAMBDA( EditorAssetsMovedDelegate, [this]( const EditorAssetMoveEvent& event )
            {
                for ( size_t index = 0; index < event._listOldID.size(); ++index )
                    onAssetMoved( event._listOldID[index], event._listNewID[index] );
                const string previousPath       = _loadedAssetPath;
                const bool   bRewrittenReferrer = EditorDocumentOpenUtil::applyAssetMove( event, _loadedAssetPath, _listRecentPath );
                if ( previousPath != _loadedAssetPath )
                {
                    // 자동 저장 사본은 옛 id 로 적혀 있다 — 남기면 비정상 종료 뒤 없는 문서를 복구하라고 묻는다. 미저장이면 다음 주기가 새 id 로 다시 쓴다.
                    EditorDocumentAutosaveEntry::remove( EditorAutosaveCommands::getSessionFolder(), previousPath );
                    _lastCopyTextHash = 0;
                    // 파생의 모델이 든 경로(저장할 자리)도 새 자리여야 한다 — 새 자리에서 다시 읽어 맞추고, 미저장이면 편집한 글을 그 위에 얹는다(`markDocumentLoaded`).
                    // 글이 없는 미저장 문서는 얹을 수 없어 계획이 거절했다(`EditorOpenDocument::_bFollowsMove`).
                    if ( _bLoadFailed == SW_FALSE )
                    {
                        if ( isDocumentDirty() )
                            _movedUnsavedText = captureDocumentText();
                        _bLoaded = SW_FALSE;
                    }
                }
                // 깨끗한 참조 문서는 디스크에서 고친 글을 다시 읽는다 — 옛 id 를 든 채 두면 다음 저장이 고친 참조를 옛 id 로 덮는다. 읽기는 다음 그리기의
                // `ensureDocumentLoaded` 가 한다(옮기기는 다른 창의 그리기 안에서 돈다). 미저장 참조자는 계획이 거절해 여기 오지 않는다.
                if ( bRewrittenReferrer && isDocumentDirty() == false && _bLoadFailed == SW_FALSE )
                    _bLoaded = SW_FALSE;
            } ) );
        }
    }

    void EditorDocumentPanel::collectFilesToSave( vector<AbsolutePath>& outListPath ) const
    {
        appendFileToSave( _loadedAssetPath, outListPath );
    }

    void EditorDocumentPanel::appendFileToSave( string_view path, vector<AbsolutePath>& outListPath )
    {
        if ( path.empty() )
            return;
        if ( AbsolutePath::isStrictAbsolute( path ) )
        {
            outListPath.push_back( FileUtil::toDiskPath( path ) );
            return;
        }
        const optional<AbsolutePath> diskPath = editor::getSubsystem<AssetPaths>().toDiskPath( path );
        if ( diskPath.has_value() )
            outListPath.push_back( *diskPath );
    }

    void EditorDocumentPanel::prepareDocumentForSave()
    {
        if ( _movedUnsavedText.empty() == false )
            ensureDocumentLoaded();
    }

    void EditorDocumentPanel::onDocumentSaved()
    {
        // 저장한 원본이 사본보다 새것이다 — 남기면 다음 기동이 이미 저장한 일을 복구하라고 묻는다(씬 자동 저장의 `onSceneSaved` 와 같다)
        EditorDocumentAutosaveEntry::remove( EditorAutosaveCommands::getSessionFolder(), _loadedAssetPath );
        _lastCopyTextHash       = 0;
        EditorContext* pContext = editor::findSubsystem<EditorContext>(); // 컨텍스트 없는 패널 테스트는 감시도 없다
        if ( _loadedAssetPath.empty() == false && pContext != nullptr )
            pContext->getSubsystem<AssetHotReload>().applySavedAsset( _loadedAssetPath );
    }

    EditorDocumentPanel::~EditorDocumentPanel()
    {
        *_pUndoTarget = nullptr; // Ctrl+Z 의 스택은 패널 id 로 찾으므로 사라질 스택을 풀 일이 없다(`setFocusedPanel`)
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
        _movedUnsavedText.clear();
        ++_documentSerial;
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
        clearDocumentDirty();
        resetDocumentHistory();
        syncDocumentUndoBaseline();
        captureDiskBaseline();
        // 디스크의 글로 (다시) 읽었다 — 앞 편집의 사본은 낡았다(저장 안 함 · 다시 읽기). 복구 청이 이 문서면 사본 글을 되돌리기와 같은 길로 적용해 dirty 로 둔다(감사 E-1).
        EditorDocumentAutosaveEntry::remove( EditorAutosaveCommands::getSessionFolder(), _loadedAssetPath );
        _lastCopyTextHash = 0;
        string restoredText;
        if ( _loadedAssetPath.empty() == false && EditorAutosaveCommands::takePendingDocumentRestore( _loadedAssetPath, restoredText ) )
            restoreDocumentFromUndo( restoredText );
        else if ( _movedUnsavedText.empty() == false )
            restoreDocumentFromUndo( _movedUnsavedText ); // 옮기기 앞의 미저장 편집
        _movedUnsavedText.clear();
    }

    void EditorDocumentPanel::applyPendingAutosaveRestore()
    {
        // 복원할 문서가 이미 열려 있으면 읽기가 다시 오지 않는다 — 그 자리에서 사본 글을 되돌릴 수 있는 편집 한 단계로 적용한다(감사 E-1). 열려 있지 않은 문서는 읽은 직후(`markDocumentLoaded`)에 적용한다.
        string restoredText;
        if ( isDocumentLoaded() == false || _bLoadFailed == SW_TRUE || _loadedAssetPath.empty() ||
             EditorAutosaveCommands::takePendingDocumentRestore( _loadedAssetPath, restoredText ) == false )
            return;
        const string before = captureDocumentText();
        applyDocumentText( restoredText );
        _pEditSpan->resetBaseline( restoredText );
        syncStackInteraction();
        if ( before != restoredText )
            recordDocumentEdit( DocumentEdit::make( before, restoredText, "Restore Autosave Copy" ) );
    }

    DocumentAutosaveResult EditorDocumentPanel::takeAutosaveCopies( vector<EditorDocumentAutosaveEntry>& inoutListEntry )
    {
        if ( isDocumentDirty() == false || _bLoadFailed == SW_TRUE || _loadedAssetPath.empty() )
            return DocumentAutosaveResult::Nothing;
        if ( _pEditSpan->isHolding() || isWidgetInteractionActive() )
            return DocumentAutosaveResult::Deferred; // 끄는 중간 상태를 사본으로 두지 않는다 — 다음 프레임에 다시 묻는다
        string       text     = captureDocumentText();
        const uint64 textHash = text.empty() ? 0u : ContentHash::computeBytes( reinterpret_cast<const uint8*>( text.data() ), text.size() );
        if ( textHash == 0 || textHash == _lastCopyTextHash )
            return DocumentAutosaveResult::Nothing; // 문서 글이 없는 패널이거나 지난 사본과 같다
        _lastCopyTextHash = textHash;
        EditorDocumentAutosaveEntry entry{};
        entry._documentPath = _loadedAssetPath;
        entry._text         = std::move( text );
        inoutListEntry.push_back( std::move( entry ) );
        return DocumentAutosaveResult::Ready;
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

    void EditorDocumentPanel::resetDocumentHistory()
    {
        _pDocumentStack->clear();
    }

    void EditorDocumentPanel::flushDocumentInteraction()
    {
        if ( _pEditSpan->isHolding() == false || isWidgetInteractionActive() )
            return;
        const optional<DocumentEdit> edit = _pEditSpan->finish( captureDocumentText() );
        syncStackInteraction(); // 기록 전에 닫는다 — 기록은 열린 상호작용과 무관하지만 다음 Ctrl+Z 는 이 단계를 되돌려야 한다
        if ( edit.has_value() )
            recordDocumentEdit( *edit );
    }

    void EditorDocumentPanel::syncStackInteraction()
    {
        const bool bHolding = _pEditSpan->isHolding();
        if ( bHolding == _pStackInteraction->isOpen() )
            return;
        if ( bHolding )
            *_pStackInteraction = _pDocumentStack->openInteraction();
        else
            _pStackInteraction->reset();
    }

    void EditorDocumentPanel::syncDocumentUndoBaseline()
    {
        _lastSavedDocumentText = captureDocumentText();
        _pEditSpan->resetBaseline( _lastSavedDocumentText ); // 문서를 다시 읽었거나 저장했다 — 쥔 구간의 기준선이 없다
        syncStackInteraction();
    }

    void EditorDocumentPanel::restoreDocumentFromUndo( string_view text )
    {
        applyDocumentText( text );
        _pEditSpan->resetBaseline( string{ text } );
        syncStackInteraction();
        if ( EditorSessionPolicy::shouldClearDocumentDirtyOnRestore( text == _lastSavedDocumentText ) )
            clearDocumentDirty();
        else
            markDocumentDirty();
    }

    expected<void> EditorDocumentPanel::validateUndoRoundTrip()
    {
        if ( isDocumentLoaded() == false || isDocumentLoadFailed() )
            return unexpected( Error{ ErrorKind::NotFound, "no document is loaded in the panel", getPanelTitle() } );
        const string before = captureDocumentText();
        if ( before.empty() )
            return unexpected( Error{ ErrorKind::Unsupported, "the panel has no document text undo (captureDocumentText is empty)", getPanelTitle() } );
        restoreDocumentFromUndo( before ); // 되돌리기 단계가 부르는 그 길이다
        if ( captureDocumentText() != before )
            return unexpected( Error{ ErrorKind::InvalidArgument, "applying the document text and capturing it again gives other text - undo would not restore the document", _loadedAssetPath } );
        return {};
    }

    void EditorDocumentPanel::notifyDocumentEdited( string_view label )
    {
        // 상호작용 구간(감사 P0-1): 위젯이 활성인 동안의 편집(슬라이더 · 노드 끌기 · 칠하기 · 글 칸)은 쥐고, 풀린 프레임에 (구간 전 글, 끝 글)을 한 번 기록한다
        // (`flushDocumentInteraction`). 예전에는 합치기 키로 합쳤는데 기록이 거래 구간 안이라 병합에 닿지 않아 끌기 한 번이 프레임마다 한 단계였고, 한도(128)를 넘겨
        // 씬 편집 기록까지 밀어냈다. 언리얼이 누를 때 FScopedTransaction 을 열고 뗄 때 닫는 것과 같은 모양이다.
        // 바뀐 것이 없으면 아무것도 하지 않는다 — 매 프레임 알리는 패널이 활성 위젯(글 칸) 동안 dirty 로 표시하면 제목이 바뀌어 그 칸이 포커스를 잃는다.
        // 직렬화는 구간이 필요할 때만 한다(시작 · 끝 두 번 — 감사 E-9). 끄는 동안의 알림은 표시만 한다.
        const optional<DocumentEdit> edit = _pEditSpan->notifyEdited( label, isWidgetInteractionActive(), [this]()
        {
            const Stopwatch stopwatch; // 차이 저장의 판정 근거(편집당 1 ms) — 구간의 시작 · 단발 편집에서만 만든다
            string          text     = captureDocumentText();
            _lastCaptureMicroseconds = static_cast<uint64>( stopwatch.getElapsedNanoseconds() / constant::time::kNanosecondsPerMicrosecond );
            return text;
        } );
        syncStackInteraction();
        if ( edit.has_value() )
            recordDocumentEdit( *edit );
        else if ( _pEditSpan->isHolding() )
            markDocumentDirty();
    }

    void EditorDocumentPanel::recordDocumentEdit( const DocumentEdit& edit )
    {
        EditorTransaction transaction; // 씬을 바꾸는 기록은 거래 구간 안에서만 한다(`EditorTransaction` 의 기록 함수는 인스턴스 멤버다)
        // 이 편집이 속한 문서를 적어 둔다 — 되돌리기는 **그 문서가 아직 열려 있을 때만** 적용한다. 주의: `this` 와 바뀐 구간만 잡으면
        // 다른 문서로 바꾼 뒤 Ctrl+Z 할 때 앞 문서의 구간을 지금 문서의 텍스트에 붙여, 지금 문서가 깨진 채 dirty 가 되고 저장하면
        // 그대로 쓰인다. (구간 다시 짜기는 길이가 맞지 않아도 메모리를 넘지 않으므로, 막는 곳은 적용하는 쪽 하나면 된다.)
        // 패널은 `this` 가 아니라 표지로 잡는다 — 백엔드 교체는 패널을 다시 만들지만 되돌리기 스택은 남긴다. 사라진 패널의 항목은 건너뛴다.
        const uint32                           documentSerial = _documentSerial;
        const shared_ptr<EditorDocumentPanel*> pTarget        = _pUndoTarget;
        transaction.recordDocumentText(
            *edit._pBefore, *edit._pAfter, edit._label, *_pUndoOwner,
            SW_DELEGATE_LAMBDA( EditorDocumentRestoreDelegate, [pTarget, documentSerial]( string_view snapshot )
        {
            EditorDocumentPanel* pPanel = *pTarget;
            if ( pPanel == nullptr || documentSerial != pPanel->_documentSerial )
            {
                SW_LOG_WARNING_ID( "EditorDocumentPanel.UndoSkippedNoLonger", "Undo skipped: the edited document is no longer open in its panel" );
                return;
            }
            pPanel->restoreDocumentFromUndo( snapshot );
        } ),
            SW_DELEGATE_LAMBDA( EditorDocumentCaptureDelegate, [pTarget]()
        {
            EditorDocumentPanel* pPanel = *pTarget;
            return pPanel != nullptr ? pPanel->captureDocumentText() : string{};
        } ),
            getDocumentCommandStack() );
        markDocumentDirty();
    }
} // namespace sw::editor
