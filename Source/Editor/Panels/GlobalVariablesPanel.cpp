#include "pch.h"

#include "Editor/Panels/GlobalVariablesPanel.h"

#include "Core/Common/Defines.h"
#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorGlobalVariableCommands.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/TypeRegistry.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct GlobalVariablesPanelInternal
        {
            static bool compareVariableInfo( const GlobalVariableInfo* pA, const GlobalVariableInfo* pB )
            {
                if ( pA->_moduleName != pB->_moduleName )
                    return pA->_moduleName < pB->_moduleName;
                return pA->_name < pB->_name;
            }

            static bool drawVariableWidget( GlobalVariableInfo& info )
            {
                ImGui::SetNextItemWidth( -1.0f );
                if ( info._pData == nullptr )
                {
                    ImGui::TextDisabled( "(null data)" );
                    return false;
                }

                bool bChanged{ false };
                switch ( info._type )
                {
                    case GlobalVariableType::Boolean:
                    {
                        bool bVal = info.getValueAsBool();
                        if ( ImGui::Checkbox( "##val", &bVal ) )
                        {
                            info.setValueAsBool( bVal );
                            bChanged = true;
                        }
                        break;
                    }
                    case GlobalVariableType::Float:
                    {
                        float32 fVal = info.getValueAsFloat();
                        if ( ImGui::DragFloat( "##val", &fVal, 0.1f ) )
                        {
                            info.setValueAsFloat( fVal );
                            bChanged = true;
                        }
                        break;
                    }
                    case GlobalVariableType::Int32:
                    {
                        int32 iVal = info.getValueAsInt();
                        if ( ImGui::DragInt( "##val", &iVal ) )
                        {
                            info.setValueAsInt( iVal );
                            bChanged = true;
                        }
                        break;
                    }
                    case GlobalVariableType::Enum:
                    {
                        const int32     currentVal = info.getValueAsInt();
                        TypeRegistry*   pRegistry  = editor::getService<TypeRegistry>();
                        const EnumInfo* pEnumInfo =
                            ( pRegistry != nullptr && info._enumType.empty() == false )
                                ? pRegistry->findEnum( hashed_string( info._enumType.c_str() ) )
                                : nullptr;
                        if ( pEnumInfo != nullptr && pEnumInfo->_mapValueToName.empty() == false )
                        {
                            const utf8* pName =
                                pRegistry->enumToString( hashed_string( info._enumType.c_str() ), currentVal );
                            const utf8* pPreview = ( pName != nullptr ) ? pName : "<Unknown>";
                            if ( ImGui::BeginCombo( "##val", pPreview ) )
                            {
                                for ( const auto& [val, nameHashed] : pEnumInfo->_mapValueToName )
                                {
                                    const int32 val32     = static_cast<int32>( val );
                                    const utf8* name      = nameHashed.c_str();
                                    const bool  bSelected = ( val32 == currentVal );
                                    if ( ImGui::Selectable( name, bSelected ) )
                                    {
                                        info.setValueAsInt( val32 );
                                        bChanged = true;
                                    }
                                    if ( bSelected )
                                        ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                        }
                        else
                        {
                            int32 iVal = currentVal;
                            if ( ImGui::DragInt( "##val", &iVal ) )
                            {
                                info.setValueAsInt( iVal );
                                bChanged = true;
                            }
                        }
                        break;
                    }
                    case GlobalVariableType::String:
                    {
                        string strVal = info.getValueAsString();
                        if ( EditorWidgets::drawTextField( "##val", strVal ) )
                        {
                            info.setValueAsString( strVal.c_str() );
                            bChanged = true;
                        }
                        break;
                    }
                    default:
                        ImGui::TextDisabled( "(unsupported)" );
                        break;
                }
                return bChanged;
            }
        };
    } // namespace

    /** @brief 변수 목록 테이블 공통 플래그. */
    constexpr ImGuiTableFlags kGlobalVarTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                     ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

} // namespace sw::editor

namespace sw::editor
{
    GlobalVariablesPanel::GlobalVariablesPanel()
        : IEditorPanel( false )
        , _uniquePinnedVar{}
        , _presetJob{}
        , _listPresetFile{}
        , _searchFilter{}
        , _presetNameBuf{}
        , _bGroupByModule{ SW_TRUE }
        , _bPresetListDirty{ SW_TRUE }
        , _reserved{ 0 }
    {
    }

