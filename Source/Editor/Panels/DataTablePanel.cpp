#include "pch.h"

#include "Editor/Panels/DataTablePanel.h"

#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/EditorDataTableCommands.h"
#include "Editor/Common/EditorSessionPolicy.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"

#include <imgui.h>

SW_LOG_CALLER( "DataTablePanel" );
namespace sw::editor
{

    DataTablePanel::DataTablePanel()
        : IEditorPanel{ false }
        , _locFilter{}
        , _newKeyBuffer{}
        , _listLocRecord{}
        , _listGameDataFile{}
        , _selectedGameDataRawText{}
        , _savedGameDataRawText{}
        , _locJob{}
        , _gameDataJob{}
        , _activeTab{ 0 }
        , _selectedGameDataIndex{ -1 }
        , _bLocLoaded{ SW_FALSE }
        , _bGameDataLoaded{ SW_FALSE }
        , _bLocDirty{ SW_FALSE }
        , _bGameDataDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool DataTablePanel::isDocumentDirty() const
    {
        return EditorSessionPolicy::isToolSessionDirty( _bLocDirty == SW_TRUE, _bGameDataDirty == SW_TRUE, false );
    }

    bool DataTablePanel::trySaveDirtyDocument()
    {
        if ( isDocumentDirty() == false )
            return false;
        if ( _bLocDirty == SW_TRUE )
            saveLocalization();
        if ( _bGameDataDirty == SW_TRUE )
            saveSelectedGameDataFile();
        return isDocumentDirty() == false;
    }

    void DataTablePanel::discardDirtyDocument()
    {
        if ( _bLocDirty == SW_TRUE )
        {
            EditorDataTableCommands::loadLocalization( _listLocRecord );
            _bLocLoaded = SW_TRUE;
            _bLocDirty  = SW_FALSE;
        }
        if ( _bGameDataDirty == SW_TRUE )
        {
            _selectedGameDataRawText = _savedGameDataRawText;
            _bGameDataDirty          = SW_FALSE;
        }
    }

    EditorPanelFlags DataTablePanel::getPanelFlags() const
    {
        if ( isDocumentDirty() )
            return EditorPanelFlags::UnsavedDocument;
        return EditorPanelFlags::None;
    }

    void DataTablePanel::markLocDirty()
    {
        _bLocDirty = SW_TRUE;
    }

    void DataTablePanel::markGameDataDirty()
    {
        _bGameDataDirty = SW_TRUE;
    }

    void DataTablePanel::pollBackgroundJobs()
    {
        vector<LocRecord> listLoc;
        if ( _locJob.take( listLoc ) )
        {
            if ( _bLocDirty == SW_FALSE )
            {
                _listLocRecord = std::move( listLoc );
                _bLocLoaded    = SW_TRUE;
            }
        }
        else if ( _bLocLoaded == SW_FALSE && _locJob.isPending() == false )
            _locJob.request();

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
        if ( EditorChrome::beginToolbar( "##locToolbar" ) )
        {
            EditorWidgets::drawSearchField( "##locFilter", _locFilter, "Search keys or translations...", 240.0f, false );
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
                for ( const LocRecord& rec : _listLocRecord )
                {
                    if ( rec._key == newKey )
                    {
                        bExists = true;
                        break;
                    }
                }

                if ( bExists == false )
                {
                    LocRecord newRec{};
                    newRec._key       = newKey;
                    newRec._bModified = true;
                    _listLocRecord.push_back( std::move( newRec ) );
                    _newKeyBuffer.clear();
                    markLocDirty();
                }
            }
        }
        EditorChrome::endToolbar();

        ImGui::Separator();

        if ( _bLocLoaded == SW_FALSE )
        {
            EditorWidgets::drawEmptyHint( "Loading localization..." );
            return;
        }

        constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

