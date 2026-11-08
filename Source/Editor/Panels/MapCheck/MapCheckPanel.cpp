#include "pch.h"

#include "Editor/Panels/MapCheck/MapCheckPanel.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Status/EditorStatusBar.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/MapCheck/Model/MapCheckFixCommands.h"
#include "Editor/Panels/Scene/EditorSceneViewUtil.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"
#include "Engine/Scene/SceneManager.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct MapCheckPanelInternal
        {
            /** @brief 오브젝트를 화면에 담는 기본 반지름(m)입니다 — 더블클릭 초점. */
            static constexpr float32 kFocusRadius = 2.0f;

            /** @brief 상태줄 항목이 프레임 사이에 기억하는 값입니다. 패널이 닫혀도 상태줄은 그려지므로 패널 멤버가 아니라 에디터 범위의 상태 저장소가 소유한다. */
            struct State
            {
                MapCheckCounts _statusCounts{};            ///< 상태줄 맵 검사 수 - 결과 번호가 바뀔 때만 다시 센다
                uint64         _statusRevisionKey{ 0 };    ///< `makeRevisionKey` — 씬이 바뀌어도 다시 센다
                bool           _bStatusCounted{ false };   ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
                uint64         _toastSceneGeneration{ 0 }; ///< 씬을 열었을 때 한 번 띄우는 "Map Check: N errors" 토스트의 기준 씬이다(이름이 바뀌면 새 씬이다)
            };

            static State& getState() { return getEditorState<State>(); }

            /** @brief 활성 씬의 검증 결과 표입니다(표는 월드마다 하나 — `SceneValidationIssues`). 씬이 없으면 nullptr 입니다. */
            static const ValidationIssueLog* findActiveIssueLog()
            {
                const GameObjectManager* pManager = editor::getActiveObjectManager();
                return pManager != nullptr ? &pManager->getSubsystem<SceneValidationIssues>().getLog() : nullptr;
            }

            /** @brief 표의 결과 번호에 활성 씬 세대를 붙인 열쇠입니다 — 씬이 바뀌면 새 표라 번호가 같아도 다시 모은다. 씬이 없으면 0 입니다. */
            static uint64 makeRevisionKey( const ValidationIssueLog* pLog )
            {
                if ( pLog == nullptr )
                    return 0;
                const uint64 generation = editor::getSubsystem<SceneManager>().getSceneGeneration();
                return ( generation << 32u ) ^ ( static_cast<uint64>( pLog->getRevision() ) + 1u );
            }

            /** @brief 활성 씬 표의 결과를 모읍니다. 씬이 없으면 비웁니다. */
            static void collectActiveIssues( const ValidationIssueLog* pLog, vector<ValidationIssue>& outListIssue )
            {
                outListIssue.clear();
                if ( pLog != nullptr )
                    pLog->collectIssues( outListIssue );
            }

            /** @brief 결과 번호가 바뀌었으면 무게별 수를 다시 셉니다. */
            static const MapCheckCounts& syncStatusCounts()
            {
                const ValidationIssueLog* pLog        = findActiveIssueLog();
                const uint64              revisionKey = makeRevisionKey( pLog );
                if ( getState()._bStatusCounted && revisionKey == getState()._statusRevisionKey )
                    return getState()._statusCounts;
                vector<ValidationIssue> listIssue;
                collectActiveIssues( pLog, listIssue );
                getState()._statusCounts      = MapCheckCounts::countIssues( listIssue );
                getState()._statusRevisionKey = revisionKey;
                getState()._bStatusCounted    = true;
                return getState()._statusCounts;
            }

            /** @brief 활성 씬이 바뀐 첫 프레임에 오류가 있으면 토스트를 한 번 띄웁니다(언리얼은 맵을 열 때 Map Check 를 띄운다). */
            static void notifyOnSceneChange( EditorContext& context, const MapCheckCounts& counts )
            {
                const SceneManager& sceneManager = editor::getSubsystem<SceneManager>();
                const uint64        generation   = sceneManager.getSceneGeneration();
                if ( generation == getState()._toastSceneGeneration )
                    return;
                getState()._toastSceneGeneration = generation;
                if ( counts._errorCount == 0 )
                    return;
                const string message = SW_LOCKEYFORMAT( "EditorMapCheck", "SceneIssuesToast", TextArgumentList().addInteger( "errorCount", counts._errorCount ).addInteger( "warningCount", counts._warningCount ) );
                context.getSubsystem<EditorNotificationManager>().push( "Map Check", message.c_str(), NotificationKind::Warning, 6.0f );
            }

            /** @brief 상태줄의 맵 검사 수 — 오류가 있으면 빨강, 경고만 있으면 주황, 없으면 흐리게. 누르면 Map Check 창. */
            static void drawStatusItem()
            {
                EditorContext&        context = editor::getSubsystem<EditorContext>();
                const MapCheckCounts& counts  = syncStatusCounts();
                notifyOnSceneChange( context, counts );
                const Color                            color = counts._errorCount > 0   ? SW_THEME_COLOR( "Signal", "Error" )
                                                             : counts._warningCount > 0 ? SW_THEME_COLOR( "Signal", "Warning" )
                                                                                        : SW_THEME_COLOR( "Base", "TextMuted" );
                fixed_string<constant::buffer::kMax64> label;
                formatstring( label.data(), label.capacity(), "%# %###statusMapCheck", SW_EDITOR_ICON( "Warning" ), counts._errorCount + counts._warningCount );
                ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( color ) );
                const bool bOpen = ImGui::SmallButton( label.c_str() );
                ImGui::PopStyleColor();
                EditorSelfTestMarks::record( "statusBar.mapCheck" );
                if ( bOpen )
                    context.getSubsystem<EditorWorkspace>().requestOpenPanel<MapCheckPanel>();
                if ( ImGui::IsItemHovered() )
                {
                    const string tooltipText = SW_LOCKEYFORMAT( "EditorMapCheck", "StatusTooltip", TextArgumentList().addInteger( "errorCount", counts._errorCount ).addInteger( "warningCount", counts._warningCount ) );
                    ImGui::SetTooltip( "%s", tooltipText.c_str() );
                }
            }

            /** @brief 줄 @p issue 의 오브젝트를 활성 씬에서 찾습니다. 없으면 nullptr 입니다(지운 오브젝트 · 다른 씬의 결과). */
            static GameObject* findIssueObject( const ValidationIssue& issue )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                return pManager != nullptr && issue._sourceID != 0 ? pManager->findGameObjectByID( issue._sourceID ) : nullptr;
            }

            /** @brief 감시 값 `Editor.MapCheckFixableRows` — 맵 검사 창이 지난 그리기에서 안전한 고침이 있다고 본 줄 수입니다. 창이 없으면 값을 내지 않는다. */
            [[nodiscard]] static bool readFixableRows( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                EditorContext*       pContext = editor::findSubsystem<EditorContext>();
                const MapCheckPanel* pPanel   = pContext != nullptr ? pContext->getSubsystem<EditorPanelManager>().findPanel<MapCheckPanel>() : nullptr;
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getFixableRowCount() );
                return true;
            }

            /** @brief 감시 값 `Editor.MapCheckIssueCount` — 활성 씬 표의 결과 수(거르기 전)입니다. */
            [[nodiscard]] static bool readIssueCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ValidationIssueLog* pLog = findActiveIssueLog();
                if ( pLog == nullptr )
                    return false;
                outValue = static_cast<float64>( pLog->getIssueCount() );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( MapCheckPanel, MapCheckPanel::kPanelID, EditorPanelCategory::Tool, 2040 );
    SW_EDITOR_STATUS_BAR_ITEM( MapCheck, MapCheckPanel::kPanelID, 100, &MapCheckPanelInternal::drawStatusItem );
    SW_AUTOMATION_WATCH( editorMapCheckFixableRows, "Editor.MapCheckFixableRows", "Map Check rows with a safe fix in the last drawn frame (the Fix All count)",
                         &MapCheckPanelInternal::readFixableRows );
    SW_AUTOMATION_WATCH( editorMapCheckIssueCount, "Editor.MapCheckIssueCount", "Validation issues of the active scene (before the Map Check filter)",
                         &MapCheckPanelInternal::readIssueCount );

    MapCheckPanel::MapCheckPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _listRow{}
        , _searchBuffer{}
        , _builtSearch{}
        , _counts{}
        , _builtRevisionKey{ 0 }
        , _fixableRowCount{ 0 }
        , _bShowErrors{ SW_TRUE }
        , _bShowWarnings{ SW_TRUE }
        , _bRowsDirty{ SW_TRUE }
        , _reservedFlags{ 0 }
    {
    }

    uint32 MapCheckPanel::validateActiveScene()
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        if ( pManager == nullptr )
            return 0;
        const Stopwatch     stopwatch;
        vector<GameObject*> listObject;
        pManager->getAllGameObjects( listObject );
        for ( const GameObject* pObject : listObject )
        {
            (void)ObjectValidation::reportGameObject( *pObject, false ); // 결과는 그 씬의 표(SceneValidationIssues)에 남고 테이블이 읽는다
        }
        SW_LOG_INFO_ID( "MapCheckPanel.MapCheckObjectChecked", "Map Check: %# object(s) checked in %# ms", listObject.size(), stopwatch.getElapsedMilliseconds() );
        return static_cast<uint32>( listObject.size() );
    }

    bool MapCheckPanel::selectRowObject( uint32 rowIndex ) const
    {
        if ( rowIndex >= _listRow.size() )
            return false;
        GameObject* pObject = MapCheckPanelInternal::findIssueObject( _listRow[rowIndex] );
        if ( pObject == nullptr )
            return false;
        editor::getMainSceneSelection().selectObject( pObject );
        return true;
    }

    void MapCheckPanel::syncRows()
    {
        const ValidationIssueLog* pLog        = MapCheckPanelInternal::findActiveIssueLog();
        const uint64              revisionKey = MapCheckPanelInternal::makeRevisionKey( pLog );
        const bool                bChanged    = revisionKey != _builtRevisionKey || _builtSearch != _searchBuffer.c_str();
        if ( bChanged == false && _bRowsDirty == SW_FALSE )
            return;
        vector<ValidationIssue> listIssue;
        MapCheckPanelInternal::collectActiveIssues( pLog, listIssue );
        MapCheckFilter filter{};
        filter._search        = _searchBuffer.c_str();
        filter._bShowErrors   = _bShowErrors == SW_TRUE;
        filter._bShowWarnings = _bShowWarnings == SW_TRUE;
        MapCheckCounts::populate( listIssue, filter, _listRow, _counts );
        _builtRevisionKey = revisionKey;
        _builtSearch      = _searchBuffer.c_str();
        _bRowsDirty       = SW_FALSE;
    }

    void MapCheckPanel::drawContent()
    {
        syncRows();

        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Refresh" ), SW_LOCKEY( "EdButton", "CheckMap" ), "Check Map" ) ) )
            (void)validateActiveScene(); // 검사한 수는 로그에 남는다
        EditorSelfTestMarks::record( "mapCheck.checkMap" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMapCheck", "CheckMap" ) );

        bool                                   bShowErrors = _bShowErrors == SW_TRUE;
        fixed_string<constant::buffer::kMax64> errorLabel;
        formatstring( errorLabel.data(), errorLabel.capacity(), "Errors (%#)", _counts._errorCount );
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( errorLabel.c_str() ) );
        if ( ImGui::Checkbox( errorLabel.c_str(), &bShowErrors ) )
        {
            _bShowErrors = bShowErrors ? SW_TRUE : SW_FALSE;
            _bRowsDirty  = SW_TRUE;
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMapCheck", "ShowErrors" ) );
        bool                                   bShowWarnings = _bShowWarnings == SW_TRUE;
        fixed_string<constant::buffer::kMax64> warningLabel;
        formatstring( warningLabel.data(), warningLabel.capacity(), "Warnings (%#)", _counts._warningCount );
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( warningLabel.c_str() ) );
        if ( ImGui::Checkbox( warningLabel.c_str(), &bShowWarnings ) )
        {
            _bShowWarnings = bShowWarnings ? SW_TRUE : SW_FALSE;
            _bRowsDirty    = SW_TRUE;
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMapCheck", "ShowWarnings" ) );
        // 보이는 줄 가운데 안전한 고침이 있는 것을 한 번에 고친다(되돌리기 한 단계). 고칠 것이 없으면 꺼 둔다
        uint32 fixableCount = 0;
        for ( const ValidationIssue& issue : _listRow )
            fixableCount += MapCheckFixCommands::canFix( issue ) ? 1u : 0u;
        _fixableRowCount = fixableCount;
        fixed_string<constant::buffer::kMax64> fixAllLabel;
        formatstring( fixAllLabel.data(), fixAllLabel.capacity(), "%# (%#)###mapCheckFixAll", SW_LOCKEY( "EdButton", "FixAll" ), fixableCount );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( fixAllLabel.c_str() ) );
        ImGui::BeginDisabled( fixableCount == 0 );
        const bool bFixAll = ImGui::Button( fixAllLabel.c_str() );
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "mapCheck.fixAll" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMapCheck", "FixAllTooltip" ) );
        if ( bFixAll )
            (void)MapCheckFixCommands::applyFixes( editor::getActiveObjectManager(), _listRow, "Map Check: Fix All" ); // 고친 결과는 표에서 빠지고 다음 그리기가 줄을 다시 만든다
        ImGui::SameLine();
        EditorWidgets::drawSearchField( "##mapcheck_search", _searchBuffer, SW_LOCKEY( "EditorMapCheck", "SearchHint" ), 0.0f, false );

        if ( _listRow.empty() )
        {
            const EditorListFilter filter{ _searchBuffer.c_str() };
            if ( filter.isActive() )
                EditorWidgets::drawNoSearchResultHint( filter.getText() );
            else
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorMapCheck", "NoIssues" ) );
            return;
        }

        constexpr ImGuiTableFlags kTableFlags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##mapcheck_rows", 6, kTableFlags, ImGui::GetContentRegionAvail() ) == false )
            return;
        const float32 dpiScale = EditorThemeUtil::getDPIScale();
        ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, 24.0f * dpiScale );
        ImGui::TableSetupColumn( "Object", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableSetupColumn( "Property", ImGuiTableColumnFlags_WidthStretch, 0.8f );
        ImGui::TableSetupColumn( "Message", ImGuiTableColumnFlags_WidthStretch, 2.4f );
        ImGui::TableSetupColumn( "Fix", ImGuiTableColumnFlags_WidthFixed, 48.0f * dpiScale );
        ImGui::TableHeadersRow();
        uint32 fixRowIndex = MathUtil::kMaxUInt32; // 이번 그리기에 누른 고침 줄 — 그린 뒤에 적용한다(적용이 줄 목록을 바꾼다)

        const GameObject* pPrimary = editor::getMainSceneSelection().getPrimaryObject();
        for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( _listRow.size() ); ++rowIndex )
        {
            const ValidationIssue& issue   = _listRow[rowIndex];
            GameObject*            pObject = MapCheckPanelInternal::findIssueObject( issue );
            ImGui::PushID( static_cast<int32>( rowIndex ) );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            const bool  bError = issue._severity == ValidationSeverity::Error;
            const Color color  = bError ? SW_THEME_COLOR( "Signal", "Error" ) : SW_THEME_COLOR( "Signal", "Warning" );
            ImGui::TextColored( EditorColorUtil::toImVec4( color ), "%s", bError ? SW_EDITOR_ICON( "Error" ) : SW_EDITOR_ICON( "Warning" ) );

            ImGui::TableSetColumnIndex( 1 );
            const bool bSelected = pObject != nullptr && pObject == pPrimary;
            if ( ImGui::Selectable( issue._sourceLabel.empty() ? "(unnamed)" : issue._sourceLabel.c_str(), bSelected,
                                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap ) ) // 줄 끝 Fix 단추가 클릭을 받는다
            {
                (void)selectRowObject( rowIndex ); // 활성 씬에 없는 오브젝트(지운 것)면 고를 것이 없다
                SceneComponent* pSceneComponent = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
                if ( EditorInputGUI::wasItemDoubleClicked() && pSceneComponent != nullptr )
                    (void)EditorSceneViewUtil::focusOn( pSceneComponent->getWorldPosition(), MapCheckPanelInternal::kFocusRadius ); // 씬 뷰가 없으면 고르기만 한다
            }
            if ( EditorSelfTestMarks::isEnabled() )
            {
                fixed_string<constant::buffer::kMax64> mark;
                formatstring( mark.data(), mark.capacity(), "mapCheck.row.%#", rowIndex );
                EditorSelfTestMarks::record( mark.c_str() );
            }
            if ( pObject == nullptr )
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMapCheck", "NotInActiveScene" ) );

            ImGui::TableSetColumnIndex( 2 );
            ImGui::TextUnformatted( issue._typeName.c_str() );
            ImGui::TableSetColumnIndex( 3 );
            ImGui::TextUnformatted( issue._propertyName.c_str() );
            ImGui::TableSetColumnIndex( 4 );
            ImGui::TextUnformatted( issue._message.c_str() );
            ImGui::TableSetColumnIndex( 5 );
            if ( MapCheckFixCommands::canFix( issue ) )
            {
                // 고칠 수 없는 줄은 단추를 두지 않는다(빈 칸) — 손으로 고칠 줄이다
                if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "Fix" ) ) )
                    fixRowIndex = rowIndex;
                EditorWidgets::drawTooltip( issue._fix._description.c_str() );
                if ( EditorSelfTestMarks::isEnabled() )
                {
                    fixed_string<constant::buffer::kMax64> mark;
                    formatstring( mark.data(), mark.capacity(), "mapCheck.fix.%#", rowIndex );
                    EditorSelfTestMarks::record( mark.c_str() );
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if ( fixRowIndex < _listRow.size() )
        {
            const ValidationIssue issue = _listRow[fixRowIndex];
            (void)MapCheckFixCommands::applyFixes( editor::getActiveObjectManager(), { issue }, issue._fix._description.empty() ? "Map Check: Fix" : issue._fix._description.c_str() );
        }
    }
} // namespace sw::editor
