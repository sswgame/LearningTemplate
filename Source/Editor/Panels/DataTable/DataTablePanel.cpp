#include "pch.h"

#include "Editor/Panels/DataTable/DataTablePanel.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/formatString.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct DataTablePanelInternal
        {
            /** @brief 열린 데이터 테이블 창의 현지화 표 레코드 수 · 보이는 행 수를 읽습니다. 창이 닫혔거나 표를 읽지 않았으면 false 입니다. */
            [[nodiscard]] static bool readRecordCounts( uint32& outRecordCount, uint32& outVisibleCount )
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                if ( pPanels == nullptr )
                    return false;
                const DataTablePanel* pPanel = pPanels->findPanel<DataTablePanel>();
                return pPanel != nullptr && pPanel->isOpen() && pPanel->findLocalizationRecordCounts( outRecordCount, outVisibleCount );
            }

            [[nodiscard]] static bool readRecordCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                uint32 recordCount{ 0 };
                uint32 visibleCount{ 0 };
                if ( readRecordCounts( recordCount, visibleCount ) == false )
                    return false;
                outValue = static_cast<float64>( recordCount );
                return true;
            }

            [[nodiscard]] static bool readGameDataRowCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                const DataTablePanel*     pPanel  = pPanels != nullptr ? pPanels->findPanel<DataTablePanel>() : nullptr;
                uint32                    rowCount{ 0 };
                if ( pPanel == nullptr || pPanel->isOpen() == false || pPanel->findGameDataRowCount( rowCount ) == false )
                    return false;
                outValue = static_cast<float64>( rowCount );
                return true;
            }

            [[nodiscard]] static bool readVisibleRecordCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                uint32 recordCount{ 0 };
                uint32 visibleCount{ 0 };
                if ( readRecordCounts( recordCount, visibleCount ) == false )
                    return false;
                outValue = static_cast<float64>( visibleCount );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( DataTablePanel, DataTablePanel::kPanelID, EditorPanelCategory::Tool, 1700 );
    SW_AUTOMATION_WALL_WATCH( editorDataTableRecordCount, "Editor.DataTableRecordCount", "Records of the localization table the Data Table window holds (fails while it is closed or not loaded)",
                              &DataTablePanelInternal::readRecordCount );
    SW_AUTOMATION_WALL_WATCH( editorDataTableVisibleRecordCount, "Editor.DataTableVisibleRecordCount", "Localization rows the Data Table window drew after its search in the last frame",
                              &DataTablePanelInternal::readVisibleRecordCount );
    SW_AUTOMATION_WALL_WATCH( editorDataTableGameDataRowCount, "Editor.DataTableGameDataRowCount",
                              "Rows (elements under the root) of the game data table open in the Data Table window (fails while it is closed or no table is open)",
                              &DataTablePanelInternal::readGameDataRowCount );

    DataTablePanel::DataTablePanel()
        : EditorDocumentPanel{ EditorAssetType{ EditorAssetTypeIDs::kData }, false }
        , _model{}
        , _localizationFilter{}
        , _newKeyBuffer{}
        , _listVisibleLocalizationIndex{}
        , _gameDataTable{}
        , _seenGameDataLoadCount{ 0 }
        , _bGameDataRaw{ false }
        , _bNeedsWorkOnly{ false }
    {
    }

    bool DataTablePanel::saveDocument()
    {
        const bool bSaved = _model.saveDocument();
        syncDocumentDirty();
        return bSaved;
    }

    void DataTablePanel::collectFilesToSave( vector<string>& outListPath ) const
    {
        _model.collectFilesToSave( outListPath );
    }

    bool DataTablePanel::canAcceptDocumentPath( string_view assetPath ) const
    {
        return FileUtil::hasExtension( assetPath, ".xml" );
    }

    ToolAssetLoadResult DataTablePanel::loadDocument()
    {
        if ( acceptDocumentToLoad().empty() )
            return ToolAssetLoadResult::Missing; // 아직 연 표가 없다 — 저장은 현지화 표만 본다
        const optional<AbsolutePath> diskPath = getDocumentDiskPath();
        // 읽지 못하면 모델이 아무 표도 열지 않고 저장을 막는다 — 앞 표의 글을 이 파일에 쓰지 않는다.
        if ( diskPath.has_value() == false || _model.openGameDataFile( diskPath->str() ) == false )
            return ToolAssetLoadResult::Malformed;
        return ToolAssetLoadResult::Loaded;
    }

    void DataTablePanel::revertDocument()
    {
        _model.revertLocalization();
        EditorDocumentPanel::revertDocument(); // 게임 데이터 표는 디스크에서 다시 읽는다
        syncDocumentDirty();
    }

    bool DataTablePanel::findLocalizationRecordCounts( uint32& outRecordCount, uint32& outVisibleCount ) const
    {
        if ( _model.isLocalizationLoaded() == false )
            return false;
        outRecordCount  = static_cast<uint32>( _model.getSheet()._listRecord.size() );
        outVisibleCount = static_cast<uint32>( _listVisibleLocalizationIndex.size() );
        return true;
    }

    bool DataTablePanel::findGameDataRowCount( uint32& outRowCount ) const
    {
        if ( _model.getGameDataPath().empty() || _model.getGameDataDocument().isParsed() == false )
            return false;
        outRowCount = _model.getGameDataDocument().countRows();
        return true;
    }

    void DataTablePanel::syncDocumentDirty()
    {
        // 기반 클래스의 dirty 비트는 "무언가 바뀌었다" 만 알려 주므로, 어느 쪽인지는 모델의 두 비트가 안다.
        if ( _model.isAnyDirty() )
            markDocumentDirty();
        else
            clearDocumentDirty();
    }

    void DataTablePanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        _model.pollBackgroundJobs();

        if ( ImGui::BeginTabBar( "##DataTableTabs" ) )
        {
            if ( ImGui::BeginTabItem( SW_LOCKEY( "EdSection", "LocalizationStrings" ) ) )
            {
                drawLocalizationTab();
                ImGui::EndTabItem();
            }

            if ( ImGui::BeginTabItem( SW_LOCKEY( "EdSection", "GameDataXMLTables" ) ) )
            {
                drawGameDataTab();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }

    void DataTablePanel::drawLocalizationTab()
    {
        drawLocalizationToolbar();

        ImGui::Separator();

        if ( _model.isLocalizationLoaded() == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorDataTable", "LoadingLocalization" ) );
            return;
        }

        drawLocalizationTable();
    }

    void DataTablePanel::drawLocalizationToolbar()
    {
        if ( EditorChrome::beginToolbar( "##locToolbar" ) )
        {
            const vector<string>& listProject          = _model.getLocalizationProjects();
            const int32           selectedProjectIndex = _model.getSelectedProjectIndex();
            const bool            bHasProject          = selectedProjectIndex >= 0 && static_cast<size_t>( selectedProjectIndex ) < listProject.size();
            const string          projectLabel         = bHasProject ? FileUtil::getFileNamePart( listProject[static_cast<size_t>( selectedProjectIndex )] ) : string( SW_LOCKEY( "EditorDataTable", "NoProject" ) );
            EditorChrome::setNextItemWidthColumns( 29.0f );
            const bool bProjectOpen = EditorWidgets::beginCombo( "##locProject", projectLabel.c_str() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataTable", "ProjectTip" ) );
            if ( bProjectOpen )
            {
                for ( size_t projectIndex = 0; projectIndex < listProject.size(); ++projectIndex )
                {
                    const bool   bSelected = static_cast<int32>( projectIndex ) == selectedProjectIndex;
                    const string label     = FileUtil::getFileNamePart( listProject[projectIndex] );
                    if ( ImGui::Selectable( label.c_str(), bSelected ) && bSelected == false )
                        _model.selectProject( projectIndex ); // 고친 것이 남았으면 모델이 바꾸지 않는다
                }
                ImGui::EndCombo();
            }
            EditorChrome::sameLineIfFits( 240.0f ); // 검색 칸 폭(아래 인자와 같다)
            EditorWidgets::drawSearchField( "##locFilter", _localizationFilter, SW_LOCKEY( "EditorDataTable", "SearchHint" ), 240.0f, false, "dataTable.search" );
            EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EditorDataTable", "NeedsWorkOnly" ) ) );
            (void)ImGui::Checkbox( SW_LOCKEY( "EditorDataTable", "NeedsWorkOnly" ), &_bNeedsWorkOnly );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataTable", "NeedsWorkOnlyTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "SaveLocalization" ) ) );

            if ( ImGui::Button( SW_LOCKEY( "EdText", "SaveLocalization" ) ) )
                _model.saveLocalization();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataTable", "SaveLocalizationTip" ) );

            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Reload" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Reload" ) ) )
                _model.reloadLocalization();
            EditorSelfTestMarks::record( "dataTable.reload" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataTable", "ReloadTip" ) );

            EditorChrome::sameLineIfFits( EditorChrome::measureColumns( 21.0f ) ); // 좁은 창에서 새 키 칸이 오른쪽 끝에서 잘렸다(uiaudit 23)
            EditorChrome::setNextItemWidthColumns( 21.0f );
            EditorWidgets::inputTextWithHint( SW_WIDGET_ID( "##newKey" ), SW_LOCKEY( "EditorDataTable", "NewKeyHint" ), _newKeyBuffer.data(), _newKeyBuffer.capacity() );
            EditorSelfTestMarks::record( "dataTable.newKey" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdLabel", "AddKey" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdLabel", "AddKey" ) ) && _model.addKey( _newKeyBuffer.c_str() ) )
                _newKeyBuffer.clear();
            EditorSelfTestMarks::record( "dataTable.addKey" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataTable", "AddKeyTip" ) );
        }
        EditorChrome::endToolbar();
    }

    void DataTablePanel::drawTranslationCell( LocalizationRecord& record, size_t cultureIndex )
    {
        string&                text         = record._listTranslation[cultureIndex];
        const TranslationState state        = record._listState[cultureIndex];
        const bool             bTooLong     = record.isTranslationTooLong( cultureIndex );
        const bool             bConfirmable = state == TranslationState::Stale || state == TranslationState::Review;

        uint32 pushedColorCount = 0;
        if ( state == TranslationState::Stale )
        {
            ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Warning" ) ) );
            ++pushedColorCount;
        }
        else if ( state == TranslationState::Review )
        {
            ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Info" ) ) );
            ++pushedColorCount;
        }
        if ( bTooLong )
        {
            ImGui::PushStyleColor( ImGuiCol_Border, EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ) );
            ++pushedColorCount;
        }
        // 확인 단추 자리를 남기고 칸이 나머지 폭을 채운다(폭 인자 0 은 이 값을 쓴다).
        ImGui::SetNextItemWidth( bConfirmable ? -( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorDataTable", "ConfirmTranslation" ) ) + ImGui::GetStyle().ItemSpacing.x ) : -1.0f );
        const bool bChanged = EditorWidgets::drawTextField( "##tr", text, 0.0f );
        if ( pushedColorCount > 0 )
            ImGui::PopStyleColor( static_cast<int32>( pushedColorCount ) );

        if ( ImGui::IsItemHovered() )
        {
            const utf8* pState = SW_LOCKEY( "EditorDataTable", "StateTranslated" );
            if ( state == TranslationState::Stale )
                pState = SW_LOCKEY( "EditorDataTable", "StateStale" );
            else if ( state == TranslationState::Review )
                pState = SW_LOCKEY( "EditorDataTable", "StateReview" );
            else if ( state == TranslationState::Missing )
                pState = SW_LOCKEY( "EditorDataTable", "StateMissing" );
            const utf8*  pOverflow = bTooLong ? SW_LOCKEY( "EditorDataTable", "OverMaxLength" ) : "";
            const string tooltip   = SW_LOCKEYFORMAT( "EditorDataTable", "TranslationCellTip",
                                                      TextArgumentList()
                                                          .addText( "context", record._context.empty() ? record._comment : record._context )
                                                          .addText( "state", pState )
                                                          .addInteger( "length", static_cast<int64>( record.measureTranslation( cultureIndex ) ) )
                                                          .addText( "overflow", pOverflow ) );
            ImGui::SetTooltip( "%s", tooltip.c_str() );
        }
        if ( bChanged )
            _model.markTranslationEdited( record, cultureIndex );
        if ( bConfirmable )
        {
            ImGui::SameLine();
            if ( ImGui::SmallButton( SW_LOCKEY( "EditorDataTable", "ConfirmTranslation" ) ) )
                _model.confirmTranslation( record, cultureIndex );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataTable", "ConfirmTranslationTip" ) );
        }
    }

    void DataTablePanel::drawLocalizationTable()
    {
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                          ImGuiTableFlags_SizingStretchProp;

        // 테이블을 열기 **전에** 걸러 둔다. 테이블이 남은 영역을 모두 차지하므로 0건 안내를 테이블 뒤에 그리면
        // 화면 밖으로 밀린다. 테이블을 아예 열지 않아야 보인다. 행마다 필터를 다시 만들지 않는 효과도 있다.
        const EditorListFilter filter{ _localizationFilter.c_str() };

        vector<size_t>& listVisibleIndex = _listVisibleLocalizationIndex;
        _model.collectVisibleRecords( filter, listVisibleIndex, _bNeedsWorkOnly ? LocalizationRowFilter::NeedsWork : LocalizationRowFilter::All );

        if ( listVisibleIndex.empty() )
        {
            if ( _bNeedsWorkOnly && filter.isActive() == false )
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorDataTable", "NoWorkLeftHint" ) );
            else if ( filter.isActive() )
                EditorWidgets::drawNoSearchResultHint( filter.getText() );
            else
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorDataTable", "NoRecordsHint" ) );
            return;
        }

        const LocalizationSheet& sheet        = _model.editSheet();
        const size_t             cultureCount = sheet._listCulture.size();
        const int32              columnCount  = static_cast<int32>( 3 + cultureCount );
        if ( ImGui::BeginTable( "##locTable", columnCount, flags, ImGui::GetContentRegionAvail() ) )
        {
            ImGui::TableSetupColumn( "Key", ImGuiTableColumnFlags_WidthStretch, 0.22f );
            const string sourceHeader = "Source (" + sheet._sourceCulture + ")";
            ImGui::TableSetupColumn( sourceHeader.c_str(), ImGuiTableColumnFlags_WidthStretch, 0.28f );
            // 머리에 진행도를 적는다(유니티 Localization 표의 완료율 자리). 줄마다 세지 않고 문화권마다 한 번 센다.
            for ( size_t cultureIndex = 0; cultureIndex < cultureCount; ++cultureIndex )
            {
                StringBuilder<constant::buffer::kMax256> cultureHeaderBuilder;
                cultureHeaderBuilder.appendFormat( "%# (%#/%#)", sheet._listCulture[cultureIndex].c_str(), static_cast<uint64>( _model.countCurrentTranslations( cultureIndex ) ), static_cast<uint64>( sheet._listRecord.size() ) );
                const string cultureHeader{ cultureHeaderBuilder.view() };
                ImGui::TableSetupColumn( cultureHeader.c_str(), ImGuiTableColumnFlags_WidthStretch, 0.5f / static_cast<float32>( cultureCount > 0 ? cultureCount : 1 ) );
            }
            ImGui::TableSetupColumn( "Action", ImGuiTableColumnFlags_WidthFixed, 50.0f * EditorThemeUtil::getDPIScale() );
            ImGui::TableSetupScrollFreeze( 0, 1 );
            ImGui::TableHeadersRow();

            // 수천 줄의 입력칸을 프레임마다 그리지 않는다. 줄 높이가 같아 보이는 줄만 그린다.
            int32            deleteIndex = -1;
            ImGuiListClipper clipper;
            clipper.Begin( static_cast<int32>( listVisibleIndex.size() ) );
            while ( clipper.Step() )
            {
                for ( int32 rowPosition = clipper.DisplayStart; rowPosition < clipper.DisplayEnd; ++rowPosition )
                {
                    const size_t recordIndex = listVisibleIndex[static_cast<size_t>( rowPosition )];
                    if ( drawLocalizationRow( recordIndex, cultureCount ) )
                        deleteIndex = static_cast<int32>( recordIndex );
                }
            }

            if ( deleteIndex >= 0 )
                _model.deleteRecord( static_cast<size_t>( deleteIndex ) );

            ImGui::EndTable();
        }
    }

    bool DataTablePanel::drawLocalizationRow( size_t recordIndex, size_t cultureCount )
    {
        bool                bDeleteRequested = false;
        LocalizationRecord& record           = _model.editSheet()._listRecord[recordIndex];

        ImGui::PushID( static_cast<int32>( recordIndex ) );
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex( 0 );
        ImGui::TextUnformatted( record._key.c_str() );
        if ( ImGui::IsItemHovered() && ( record._context.empty() == false || record._comment.empty() == false ) )
        {
            const string tooltip = SW_LOCKEYFORMAT( "EditorDataTable", "KeyTip",
                                                    TextArgumentList()
                                                        .addText( "context", record._context )
                                                        .addText( "comment", record._comment )
                                                        .addInteger( "maxLength", static_cast<int64>( record._maxLength ) ) );
            ImGui::SetTooltip( "%s", tooltip.c_str() );
        }

        ImGui::TableSetColumnIndex( 1 );
        if ( EditorWidgets::drawTextField( "##source", record._source, -1.0f ) )
            _model.markSourceEdited( record );

        for ( size_t cultureIndex = 0; cultureIndex < cultureCount; ++cultureIndex )
        {
            ImGui::TableSetColumnIndex( static_cast<int32>( 2 + cultureIndex ) );
            ImGui::PushID( static_cast<int32>( cultureIndex ) );
            drawTranslationCell( record, cultureIndex );
            ImGui::PopID();
        }

        ImGui::TableSetColumnIndex( static_cast<int32>( 2 + cultureCount ) );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "Del" ) ) )
            bDeleteRequested = true;
        fixed_string<constant::buffer::kMax128> mark;
        formatstring( mark.data(), mark.capacity(), "dataTable.delete.%#", record._key.c_str() );
        EditorSelfTestMarks::record( mark.c_str() ); // 시나리오가 더한 키를 지운다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataTable", "DeleteKeyTip" ) );

        ImGui::PopID();
        return bDeleteRequested;
    }

    void DataTablePanel::drawGameDataTab()
    {
        ImGui::BeginGroup();
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "XMLDataFiles" ) );
        ImGui::Separator();
        if ( ImGui::Button( SW_LOCKEY( "EdText", "RefreshFilesAlt" ) ) )
            _model.reloadGameDataFiles();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataTable", "RefreshFilesTip" ) );

        editor::EditorSectionDesc listDesc{};
        listDesc._pID       = "##DataFileList";
        listDesc._kind      = editor::EditorSectionKind::Child;
        listDesc._childSize = float2{ 200.0f, 0.0f };
        listDesc._flags     = editor::EditorSectionFlags::Border | editor::EditorSectionFlags::ResizeX;
        if ( EditorChrome::beginSection( listDesc ) )
        {
            const vector<GameDataFileEntry>& listGameDataFile = _model.getGameDataFiles();
            for ( const GameDataFileEntry& entry : listGameDataFile )
            {
                const bool bSelected = FileUtil::isSameDiskPath( entry._absolutePath, _model.getGameDataPath() ); // 연 자리는 문서 id 를 푼 디스크 경로라 표기가 목록과 다를 수 있다
                const bool bBlocked  = _model.isGameDataDirty() && bSelected == false;                            // 고친 표를 두고 다른 표로 가지 않는다 — 저장하거나 되돌린 뒤 고른다
                ImGui::BeginDisabled( bBlocked );
                if ( ImGui::Selectable( entry._fileName.c_str(), bSelected ) && bSelected == false )
                {
                    // 콘텐츠 브라우저와 같은 길 — 리소스 id 로 바꿔 문서 열기(포커스)로 넘기고, 다음 그리기의 `updateFocusedDocument` 가 연다.
                    const string assetID = DataTableModel::makeGameDataAssetID( entry._absolutePath );
                    if ( assetID.empty() == false )
                        requestOpenDocument( assetID );
                }
                ImGui::EndDisabled();
                if ( bBlocked )
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataTable", "SaveBeforeSwitchingTip" ) );
            }
        }
        EditorChrome::endSection();
        ImGui::EndGroup();

        ImGui::SameLine();

        // 오른쪽: 파일 내용 보기 · 편집
        ImGui::BeginGroup();
        if ( _seenGameDataLoadCount != _model.getGameDataLoadCount() )
        {
            _gameDataTable.reset(); // 새로 읽은(되돌린) 문서다 — 앞 문서의 고른 칸을 들지 않는다
            _seenGameDataLoadCount = _model.getGameDataLoadCount();
        }
        if ( _model.getGameDataPath().empty() == false )
        {
            const string fileName = FileUtil::getFileNamePart( _model.getGameDataPath() );
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "File", TextArgumentList().addText( "file", fileName ) ).c_str() );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "SaveFile" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "SaveFile" ) ) )
            {
                (void)_model.saveGameDataFile(); // 이 표만 쓴다 — 실패는 모델이 알리고 dirty 가 남는다
                syncDocumentDirty();
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataTable", "SaveFileTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Revert" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Revert" ) ) )
                EditorDocumentPanel::reloadDocument(); // 이 표만 디스크에서 다시 읽는다 — 현지화 표는 그대로다
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataTable", "RevertTip" ) );

            EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "RawXML" ) ) );
            (void)ImGui::Checkbox( SW_LOCKEY( "EdLabel", "RawXML" ), &_bGameDataRaw );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "TableEditsKeepCommentsAndIndentationNested" ) );

            ImGui::Separator();

            if ( _bGameDataRaw || _model.getGameDataDocument().isParsed() == false )
            {
                // XML 원문 텍스트 상자 - 테이블로 못 읽는 문서도 여기서 고친다. 고정 버퍼면 그 크기를 넘게 붙여 넣은 글이 말없이 잘린다.
                const float32 boxHeight = ImGui::GetContentRegionAvail().y - ImGui::GetStyle().FramePadding.y * 2.0f;
                const uint32  lineCount = MathUtil::max( static_cast<uint32>( boxHeight / ImGui::GetTextLineHeight() ), 4u );
                if ( EditorWidgets::drawTextFieldMultiline( "##rawXMLEdit", _model.editGameDataRawText(), lineCount, nullptr, ImGuiInputTextFlags_AllowTabInput ) )
                    _model.commitGameDataRawEdit();
            }
            else if ( _gameDataTable.draw( _model.editGameDataDocument() ) )
                _model.commitGameDataTableEdit();
        }
        else
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorDataTable", "SelectTableHint" ) );
        }
        ImGui::EndGroup();
    }

} // namespace sw::editor