    bool GlobalVariablesPanel::saveDocument()
    {
        if ( EditorGlobalVariableCommands::saveSessionPreset() == false )
            return false;
        clearDocumentDirty();
        return true;
    }

    void GlobalVariablesPanel::revertDocument()
    {
        GlobalVariableManager* pGvm = editor::getService<GlobalVariableManager>();
        if ( pGvm != nullptr )
            pGvm->resetAllToDefault();
    }

    void GlobalVariablesPanel::markSessionDirty()
    {
        markDocumentDirty();
    }

    void GlobalVariablesPanel::drawContent()
    {
        GlobalVariableManager* pGvm = editor::getService<GlobalVariableManager>();
        if ( pGvm == nullptr )
            return;

        drawVariableToolbar( *pGvm );

        // 2) 변수 목록 수집
        const vector<string> listAllName   = pGvm->collectVariableNames();
        const uint32         totalVarCount = pGvm->getVariableCount();

        const EditorListFilter filter{ _searchFilter.c_str() };

        vector<GlobalVariableInfo*> listFiltered;
        listFiltered.reserve( listAllName.size() );

        for ( const string& varName : listAllName )
        {
            GlobalVariableInfo* pInfo = pGvm->findVariable( varName );
            if ( pInfo == nullptr )
                continue;
            if ( filter.matchesAny( { pInfo->_name, pInfo->_description, pInfo->_moduleName } ) )
                listFiltered.push_back( pInfo );
        }

        // 포인터를 담은 컨테이너를 정렬하지만 **비교는 포인터 값이 아니라 (모듈, 이름)** 으로 한다.
        // 전역 변수 이름은 GlobalVariableManager 의 키라 유일하므로 순서가 매번 같다.
        // NOLINTNEXTLINE(bugprone-nondeterministic-pointer-iteration-order)
        std::sort( listFiltered.begin(), listFiltered.end(), GlobalVariablesPanelInternal::compareVariableInfo );

        EditorWidgets::drawCountLabel( static_cast<uint32>( listFiltered.size() ), totalVarCount, "variables" );

        drawPinnedSection( *pGvm );

        // 걸러져서 0건인 것과 애초에 없는 것은 다르게 말해 준다. 예전에는 둘 다 설명 없는 빈 표였다.
        if ( listFiltered.empty() )
        {
            if ( filter.isActive() )
                EditorWidgets::drawNoSearchResultHint( filter.getText() );
            else
                EditorWidgets::drawEmptyHint( "No global variables registered." );
            return;
        }

        drawVariableTable( listFiltered );
    }

