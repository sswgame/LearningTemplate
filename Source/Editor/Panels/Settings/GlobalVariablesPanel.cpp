#include "pch.h"

#include "Editor/Panels/Settings/GlobalVariablesPanel.h"

#include "Core/Foundation/Defines.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/LogOnce.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/Dev/EditorGlobalVariableCommands.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/TypeRegistry.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct GlobalVariablesPanelInternal
        {
            static constexpr Color kPinnedText{ 1.0f, 0.85f, 0.2f, 1.0f };

            /**
             * @brief 모듈별 그룹 테이블의 플래그입니다. 스크롤(ScrollY)이 없으므로 행 수만큼만 차지합니다.
             * @details ScrollY 테이블에 바깥 높이 0 을 주면 ImGui 는 남은 높이를 모두 준다. 그러면 첫 그룹이 패널을 다 차지해 다음 모듈은 바깥을 한참
             *          스크롤해야 보인다. 스크롤은 패널(바깥 창)이 한다. 평면 테이블만 ScrollY 로 남은 높이를 채운다.
             */
            static constexpr ImGuiTableFlags kGroupedTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;

            /**
             * @brief 변수 테이블의 틀을 엽니다. 핀, 이름, 타입, 값, 리셋 다섯 열은 고정 섹션과 본문(모듈별, 평면)이 같습니다.
             * @return 테이블이 열렸으면 true 입니다. `EndTable` 은 호출하는 쪽이 호출합니다.
             */
            static bool beginVariableTable( const utf8* pID, ImGuiTableFlags flags, float32 outerHeight )
            {
                if ( ImGui::BeginTable( pID, 5, flags, ImVec2( 0.0f, outerHeight ) ) == false )
                    return false;
                ImGui::TableSetupColumn( "Pin", ImGuiTableColumnFlags_WidthFixed, 30.0f * EditorThemeUtil::getDPIScale() );
                ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthFixed, 180.0f * EditorThemeUtil::getDPIScale() );
                ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 60.0f * EditorThemeUtil::getDPIScale() );
                ImGui::TableSetupColumn( "Value", ImGuiTableColumnFlags_WidthStretch );
                ImGui::TableSetupColumn( "Reset", ImGuiTableColumnFlags_WidthFixed, 50.0f * EditorThemeUtil::getDPIScale() );
                ImGui::TableHeadersRow();
                return true;
            }

            static bool drawBooleanWidget( GlobalVariableInfo& info )
            {
                bool bVal = info.getValueAsBool();
                if ( ImGui::Checkbox( "##val", &bVal ) == false )
                    return false;
                info.setValueAsBool( bVal );
                return true;
            }

            static bool drawInt32Widget( GlobalVariableInfo& info )
            {
                int32 iVal = info.getValueAsInt();
                if ( ImGui::DragInt( "##val", &iVal ) == false )
                    return false;
                info.setValueAsInt( iVal );
                return true;
            }

            static bool drawFloatWidget( GlobalVariableInfo& info )
            {
                float32 fVal = info.getValueAsFloat();
                if ( ImGui::DragFloat( "##val", &fVal, 0.1f ) == false )
                    return false;
                info.setValueAsFloat( fVal );
                return true;
            }

            static bool drawStringWidget( GlobalVariableInfo& info )
            {
                string strVal = info.getValueAsString();
                if ( EditorWidgets::drawTextField( "##val", strVal ) == false )
                    return false;
                info.setValueAsString( strVal.c_str() );
                return true;
            }

            /** @brief 리플렉션 enum 테이블이 있으면 열거자 콤보, 없으면 정수 드래그입니다. */
            static bool drawEnumWidget( GlobalVariableInfo& info )
            {
                const int32     currentVal = info.getValueAsInt();
                TypeRegistry&   registry   = editor::getSubsystem<TypeRegistry>();
                const EnumInfo* pEnumInfo  = info._enumType.empty() == false ? registry.findEnum( hashed_string( info._enumType.c_str() ) ) : nullptr;
                if ( pEnumInfo == nullptr || pEnumInfo->_mapValueToName.empty() )
                    return drawInt32Widget( info );

                bool        bChanged{ false };
                const utf8* pName    = registry.enumToString( hashed_string( info._enumType.c_str() ), currentVal );
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
                return bChanged;
            }

            /** @brief 값 위젯 테이블의 행입니다. 값을 바꿨으면 true 를 반환합니다. */
            struct WidgetRow
            {
                GlobalVariableType _type;
                bool ( *_draw )( GlobalVariableInfo& info );
            };

            /**
             * @brief 타입별 값 위젯 테이블입니다. 행 순서는 `GlobalVariableType` 열거자 순서이며, 새 타입은 여기 행 하나를 더합니다.
             * @details Core 는 에디터를 모르므로 위젯은 에디터가 자기 테이블로 가집니다(Core 의 처리 테이블과 같은 열거자로 고른다).
             */
            static constexpr WidgetRow kArrWidgetRow[] = {
                {GlobalVariableType::Boolean, &drawBooleanWidget},
                {  GlobalVariableType::Int32,   &drawInt32Widget},
                {  GlobalVariableType::Float,   &drawFloatWidget},
                { GlobalVariableType::String,  &drawStringWidget},
                {   GlobalVariableType::Enum,    &drawEnumWidget},
            };

            static constexpr bool isWidgetTableInTypeOrder()
            {
                for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( GlobalVariableType::Count ); ++rowIndex )
                {
                    if ( kArrWidgetRow[rowIndex]._type != static_cast<GlobalVariableType>( rowIndex ) )
                        return false;
                }
                return true;
            }

            static bool drawVariableWidget( GlobalVariableInfo& info )
            {
                ImGui::SetNextItemWidth( -1.0f );
                if ( info._pData == nullptr )
                {
                    ImGui::TextDisabled( "(null data)" );
                    return false;
                }
                // 등록이 범위 밖 타입을 거절하므로 인덱스는 늘 행 안이다.
                return kArrWidgetRow[static_cast<uint32>( info._type )]._draw( info );
            }
        };
        static_assert( std::size( GlobalVariablesPanelInternal::kArrWidgetRow ) == static_cast<size_t>( GlobalVariableType::Count ),
                       "GlobalVariablesPanel needs one value widget per GlobalVariableType" );
        static_assert( GlobalVariablesPanelInternal::isWidgetTableInTypeOrder(), "GlobalVariablesPanel widget rows must follow GlobalVariableType order" );
    } // namespace

    /** @brief 변수 목록 테이블의 공통 플래그입니다. */
    constexpr ImGuiTableFlags kGlobalVarTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                     ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( GlobalVariablesPanel, GlobalVariablesPanel::kPanelID, EditorPanelCategory::Tool, 800 );

    GlobalVariablesPanel::GlobalVariablesPanel()
        : IEditorPanel( false )
        , _model{}
        , _searchFilter{}
        , _presetNameBuf{}
        , _bGroupByModule{ true }
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
        editor::getSubsystem<GlobalVariableManager>().resetAllToDefault();
    }

    void GlobalVariablesPanel::markSessionDirty()
    {
        markDocumentDirty();
    }

    void GlobalVariablesPanel::drawContent()
    {
        GlobalVariableManager& gvm = editor::getSubsystem<GlobalVariableManager>();

        drawVariableToolbar( gvm );

        // 2) 변수 목록 — 등록 세대나 검색 글이 바뀐 프레임에만 다시 짓는다.
        const vector<GlobalVariableInfo*>& listFiltered = _model.refreshVariableList( gvm, _searchFilter.c_str() );

        EditorWidgets::drawCountLabel( static_cast<uint32>( listFiltered.size() ), _model.getTotalVariableCount(), "variables" );

        drawPinnedSection( gvm );

        // 필터에 걸러져서 0건인 것과 애초에 없는 것은 다르게 안내한다.
        if ( listFiltered.empty() )
        {
            const EditorListFilter filter{ _searchFilter.c_str() };
            if ( filter.isActive() )
                EditorWidgets::drawNoSearchResultHint( filter.getText() );
            else
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSettings", "NoGlobalVariables" ) );
            return;
        }

        drawVariableTable( listFiltered );
    }

    void GlobalVariablesPanel::drawVariableToolbar( GlobalVariableManager& gvm )
    {
        // 1) 상단 툴바. 검색, 모듈 그룹화, 기본값 리셋, 프리셋 메뉴가 있다
        if ( EditorChrome::beginToolbar( "##GvToolbar" ) )
        {
            EditorWidgets::drawSearchField( "##GvSearch", _searchFilter,
                                            SW_LOCKEY( "EditorSettings", "FilterVariablesHint" ), 200.0f, true );

            EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( "Group by Module" ) );
            (void)ImGui::Checkbox( "Group by Module", &_bGroupByModule );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "GroupByModule" ) );

            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Reset All" ) );
            if ( ImGui::Button( "Reset All" ) )
            {
                gvm.resetAllToDefault();
                markSessionDirty();
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "ResetAllVariables" ) );

            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Save Session" ) );
            if ( ImGui::Button( "Save Session" ) )
                (void)trySaveDirtyDocument(); // 실패는 저장이 알리고 dirty 가 남는다
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "SaveSession" ) );

            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Presets..." ) );
            if ( ImGui::Button( "Presets..." ) )
                EditorInputGUI::openPopup( "##GvPresetsPopup" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "Presets" ) );

            drawPresetsPopup();
        }
        EditorChrome::endToolbar();

        ImGui::Separator();
    }

    void GlobalVariablesPanel::drawPresetsPopup()
    {
        if ( ImGui::BeginPopup( "##GvPresetsPopup" ) == false )
            return;

        ImGui::Text( "Global Variable Presets" );
        ImGui::Separator();

        ImGui::InputTextWithHint( "##PresetNameInput", SW_LOCKEY( "EditorSettings", "PresetNameHint" ), _presetNameBuf.data(),
                                  _presetNameBuf.capacity() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Save" ) );
        if ( ImGui::Button( "Save" ) && _model.savePreset( _presetNameBuf.c_str() ) )
            _presetNameBuf.clear();

        ImGui::Separator();
        ImGui::TextDisabled( "Saved Presets:" );

        if ( _model.pollPresetFiles() )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorSettings", "ScanningPresets" ) );
        }
        else if ( _model.getPresetFiles().empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSettings", "NoPresets" ) );
        }
        else
        {
            for ( const string& presetFile : _model.getPresetFiles() )
            {
                const string displayName = GlobalVariablesModel::makePresetDisplayName( presetFile );
                if ( ImGui::MenuItem( displayName.c_str() ) )
                    GlobalVariablesModel::loadPreset( presetFile );
            }
        }

        ImGui::EndPopup();
    }

    void GlobalVariablesPanel::drawPinnedSection( GlobalVariableManager& gvm )
    {
        // 3) 핀으로 고정한 즐겨찾기 변수 섹션
        if ( _model.getPinnedVariables().empty() == false )
        {
            if ( ImGui::CollapsingHeader( "Pinned / Favorites", ImGuiTreeNodeFlags_DefaultOpen ) )
            {
                // 고정 섹션은 스크롤하지 않는다. 본문 테이블 위에 통째로 보인다.
                if ( GlobalVariablesPanelInternal::beginVariableTable(
                         "PinnedGvTable", ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable, 0.0f ) )
                {
                    for ( const string& pinnedName : _model.getPinnedVariables() )
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
        // 4) 메인 변수 테이블. 모듈별 그룹이거나 평면이다
        if ( _bGroupByModule )
        {
            string currentModule;

            for ( size_t varIndex = 0; varIndex < listFiltered.size(); )
            {
                GlobalVariableInfo* pInfo      = listFiltered[varIndex];
                const string&       moduleName = pInfo->_moduleName.empty() ? "Global / Core" : pInfo->_moduleName;

                if ( moduleName != currentModule )
                    currentModule = moduleName;

                const bool bModuleHeaderOpen =
                    ImGui::CollapsingHeader( currentModule.c_str(), ImGuiTreeNodeFlags_DefaultOpen );

                size_t rangeEnd = varIndex;
                while ( rangeEnd < listFiltered.size() )
                {
                    const string& rowModule = listFiltered[rangeEnd]->_moduleName.empty()
                                                ? "Global / Core"
                                                : listFiltered[rangeEnd]->_moduleName;
                    if ( rowModule != currentModule )
                        break;
                    ++rangeEnd;
                }

                if ( bModuleHeaderOpen )
                {
                    const string tableID = "GvTable_" + currentModule;
                    if ( GlobalVariablesPanelInternal::beginVariableTable( tableID.c_str(), GlobalVariablesPanelInternal::kGroupedTableFlags, 0.0f ) )
                    {
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
            if ( GlobalVariablesPanelInternal::beginVariableTable( "GlobalVarsTable", kGlobalVarTableFlags, -1.0f ) )
            {
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
            const bool bPinned = _model.isPinned( info._name );
            if ( bPinned )
                ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( GlobalVariablesPanelInternal::kPinnedText ) );

            if ( ImGui::SmallButton( bPinned ? "*" : "-" ) )
                _model.requestTogglePin( info._name ); // 핀 섹션이 핀 집합을 도는 중일 수 있어 다음 프레임에 적용한다

            if ( bPinned )
                ImGui::PopStyleColor();

            EditorWidgets::drawTooltip( bPinned ? SW_LOCKEY( "EditorSettings", "UnpinVariable" ) : SW_LOCKEY( "EditorSettings", "PinVariable" ) );
        }

        // Name 컬럼 (툴팁 표시)
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( info._name.c_str() );
        if ( ImGui::IsItemHovered() )
        {
            const utf8*  pDescription    = info._description.empty() ? SW_LOCKEY( "EditorSettings", "NoDescription" ) : info._description.c_str();
            const utf8*  pModuleName     = info._moduleName.empty() ? "Core" : info._moduleName.c_str();
            const string descriptionLine = SW_LOCKEYFORMAT( "EditorSettings", "VariableDescription", TextArgumentList().addText( "description", pDescription ) );
            const string moduleLine      = SW_LOCKEYFORMAT( "EditorSettings", "VariableModule", TextArgumentList().addText( "module", pModuleName ) );
            ImGui::BeginTooltip();
            ImGui::TextUnformatted( descriptionLine.c_str() );
            ImGui::TextUnformatted( moduleLine.c_str() );
            ImGui::EndTooltip();
        }

        // Type 컬럼
        ImGui::TableNextColumn();
        ImGui::TextDisabled( "%s", info.getTypeName().c_str() );

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
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "ResetVariable" ) );
    }
} // namespace sw::editor