        if ( ImGui::BeginTable( "##locTable", 5, flags, ImGui::GetContentRegionAvail() ) )
        {
            ImGui::TableSetupColumn( "Key", ImGuiTableColumnFlags_WidthStretch, 0.25f );
            ImGui::TableSetupColumn( "en_US", ImGuiTableColumnFlags_WidthStretch, 0.25f );
            ImGui::TableSetupColumn( "ko_KR", ImGuiTableColumnFlags_WidthStretch, 0.25f );
            ImGui::TableSetupColumn( "ja_JP", ImGuiTableColumnFlags_WidthStretch, 0.20f );
            ImGui::TableSetupColumn( "Action", ImGuiTableColumnFlags_WidthFixed, 50.0f );
            ImGui::TableHeadersRow();

            int32 deleteIndex = -1;

            for ( size_t recordIndex = 0; recordIndex < _listLocRecord.size(); ++recordIndex )
            {
                LocRecord& rec = _listLocRecord[recordIndex];

                if ( _locFilter.empty() == false )
                {
                    if ( StringUtil::stristr( rec._key.c_str(), _locFilter.c_str() ) == nullptr &&
                         StringUtil::stristr( rec._enUS.c_str(), _locFilter.c_str() ) == nullptr &&
                         StringUtil::stristr( rec._koKR.c_str(), _locFilter.c_str() ) == nullptr &&
                         StringUtil::stristr( rec._jaJP.c_str(), _locFilter.c_str() ) == nullptr )
                    {
                        continue;
                    }
                }

                ImGui::PushID( static_cast<int32>( recordIndex ) );
                ImGui::TableNextRow();

                // Col 0: Key
                ImGui::TableSetColumnIndex( 0 );
                ImGui::TextUnformatted( rec._key.c_str() );

                // Col 1: en_US
                ImGui::TableSetColumnIndex( 1 );
                fixed_string<constant::kMaxBuffer512> arrEnBuf{ rec._enUS.c_str() };
                ImGui::SetNextItemWidth( -1.0f );
                if ( ImGui::InputText( "##en", arrEnBuf.data(), arrEnBuf.capacity() ) )
                {
                    rec._enUS      = arrEnBuf.c_str();
                    rec._bModified = true;
                    markLocDirty();
                }

                // Col 2: ko_KR
                ImGui::TableSetColumnIndex( 2 );
                fixed_string<constant::kMaxBuffer512> arrKoBuf{ rec._koKR.c_str() };
                ImGui::SetNextItemWidth( -1.0f );
                if ( ImGui::InputText( "##ko", arrKoBuf.data(), arrKoBuf.capacity() ) )
                {
                    rec._koKR      = arrKoBuf.c_str();
                    rec._bModified = true;
                    markLocDirty();
                }

                // Col 3: ja_JP
                ImGui::TableSetColumnIndex( 3 );
                fixed_string<constant::kMaxBuffer512> arrJaBuf{ rec._jaJP.c_str() };
                ImGui::SetNextItemWidth( -1.0f );
                if ( ImGui::InputText( "##ja", arrJaBuf.data(), arrJaBuf.capacity() ) )
                {
                    rec._jaJP      = arrJaBuf.c_str();
                    rec._bModified = true;
                    markLocDirty();
                }

                // Col 4: Action
                ImGui::TableSetColumnIndex( 4 );
                if ( ImGui::SmallButton( "Del" ) )
                    deleteIndex = static_cast<int32>( recordIndex );

                ImGui::PopID();
            }

            if ( 0 <= deleteIndex && static_cast<size_t>( deleteIndex ) < _listLocRecord.size() )
            {
                _listLocRecord.erase( _listLocRecord.begin() + deleteIndex );
                markLocDirty();
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

        // Right: File Content Viewer & Editor
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

            // Editor / Raw XML text box
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
        _bLocDirty  = SW_FALSE;
        _bLocLoaded = SW_FALSE;
        _locJob.request();
    }

    void DataTablePanel::saveLocalization()
    {
        EditorDataTableCommands::saveLocalization( _listLocRecord );
        _bLocDirty = SW_FALSE;
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
        SW_LOG_INFO( "Saved game data table %#", entry._fileName.c_str() );
    }
} // namespace sw::editor
