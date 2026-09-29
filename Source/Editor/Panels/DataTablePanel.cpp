#include "pch.h"

#include "Editor/Panels/DataTablePanel.h"

#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/EditorDataTableCommands.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"

#include <imgui.h>

SW_LOG_CALLER( "DataTablePanel" );
namespace sw::editor
{

    DataTablePanel::DataTablePanel()
        : IEditorPanel{ false }
        , _localizationFilter{}
        , _newKeyBuffer{}
        , _listLocalizationRecord{}
        , _listGameDataFile{}
        , _selectedGameDataRawText{}
        , _savedGameDataRawText{}
        , _localizationJob{}
        , _gameDataJob{}
        , _activeTab{ 0 }
        , _selectedGameDataIndex{ -1 }
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
            EditorDataTableCommands::loadLocalization( _listLocalizationRecord );
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
        vector<LocalizationRecord> listLocalizationRecord;
        if ( _localizationJob.take( listLocalizationRecord ) )
        {
            if ( _bLocalizationDirty == SW_FALSE )
            {
                _listLocalizationRecord = std::move( listLocalizationRecord );
                _bLocalizationLoaded    = SW_TRUE;
            }
        }
        else if ( _bLocalizationLoaded == SW_FALSE && _localizationJob.isPending() == false )
            _localizationJob.request();

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
                _activeTab = 0;
                drawLocalizationTab();
                ImGui::EndTabItem();
            }

            if ( ImGui::BeginTabItem( "Game Data XML Tables" ) )
            {
                _activeTab = 1;
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
            EditorWidgets::drawSearchField( "##locFilter", _localizationFilter, "Search keys or translations...", 240.0f, false );
            ImGui::SameLine();

            if ( ImGui::Button( "Save Localization" ) )
                saveLocalization();

            ImGui::SameLine();
            if ( ImGui::Button( "Reload" ) )
                reloadLocalization();

            ImGui::SameLine();
            ImGui::SetNextItemWidth( 160.0f );
            ImGui::InputTextWithHint( "##newKey", "New string key...", _newKeyBuffer.data(), _newKeyBuffer.capacity() );
            ImGui::SameLine();
            if ( ImGui::Button( "Add Key" ) && _newKeyBuffer.empty() == false )
            {
                const string newKey{ _newKeyBuffer.c_str() };
                bool         bExists{ false };
                for ( const LocalizationRecord& record : _listLocalizationRecord )
                {
                    if ( record._key == newKey )
                    {
                        bExists = true;
                        break;
                    }
                }

                if ( bExists == false )
                {
                    LocalizationRecord newRecord{};
                    newRecord._key       = newKey;
                    newRecord._bModified = true;
                    _listLocalizationRecord.push_back( std::move( newRecord ) );
                    _newKeyBuffer.clear();
                    markLocalizationDirty();
                }
            }
        }
        EditorChrome::endToolbar();
    }

    void DataTablePanel::drawLocalizationTable()
    {
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

        // 표를 열기 **전에** 걸러 둔다. 표가 남은 영역을 모두 차지하므로 0건 안내를 표 뒤에 그리면
        // 화면 밖으로 밀린다. 표를 아예 열지 않아야 보인다. 행마다 필터를 다시 만들지 않는 효과도 있다.
        const EditorListFilter filter{ _localizationFilter.c_str() };

        // **멤버 버퍼를 다시 쓴다.** 지역 `vector` 였을 때는 프레임마다 할당하고 해제했다.
        vector<size_t>& listVisibleIndex = _listVisibleLocalizationIndex;
        listVisibleIndex.clear();
        listVisibleIndex.reserve( _listLocalizationRecord.size() );
        for ( size_t recordIndex = 0; recordIndex < _listLocalizationRecord.size(); ++recordIndex )
        {
            const LocalizationRecord& record = _listLocalizationRecord[recordIndex];
            if ( filter.matchesAny( { record._key, record._enUS, record._koKR, record._jaJP } ) )
                listVisibleIndex.push_back( recordIndex );
        }

        if ( listVisibleIndex.empty() )
        {
            if ( filter.isActive() )
                EditorWidgets::drawNoSearchResultHint( filter.getText() );
            else
                EditorWidgets::drawEmptyHint( "No localization records." );
            return;
        }

        if ( ImGui::BeginTable( "##locTable", 5, flags, ImGui::GetContentRegionAvail() ) )
        {
            ImGui::TableSetupColumn( "Key", ImGuiTableColumnFlags_WidthStretch, 0.25f );
            ImGui::TableSetupColumn( "en_US", ImGuiTableColumnFlags_WidthStretch, 0.25f );
            ImGui::TableSetupColumn( "ko_KR", ImGuiTableColumnFlags_WidthStretch, 0.25f );
            ImGui::TableSetupColumn( "ja_JP", ImGuiTableColumnFlags_WidthStretch, 0.20f );
            ImGui::TableSetupColumn( "Action", ImGuiTableColumnFlags_WidthFixed, 50.0f );
            ImGui::TableHeadersRow();

            int32 deleteIndex = -1;

            for ( const size_t recordIndex : listVisibleIndex )
            {
                LocalizationRecord& record = _listLocalizationRecord[recordIndex];

                ImGui::PushID( static_cast<int32>( recordIndex ) );
                ImGui::TableNextRow();

                // Col 0: Key
                ImGui::TableSetColumnIndex( 0 );
                ImGui::TextUnformatted( record._key.c_str() );

                // Col 1: en_US
                ImGui::TableSetColumnIndex( 1 );
                if ( EditorWidgets::drawTextField( "##en", record._enUS, -1.0f ) )
                {
                    record._bModified = true;
                    markLocalizationDirty();
                }

                // Col 2: ko_KR
                ImGui::TableSetColumnIndex( 2 );
                if ( EditorWidgets::drawTextField( "##ko", record._koKR, -1.0f ) )
                {
                    record._bModified = true;
                    markLocalizationDirty();
                }

                // Col 3: ja_JP
                ImGui::TableSetColumnIndex( 3 );
                if ( EditorWidgets::drawTextField( "##ja", record._jaJP, -1.0f ) )
                {
                    record._bModified = true;
                    markLocalizationDirty();
                }

                // Col 4: Action
                ImGui::TableSetColumnIndex( 4 );
                if ( ImGui::SmallButton( "Del" ) )
                    deleteIndex = static_cast<int32>( recordIndex );

                ImGui::PopID();
            }

            if ( 0 <= deleteIndex && static_cast<size_t>( deleteIndex ) < _listLocalizationRecord.size() )
            {
                _listLocalizationRecord.erase( _listLocalizationRecord.begin() + deleteIndex );
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
        _localizationJob.request();
    }

    void DataTablePanel::saveLocalization()
    {
        EditorDataTableCommands::saveLocalization( _listLocalizationRecord );
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
        FileUtil::readTextFile( entry._absolutePath, _selectedGameDataRawText );
        _savedGameDataRawText = _selectedGameDataRawText;
        _bGameDataDirty       = SW_FALSE;
        syncDocumentDirty();
    }

    void DataTablePanel::saveSelectedGameDataFile()
    {
        if ( _selectedGameDataIndex < 0 || static_cast<size_t>( _selectedGameDataIndex ) >= _listGameDataFile.size() )
            return;

        const GameDataFileEntry& entry = _listGameDataFile[static_cast<size_t>( _selectedGameDataIndex )];
        if ( FileUtil::writeTextFile( entry._absolutePath, _selectedGameDataRawText ) == false )
            return;
        _savedGameDataRawText = _selectedGameDataRawText;
        _bGameDataDirty       = SW_FALSE;
        syncDocumentDirty();
        SW_LOG_INFO( "Saved game data table %#", entry._fileName.c_str() );
    }
} // namespace sw::editor
