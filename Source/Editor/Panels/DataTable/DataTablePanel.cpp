#include "pch.h"

#include "Editor/Panels/DataTable/DataTablePanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct DataTablePanelInternal
        {
            static constexpr Color kStaleText{ 1.0f, 0.62f, 0.25f, 1.0f };
            static constexpr Color kReviewText{ 0.95f, 0.85f, 0.30f, 1.0f };
            static constexpr Color kTooLongBorder{ 0.95f, 0.25f, 0.25f, 1.0f };
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( DataTablePanel, DataTablePanel::kPanelID, EditorPanelCategory::Tool, 1700 );

    DataTablePanel::DataTablePanel()
        : IEditorPanel{ false }
        , _model{}
        , _localizationFilter{}
        , _newKeyBuffer{}
        , _listVisibleLocalizationIndex{}
        , _gameDataTable{}
        , _seenGameDataLoadCount{ 0 }
        , _bGameDataRaw{ false }
    {
    }

    /** @brief 두 문서(로컬라이즈 · 게임 데이터)를 한 패널이 들기 때문에 모델이 dirty 한 쪽만 저장합니다. */
    bool DataTablePanel::saveDocument()
    {
        const bool bSaved = _model.save();
        syncDocumentDirty();
        return bSaved;
    }

    void DataTablePanel::revertDocument()
    {
        _model.revert();
        syncDocumentDirty();
    }

    void DataTablePanel::syncDocumentDirty()
    {
        // 기반 클래스의 dirty 비트는 "무언가 바뀌었다" 만 알려 주므로, 어느 쪽인지는 모델의 두 비트가 안다.
        if ( _model.isAnyDirty() )
            markDocumentDirty();
        else
            clearDocumentDirty();
    }

    void DataTablePanel::drawContent()
    {
        _model.pollBackgroundJobs();

        if ( ImGui::BeginTabBar( "##DataTableTabs" ) )
        {
            if ( ImGui::BeginTabItem( "Localization Strings" ) )
            {
                drawLocalizationTab();
                ImGui::EndTabItem();
            }

            if ( ImGui::BeginTabItem( "Game Data XML Tables" ) )
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
            const bool bProjectOpen = ImGui::BeginCombo( "##locProject", projectLabel.c_str() );
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
            ImGui::SameLine();
            EditorWidgets::drawSearchField( "##locFilter", _localizationFilter, SW_LOCKEY( "EditorDataTable", "SearchHint" ), 240.0f, false );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Save Localization" ) );

            if ( ImGui::Button( "Save Localization" ) )
                _model.saveLocalization();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataTable", "SaveLocalizationTip" ) );

            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Reload" ) );
            if ( ImGui::Button( "Reload" ) )
                _model.reloadLocalization();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataTable", "ReloadTip" ) );

            ImGui::SameLine();
            EditorChrome::setNextItemWidthColumns( 21.0f );
            ImGui::InputTextWithHint( "##newKey", SW_LOCKEY( "EditorDataTable", "NewKeyHint" ), _newKeyBuffer.data(), _newKeyBuffer.capacity() );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Add Key" ) );
            if ( ImGui::Button( "Add Key" ) && _model.addKey( _newKeyBuffer.c_str() ) )
                _newKeyBuffer.clear();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataTable", "AddKeyTip" ) );
        }
        EditorChrome::endToolbar();
    }

    bool DataTablePanel::drawTranslationCell( LocalizationRecord& record, size_t cultureIndex )
    {
        string&                text   = record._listTranslation[cultureIndex];
        const TranslationState state  = cultureIndex < record._listState.size() ? record._listState[cultureIndex] : TranslationState::Missing;
        uint32                 length = 0;
        for ( size_t offset = 0; offset < text.size(); )
        {
            (void)StringUtil::decodeUTF8( text, offset );
            ++length;
        }
        const bool bTooLong = record._maxLength != 0 && length > record._maxLength;

        uint32 pushedColorCount = 0;
        if ( state == TranslationState::Stale )
        {
            ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( DataTablePanelInternal::kStaleText ) );
            ++pushedColorCount;
        }
        else if ( state == TranslationState::Review )
        {
            ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( DataTablePanelInternal::kReviewText ) );
            ++pushedColorCount;
        }
        if ( bTooLong )
        {
            ImGui::PushStyleColor( ImGuiCol_Border, EditorColorUtil::toImVec4( DataTablePanelInternal::kTooLongBorder ) );
            ++pushedColorCount;
        }
        const bool bChanged = EditorWidgets::drawTextField( "##tr", text, -1.0f );
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
                                                          .addInteger( "length", static_cast<int64>( length ) )
                                                          .addText( "overflow", pOverflow ) );
            ImGui::SetTooltip( "%s", tooltip.c_str() );
        }
        return bChanged;
    }

    void DataTablePanel::drawLocalizationTable()
    {
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                          ImGuiTableFlags_SizingStretchProp;

        // 테이블을 열기 **전에** 걸러 둔다. 테이블이 남은 영역을 모두 차지하므로 0건 안내를 테이블 뒤에 그리면
        // 화면 밖으로 밀린다. 테이블을 아예 열지 않아야 보인다. 행마다 필터를 다시 만들지 않는 효과도 있다.
        const EditorListFilter filter{ _localizationFilter.c_str() };

        vector<size_t>& listVisibleIndex = _listVisibleLocalizationIndex;
        _model.collectVisibleRecords( filter, listVisibleIndex );

        if ( listVisibleIndex.empty() )
        {
            if ( filter.isActive() )
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
            for ( const string& culture : sheet._listCulture )
            {
                ImGui::TableSetupColumn( culture.c_str(), ImGuiTableColumnFlags_WidthStretch, 0.5f / static_cast<float32>( cultureCount > 0 ? cultureCount : 1 ) );
            }
            ImGui::TableSetupColumn( "Action", ImGuiTableColumnFlags_WidthFixed, 50.0f * EditorThemeUtil::getDPIScale() );
            ImGui::TableHeadersRow();

            int32 deleteIndex = -1;
            for ( const size_t recordIndex : listVisibleIndex )
            {
                if ( drawLocalizationRow( recordIndex, cultureCount ) )
                    deleteIndex = static_cast<int32>( recordIndex );
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
        record._listTranslation.resize( cultureCount );

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
            _model.markRecordModified( record );

        for ( size_t cultureIndex = 0; cultureIndex < cultureCount; ++cultureIndex )
        {
            ImGui::TableSetColumnIndex( static_cast<int32>( 2 + cultureIndex ) );
            ImGui::PushID( static_cast<int32>( cultureIndex ) );
            if ( drawTranslationCell( record, cultureIndex ) )
                _model.markRecordModified( record );
            ImGui::PopID();
        }

        ImGui::TableSetColumnIndex( static_cast<int32>( 2 + cultureCount ) );
        if ( ImGui::SmallButton( "Del" ) )
            bDeleteRequested = true;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataTable", "DeleteKeyTip" ) );

        ImGui::PopID();
        return bDeleteRequested;
    }

    void DataTablePanel::drawGameDataTab()
    {
        ImGui::BeginGroup();
        ImGui::Text( "XML Data Files" );
        ImGui::Separator();
        if ( ImGui::Button( "Refresh Files" ) )
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
            for ( size_t fileIndex = 0; fileIndex < listGameDataFile.size(); ++fileIndex )
            {
                const GameDataFileEntry& entry     = listGameDataFile[fileIndex];
                const bool               bSelected = ( _model.getSelectedGameDataIndex() == static_cast<int32>( fileIndex ) );
                if ( ImGui::Selectable( entry._fileName.c_str(), bSelected ) )
                    _model.selectGameDataFile( fileIndex ); // 다른 파일의 고친 것이 남았으면 모델이 바꾸지 않는다
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
        const GameDataFileEntry* pEntry = _model.findSelectedGameDataFile();
        if ( pEntry != nullptr )
        {
            ImGui::Text( "File: %s", pEntry->_fileName.c_str() );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Save File" ) );
            if ( ImGui::Button( "Save File" ) )
                _model.saveSelectedGameDataFile();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataTable", "SaveFileTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Revert" ) );
            if ( ImGui::Button( "Revert" ) )
                _model.loadSelectedGameDataFile();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDataTable", "RevertTip" ) );

            EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( "Raw XML" ) );
            (void)ImGui::Checkbox( "Raw XML", &_bGameDataRaw );
            EditorWidgets::drawTooltip( "Table edits keep comments and indentation. Nested elements are edited in the raw text view." );

            ImGui::Separator();

            if ( _bGameDataRaw || _model.getGameDataDocument().isParsed() == false )
            {
                // XML 원문 텍스트 상자 - 테이블로 못 읽는 문서도 여기서 고친다
                const string& rawText = _model.getGameDataRawText();
                vector<utf8>  arrEditBuffer;
                arrEditBuffer.resize( rawText.size() + 4096, 0 );
                if ( rawText.empty() == false )
                    Memory::copy( arrEditBuffer.data(), rawText.c_str(), rawText.size() );

                constexpr ImGuiInputTextFlags editFlags = ImGuiInputTextFlags_AllowTabInput;
                if ( ImGui::InputTextMultiline( "##rawXMLEdit", arrEditBuffer.data(), arrEditBuffer.size(), ImGui::GetContentRegionAvail(), editFlags ) )
                    _model.replaceGameDataRawText( arrEditBuffer.data() );
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
