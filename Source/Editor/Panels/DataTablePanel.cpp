#include "pch.h"

#include "Editor/Panels/DataTablePanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Commands/EditorDataTableCommands.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Gui/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"
#include "Editor/Panels/EditorPanelManager.h"

#include <imgui.h>

SW_LOG_CALLER( "DataTablePanel" );
namespace sw::editor
{
    SW_EDITOR_PANEL( DataTablePanel, "data_table", EditorPanelCategory::Tool, 1700 );

    DataTablePanel::DataTablePanel()
        : IEditorPanel{ false }
        , _localizationFilter{}
        , _newKeyBuffer{}
        , _localizationSheet{}
        , _listLocalizationProject{}
        , _listGameDataFile{}
        , _selectedGameDataRawText{}
        , _savedGameDataRawText{}
        , _localizationJob{}
        , _gameDataJob{}
        , _selectedGameDataIndex{ -1 }
        , _selectedProjectIndex{ 0 }
        , _bLocalizationLoaded{ SW_FALSE }
        , _bGameDataLoaded{ SW_FALSE }
        , _bLocalizationDirty{ SW_FALSE }
        , _bGameDataDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    /**
     * @brief 두 문서(로컬라이즈 · 게임 데이터)를 한 패널이 들기 때문에 dirty 한 쪽만 저장합니다.
     * @details 기반 클래스의 dirty 비트는 "무언가 바뀌었다" 만 알려 주므로, 어느 쪽인지는 여기 두 비트가 압니다.
     */
    bool DataTablePanel::saveDocument()
    {
        if ( _bLocalizationDirty == SW_TRUE )
            saveLocalization();
        if ( _bGameDataDirty == SW_TRUE )
            saveSelectedGameDataFile();
        return _bLocalizationDirty == SW_FALSE && _bGameDataDirty == SW_FALSE;
    }

    void DataTablePanel::revertDocument()
    {
        if ( _bLocalizationDirty == SW_TRUE )
        {
            // 읽지 못한 표는 비어 보이고, 저장은 그 파일을 덮지 않는다(`saveLocalization`).
            if ( EditorDataTableCommands::loadLocalizationProject( _localizationSheet._projectPath, _localizationSheet ) == false )
                SW_LOG_WARNING( "Some localization files could not be read - see the warnings above" );
            _bLocalizationLoaded = SW_TRUE;
            _bLocalizationDirty  = SW_FALSE;
            syncDocumentDirty();
        }
        if ( _bGameDataDirty == SW_TRUE )
        {
            _selectedGameDataRawText = _savedGameDataRawText;
            _bGameDataDirty          = SW_FALSE;
            syncDocumentDirty();
        }
    }

    void DataTablePanel::markLocalizationDirty()
    {
        _bLocalizationDirty = SW_TRUE;
        syncDocumentDirty();
    }

    void DataTablePanel::markGameDataDirty()
    {
        _bGameDataDirty = SW_TRUE;
        syncDocumentDirty();
    }

    void DataTablePanel::syncDocumentDirty()
    {
        const bool bAnyDirty = ( _bLocalizationDirty == SW_TRUE || _bGameDataDirty == SW_TRUE );
        if ( bAnyDirty )
            markDocumentDirty();
        else
            clearDocumentDirty();
    }

    void DataTablePanel::pollBackgroundJobs()
    {
        LocalizationSheet loadedSheet;
        if ( _localizationJob.take( loadedSheet ) )
        {
            if ( _bLocalizationDirty == SW_FALSE )
            {
                _localizationSheet   = std::move( loadedSheet );
                _bLocalizationLoaded = SW_TRUE;
            }
        }
        else if ( _bLocalizationLoaded == SW_FALSE && _localizationJob.isPending() == false )
        {
            EditorDataTableCommands::collectLocalizationProjects( _listLocalizationProject );
            if ( _listLocalizationProject.empty() )
                _bLocalizationLoaded = SW_TRUE; // 프로젝트가 없다 — 빈 안내를 보인다
            else
            {
                if ( _selectedProjectIndex < 0 || static_cast<size_t>( _selectedProjectIndex ) >= _listLocalizationProject.size() )
                    _selectedProjectIndex = 0;
                _localizationJob.request( _listLocalizationProject[static_cast<size_t>( _selectedProjectIndex )] );
            }
        }

        vector<GameDataFileEntry> listGameData;
        if ( _gameDataJob.take( listGameData ) )
        {
            _listGameDataFile = std::move( listGameData );
            _bGameDataLoaded  = SW_TRUE;
        }
        else if ( _bGameDataLoaded == SW_FALSE && _gameDataJob.isPending() == false )
            _gameDataJob.request();
    }

