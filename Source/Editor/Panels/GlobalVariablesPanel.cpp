#include "pch.h"

#include "Editor/Panels/GlobalVariablesPanel.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/TypeRegistry.h"

#include "RuntimeAPI/Service/EditorService.h"

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
	} // namespace

	GlobalVariablesPanel::GlobalVariablesPanel()
		: IEditorPanel( false )
		, _arrSearchFilter{ 0 }
		, _bGroupByModule{ SW_TRUE }
		, _reserved{ 0 }
	{
	}

	void GlobalVariablesPanel::drawContent()
	{
		GlobalVariableManager* pGvm = editor::getService<GlobalVariableManager>();
		if ( pGvm == nullptr )
			return;

		// 1) 상단 툴바 (검색, 모듈 그룹화, 기본값 리셋)
		ImGui::SetNextItemWidth( 260.0f );
		ImGui::InputTextWithHint( "##GvSearch", "Filter variables...", _arrSearchFilter, sizeof( _arrSearchFilter ) );

		ImGui::SameLine();
		if ( ImGui::Button( "Clear" ) )
			_arrSearchFilter[0] = '\0';

		ImGui::SameLine();
		bool bGroup = ( _bGroupByModule == SW_TRUE );
		if ( ImGui::Checkbox( "Group by Module", &bGroup ) )
			_bGroupByModule = bGroup ? SW_TRUE : SW_FALSE;

		ImGui::SameLine();
		if ( ImGui::Button( "Reset All Defaults" ) )
			pGvm->resetAllToDefault();

		ImGui::Separator();

		// 2) 변수 이름 목록 스냅샷 수집 (thread-safe, const_cast 없음)
		//    이름 → findVariable() 로 mutable 포인터 재획득하여 편집
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

		ImGui::TextDisabled( "%zu / %u variables", listFiltered.size(), totalVarCount );

		// 3) 공통 테이블 플래그
		constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

		// 4) 변수 테이블 렌더링 — 그룹/비그룹 모두 동일한 4-컬럼 BeginTable/EndTable 사용
		if ( _bGroupByModule == SW_TRUE )
		{
			string currentModule;

			for ( size_t varIndex = 0; varIndex < listFiltered.size(); )
			{
				GlobalVariableInfo* pInfo	= listFiltered[varIndex];
				const string&		modName = pInfo->_moduleName.empty() ? "Global / Core" : pInfo->_moduleName;

				// 새 모듈 시작 시 CollapsingHeader 생성
				if ( modName != currentModule )
					currentModule = modName;

				const bool bModuleHeaderOpen = ImGui::CollapsingHeader( currentModule.c_str(), ImGuiTreeNodeFlags_DefaultOpen );

				// 같은 모듈에 속하는 항목 범위 계산
				size_t rangeEnd = varIndex;
				while ( rangeEnd < listFiltered.size() )
				{
					const string& rowMod = listFiltered[rangeEnd]->_moduleName.empty() ? "Global / Core" : listFiltered[rangeEnd]->_moduleName;
					if ( rowMod != currentModule )
						break;
					++rangeEnd;
				}

				if ( bModuleHeaderOpen )
				{
					const string tableId = "GvTable_" + currentModule;
					if ( ImGui::BeginTable( tableId.c_str(), 4, kTableFlags, ImVec2( 0.0f, 0.0f ) ) )
					{
						ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthFixed, 200.0f );
						ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 60.0f );
						ImGui::TableSetupColumn( "Value", ImGuiTableColumnFlags_WidthStretch );
						ImGui::TableSetupColumn( "Reset", ImGuiTableColumnFlags_WidthFixed, 50.0f );
						ImGui::TableHeadersRow();

						for ( size_t rowIndex = varIndex; rowIndex < rangeEnd; ++rowIndex )
						{
							ImGui::PushID( listFiltered[rowIndex]->_name.c_str() );
							drawVariableRow( *listFiltered[rowIndex] );
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
			// 비그룹 모드 — 단일 ScrollY 테이블, outer_size.y = -1 (남은 공간 채움)
			if ( ImGui::BeginTable( "GlobalVarsTable", 4, kTableFlags, ImVec2( 0.0f, -1.0f ) ) )
			{
				ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthFixed, 200.0f );
				ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 60.0f );
				ImGui::TableSetupColumn( "Value", ImGuiTableColumnFlags_WidthStretch );
				ImGui::TableSetupColumn( "Reset", ImGuiTableColumnFlags_WidthFixed, 50.0f );
				ImGui::TableHeadersRow();

				for ( GlobalVariableInfo* pInfo : listFiltered )
				{
					ImGui::PushID( pInfo->_name.c_str() );
					drawVariableRow( *pInfo );
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
		}
	}

	void GlobalVariablesPanel::drawVariableRow( GlobalVariableInfo& info )
	{
		ImGui::TableNextRow();

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
