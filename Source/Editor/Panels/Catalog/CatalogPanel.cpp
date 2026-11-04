#include "pch.h"

#include "Editor/Panels/Catalog/CatalogPanel.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Asset/AssetDataKind.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 검증이 도는 동안 로더가 남기는 경고 · 오류 줄을 모읍니다. */
        class CatalogWarningCollector
        {
        public:
            CatalogWarningCollector()
                : _mutex{}
                , _listWarning{}
                , _subscription{}
            {
                _subscription = Logger::registerGlobalListener( LogWrittenDelegate::create<&CatalogWarningCollector::onLog>( this ) );
            }

            CatalogWarningCollector( const CatalogWarningCollector& )            = delete;
            CatalogWarningCollector& operator=( const CatalogWarningCollector& ) = delete;

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
                ScopedLock<mutex> lock{ _mutex };
                _listWarning.push_back( entry._message );
            }

        private:
            mutex          _mutex;
            vector<string> _listWarning;
            Subscription   _subscription; ///< 마지막 멤버라 먼저 풀린다 — 콜백이 만지는 `_mutex` · `_listWarning` 보다 먼저
        };

        // 시나리오 감시 값이 읽는 값입니다. 패널이 그릴 때 갱신하고 감시 값은 게임 스레드에서 읽습니다.
        atomic<int32> s_catalogFileCount{ 0 };
        atomic<int32> s_catalogRowCount{ 0 };
        atomic<int32> s_catalogValidation{ 0 }; ///< 0 아직, 1 통과, 2 실패

        struct CatalogPanelInternal
        {
            [[nodiscard]] static bool readFileCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( s_catalogFileCount.load() );
                return true;
            }

            [[nodiscard]] static bool readRowCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( s_catalogRowCount.load() );
                return true;
            }

            [[nodiscard]] static bool readValidation( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( s_catalogValidation.load() );
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
        , _listFile{}
        , _listWarning{}
        , _indexJob{}
        , _document{}
        , _tableEditor{}
        , _selectedID{}
        , _selectedKindLabel{}
        , _previousFileText{}
        , _statusText{}
        , _validationState{ ValidationState::None }
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
        _listFile.clear();
        vector<const AssetDataKind*> listKind;
        for ( const EditorAssetIndexEntry& entry : listEntry )
        {
            AssetDataKindUtil::collectMatches( entry._path, listKind );
            if ( listKind.size() != 1 )
                continue; // 카탈로그가 아니거나(0) 종류가 겹치는(스키마 테스트가 잡는다) 파일은 이 창이 다루지 않는다
            FileRow row;
            row._assetID   = entry._path;
            row._kindLabel = listKind[0]->_pLabel;
            _listFile.push_back( std::move( row ) );
        }
        VectorUtil::sort( _listFile, &CatalogPanel::isFileBefore );
        s_catalogFileCount.store( static_cast<int32>( _listFile.size() ) );
    }

    bool CatalogPanel::isFileBefore( const FileRow& lhs, const FileRow& rhs )
    {
        const int32 byKind = StringUtil::compare( lhs._kindLabel, rhs._kindLabel );
        return byKind != 0 ? byKind < 0 : StringUtil::compare( lhs._assetID, rhs._assetID ) < 0;
    }

    void CatalogPanel::drawToolbar()
    {
        if ( ImGui::Button( SW_LOCKEY( "EdText", "RefreshFiles" ) ) )
            _bIndexLoaded = SW_FALSE;
        EditorSelfTestMarks::note( "catalog.refresh" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Save" ) ) );
        ImGui::BeginDisabled( _selectedID.empty() );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Save" ) ) )
            (void)saveDocumentAndClearDirty(); // 실패하면 상태 줄에 이유가 있다
        EditorSelfTestMarks::note( "catalog.save" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Revert" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Revert" ) ) )
            revertDocument();
        EditorSelfTestMarks::note( "catalog.revert" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Undo" ) ) );
        ImGui::BeginDisabled( _document.canUndo() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Undo" ) ) )
        {
            SW_LOG_INFO_ID( "Catalog.Undo", "Catalog undo" );
            _document.undo();
            _tableEditor.reset();
            syncDirty();
        }
        EditorSelfTestMarks::note( "catalog.undo" );
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Redo" ) ) );
        ImGui::BeginDisabled( _document.canRedo() == false );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Redo" ) ) )
        {
            _document.redo();
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
        EditorSelfTestMarks::note( "catalog.validate" );
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "RawText" ) ) );
        ImGui::Checkbox( SW_LOCKEY( "EdLabel", "RawText" ), &_bRawMode );
        ImGui::EndDisabled();
    }

    void CatalogPanel::drawFileList()
    {
        EditorWidgets::drawSearchField( "##catalogFilter", _filter, SW_LOCKEY( "EditorCatalog", "FilterHint" ), -1.0f, false );
        if ( _bIndexLoaded == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorCatalog", "Scanning" ) );
            return;
        }
        if ( _listFile.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorCatalog", "NoKinds" ) );
            return;
        }

        const string_view filter     = _filter.c_str();
        bool              bFirstFile = true;
        size_t            index      = 0;
        while ( index < _listFile.size() )
        {
            size_t end = index;
            while ( end < _listFile.size() && _listFile[end]._kindLabel == _listFile[index]._kindLabel )
            {
                ++end;
            }
            StringBuilder<constant::buffer::kMax128> header;
            header.appendFormat( "%# (%#)###kind_%#", _listFile[index]._kindLabel, end - index, _listFile[index]._kindLabel );
            // 종류는 트리 노드(띠 없는 머리)로, 줄은 파일 이름 + 회색 폴더 — 좁은 왼쪽 칸에서 전체 경로가 잘려 같은 종류의 파일이 구분되지 않았다(uiaudit 24).
            if ( ImGui::TreeNodeEx( header.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen ) )
            {
                for ( size_t fileIndex = index; fileIndex < end; ++fileIndex )
                {
                    const FileRow& file = _listFile[fileIndex];
                    if ( filter.empty() == false && StringUtil::contains( file._assetID, filter, true ) == false )
                        continue;
                    const bool   bSelected = file._assetID == _selectedID;
                    const string fileName  = FileUtil::getFileNamePart( file._assetID );
                    const string label     = fileName + "##" + file._assetID;
                    if ( ImGui::Selectable( label.c_str(), bSelected ) && bSelected == false )
                    {
                        if ( _document.isDirty() )
                            _statusText = SW_LOCKEY( "EditorCatalog", "SaveOrRevertFirst" );
                        else
                            openFile( file._assetID );
                    }
                    if ( bFirstFile )
                    {
                        EditorSelfTestMarks::note( "catalog.file.first" ); // 표식은 마지막 항목을 가리킨다 — 회색 폴더 글보다 먼저
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
            index = end;
        }
    }

    void CatalogPanel::drawDocumentArea()
    {
        if ( _selectedID.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdLabel", "PickACatalogFileOnTheLeft" ) );
            return;
        }

        ImGui::Text( "%s", _selectedID.c_str() );
        ImGui::SameLine();
        ImGui::TextDisabled( "[%s]%s", _selectedKindLabel.c_str(), _document.isDirty() ? " *" : "" );
        drawValidation();
        ImGui::Separator();

        if ( _bRawMode || _document.isParsed() == false )
        {
            if ( _document.isParsed() == false )
                ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEMECOLOR( "Signal", "Error" ) ), "%s", SW_LOCKEYFORMAT( "EdFormat", "TheFileIsNot", TextArgumentList().addText( "error", _document.getError().c_str() ) ).c_str() );
            string       raw       = _document.getText();
            const uint32 lineCount = static_cast<uint32>( ImGui::GetContentRegionAvail().y / ImGui::GetTextLineHeightWithSpacing() ) - 1;
            if ( EditorWidgets::drawTextFieldMultiline( "##catalogRaw", raw, lineCount > 4 ? lineCount : 4 ) )
            {
                _document.replaceText( raw, false );
                syncDirty();
            }
            return;
        }

        if ( _tableEditor.draw( _document ) )
        {
            _statusText = _tableEditor.getLastActionText();
            syncDirty();
        }
    }

    void CatalogPanel::drawValidation()
    {
        if ( _statusText.empty() == false )
            ImGui::TextDisabled( "%s", _statusText.c_str() );
        if ( _validationState == ValidationState::Passed )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEMECOLOR( "Signal", "Success" ) ), "Validation passed" );
        }
        else if ( _validationState == ValidationState::Failed )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEMECOLOR( "Signal", "Error" ) ), "%s", SW_LOCKEYFORMAT( "EdFormat", "ValidationFailedWarnings", TextArgumentList().addInteger( "warningCount", static_cast<int64>( _listWarning.size() ) ) ).c_str() );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "RestorePreviousFile" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "RestorePreviousFile" ) ) )
                restorePreviousFile();
            EditorSelfTestMarks::note( "catalog.restore" );
            for ( const string& warning : _listWarning )
            {
                ImGui::BulletText( "%s", warning.c_str() );
            }
        }
    }

    void CatalogPanel::openFile( const string& assetID )
    {
        const std::optional<AbsolutePath> absolutePath = editor::getSubsystem<AssetPaths>().findLooseFile( assetID ); // 붙인 도메인과 게임 폴더를 거치므로 루트에 직접 붙이지 않는다
        string                            text;
        // 읽지 못하면 고르지 않는다 — 앞 파일의 글을 가진 채 고르면 저장할 때 앞 파일의 내용을 이 파일에 쓴다.
        if ( absolutePath.has_value() == false || FileUtil::readTextFile( absolutePath->str(), text ) == false )
        {
            SW_LOG_ERROR_ID( "CatalogPanel.CouldNotReadCatalog", "Could not read catalog file '%#'", assetID.c_str() );
            _statusText = "Could not read the file";
            return;
        }
        _selectedID.clear();
        _selectedKindLabel.clear();
        vector<const AssetDataKind*> listKind;
        AssetDataKindUtil::collectMatches( assetID, listKind );
        if ( listKind.size() == 1 )
            _selectedKindLabel = listKind[0]->_pLabel;
        _selectedID = assetID;
        (void)_document.load( text ); // 깨진 원문은 글 상자로 고치게 열어 둔다 — 이유는 문서가 안다
        _tableEditor.reset();
        _listWarning.clear();
        _validationState  = ValidationState::None;
        _previousFileText = text;
        _statusText.clear();
        syncDirty();
        SW_LOG_INFO_ID( "Catalog.Opened", "Catalog opened %#", assetID.c_str() );
    }

    bool CatalogPanel::writeCurrentFile()
    {
        if ( _selectedID.empty() )
            return false;
        if ( _document.isParsed() == false )
        {
            _statusText = "Fix the XML before saving";
            return false;
        }
        const std::optional<AbsolutePath> absolutePath = editor::getSubsystem<AssetPaths>().toDiskPath( _selectedID );
        if ( absolutePath.has_value() == false || FileUtil::writeTextFile( absolutePath->str(), _document.getText() ) == false )
        {
            SW_LOG_ERROR_ID( "CatalogPanel.CouldNotWriteCatalog", "Could not write catalog file '%#'", _selectedID.c_str() );
            _statusText = "Could not write the file";
            return false;
        }
        _previousFileText = _document.getSavedText(); // 저장 직전에 디스크에 있던 글
        _document.markSaved();
        SW_LOG_INFO_ID( "CatalogPanel.CatalogSaved", "Catalog saved %#", _selectedID.c_str() );
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
        _document.revert();
        _tableEditor.reset();
        _statusText = "Reverted to the saved text";
        syncDirty();
        SW_LOG_INFO_ID( "Catalog.Reverted", "Catalog reverted %#", _selectedID.c_str() );
    }

    void CatalogPanel::validateCurrentFile()
    {
        _listWarning.clear();
        _validationState           = ValidationState::None;
        const AssetDataKind* pKind = _selectedKindLabel.empty() ? nullptr : AssetDataKindUtil::findByLabel( _selectedKindLabel );
        if ( pKind == nullptr )
        {
            _statusText = "No registered kind - nothing to validate with";
            return;
        }
        bool bLoaded = false;
        {
            CatalogWarningCollector collector;
            bLoaded      = pKind->_pLoad( AssetID{ _selectedID } );
            _listWarning = collector.take();
        }
        const bool bPassed = bLoaded && _listWarning.empty();
        _statusText        = _document.isDirty() ? "Validated the saved file - unsaved edits are not included" : string();
        _validationState   = bPassed ? ValidationState::Passed : ValidationState::Failed;
        if ( bPassed )
            SW_LOG_INFO_ID( "Catalog.ValidationPassed", "Catalog validation passed (%#)", _selectedKindLabel.c_str() );
        else
            SW_LOG_INFO_ID( "CatalogPanel.CatalogValidationFailedWarnings", "Catalog validation failed (%#): %# warnings", _selectedKindLabel.c_str(), _listWarning.size() );
    }

    void CatalogPanel::restorePreviousFile()
    {
        if ( _selectedID.empty() )
            return;
        const std::optional<AbsolutePath> absolutePath = editor::getSubsystem<AssetPaths>().toDiskPath( _selectedID );
        if ( absolutePath.has_value() == false || FileUtil::writeTextFile( absolutePath->str(), _previousFileText ) == false )
        {
            SW_LOG_ERROR_ID( "CatalogPanel.CouldNotRestoreCatalog", "Could not restore catalog file '%#'", _selectedID.c_str() );
            _statusText = "Could not restore the file";
            return;
        }
        (void)_document.load( _previousFileText ); // 되돌린 글이 디스크와 문서의 새 기준이다
        _tableEditor.reset();
        _statusText = "Restored the file as it was before saving";
        validateCurrentFile();
        syncDirty();
        SW_LOG_INFO_ID( "CatalogPanel.CatalogRestored", "Catalog restored %#", _selectedID.c_str() );
    }

    void CatalogPanel::syncDirty()
    {
        int32 rowCount = 0;
        for ( const CatalogDocument::Group& group : _document.getGroups() )
        {
            rowCount += static_cast<int32>( group._listRow.size() );
        }
        s_catalogRowCount.store( rowCount );
        s_catalogValidation.store( static_cast<int32>( _validationState ) );
        if ( _document.isDirty() )
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
    SW_AUTOMATION_WATCH( catalogValidation, "Editor.CatalogValidation", "Last validation of the open catalog file: 0 not run, 1 passed, 2 failed",
                         &editor::CatalogPanelInternal::readValidation );
} // namespace sw
