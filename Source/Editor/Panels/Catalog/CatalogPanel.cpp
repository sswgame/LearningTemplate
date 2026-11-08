#include "pch.h"

#include "Editor/Panels/Catalog/CatalogPanel.h"

#include "Core/Concurrency/mutex.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 검증이 도는 동안 **이 호출의** 로더가 남기는 경고 · 오류 줄을 모읍니다. 다른 스레드의 줄이 섞이면 멀쩡한 파일이 실패로 보이므로 추적 id 로 가른다. */
        class CatalogWarningCollector
        {
        public:
            CatalogWarningCollector()
                : _traceID{ LogTraceID::makeRandom() }
                , _mutex{}
                , _listWarning{}
                , _subscription{}
            {
                _subscription = Logger::registerGlobalListener( LogWrittenDelegate::create<&CatalogWarningCollector::onLog>( this ) );
            }

            CatalogWarningCollector( const CatalogWarningCollector& )            = delete;
            CatalogWarningCollector& operator=( const CatalogWarningCollector& ) = delete;

            /** @brief 지금 스레드의 로그 문맥에 이 추적 id 를 얹은 값입니다. `ScopedLogContext` 에 넘깁니다. */
            [[nodiscard]] LogContext makeContext() const
            {
                LogContext context = LogContext::getCurrent();
                context._traceID   = _traceID;
                return context;
            }

            vector<string> take()
            {
                ScopedLock<mutex> lock{ _mutex };
                return std::move( _listWarning );
            }

        private:
            void onLog( const LogEntry& entry )
            {
                if ( entry._level != LogLevel::Warning && entry._level != LogLevel::Error )
                    return;
                if ( entry._context._traceID != _traceID )
                    return;
                ScopedLock<mutex> lock{ _mutex };
                _listWarning.push_back( entry._message );
            }

        private:
            LogTraceID     _traceID;
            mutex          _mutex;
            vector<string> _listWarning;
            Subscription   _subscription; ///< 마지막 멤버라 먼저 풀린다 — 콜백이 만지는 `_mutex` · `_listWarning` 보다 먼저
        };

        struct CatalogPanelInternal
        {
            /** @brief 열린 카탈로그 편집기입니다. 닫혔으면 nullptr 입니다(감시 값이 읽는다). */
            [[nodiscard]] static const CatalogPanel* findOpenPanel()
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                if ( pPanels == nullptr )
                    return nullptr;
                const CatalogPanel* pPanel = pPanels->findPanel<CatalogPanel>();
                return pPanel != nullptr && pPanel->isOpen() ? pPanel : nullptr;
            }

            [[nodiscard]] static bool readFileCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const CatalogPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getFileCount() );
                return true;
            }

            [[nodiscard]] static bool readRowCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const CatalogPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->countRows() );
                return true;
            }

            /** @brief 감시 값 `Editor.CatalogTextBytes` - 고치는 원문의 크기입니다. 부모와 같은 값을 적으면 원문이 처음과 같아지는지 시나리오가 봅니다. */
            [[nodiscard]] static bool readTextBytes( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const CatalogPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getTextByteCount() );
                return true;
            }

            [[nodiscard]] static bool readValidation( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const CatalogPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( static_cast<uint8>( pPanel->getValidationState() ) );
                return true;
            }

            static constexpr float32 kFileListWidth = 300.0f; ///< 96 DPI 기준입니다
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    CatalogPanel::CatalogPanel()
        : IEditorDocumentPanel( false )
        , _filter{}
        , _fileList{}
        , _listFileGroup{}
        , _indexJob{}
        , _inheritanceSource{}
        , _session{}
        , _tableEditor{}
        , _bIndexLoaded{ SW_FALSE }
        , _bRawMode{ false }
    {
    }

    void CatalogPanel::drawContent()
    {
        pollIndexJob();
        drawToolbar();
        ImGui::Separator();

        EditorSectionDesc listDesc{};
        listDesc._pID       = "##CatalogFileList";
        listDesc._kind      = EditorSectionKind::Child;
        listDesc._childSize = float2{ CatalogPanelInternal::kFileListWidth, 0.0f };
        listDesc._flags     = EditorSectionFlags::Border | EditorSectionFlags::ResizeX;
        if ( EditorChrome::beginSection( listDesc ) )
            drawFileList();
        EditorChrome::endSection();

        ImGui::SameLine();
        ImGui::BeginGroup();
        drawDocumentArea();
        ImGui::EndGroup();
    }

    void CatalogPanel::pollIndexJob()
    {
        vector<EditorAssetIndexEntry> listEntry;
        const EditorTaskPoll          poll = _indexJob.take( listEntry );
        if ( poll == EditorTaskPoll::Ready )
        {
            rebuildFileList( listEntry );
            _bIndexLoaded = SW_TRUE;
        }
        else if ( _bIndexLoaded == false && poll == EditorTaskPoll::Idle )
        {
            _indexJob.request();
        }
    }

    void CatalogPanel::rebuildFileList( const vector<EditorAssetIndexEntry>& listEntry )
    {
        vector<CatalogFileRow>      listRow;
        vector<const AssetTypeDef*> listKind;
        for ( const EditorAssetIndexEntry& entry : listEntry )
        {
            AssetTypeRegistry::collectPathMatches( entry._path, listKind );
            if ( listKind.size() != 1 )
                continue; // 카탈로그가 아니거나(0) 종류가 겹치는(스키마 테스트가 잡는다) 파일은 이 창이 다루지 않는다
            listRow.push_back( CatalogFileRow{ entry._path, string{ listKind[0]->_name } } );
        }
        _fileList.setRows( std::move( listRow ) );
    }

    void CatalogPanel::drawToolbar()
    {
        if ( ImGui::Button( SW_LOCKEY( "EdText", "RefreshFiles" ) ) )
            _bIndexLoaded = SW_FALSE;
        EditorSelfTestMarks::record( "catalog.refresh" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Save" ) ) );
        CatalogDocument& document = _session.editDocument();
        ImGui::BeginDisabled( _session.hasFile() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Save" ) ) )
            (void)saveDocumentAndClearDirty(); // 실패하면 상태 줄에 이유가 있다
        EditorSelfTestMarks::record( "catalog.save" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Revert" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Revert" ) ) )
            revertDocument();
        EditorSelfTestMarks::record( "catalog.revert" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Undo" ) ) );
        ImGui::BeginDisabled( document.canUndo() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Undo" ) ) )
        {
            SW_LOG_INFO_ID( "Catalog.Undo", "Catalog undo" );
            document.undo();
            _tableEditor.reset();
            syncDirty();
        }
        EditorSelfTestMarks::record( "catalog.undo" );
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Redo" ) ) );
        ImGui::BeginDisabled( document.canRedo() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Redo" ) ) )
        {
            document.redo();
            _tableEditor.reset();
            syncDirty();
        }
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Validate" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Validate" ) ) )
        {
            validateCurrentFile();
            syncDirty(); // 검증 결과 감시 값(`Editor.CatalogValidation`)도 여기서 갱신된다 — 저장 · 복원 길과 같다
        }
        EditorSelfTestMarks::record( "catalog.validate" );
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "RawText" ) ) );
        ImGui::Checkbox( SW_LOCKEY( "EdLabel", "RawText" ), &_bRawMode );
        ImGui::EndDisabled();
    }

    void CatalogPanel::drawFileList()
    {
        EditorWidgets::drawSearchField( "##catalogFilter", _filter, SW_LOCKEY( "EditorCatalog", "FilterHint" ), -1.0f, false );
        EditorSelfTestMarks::record( "catalog.filter" );
        if ( _bIndexLoaded == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorCatalog", "Scanning" ) );
            return;
        }
        if ( _fileList.getRows().empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorCatalog", "NoKinds" ) );
            return;
        }

        if ( _fileList.collectGroups( _filter.c_str(), _listFileGroup ) == 0 )
        {
            EditorWidgets::drawNoSearchResultHint( _filter.c_str() ); // 머리만 남으면 왜 비었는지 알 수 없다
            return;
        }
        bool bFirstFile = true;
        for ( const CatalogFileGroup& group : _listFileGroup )
        {
            StringBuilder<constant::buffer::kMax128> header;
            header.appendFormat( "%# (%#)###kind_%#", group._kindLabel, group._listRow.size(), group._kindLabel );
            // 종류는 트리 노드(띠 없는 머리)로, 줄은 파일 이름 + 회색 폴더 — 좁은 왼쪽 칸에서 전체 경로가 잘려 같은 종류의 파일이 구분되지 않았다(uiaudit 24).
            if ( ImGui::TreeNodeEx( header.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen ) == false )
                continue;
            for ( const uint32 rowIndex : group._listRow )
            {
                const CatalogFileRow& file      = _fileList.getRows()[rowIndex];
                const bool            bSelected = file._assetID == _session.getAssetID();
                const string          fileName  = FileUtil::getFileNamePart( file._assetID );
                const string          label     = fileName + "##" + file._assetID;
                if ( ImGui::Selectable( label.c_str(), bSelected ) && bSelected == false )
                {
                    if ( _session.getDocument().isDirty() )
                        _session.setStatus( SW_LOCKEY( "EditorCatalog", "SaveOrRevertFirst" ) );
                    else
                        openFile( file._assetID );
                }
                if ( bFirstFile )
                {
                    EditorSelfTestMarks::record( "catalog.file.first" ); // 표식은 마지막 항목을 가리킨다 — 회색 폴더 글보다 먼저
                    bFirstFile = false;
                }
                EditorWidgets::drawTooltip( file._assetID.c_str() );
                const string_view folder = string_view( file._assetID ).substr( 0, file._assetID.size() - fileName.size() );
                if ( folder.empty() == false )
                {
                    // 선택 줄은 폭 전체를 쓰므로 같은 줄에 잇지 않고 그 줄 위, 파일 이름 뒤에 회색으로 얹는다(배치는 바꾸지 않는다).
                    const ImVec2  itemMin = ImGui::GetItemRectMin();
                    const float32 textY   = itemMin.y + ( ImGui::GetItemRectSize().y - ImGui::GetFontSize() ) * 0.5f; // 선택 줄이 이름을 세로 가운데에 그리는 자리
                    const ImVec2  at{ itemMin.x + ImGui::CalcTextSize( fileName.c_str() ).x + ImGui::GetStyle().ItemSpacing.x, textY };
                    ImGui::GetWindowDrawList()->AddText( at, ImGui::GetColorU32( ImGuiCol_TextDisabled ), folder.data(), folder.data() + folder.size() );
                }
            }
        }
    }

    void CatalogPanel::drawDocumentArea()
    {
        if ( _session.hasFile() == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdLabel", "PickACatalogFileOnTheLeft" ) );
            return;
        }

        CatalogDocument& document = _session.editDocument();
        ImGui::Text( "%s", _session.getAssetID().c_str() );
        ImGui::SameLine();
        ImGui::TextDisabled( "[%s]%s", _session.getKindLabel().c_str(), document.isDirty() ? " *" : "" );
        drawValidation();
        ImGui::Separator();

        if ( _bRawMode || document.isParsed() == false )
        {
            if ( document.isParsed() == false )
                ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", SW_LOCKEYFORMAT( "EdFormat", "TheFileIsNot", TextArgumentList().addText( "error", document.getError().c_str() ) ).c_str() );
            string raw = document.getText();
            // 줄 수를 부호 있게 센다 — 남은 높이가 한 줄도 안 되면 부호 없는 뺄셈이 넘쳐 글 상자가 터무니없는 높이가 되었다.
            const int32 lineCount = MathUtil::max( static_cast<int32>( ImGui::GetContentRegionAvail().y / ImGui::GetTextLineHeightWithSpacing() ) - 1, 4 );
            if ( EditorWidgets::drawTextFieldMultiline( "##catalogRaw", raw, static_cast<uint32>( lineCount ) ) )
            {
                document.replaceText( raw );
                syncDirty();
            }
            if ( ImGui::IsItemDeactivated() )
                document.endTextEdit(); // 다음 입력은 새 되돌리기 단계다
            return;
        }

        const bool bChanged = _tableEditor.draw( document );
        if ( bChanged || _tableEditor.takeEditFailed() )
            _session.setStatus( _tableEditor.getLastActionText() );
        if ( bChanged )
            syncDirty();
    }

    void CatalogPanel::drawValidation()
    {
        if ( _session.getStatus().empty() == false )
            ImGui::TextDisabled( "%s", _session.getStatus().c_str() );
        const CatalogFileSession::ValidationState state = _session.getValidationState();
        if ( state == CatalogFileSession::ValidationState::Passed )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Success" ) ), "Validation passed" );
        }
        else if ( state == CatalogFileSession::ValidationState::Failed )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", SW_LOCKEYFORMAT( "EdFormat", "ValidationFailedWarnings", TextArgumentList().addInteger( "warningCount", static_cast<int64>( _session.getWarnings().size() ) ) ).c_str() );
            // 이번 저장 전 글이 없으면(방금 연 파일) 되쓸 것이 없다 — 같은 글을 되쓰는 단추를 보이지 않는다.
            if ( _session.canRestorePrevious() )
            {
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "RestorePreviousFile" ) ) );
                if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "RestorePreviousFile" ) ) )
                    restorePreviousFile();
                EditorSelfTestMarks::record( "catalog.restore" );
            }
            for ( const string& warning : _session.getWarnings() )
            {
                ImGui::BulletText( "%s", warning.c_str() );
            }
        }
    }

    void CatalogPanel::openFile( const string& assetID )
    {
        const optional<AbsolutePath> absolutePath = EditorPaths::getAssetPaths().findLooseFile( assetID ); // 붙인 도메인과 게임 폴더를 거치므로 루트에 직접 붙이지 않는다
        string                       text;
        // 읽지 못하면 고르지 않는다 — 앞 파일의 글을 가진 채 고르면 저장할 때 앞 파일의 내용을 이 파일에 쓴다.
        if ( absolutePath.has_value() == false || FileUtil::readTextFile( absolutePath->str(), text ) == false )
        {
            SW_LOG_ERROR_ID( "CatalogPanel.CouldNotReadCatalog", "Could not read catalog file '%#'", assetID.c_str() );
            _session.setStatus( "Could not read the file" );
            return;
        }
        vector<const AssetTypeDef*> listKind;
        AssetTypeRegistry::collectPathMatches( assetID, listKind );
        _session.open( assetID, listKind.size() == 1 ? listKind[0]->_name : string_view(), text, &_inheritanceSource );
        _tableEditor.reset();
        syncDirty();
        SW_LOG_INFO_ID( "Catalog.Opened", "Catalog opened %#", assetID.c_str() );
    }

    bool CatalogPanel::writeCurrentFile()
    {
        if ( _session.hasFile() == false )
            return false;
        if ( _session.getDocument().isParsed() == false )
        {
            _session.setStatus( "Fix the XML before saving" );
            return false;
        }
        const string&                assetID      = _session.getAssetID();
        const optional<AbsolutePath> absolutePath = EditorPaths::getAssetPaths().toDiskPath( assetID );
        if ( absolutePath.has_value() == false )
        {
            SW_LOG_ERROR_ID( "CatalogPanel.CouldNotWriteCatalog", "Could not write catalog file '%#'", assetID.c_str() );
            _session.setStatus( "Could not write the file" );
            return false;
        }
        // 연 뒤 밖에서 바뀐 파일을 말없이 덮어쓰지 않는다. 지워진 파일(읽지 못함)은 다시 만든다.
        string diskText;
        if ( FileUtil::readTextFile( absolutePath->str(), diskText ) && _session.checkDiskBeforeSave( diskText ) == false )
            return false;
        if ( FileUtil::writeTextFile( absolutePath->str(), _session.getDocument().getText() ) == false )
        {
            SW_LOG_ERROR_ID( "CatalogPanel.CouldNotWriteCatalog", "Could not write catalog file '%#'", assetID.c_str() );
            _session.setStatus( "Could not write the file" );
            return false;
        }
        _session.onSaved();
        SW_LOG_INFO_ID( "CatalogPanel.CatalogSaved", "Catalog saved %#", assetID.c_str() );
        return true;
    }

    bool CatalogPanel::saveDocument()
    {
        if ( writeCurrentFile() == false )
            return false;
        validateCurrentFile();
        syncDirty();
        return true;
    }

    void CatalogPanel::revertDocument()
    {
        _session.editDocument().revert();
        _tableEditor.reset();
        _session.setStatus( "Reverted to the saved text - Undo brings the edits back" );
        syncDirty();
        SW_LOG_INFO_ID( "Catalog.Reverted", "Catalog reverted %#", _session.getAssetID().c_str() );
    }

    void CatalogPanel::validateCurrentFile()
    {
        _session.clearValidation();
        const string&       kindLabel = _session.getKindLabel();
        const AssetTypeDef* pKind     = kindLabel.empty() ? nullptr : AssetTypeRegistry::findType( kindLabel );
        if ( pKind == nullptr )
        {
            _session.setStatus( "No registered kind - nothing to validate with" );
            return;
        }
        bool           bLoaded = false;
        vector<string> listWarning;
        {
            CatalogWarningCollector collector;
            const ScopedLogContext  context{ collector.makeContext() };
            bLoaded     = AssetTypeRegistry::checkLoad( *pKind, AssetID{ _session.getAssetID() } );
            listWarning = collector.take();
        }
        _session.onValidated( bLoaded, std::move( listWarning ) );
        if ( _session.getValidationState() == CatalogFileSession::ValidationState::Passed )
            SW_LOG_INFO_ID( "Catalog.ValidationPassed", "Catalog validation passed (%#)", kindLabel.c_str() );
        else
            SW_LOG_INFO_ID( "CatalogPanel.CatalogValidationFailedWarnings", "Catalog validation failed (%#): %# warnings", kindLabel.c_str(), _session.getWarnings().size() );
    }

    void CatalogPanel::restorePreviousFile()
    {
        if ( _session.hasFile() == false )
            return;
        const optional<AbsolutePath> absolutePath = EditorPaths::getAssetPaths().toDiskPath( _session.getAssetID() );
        if ( absolutePath.has_value() == false || FileUtil::writeTextFile( absolutePath->str(), _session.getPreviousText() ) == false )
        {
            SW_LOG_ERROR_ID( "CatalogPanel.CouldNotRestoreCatalog", "Could not restore catalog file '%#'", _session.getAssetID().c_str() );
            _session.setStatus( "Could not restore the file" );
            return;
        }
        _session.onRestored();
        _tableEditor.reset();
        validateCurrentFile();
        _session.setStatus( "Restored the file as it was before saving" ); // 검증이 상태 줄을 비우므로 그 뒤에 적는다
        syncDirty();
        SW_LOG_INFO_ID( "CatalogPanel.CatalogRestored", "Catalog restored %#", _session.getAssetID().c_str() );
    }

    void CatalogPanel::syncDirty()
    {
        if ( _session.getDocument().isDirty() )
            markDocumentDirty();
        else
            clearDocumentDirty();
    }

    SW_EDITOR_PANEL( CatalogPanel, CatalogPanel::kPanelID, EditorPanelCategory::Tool, 1710 );
} // namespace sw::editor

namespace sw
{
    SW_AUTOMATION_WATCH( catalogFileCount, "Editor.CatalogFileCount", "Catalog files the Catalog Editor lists (0 until its scan finishes)",
                         &editor::CatalogPanelInternal::readFileCount );
    SW_AUTOMATION_WATCH( catalogRowCount, "Editor.CatalogRowCount", "Rows (elements under the root) of the catalog file open in the Catalog Editor",
                         &editor::CatalogPanelInternal::readRowCount );
    SW_AUTOMATION_WATCH( catalogTextBytes, "Editor.CatalogTextBytes", "Size in bytes of the text of the catalog file open in the Catalog Editor", &editor::CatalogPanelInternal::readTextBytes );
    SW_AUTOMATION_WATCH( catalogValidation, "Editor.CatalogValidation", "Last validation of the open catalog file: 0 not run, 1 passed, 2 failed",
                         &editor::CatalogPanelInternal::readValidation );
} // namespace sw
