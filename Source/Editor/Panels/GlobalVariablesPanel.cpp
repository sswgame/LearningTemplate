#include "pch.h"

#include "Editor/Panels/GlobalVariablesPanel.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <algorithm>
#include <cstdlib>
#include <imgui.h>

namespace sw::editor
{
	namespace
	{
		string getTypeString( const GlobalVariableInfo& info )
		{
			switch ( info._type )
			{
				case GlobalVariableType::Boolean:
					return "Bool";
				case GlobalVariableType::Int32:
					return "Int32";
				case GlobalVariableType::Float:
					return "Float";
				case GlobalVariableType::String:
					return "String";
				case GlobalVariableType::Enum:
					return info._enumType.empty() == false ? info._enumType : "Enum";
				default:
					return "Unknown";
			}
		}

		bool matchFilter( const GlobalVariableInfo& info, const utf8* pFilter )
		{
			if ( pFilter == nullptr || pFilter[0] == '\0' )
				return true;

			const string filterLower = StringUtil::toLower( pFilter );
			const string nameLower	 = StringUtil::toLower( info._name.c_str() );
			const string descLower	 = StringUtil::toLower( info._description.c_str() );
			const string modLower	 = StringUtil::toLower( info._moduleName.c_str() );

			const bool bMatchName = ( nameLower.find( filterLower ) != string::npos );
			const bool bMatchDesc = ( descLower.find( filterLower ) != string::npos );
			const bool bMatchMod  = ( modLower.find( filterLower ) != string::npos );

			return bMatchName || bMatchDesc || bMatchMod;
		}

		bool compareVariableInfo( const GlobalVariableInfo* pA, const GlobalVariableInfo* pB )
		{
			if ( pA->_moduleName != pB->_moduleName )
				return pA->_moduleName < pB->_moduleName;
			return pA->_name < pB->_name;
		}

		void drawVariableWidget( GlobalVariableInfo& info )
		{
			ImGui::SetNextItemWidth( -1.0f );
			if ( info._pData == nullptr )
			{
				ImGui::TextDisabled( "(null data)" );
				return;
			}

			switch ( info._type )
			{
				case GlobalVariableType::Boolean:
				{
					bool* pVal = static_cast<bool*>( info._pData );
					bool  bVal = *pVal;
					if ( ImGui::Checkbox( "##val", &bVal ) )
					{
						*pVal = bVal;
						if ( info._onValueChanged.isBound() )
							info._onValueChanged( &info );
					}
					break;
				}
				case GlobalVariableType::Float:
				{
					float32* pVal = static_cast<float32*>( info._pData );
					float32	 fVal = *pVal;
					if ( ImGui::DragFloat( "##val", &fVal, 0.1f ) )
					{
						*pVal = fVal;
						if ( info._onValueChanged.isBound() )
							info._onValueChanged( &info );
					}
					break;
				}
				case GlobalVariableType::Int32:
				{
					int32* pVal = static_cast<int32*>( info._pData );
					int32  iVal = *pVal;
					if ( ImGui::DragInt( "##val", &iVal ) )
					{
						*pVal = iVal;
						if ( info._onValueChanged.isBound() )
							info._onValueChanged( &info );
					}
					break;
				}
				case GlobalVariableType::Enum:
				{
					int32*			pVal	  = static_cast<int32*>( info._pData );
					TypeRegistry*	pRegistry = editor::getService<TypeRegistry>();
					const EnumInfo* pEnumInfo =
						( pRegistry != nullptr && info._enumType.empty() == false )
							? pRegistry->findEnum( hashed_string( info._enumType.c_str() ) )
							: nullptr;
					if ( pEnumInfo != nullptr && pEnumInfo->_mapValueToName.empty() == false )
					{
						const utf8* pName =
							pRegistry->enumToString( hashed_string( info._enumType.c_str() ), *pVal );
						const utf8* pPreview = ( pName != nullptr ) ? pName : "<Unknown>";
						if ( ImGui::BeginCombo( "##val", pPreview ) )
						{
							for ( const auto& [val, nameHashed] : pEnumInfo->_mapValueToName )
							{
								const int32 val32	  = static_cast<int32>( val );
								const utf8* name	  = nameHashed.c_str();
								const bool	bSelected = ( val32 == *pVal );
								if ( ImGui::Selectable( name, bSelected ) )
								{
									*pVal = val32;
									if ( info._onValueChanged.isBound() )
										info._onValueChanged( &info );
								}
								if ( bSelected )
									ImGui::SetItemDefaultFocus();
							}
							ImGui::EndCombo();
						}
					}
					else
					{
						int32 iVal = *pVal;
						if ( ImGui::DragInt( "##val", &iVal ) )
						{
							*pVal = iVal;
							if ( info._onValueChanged.isBound() )
								info._onValueChanged( &info );
						}
					}
					break;
				}
				case GlobalVariableType::String:
				{
					string* pVal = static_cast<string*>( info._pData );
					utf8	arrBuf[512];
					StringUtil::strncpy( arrBuf, pVal->c_str(), sizeof( arrBuf ) );
					if ( ImGui::InputText( "##val", arrBuf, sizeof( arrBuf ) ) )
					{
						*pVal = arrBuf;
						if ( info._onValueChanged.isBound() )
							info._onValueChanged( &info );
					}
					break;
				}
			}
		}