    void GlobalVariablesPanel::drawVariableToolbar( GlobalVariableManager& gvm )
    {
        // 1) 상단 툴바 (검색, 모듈 그룹화, 기본값 리셋, 프리셋 메뉴)
        if ( EditorChrome::beginToolbar( "##GvToolbar" ) )
        {
            EditorWidgets::drawSearchField( "##GvSearch", _searchFilter,
                                            "Filter variables...", 200.0f, true );

            ImGui::SameLine();
            bool bGroup = ( _bGroupByModule == SW_TRUE );
            if ( ImGui::Checkbox( "Group by Module", &bGroup ) )
                _bGroupByModule = bGroup ? SW_TRUE : SW_FALSE;

            ImGui::SameLine();
            if ( ImGui::Button( "Reset All" ) )
            {
                gvm.resetAllToDefault();
                markSessionDirty();
            }

            ImGui::SameLine();
            if ( ImGui::Button( "Save Session" ) )
                trySaveDirtyDocument();

            ImGui::SameLine();
            if ( ImGui::Button( "Presets..." ) )
                ImGui::OpenPopup( "##GvPresetsPopup" );

            if ( ImGui::BeginPopup( "##GvPresetsPopup" ) )
            {
                ImGui::Text( "Global Variable Presets" );
                ImGui::Separator();

                ImGui::InputTextWithHint( "##PresetNameInput", "Preset Name...", _presetNameBuf.data(),
                                          _presetNameBuf.capacity() );
                ImGui::SameLine();
                if ( ImGui::Button( "Save" ) && _presetNameBuf.empty() == false )
                {
                    const string presetPath = FileUtil::joinPath(
                        EditorGlobalVariableCommands::getPresetFolderPath(),
                        string( _presetNameBuf.c_str() ) + ".gvpreset.xml" );
                    EditorGlobalVariableCommands::savePreset( presetPath, _presetNameBuf.c_str() );
                    _presetNameBuf.clear();
                    _bPresetListDirty = SW_TRUE;
                    clearDocumentDirty();
                }

                ImGui::Separator();
                ImGui::TextDisabled( "Saved Presets:" );

                if ( _bPresetListDirty == SW_TRUE && _presetJob.isPending() == false )
                    _presetJob.request( EditorGlobalVariableCommands::getPresetFolderPath(), ".gvpreset.xml", false );

                vector<string> listNewPresetFile;
                if ( _presetJob.take( listNewPresetFile ) )
                {
                    _listPresetFile   = std::move( listNewPresetFile );
                    _bPresetListDirty = SW_FALSE;
                }

                if ( _presetJob.isPending() )
                {
                    ImGui::TextDisabled( "Scanning presets..." );
                }
                else if ( _listPresetFile.empty() )
                {
                    EditorWidgets::drawEmptyHint( "No presets found." );
                }
                else
                {
                    for ( const string& presetFile : _listPresetFile )
                    {
                        const string          fname         = FileUtil::getFileNamePart( presetFile );
                        string                displayName   = fname;
                        constexpr string_view kPresetSuffix = ".gvpreset.xml";
                        if ( StringUtil::endsWith( displayName, kPresetSuffix, true ) )
                        {
                            displayName = displayName.substr( 0, displayName.size() - kPresetSuffix.size() );
                        }

                        if ( ImGui::MenuItem( displayName.c_str() ) )
                        {
                            EditorGlobalVariableCommands::loadPreset( presetFile );
                            clearDocumentDirty();
                        }
                    }
                }

                ImGui::EndPopup();
            }
        }
        EditorChrome::endToolbar();

        ImGui::Separator();
    }

    void GlobalVariablesPanel::drawPinnedSection( GlobalVariableManager& gvm )
    {
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
                        GlobalVariableInfo* pInfo = gvm.findVariable( pinnedName );
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
    }

    void GlobalVariablesPanel::drawVariableTable( const vector<GlobalVariableInfo*>& listFiltered )
    {
        // 4) 메인 변수 테이블 (그룹 또는 비그룹)
        if ( _bGroupByModule == SW_TRUE )
        {
            string currentModule;

            for ( size_t varIndex = 0; varIndex < listFiltered.size(); )
            {
                GlobalVariableInfo* pInfo   = listFiltered[varIndex];
                const string&       modName = pInfo->_moduleName.empty() ? "Global / Core" : pInfo->_moduleName;

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
                    if ( ImGui::BeginTable( tableId.c_str(), 5, kGlobalVarTableFlags, ImVec2( 0.0f, 0.0f ) ) )
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
            if ( ImGui::BeginTable( "GlobalVarsTable", 5, kGlobalVarTableFlags, ImVec2( 0.0f, -1.0f ) ) )
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

            EditorWidgets::drawTooltip( bPinned ? "즐겨찾기 고정 해제" : "즐겨찾기에 고정" );
        }

        // Name 컬럼 (툴팁 표시)
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( info._name.c_str() );
        if ( ImGui::IsItemHovered() )
        {
            ImGui::BeginTooltip();
            ImGui::Text( "설명: %s", info._description.empty() ? "(설명 없음)" : info._description.c_str() );
            ImGui::Text( "모듈: %s", info._moduleName.empty() ? "Core" : info._moduleName.c_str() );
            ImGui::EndTooltip();
        }

        // Type 컬럼
        ImGui::TableNextColumn();
        ImGui::TextDisabled( "%s", EditorGlobalVariableCommands::getTypeString( info ).c_str() );

        // Value / Widget 컬럼
        ImGui::TableNextColumn();
        if ( GlobalVariablesPanelInternal::drawVariableWidget( info ) )
            markSessionDirty();

        // Reset 컬럼
        ImGui::TableNextColumn();
        if ( ImGui::SmallButton( "Reset" ) )
        {
            info.resetToDefault();
            markSessionDirty();
        }
    }
} // namespace sw::editor