    void DataTablePanel::drawContent()
    {
        pollBackgroundJobs();

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

        if ( _bLocalizationLoaded == SW_FALSE )
        {
            EditorWidgets::drawEmptyHint( "Loading localization..." );
            return;
        }

        drawLocalizationTable();
    }

    void DataTablePanel::drawLocalizationToolbar()
    {
        if ( EditorChrome::beginToolbar( "##locToolbar" ) )
        {
            const bool   bHasProject  = _selectedProjectIndex >= 0 && static_cast<size_t>( _selectedProjectIndex ) < _listLocalizationProject.size();
            const string projectLabel = bHasProject ? FileUtil::getFileNamePart( _listLocalizationProject[static_cast<size_t>( _selectedProjectIndex )] ) : string( "(no project)" );
            ImGui::SetNextItemWidth( 220.0f * EditorThemeUtil::getDpiScale() );
            if ( ImGui::BeginCombo( "##locProject", projectLabel.c_str() ) )
            {
                for ( size_t projectIndex = 0; projectIndex < _listLocalizationProject.size(); ++projectIndex )
                {
                    const bool   bSelected = static_cast<int32>( projectIndex ) == _selectedProjectIndex;
                    const string label     = FileUtil::getFileNamePart( _listLocalizationProject[projectIndex] );
                    if ( ImGui::Selectable( label.c_str(), bSelected ) && bSelected == false && _bLocalizationDirty == SW_FALSE )
                    {
                        _selectedProjectIndex = static_cast<int32>( projectIndex );
                        reloadLocalization();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            EditorWidgets::drawSearchField( "##locFilter", _localizationFilter, "Search keys, source or translations...", 240.0f, false );
            ImGui::SameLine();

            if ( ImGui::Button( "Save Localization" ) )
                saveLocalization();

            ImGui::SameLine();
            if ( ImGui::Button( "Reload" ) )
                reloadLocalization();

            ImGui::SameLine();
            ImGui::SetNextItemWidth( 160.0f * EditorThemeUtil::getDpiScale() );
            ImGui::InputTextWithHint( "##newKey", "New string key...", _newKeyBuffer.data(), _newKeyBuffer.capacity() );
            ImGui::SameLine();
            if ( ImGui::Button( "Add Key" ) && _newKeyBuffer.empty() == false && _localizationSheet._listTablePath.empty() == false )
            {
                const string newKey{ _newKeyBuffer.c_str() };
                bool         bExists{ false };
                for ( const LocalizationRecord& record : _localizationSheet._listRecord )
                {
                    bExists = bExists || record._key == newKey;
                }

                if ( bExists == false )
                {
                    LocalizationRecord newRecord{};
                    newRecord._key       = newKey;
                    newRecord._bModified = true;
                    newRecord._listTranslation.resize( _localizationSheet._listCulture.size() );
                    newRecord._listState.resize( _localizationSheet._listCulture.size(), TranslationState::Missing );
                    _localizationSheet._listRecord.push_back( std::move( newRecord ) );
                    _newKeyBuffer.clear();
                    markLocalizationDirty();
                }
            }
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
            (void)StringUtil::decodeUtf8( text, offset );
            ++length;
        }
        const bool bTooLong = record._maxLength != 0 && length > record._maxLength;

        uint32 pushedColorCount = 0;
        if ( state == TranslationState::Stale )
        {
            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 0.62f, 0.25f, 1.0f ) );
            ++pushedColorCount;
        }
        else if ( state == TranslationState::Review )
        {
            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.95f, 0.85f, 0.30f, 1.0f ) );
            ++pushedColorCount;
        }
        if ( bTooLong )
        {
            ImGui::PushStyleColor( ImGuiCol_Border, ImVec4( 0.95f, 0.25f, 0.25f, 1.0f ) );
            ++pushedColorCount;
        }
        const bool bChanged = EditorWidgets::drawTextField( "##tr", text, -1.0f );
        if ( pushedColorCount > 0 )
            ImGui::PopStyleColor( static_cast<int32>( pushedColorCount ) );