		bool savePresetToFile( const string& filePath, const string& presetName )
		{
			GlobalVariableManager* pGvm = editor::getService<GlobalVariableManager>();
			if ( pGvm == nullptr )
				return false;

			const vector<string> listAllNames = pGvm->collectVariableNames();
			string				 xml		  = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
			xml += "<GlobalVariablesPreset name=\"" + presetName + "\">\n";

			for ( const string& varName : listAllNames )
			{
				const GlobalVariableInfo* pInfo = pGvm->findVariable( varName );
				if ( pInfo == nullptr || pInfo->_pData == nullptr )
					continue;

				string valStr;
				switch ( pInfo->_type )
				{
					case GlobalVariableType::Boolean:
						valStr = ( *static_cast<const bool*>( pInfo->_pData ) ) ? "1" : "0";
						break;
					case GlobalVariableType::Int32:
					case GlobalVariableType::Enum:
						valStr = to_string( *static_cast<const int32*>( pInfo->_pData ) );
						break;
					case GlobalVariableType::Float:
						valStr = to_string( *static_cast<const float32*>( pInfo->_pData ) );
						break;
					case GlobalVariableType::String:
						valStr = *static_cast<const string*>( pInfo->_pData );
						break;
				}

				xml += "    <Var name=\"" + pInfo->_name + "\" type=\"" + getTypeString( *pInfo ) +
					   "\" value=\"" + valStr + "\" />\n";
			}

			xml += "</GlobalVariablesPreset>\n";

			const string dir = FileUtil::getDirectoryPart( filePath );
			FileUtil::ensureDirectoryExists( dir );
			return FileUtil::writeFile( filePath, reinterpret_cast<const uint8*>( xml.c_str() ), xml.size() );
		}

