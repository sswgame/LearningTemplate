/**
 * @file GlobalVariablesPanel.cpp
 */
#include "Panels/GlobalVariablesPanel.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Utility/GlobalVariable/GlobalVariableManager.h"
#include "Core/Utility/String/StringUtil.h"
#include <imgui.h>

namespace sw
{
	void GlobalVariablesPanel::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle() ) == false )
		{
			ImGui::End();
			return;
		}

		ImGui::InputText( "Filter", _filterBuffer, sizeof( _filterBuffer ) );
		ImGui::SameLine();
		if ( ImGui::Button( "Reset All Defaults" ) )
			sw::getGlobalVariableManager().resetAllToDefault();

		ImGui::Separator();

		const ImGuiTableFlags flags =
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

		if ( ImGui::BeginTable( "GlobalVariablesTable", 5, flags ) )
		{
			ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthFixed, 150.0f );
			ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 60.0f );
			ImGui::TableSetupColumn( "Value / Control", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableSetupColumn( "Description", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableSetupColumn( "Actions", ImGuiTableColumnFlags_WidthFixed, 60.0f );
			ImGui::TableHeadersRow();

			const auto& vars	  = sw::getGlobalVariableManager().getAllVariables();
			std::string filterStr = _filterBuffer;
			std::transform( filterStr.begin(), filterStr.end(), filterStr.begin(), []( unsigned char c )
			{ return static_cast<char>( std::tolower( c ) ); } );

			int idIndex = 0;
			for ( const auto& [name, info] : vars )
			{
				if ( filterStr.empty() == false )
				{
					std::string nameLower = name;
					std::string descLower = info._description;
					std::transform( nameLower.begin(), nameLower.end(), nameLower.begin(), []( unsigned char c )
					{ return static_cast<char>( std::tolower( c ) ); } );
					std::transform( descLower.begin(), descLower.end(), descLower.begin(), []( unsigned char c )
					{ return static_cast<char>( std::tolower( c ) ); } );

					if ( nameLower.find( filterStr ) == std::string::npos && descLower.find( filterStr ) == std::string::npos )
						continue;
				}

				ImGui::PushID( idIndex++ );
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex( 0 );
				ImGui::TextUnformatted( name.c_str() );

				ImGui::TableSetColumnIndex( 1 );
				switch ( info._type )
				{
					case GlobalVariableType::Bool:
						ImGui::TextColored( ImVec4( 0.4f, 0.8f, 1.0f, 1.0f ), "Bool" );
						break;
					case GlobalVariableType::Int32:
						ImGui::TextColored( ImVec4( 0.4f, 1.0f, 0.4f, 1.0f ), "Int32" );
						break;
					case GlobalVariableType::Float:
						ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.4f, 1.0f ), "Float" );
						break;
					case GlobalVariableType::String:
						ImGui::TextColored( ImVec4( 1.0f, 0.4f, 0.8f, 1.0f ), "String" );
						break;
					case GlobalVariableType::Enum:
						ImGui::TextColored( ImVec4( 0.8f, 1.0f, 0.4f, 1.0f ), "Enum" );
						break;
				}

				ImGui::TableSetColumnIndex( 2 );
				if ( info._pData != nullptr )
				{
					switch ( info._type )
					{
						case GlobalVariableType::Bool:
						{
							bool* pVal = static_cast<bool*>( info._pData );
							if ( ImGui::Checkbox( "##bool_val", pVal ) && info._onValueChanged.isBound() )
								info._onValueChanged( const_cast<sw::GlobalVariableInfo*>( &info ) );
							break;
						}
						case GlobalVariableType::Int32:
						{
							int32* pVal = static_cast<int32*>( info._pData );
							if ( ImGui::InputInt( "##int_val", pVal ) && info._onValueChanged.isBound() )
								info._onValueChanged( const_cast<sw::GlobalVariableInfo*>( &info ) );
							break;
						}
						case GlobalVariableType::Float:
						{
							float32* pVal = static_cast<float32*>( info._pData );
							if ( ImGui::InputFloat( "##float_val", pVal ) && info._onValueChanged.isBound() )
								info._onValueChanged( const_cast<sw::GlobalVariableInfo*>( &info ) );
							break;
						}
						case GlobalVariableType::String:
						{
							std::string* pVal = static_cast<std::string*>( info._pData );
							char		 buf[256];
							strncpy_s( buf, pVal->c_str(), sizeof( buf ) - 1 );
							if ( ImGui::InputText( "##string_val", buf, sizeof( buf ) ) )
							{
								*pVal = buf;
								if ( info._onValueChanged.isBound() )
									info._onValueChanged( const_cast<sw::GlobalVariableInfo*>( &info ) );
							}
							break;
						}
						case GlobalVariableType::Enum:
						{
							int32*				pVal	  = static_cast<int32*>( info._pData );
							const sw::EnumInfo* pEnumInfo = sw::getTypeRegistry().findEnum( sw::hashed_string( info._enumType.c_str() ) );
							if ( pEnumInfo )
							{
								const hashed_string currentName = pEnumInfo->toString( *pVal );
								const utf8*			preview		= currentName.empty() == false ? currentName.c_str() : "(invalid)";
								if ( ImGui::BeginCombo( "##enum_val", preview ) )
								{
									std::unordered_set<int64> seenValues;
									for ( const auto& [enumName, enumValue] : pEnumInfo->_mapNameToValue )
									{
										if ( seenValues.insert( enumValue ).second == false )
											continue;

										const bool isSelected = ( *pVal == static_cast<int32>( enumValue ) );
										if ( ImGui::Selectable( enumName.c_str(), isSelected ) )
										{
											*pVal = static_cast<int32>( enumValue );
											if ( info._onValueChanged.isBound() )
												info._onValueChanged( const_cast<sw::GlobalVariableInfo*>( &info ) );
										}
										if ( isSelected )
											ImGui::SetItemDefaultFocus();
									}
									ImGui::EndCombo();
								}
							}
							else
							{
								ImGui::TextUnformatted( "(enum metadata missing)" );
								ImGui::SameLine();
								ImGui::Text( "%d", *pVal );
							}
							break;
						}
					}
				}

				ImGui::TableSetColumnIndex( 3 );
				ImGui::TextUnformatted( info._description.c_str() );

				ImGui::TableSetColumnIndex( 4 );
				if ( ImGui::Button( "Reset" ) )
					sw::getGlobalVariableManager().resetToDefault( name );

				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		ImGui::End();
	}
}