        if ( ImGui::IsItemHovered() )
        {
            const utf8* pState = "translated";
            if ( state == TranslationState::Stale )
                pState = "stale - the source changed after this translation";
            else if ( state == TranslationState::Review )
                pState = "needs review (translation memory suggestion or fuzzy import)";
            else if ( state == TranslationState::Missing )
                pState = "missing";
            ImGui::SetTooltip( "%s\nState: %s\nLength: %u%s", record._context.empty() ? record._comment.c_str() : record._context.c_str(), pState, length,
                               bTooLong ? " (over the maximum length)" : "" );
        }
        return bChanged;
    }

    void DataTablePanel::drawLocalizationTable()
    {
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                          ImGuiTableFlags_SizingStretchProp;

        // 표를 열기 **전에** 걸러 둔다. 표가 남은 영역을 모두 차지하므로 0건 안내를 표 뒤에 그리면
        // 화면 밖으로 밀린다. 표를 아예 열지 않아야 보인다. 행마다 필터를 다시 만들지 않는 효과도 있다.
        const EditorListFilter filter{ _localizationFilter.c_str() };

        // **멤버 버퍼를 다시 쓴다** — 지역 `vector` 면 프레임마다 할당하고 해제한다.
        vector<size_t>& listVisibleIndex = _listVisibleLocalizationIndex;
        listVisibleIndex.clear();
        listVisibleIndex.reserve( _localizationSheet._listRecord.size() );
        for ( size_t recordIndex = 0; recordIndex < _localizationSheet._listRecord.size(); ++recordIndex )
        {
            const LocalizationRecord& record   = _localizationSheet._listRecord[recordIndex];
            bool                      bMatches = filter.matchesAny( { record._key, record._source } );
            for ( const string& translation : record._listTranslation )
            {
                bMatches = bMatches || filter.matchesAny( { translation } );
            }
            if ( bMatches )
                listVisibleIndex.push_back( recordIndex );
        }

        if ( listVisibleIndex.empty() )
        {
            if ( filter.isActive() )
                EditorWidgets::drawNoSearchResultHint( filter.getText() );
            else
                EditorWidgets::drawEmptyHint( "No localization records. Add a key or run App --gather-text." );
            return;
        }

        const size_t cultureCount = _localizationSheet._listCulture.size();
        const int32  columnCount  = static_cast<int32>( 3 + cultureCount );
        if ( ImGui::BeginTable( "##locTable", columnCount, flags, ImGui::GetContentRegionAvail() ) )
        {
            ImGui::TableSetupColumn( "Key", ImGuiTableColumnFlags_WidthStretch, 0.22f );
            const string sourceHeader = "Source (" + _localizationSheet._sourceCulture + ")";
            ImGui::TableSetupColumn( sourceHeader.c_str(), ImGuiTableColumnFlags_WidthStretch, 0.28f );
            for ( const string& culture : _localizationSheet._listCulture )
            {
                ImGui::TableSetupColumn( culture.c_str(), ImGuiTableColumnFlags_WidthStretch, 0.5f / static_cast<float32>( cultureCount > 0 ? cultureCount : 1 ) );
            }
            ImGui::TableSetupColumn( "Action", ImGuiTableColumnFlags_WidthFixed, 50.0f * EditorThemeUtil::getDpiScale() );
            ImGui::TableHeadersRow();

            int32 deleteIndex = -1;
            for ( const size_t recordIndex : listVisibleIndex )
            {
                LocalizationRecord& record = _localizationSheet._listRecord[recordIndex];
                record._listTranslation.resize( cultureCount );

                ImGui::PushID( static_cast<int32>( recordIndex ) );
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex( 0 );
                ImGui::TextUnformatted( record._key.c_str() );
                if ( ImGui::IsItemHovered() && ( record._context.empty() == false || record._comment.empty() == false ) )
                    ImGui::SetTooltip( "Context: %s\nComment: %s\nMax length: %u", record._context.c_str(), record._comment.c_str(), record._maxLength );

                ImGui::TableSetColumnIndex( 1 );
                if ( EditorWidgets::drawTextField( "##source", record._source, -1.0f ) )
                {
                    record._bModified = true;
                    markLocalizationDirty();
                }

                for ( size_t cultureIndex = 0; cultureIndex < cultureCount; ++cultureIndex )
                {
                    ImGui::TableSetColumnIndex( static_cast<int32>( 2 + cultureIndex ) );
                    ImGui::PushID( static_cast<int32>( cultureIndex ) );
                    if ( drawTranslationCell( record, cultureIndex ) )
                    {
                        record._bModified = true;
                        markLocalizationDirty();
                    }
                    ImGui::PopID();
                }

                ImGui::TableSetColumnIndex( static_cast<int32>( 2 + cultureCount ) );
                if ( ImGui::SmallButton( "Del" ) )
                    deleteIndex = static_cast<int32>( recordIndex );

                ImGui::PopID();
            }

            if ( 0 <= deleteIndex && static_cast<size_t>( deleteIndex ) < _localizationSheet._listRecord.size() )
            {
                _localizationSheet._listRemovedKey.push_back( _localizationSheet._listRecord[static_cast<size_t>( deleteIndex )]._key );
                _localizationSheet._listRecord.erase( _localizationSheet._listRecord.begin() + deleteIndex );
                markLocalizationDirty();
            }

            ImGui::EndTable();
        }
    }

    void DataTablePanel::drawGameDataTab()
    {
        ImGui::BeginGroup();
        ImGui::Text( "XML Data Files" );
        ImGui::Separator();
        if ( ImGui::Button( "Refresh Files" ) )
            reloadGameDataFiles();

        editor::EditorSectionDesc listDesc{};
        listDesc._pId       = "##DataFileList";
        listDesc._kind      = editor::EditorSectionKind::Child;
        listDesc._childSize = float2{ 200.0f, 0.0f };
        listDesc._flags     = editor::EditorSectionFlags::Border | editor::EditorSectionFlags::ResizeX;
        if ( EditorChrome::beginSection( listDesc ) )
        {
            for ( size_t fileIndex = 0; fileIndex < _listGameDataFile.size(); ++fileIndex )
            {
                const GameDataFileEntry& entry     = _listGameDataFile[fileIndex];
                const bool               bSelected = ( _selectedGameDataIndex == static_cast<int32>( fileIndex ) );
                if ( ImGui::Selectable( entry._fileName.c_str(), bSelected ) )
                {
                    if ( _bGameDataDirty == SW_FALSE || bSelected )
                    {
                        _selectedGameDataIndex = static_cast<int32>( fileIndex );
                        loadSelectedGameDataFile();
                    }
                }
            }
        }
        EditorChrome::endSection();
        ImGui::EndGroup();

        ImGui::SameLine();

        // 오른쪽: 파일 내용 보기 · 편집
        ImGui::BeginGroup();
        if ( 0 <= _selectedGameDataIndex && static_cast<size_t>( _selectedGameDataIndex ) < _listGameDataFile.size() )
        {
            const GameDataFileEntry& entry = _listGameDataFile[static_cast<size_t>( _selectedGameDataIndex )];
            ImGui::Text( "File: %s", entry._fileName.c_str() );
            ImGui::SameLine();
            if ( ImGui::Button( "Save File" ) )
                saveSelectedGameDataFile();
            ImGui::SameLine();
            if ( ImGui::Button( "Revert" ) )
                loadSelectedGameDataFile();

            ImGui::Separator();

            // 편집기 / XML 원문 텍스트 상자
            vector<utf8> arrEditBuffer;
            arrEditBuffer.resize( _selectedGameDataRawText.size() + 4096, 0 );
            if ( _selectedGameDataRawText.empty() == false )
                Memory::copy( arrEditBuffer.data(), _selectedGameDataRawText.c_str(), _selectedGameDataRawText.size() );

            constexpr ImGuiInputTextFlags editFlags = ImGuiInputTextFlags_AllowTabInput;
            if ( ImGui::InputTextMultiline( "##rawXmlEdit", arrEditBuffer.data(), arrEditBuffer.size(),
                                            ImGui::GetContentRegionAvail(), editFlags ) )
            {
                _selectedGameDataRawText = arrEditBuffer.data();
                markGameDataDirty();
            }
        }
        else
        {
            EditorWidgets::drawEmptyHint( "Select an XML data table from the list on the left." );
        }
        ImGui::EndGroup();
    }

    void DataTablePanel::reloadLocalization()
    {
        _bLocalizationDirty  = SW_FALSE;
        _bLocalizationLoaded = SW_FALSE;
        syncDocumentDirty();
    }

    void DataTablePanel::saveLocalization()
    {
        // 하나라도 쓰지 못했으면 dirty 를 지우지 않는다 — 저장 확인이 다시 묻는다.
        if ( EditorDataTableCommands::saveLocalizationProject( _localizationSheet ) == false )
            return;
        _bLocalizationDirty = SW_FALSE;
        syncDocumentDirty();
    }

    void DataTablePanel::reloadGameDataFiles()
    {
        _bGameDataLoaded = SW_FALSE;
        _gameDataJob.request();
    }

    void DataTablePanel::loadSelectedGameDataFile()
    {
        if ( _selectedGameDataIndex < 0 || static_cast<size_t>( _selectedGameDataIndex ) >= _listGameDataFile.size() )
            return;

        const GameDataFileEntry& entry = _listGameDataFile[static_cast<size_t>( _selectedGameDataIndex )];
        // 읽지 못하면 고르지 않는다 — 앞 파일의 글을 그대로 들고 고르면, 저장할 때 **앞 파일의 내용을 이 파일에** 쓴다.
        string text;
        if ( FileUtil::readTextFile( entry._absolutePath, text ) == false )
        {
            SW_LOG_ERROR( "Could not read game data table '%#' - it is not opened", entry._absolutePath.c_str() );
            _selectedGameDataIndex = -1;
            _selectedGameDataRawText.clear();
            _savedGameDataRawText.clear();
            _bGameDataDirty = SW_FALSE;
            syncDocumentDirty();
            return;
        }
        _selectedGameDataRawText = std::move( text );
        _savedGameDataRawText    = _selectedGameDataRawText;
        _bGameDataDirty          = SW_FALSE;
        syncDocumentDirty();
    }

    void DataTablePanel::saveSelectedGameDataFile()
    {
        if ( _selectedGameDataIndex < 0 || static_cast<size_t>( _selectedGameDataIndex ) >= _listGameDataFile.size() )
            return;

        const GameDataFileEntry& entry = _listGameDataFile[static_cast<size_t>( _selectedGameDataIndex )];
        if ( FileUtil::writeTextFile( entry._absolutePath, _selectedGameDataRawText ) == false )
        {
            SW_LOG_ERROR( "Could not write game data table '%#'", entry._absolutePath.c_str() );
            return;
        }
        _savedGameDataRawText = _selectedGameDataRawText;
        _bGameDataDirty       = SW_FALSE;
        syncDocumentDirty();
        SW_LOG_INFO( "Saved game data table %#", entry._fileName.c_str() );
    }
} // namespace sw::editor