		bool loadPresetFromFile( const string& filePath )
		{
			GlobalVariableManager* pGvm = editor::getService<GlobalVariableManager>();
			if ( pGvm == nullptr )
				return false;

			XmlDocument doc;
			if ( doc.loadFile( filePath ) == false )
				return false;

			XmlNode rootNode = doc.root();
			if ( rootNode.isValid() == false )
				return false;

			for ( XmlNode varNode = rootNode.child( "Var" ); varNode.isValid();
				  varNode		  = varNode.next( "Var" ) )
			{
				const utf8* pName = varNode.attr( "name" );
				const utf8* pVal  = varNode.attr( "value" );
				if ( pName == nullptr || pVal == nullptr )
					continue;

				GlobalVariableInfo* pInfo = pGvm->findVariable( pName );
				if ( pInfo == nullptr || pInfo->_pData == nullptr )
					continue;

				switch ( pInfo->_type )
				{
					case GlobalVariableType::Boolean:
					{
						bool* pValPtr = static_cast<bool*>( pInfo->_pData );
						*pValPtr	  = ( StringUtil::stristr( pVal, "1" ) != nullptr ||
										  StringUtil::stristr( pVal, "true" ) != nullptr );
						if ( pInfo->_onValueChanged.isBound() )
							pInfo->_onValueChanged( pInfo );
						break;
					}
					case GlobalVariableType::Int32:
					case GlobalVariableType::Enum:
					{
						int32* pValPtr = static_cast<int32*>( pInfo->_pData );
						*pValPtr	   = static_cast<int32>( std::strtol( pVal, nullptr, 10 ) );
						if ( pInfo->_onValueChanged.isBound() )
							pInfo->_onValueChanged( pInfo );
						break;
					}
					case GlobalVariableType::Float:
					{
						float32* pValPtr = static_cast<float32*>( pInfo->_pData );
						*pValPtr		 = static_cast<float32>( std::strtod( pVal, nullptr ) );
						if ( pInfo->_onValueChanged.isBound() )
							pInfo->_onValueChanged( pInfo );
						break;
					}
					case GlobalVariableType::String:
					{
						string* pValPtr = static_cast<string*>( pInfo->_pData );
						*pValPtr		= pVal;
						if ( pInfo->_onValueChanged.isBound() )
							pInfo->_onValueChanged( pInfo );
						break;
					}
				}
			}
			return true;
		}
	} // namespace

	GlobalVariablesPanel::GlobalVariablesPanel()
		: IEditorPanel( false )
		, _uniquePinnedVar{}
		, _arrSearchFilter{ 0 }
		, _arrPresetNameBuf{ 0 }
		, _bGroupByModule{ SW_TRUE }
		, _reserved{ 0 }
	{
	}

