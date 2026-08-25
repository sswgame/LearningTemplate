#include "pch.h"

#include "Editor/Windows/GlobalVariablesWindow.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/TypeRegistry.h"

#include "RuntimeAPI/EditorService.h"

#include <imgui.h>
#include <algorithm>

namespace sw
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
	} // namespace

	GlobalVariablesWindow::GlobalVariablesWindow()
		: IEditorWindow( false )
		, _arrSearchFilter{ 0 }
		, _bGroupByModule{ 1 }
		, _reserved{ 0 }
	{
	}

	void GlobalVariablesWindow::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( isOpen() == false )
			return;

		GlobalVariableManager* pGvm = editor::getService<GlobalVariableManager>();
		if ( pGvm == nullptr )
			return;

		ImGui::SetNextWindowSize( ImVec2( 620.0f, 440.0f ), ImGuiCond_FirstUseEver );
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) )
		{
			// 1) 상단 툴바 (검색, 모듈 그룹화, 기본값 리셋)
			ImGui::SetNextItemWidth( 260.0f );
			ImGui::InputTextWithHint( "##GvSearch", "Filter variables...", _arrSearchFilter, sizeof( _arrSearchFilter ) );

			ImGui::SameLine();
			if ( ImGui::Button( "Clear" ) )
				_arrSearchFilter[0] = '\0';

			ImGui::SameLine();
			bool bGroup = _bGroupByModule != 0;
			if ( ImGui::Checkbox( "Group by Module", &bGroup ) )
				_bGroupByModule = bGroup ? 1 : 0;

			ImGui::SameLine();
			if ( ImGui::Button( "Reset All Defaults" ) )
				pGvm->resetAllToDefault();

			ImGui::Separator();

			// 2) 변수 목록 수집 및 정렬
			const auto&							   mapVars = pGvm->getAllVariables();
			vector<const GlobalVariableInfo*> listFiltered;
			listFiltered.reserve( mapVars.size() );

			for ( const auto& [name, info] : mapVars )
			{
				if ( matchFilter( info, _arrSearchFilter ) )
					listFiltered.push_back( &info );
			}

			std::sort( listFiltered.begin(), listFiltered.end(),
					   []( const GlobalVariableInfo* pA, const GlobalVariableInfo* pB )
			{
				if ( pA->_moduleName != pB->_moduleName )
					return pA->_moduleName < pB->_moduleName;
				return pA->_name < pB->_name;
			} );

			ImGui::TextDisabled( "%zu / %zu variables", listFiltered.size(), mapVars.size() );

			// 3) 변수 테이블 렌더링
			if ( _bGroupByModule != 0 )
			{
				string currentModule;
				bool   bModuleHeaderOpen = true;

				for ( const GlobalVariableInfo* pConstInfo : listFiltered )
				{
					GlobalVariableInfo* pInfo = const_cast<GlobalVariableInfo*>( pConstInfo );
					if ( pInfo == nullptr )
						continue;

					const string& modName = pInfo->_moduleName.empty() ? "Global / Core" : pInfo->_moduleName;
					if ( modName != currentModule )
					{
						currentModule	  = modName;
						bModuleHeaderOpen = ImGui::CollapsingHeader( currentModule.c_str(), ImGuiTreeNodeFlags_DefaultOpen );
					}

					if ( bModuleHeaderOpen )
					{
						ImGui::PushID( pInfo->_name.c_str() );
						drawVariableRow( *pInfo );
						ImGui::PopID();
					}
				}
			}
			else
			{
				if ( ImGui::BeginTable( "GlobalVarsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY ) )
				{
					ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthFixed, 200.0f );
					ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 60.0f );
					ImGui::TableSetupColumn( "Value", ImGuiTableColumnFlags_WidthStretch );
					ImGui::TableSetupColumn( "Reset", ImGuiTableColumnFlags_WidthFixed, 50.0f );
					ImGui::TableHeadersRow();

					for ( const GlobalVariableInfo* pConstInfo : listFiltered )
					{
						GlobalVariableInfo* pInfo = const_cast<GlobalVariableInfo*>( pConstInfo );
						if ( pInfo == nullptr )
							continue;

						ImGui::PushID( pInfo->_name.c_str() );

						ImGui::TableNextRow();
						ImGui::TableNextColumn();

						// Name 컬럼 (툴팁 표시)
						ImGui::TextUnformatted( pInfo->_name.c_str() );
						if ( ImGui::IsItemHovered() )
						{
							ImGui::BeginTooltip();
							ImGui::Text( "Description: %s", pInfo->_description.empty() ? "(none)" : pInfo->_description.c_str() );
							ImGui::Text( "Module: %s", pInfo->_moduleName.empty() ? "Core" : pInfo->_moduleName.c_str() );
							ImGui::EndTooltip();
						}

						// Type 컬럼
						ImGui::TableNextColumn();
						ImGui::TextDisabled( "%s", getTypeString( *pInfo ).c_str() );

						// Value / Widget 컬럼
						ImGui::TableNextColumn();
						ImGui::SetNextItemWidth( -1.0f );

						if ( pInfo->_pData != nullptr )
						{
							switch ( pInfo->_type )
							{
								case GlobalVariableType::Boolean:
								{
									bool* pVal = static_cast<bool*>( pInfo->_pData );
									bool  bVal = *pVal;
									if ( ImGui::Checkbox( "##val", &bVal ) )
									{
										*pVal = bVal;
										if ( pInfo->_onValueChanged.isBound() )
											pInfo->_onValueChanged( pInfo );
									}
									break;
								}
								case GlobalVariableType::Float:
								{
									float32* pVal = static_cast<float32*>( pInfo->_pData );
									float32	 fVal = *pVal;
									if ( ImGui::DragFloat( "##val", &fVal, 0.1f ) )
									{
										*pVal = fVal;
										if ( pInfo->_onValueChanged.isBound() )
											pInfo->_onValueChanged( pInfo );
									}
									break;
								}
								case GlobalVariableType::Int32:
								{
									int32* pVal = static_cast<int32*>( pInfo->_pData );
									int32  iVal = *pVal;
									if ( ImGui::DragInt( "##val", &iVal ) )
									{
										*pVal = iVal;
										if ( pInfo->_onValueChanged.isBound() )
											pInfo->_onValueChanged( pInfo );
									}
									break;
								}
								case GlobalVariableType::Enum:
								{
									int32*			pVal	  = static_cast<int32*>( pInfo->_pData );
									auto*			pRegistry = editor::getService<TypeRegistry>();
									const EnumInfo* pEnumInfo = ( pRegistry != nullptr && pInfo->_enumType.empty() == false )
																	? pRegistry->findEnum( hashed_string( pInfo->_enumType.c_str() ) )
																	: nullptr;
									if ( pEnumInfo != nullptr && pEnumInfo->_mapValueToName.empty() == false )
									{
										const utf8* pName	 = pRegistry->enumToString( hashed_string( pInfo->_enumType.c_str() ), *pVal );
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
													if ( pInfo->_onValueChanged.isBound() )
														pInfo->_onValueChanged( pInfo );
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
											if ( pInfo->_onValueChanged.isBound() )
												pInfo->_onValueChanged( pInfo );
										}
									}
									break;
								}
								case GlobalVariableType::String:
								{
									string* pVal = static_cast<string*>( pInfo->_pData );
									char	buf[256];
									StringUtil::strncpy( buf, pVal->c_str(), sizeof( buf ) );
									if ( ImGui::InputText( "##val", buf, sizeof( buf ) ) )
									{
										*pVal = buf;
										if ( pInfo->_onValueChanged.isBound() )
											pInfo->_onValueChanged( pInfo );
									}
									break;
								}
							}
						}
						else
						{
							ImGui::TextDisabled( "(null data)" );
						}

						// Reset 컬럼
						ImGui::TableNextColumn();
						if ( ImGui::SmallButton( "Reset" ) )
						{
							pInfo->resetToDefault();
						}

						ImGui::PopID();
					}
					ImGui::EndTable();
				}
			}
		}
		ImGui::End();
	}

	void GlobalVariablesWindow::drawVariableRow( GlobalVariableInfo& info )
	{
		ImGui::Columns( 3, nullptr, false );
		ImGui::SetColumnWidth( 0, 220.0f );
		ImGui::SetColumnWidth( 1, 260.0f );

		// 1) Name & Description
		ImGui::TextUnformatted( info._name.c_str() );
		if ( ImGui::IsItemHovered() && info._description.empty() == false )
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted( info._description.c_str() );
			ImGui::EndTooltip();
		}

		ImGui::NextColumn();

		// 2) Control Widget
		ImGui::SetNextItemWidth( -1.0f );
		if ( info._pData != nullptr )
		{
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
					auto*			pRegistry = editor::getService<TypeRegistry>();
					const EnumInfo* pEnumInfo = ( pRegistry != nullptr && info._enumType.empty() == false )
													? pRegistry->findEnum( hashed_string( info._enumType.c_str() ) )
													: nullptr;
					if ( pEnumInfo != nullptr && pEnumInfo->_mapValueToName.empty() == false )
					{
						const utf8* pName	 = pRegistry->enumToString( hashed_string( info._enumType.c_str() ), *pVal );
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
					char	buf[256];
					StringUtil::strncpy( buf, pVal->c_str(), sizeof( buf ) );
					if ( ImGui::InputText( "##val", buf, sizeof( buf ) ) )
					{
						*pVal = buf;
						if ( info._onValueChanged.isBound() )
							info._onValueChanged( &info );
					}
					break;
				}
			}
		}
		else
		{
			ImGui::TextDisabled( "(null data)" );
		}

		ImGui::NextColumn();

		// 3) Reset button
		if ( ImGui::SmallButton( "Reset" ) )
		{
			info.resetToDefault();
		}

		ImGui::Columns( 1 );
	}

} // namespace sw
