#include "pch.h"

#include "Editor/Panels/DataTablePanel.h"

#include "Editor/Common/Commands/EditorDataTableCommands.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"

#include <imgui.h>

SW_LOG_CALLER( "DataTablePanel" );
namespace sw::editor
{

	DataTablePanel::DataTablePanel()
		: _activeTab{ 0 }
		, _arrLocFilter{}
		, _arrNewKeyBuffer{}
		, _listLocRecord{}
		, _listGameDataFile{}
		, _selectedGameDataIndex{ -1 }
		, _selectedGameDataRawText{}
		, _bLocLoaded{ false }
		, _bGameDataLoaded{ false }
	{
	}

	void DataTablePanel::drawContent()
	{
		if ( _bLocLoaded == false )
			reloadLocalization();

		if ( _bGameDataLoaded == false )
			reloadGameDataFiles();

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
		if ( editor::beginToolbar( "##locToolbar" ) )
		{
			editor::drawSearchField( "##locFilter", _arrLocFilter, sizeof( _arrLocFilter ), "Search keys or translations...", 240.0f, false );
			ImGui::SameLine();

			if ( ImGui::Button( "Save Localization" ) )
				saveLocalization();

			ImGui::SameLine();
			if ( ImGui::Button( "Reload" ) )
				reloadLocalization();

			ImGui::SameLine();
			ImGui::SetNextItemWidth( 160.0f );
			ImGui::InputTextWithHint( "##newKey", "New string key...", _arrNewKeyBuffer, sizeof( _arrNewKeyBuffer ) );
			ImGui::SameLine();
			if ( ImGui::Button( "Add Key" ) && _arrNewKeyBuffer[0] != '\0' )
			{
				const string newKey{ _arrNewKeyBuffer };
				bool		 bExists{ false };
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
					newRec._key		  = newKey;
					newRec._bModified = true;
					_listLocRecord.push_back( std::move( newRec ) );
					_arrNewKeyBuffer[0] = '\0';
				}
			}
		}
		editor::endToolbar();

		ImGui::Separator();

		const string filter = StringUtil::toLower( _arrLocFilter );

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

				if ( filter.empty() == false )
				{
					const string lowerKey = StringUtil::toLower( rec._key.c_str() );
					const string lowerEn  = StringUtil::toLower( rec._enUS.c_str() );
					const string lowerKo  = StringUtil::toLower( rec._koKR.c_str() );
					const string lowerJa  = StringUtil::toLower( rec._jaJP.c_str() );

					if ( lowerKey.find( filter ) == string::npos && lowerEn.find( filter ) == string::npos &&
						 lowerKo.find( filter ) == string::npos && lowerJa.find( filter ) == string::npos )
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
				utf8 arrEnBuf[constant::kMaxBuffer512];
				formatstring( arrEnBuf, sizeof( arrEnBuf ), "%s", rec._enUS.c_str() );
				ImGui::SetNextItemWidth( -1.0f );
				if ( ImGui::InputText( "##en", arrEnBuf, sizeof( arrEnBuf ) ) )
				{
					rec._enUS	   = arrEnBuf;
					rec._bModified = true;
				}

				// Col 2: ko_KR
				ImGui::TableSetColumnIndex( 2 );
				utf8 arrKoBuf[constant::kMaxBuffer512];
				formatstring( arrKoBuf, sizeof( arrKoBuf ), "%s", rec._koKR.c_str() );
				ImGui::SetNextItemWidth( -1.0f );
				if ( ImGui::InputText( "##ko", arrKoBuf, sizeof( arrKoBuf ) ) )
				{
					rec._koKR	   = arrKoBuf;
					rec._bModified = true;
				}

				// Col 3: ja_JP
				ImGui::TableSetColumnIndex( 3 );
				utf8 arrJaBuf[constant::kMaxBuffer512];
				formatstring( arrJaBuf, sizeof( arrJaBuf ), "%s", rec._jaJP.c_str() );
				ImGui::SetNextItemWidth( -1.0f );
				if ( ImGui::InputText( "##ja", arrJaBuf, sizeof( arrJaBuf ) ) )
				{
					rec._jaJP	   = arrJaBuf;
					rec._bModified = true;
				}

				// Col 4: Action
				ImGui::TableSetColumnIndex( 4 );
				if ( ImGui::SmallButton( "Del" ) )
					deleteIndex = static_cast<int32>( recordIndex );

				ImGui::PopID();
			}