	void GlobalVariablesPanel::drawContent()
	{
		GlobalVariableManager* pGvm = editor::getService<GlobalVariableManager>();
		if ( pGvm == nullptr )
			return;

		// 1) 상단 툴바 (검색, 모듈 그룹화, 기본값 리셋, 프리셋 메뉴)
		if ( editor::beginToolbar( "##GvToolbar" ) )
		{
			editor::drawSearchField( "##GvSearch", _arrSearchFilter, sizeof( _arrSearchFilter ),
									 "Filter variables...", 200.0f, true );

			ImGui::SameLine();
			bool bGroup = ( _bGroupByModule == SW_TRUE );
			if ( ImGui::Checkbox( "Group by Module", &bGroup ) )
				_bGroupByModule = bGroup ? SW_TRUE : SW_FALSE;

			ImGui::SameLine();
			if ( ImGui::Button( "Reset All" ) )
				pGvm->resetAllToDefault();

			ImGui::SameLine();
			if ( ImGui::Button( "Presets..." ) )
				ImGui::OpenPopup( "##GvPresetsPopup" );

			if ( ImGui::BeginPopup( "##GvPresetsPopup" ) )
			{
				ImGui::Text( "Global Variable Presets" );
				ImGui::Separator();

				ImGui::InputTextWithHint( "##PresetNameInput", "Preset Name...", _arrPresetNameBuf,
										  sizeof( _arrPresetNameBuf ) );
				ImGui::SameLine();
				if ( ImGui::Button( "Save" ) && _arrPresetNameBuf[0] != '\0' )
				{
					const string presetPath = FileUtil::joinPath(
						FileUtil::getCurrentPath(),
						"Resource/game/demo/data/presets/globalvars/" + string( _arrPresetNameBuf ) + ".gvpreset.xml" );
					savePresetToFile( presetPath, _arrPresetNameBuf );
					_arrPresetNameBuf[0] = '\0';
				}

				ImGui::Separator();
				ImGui::TextDisabled( "Saved Presets:" );

				const string   presetFolder = FileUtil::joinPath( FileUtil::getCurrentPath(),
																  "Resource/game/demo/data/presets/globalvars" );
				vector<string> listPresetFiles;
				FileUtil::collectFiles( presetFolder, ".gvpreset.xml", listPresetFiles, false, false );

				if ( listPresetFiles.empty() )
				{
					ImGui::TextDisabled( "No presets found." );
				}
				else
				{
					for ( const string& presetFile : listPresetFiles )
					{
						const string fname		 = FileUtil::getFileNamePart( presetFile );
						string		 displayName = fname;
						if ( displayName.size() > 13 && displayName.substr( displayName.size() - 13 ) == ".gvpreset.xml" )
						{
							displayName = displayName.substr( 0, displayName.size() - 13 );
						}

						if ( ImGui::MenuItem( displayName.c_str() ) )
							loadPresetFromFile( presetFile );
					}
				}

				ImGui::EndPopup();
			}
		}
		editor::endToolbar();

		ImGui::Separator();

		// 2) 변수 목록 수집
		const vector<string> listAllNames  = pGvm->collectVariableNames();
		const uint32		 totalVarCount = pGvm->getVariableCount();

		vector<GlobalVariableInfo*> listFiltered;
		listFiltered.reserve( listAllNames.size() );

		for ( const string& varName : listAllNames )
		{
			GlobalVariableInfo* pInfo = pGvm->findVariable( varName );
			if ( pInfo == nullptr )
				continue;
			if ( matchFilter( *pInfo, _arrSearchFilter ) )
				listFiltered.push_back( pInfo );
		}

		std::sort( listFiltered.begin(), listFiltered.end(), compareVariableInfo );

		editor::drawCountLabel( static_cast<uint32>( listFiltered.size() ), totalVarCount, "variables" );

		constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
												ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

		// 3) ⭐ 핀 고정된 즐겨찾기 변수 섹션
		if ( _uniquePinnedVar.empty() == false )
		{
			if ( ImGui::CollapsingHeader( "Pinned / Favorites", ImGuiTreeNodeFlags_DefaultOpen ) )
			{
				if ( ImGui::BeginTable( "PinnedGvTable", 5,
										ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable,
										ImVec2( 0.0f, 0.0f ) ) )
				{
					ImGui::TableSetupColumn( "Pin", ImGuiTableColumnFlags_WidthFixed, 30.0f );
					ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthFixed, 180.0f );
					ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 60.0f );
					ImGui::TableSetupColumn( "Value", ImGuiTableColumnFlags_WidthStretch );
					ImGui::TableSetupColumn( "Reset", ImGuiTableColumnFlags_WidthFixed, 50.0f );
					ImGui::TableHeadersRow();

					for ( const string& pinnedName : _uniquePinnedVar )
					{
						GlobalVariableInfo* pInfo = pGvm->findVariable( pinnedName );
						if ( pInfo != nullptr )
						{
							ImGui::PushID( ( "Pinned_" + pInfo->_name ).c_str() );
							drawVariableRow( *pInfo, true );
							ImGui::PopID();
						}
					}
					ImGui::EndTable();
				}
			}
			ImGui::Separator();
		}

		// 4) 메인 변수 테이블 (그룹 또는 비그룹)
		if ( _bGroupByModule == SW_TRUE )
		{
			string currentModule;

			for ( size_t varIndex = 0; varIndex < listFiltered.size(); )
			{
				GlobalVariableInfo* pInfo	= listFiltered[varIndex];
				const string&		modName = pInfo->_moduleName.empty() ? "Global / Core" : pInfo->_moduleName;

				if ( modName != currentModule )
					currentModule = modName;

				const bool bModuleHeaderOpen =
					ImGui::CollapsingHeader( currentModule.c_str(), ImGuiTreeNodeFlags_DefaultOpen );

				size_t rangeEnd = varIndex;
				while ( rangeEnd < listFiltered.size() )
				{
					const string& rowMod = listFiltered[rangeEnd]->_moduleName.empty()
											 ? "Global / Core"
											 : listFiltered[rangeEnd]->_moduleName;
					if ( rowMod != currentModule )
						break;
					++rangeEnd;
				}

				if ( bModuleHeaderOpen )
				{
					const string tableId = "GvTable_" + currentModule;
					if ( ImGui::BeginTable( tableId.c_str(), 5, kTableFlags, ImVec2( 0.0f, 0.0f ) ) )
					{
						ImGui::TableSetupColumn( "Pin", ImGuiTableColumnFlags_WidthFixed, 30.0f );
						ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthFixed, 180.0f );
						ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 60.0f );
						ImGui::TableSetupColumn( "Value", ImGuiTableColumnFlags_WidthStretch );
						ImGui::TableSetupColumn( "Reset", ImGuiTableColumnFlags_WidthFixed, 50.0f );
						ImGui::TableHeadersRow();

						for ( size_t rowIndex = varIndex; rowIndex < rangeEnd; ++rowIndex )
						{
							ImGui::PushID( listFiltered[rowIndex]->_name.c_str() );
							drawVariableRow( *listFiltered[rowIndex], true );
							ImGui::PopID();
						}
						ImGui::EndTable();
					}
				}

				varIndex = rangeEnd;
			}
		}
		else
		{
			if ( ImGui::BeginTable( "GlobalVarsTable", 5, kTableFlags, ImVec2( 0.0f, -1.0f ) ) )
			{
				ImGui::TableSetupColumn( "Pin", ImGuiTableColumnFlags_WidthFixed, 30.0f );
				ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthFixed, 180.0f );
				ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 60.0f );
				ImGui::TableSetupColumn( "Value", ImGuiTableColumnFlags_WidthStretch );
				ImGui::TableSetupColumn( "Reset", ImGuiTableColumnFlags_WidthFixed, 50.0f );
				ImGui::TableHeadersRow();

				for ( GlobalVariableInfo* pInfo : listFiltered )
				{
					ImGui::PushID( pInfo->_name.c_str() );
					drawVariableRow( *pInfo, true );
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
		}
	}

	void GlobalVariablesPanel::drawVariableRow( GlobalVariableInfo& info, bool bShowPin )
	{
		ImGui::TableNextRow();

		// Pin 컬럼
		ImGui::TableNextColumn();
		if ( bShowPin )
		{
			const bool bPinned = ( _uniquePinnedVar.find( info._name ) != _uniquePinnedVar.end() );
			if ( bPinned )
				ImGui::PushStyleColor( ImGuiCol_Text, ImVec4{ 1.0f, 0.85f, 0.2f, 1.0f } );

			if ( ImGui::SmallButton( bPinned ? "*" : "-" ) )
			{
				if ( bPinned )
					_uniquePinnedVar.erase( info._name );
				else
					_uniquePinnedVar.insert( info._name );
			}

			if ( bPinned )
				ImGui::PopStyleColor();

			if ( ImGui::IsItemHovered() )
				ImGui::SetTooltip( bPinned ? "Unpin from favorites" : "Pin to favorites" );
		}

		// Name 컬럼 (툴팁 표시)
		ImGui::TableNextColumn();
		ImGui::TextUnformatted( info._name.c_str() );
		if ( ImGui::IsItemHovered() )
		{
			ImGui::BeginTooltip();
			ImGui::Text( "Description: %s", info._description.empty() ? "(none)" : info._description.c_str() );
			ImGui::Text( "Module: %s", info._moduleName.empty() ? "Core" : info._moduleName.c_str() );
			ImGui::EndTooltip();
		}

		// Type 컬럼
		ImGui::TableNextColumn();
		ImGui::TextDisabled( "%s", getTypeString( info ).c_str() );

		// Value / Widget 컬럼
		ImGui::TableNextColumn();
		drawVariableWidget( info );

		// Reset 컬럼
		ImGui::TableNextColumn();
		if ( ImGui::SmallButton( "Reset" ) )
			info.resetToDefault();
	}
} // namespace sw::editor