			if ( deleteIndex >= 0 && static_cast<size_t>( deleteIndex ) < _listLocRecord.size() )
				_listLocRecord.erase( _listLocRecord.begin() + deleteIndex );

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
		listDesc._pId		= "##DataFileList";
		listDesc._kind		= editor::EditorSectionKind::Child;
		listDesc._childSize = float2{ 200.0f, 0.0f };
		listDesc._flags		= editor::EditorSectionFlags::Border | editor::EditorSectionFlags::ResizeX;
		if ( editor::beginSection( listDesc ) )
		{
			for ( size_t fileIndex = 0; fileIndex < _listGameDataFile.size(); ++fileIndex )
			{
				const GameDataFileEntry& entry	   = _listGameDataFile[fileIndex];
				const bool				 bSelected = ( _selectedGameDataIndex == static_cast<int32>( fileIndex ) );
				if ( ImGui::Selectable( entry._fileName.c_str(), bSelected ) )
				{
					_selectedGameDataIndex = static_cast<int32>( fileIndex );
					loadSelectedGameDataFile();
				}
			}
		}
		editor::endSection();
		ImGui::EndGroup();

		ImGui::SameLine();

		// Right: File Content Viewer & Editor
		ImGui::BeginGroup();
		if ( _selectedGameDataIndex >= 0 && static_cast<size_t>( _selectedGameDataIndex ) < _listGameDataFile.size() )
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
				memcpy( arrEditBuffer.data(), _selectedGameDataRawText.c_str(), _selectedGameDataRawText.size() );

			constexpr ImGuiInputTextFlags editFlags = ImGuiInputTextFlags_AllowTabInput;
			if ( ImGui::InputTextMultiline( "##rawXmlEdit", arrEditBuffer.data(), arrEditBuffer.size(),
											ImGui::GetContentRegionAvail(), editFlags ) )
			{
				_selectedGameDataRawText = arrEditBuffer.data();
			}
		}
		else
		{
			editor::drawEmptyHint( "Select an XML data table from the list on the left." );
		}
		ImGui::EndGroup();
	}

	void DataTablePanel::reloadLocalization()
	{
		EditorDataTableCommands::loadLocalization( _listLocRecord );
		_bLocLoaded = true;
	}

	void DataTablePanel::saveLocalization()
	{
		EditorDataTableCommands::saveLocalization( _listLocRecord );
	}

	void DataTablePanel::reloadGameDataFiles()
	{
		EditorDataTableCommands::collectGameDataFiles( _listGameDataFile );
		_bGameDataLoaded = true;
	}

	void DataTablePanel::loadSelectedGameDataFile()
	{
		if ( _selectedGameDataIndex < 0 || static_cast<size_t>( _selectedGameDataIndex ) >= _listGameDataFile.size() )
			return;

		const GameDataFileEntry& entry = _listGameDataFile[static_cast<size_t>( _selectedGameDataIndex )];
		EditorDataTableCommands::loadGameDataFile( entry._absolutePath, _selectedGameDataRawText );
	}

	void DataTablePanel::saveSelectedGameDataFile()
	{
		if ( _selectedGameDataIndex < 0 || static_cast<size_t>( _selectedGameDataIndex ) >= _listGameDataFile.size() )
			return;

		const GameDataFileEntry& entry = _listGameDataFile[static_cast<size_t>( _selectedGameDataIndex )];
		EditorDataTableCommands::saveGameDataFile( entry._absolutePath, _selectedGameDataRawText );
	}
} // namespace sw::editor
