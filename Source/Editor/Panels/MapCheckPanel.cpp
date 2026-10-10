#include "pch.h"

#include "Editor/Panels/MapCheckPanel.h"

#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorSceneViewUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectValidation.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "MapCheckPanel" );

    namespace
    {
        struct MapCheckPanelInternal
        {
            /** @brief 오브젝트를 화면에 담는 기본 반지름(m)입니다 — 더블클릭 초점. */
            static constexpr float32 kFocusRadius = 2.0f;

            /** @brief 줄 @p issue 의 오브젝트를 활성 씬에서 찾습니다. 없으면 nullptr 입니다(지운 오브젝트 · 다른 씬의 결과). */
            static GameObject* findIssueObject( const ValidationIssue& issue )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                return pManager != nullptr && issue._sourceID != 0 ? pManager->findGameObjectByID( issue._sourceID ) : nullptr;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( MapCheckPanel, MapCheckPanel::kPanelID, EditorPanelCategory::Tool, 2040 );

    MapCheckPanel::MapCheckPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _listRow{}
        , _searchBuffer{}
        , _builtSearch{}
        , _counts{}
        , _builtRevision{ 0 }
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
            (void)ObjectValidation::reportGameObject( *pObject, false ); // 결과는 ValidationIssueLog 에 남고 표가 읽는다
        }
        SW_LOG_INFO( "Map Check: %# object(s) checked in %# ms", listObject.size(), stopwatch.getElapsedMilliseconds() );
        return static_cast<uint32>( listObject.size() );
    }

    bool MapCheckPanel::selectRowObject( uint32 rowIndex ) const
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr || rowIndex >= _listRow.size() )
            return false;
        GameObject* pObject = MapCheckPanelInternal::findIssueObject( _listRow[rowIndex] );
        if ( pObject == nullptr )
            return false;
        pContext->getEditorSelection().selectObject( pObject );
        return true;
    }

    void MapCheckPanel::syncRows()
    {
        const uint32 revision = ValidationIssueLog::get().getRevision();
        const bool   bChanged = revision != _builtRevision || _builtSearch != _searchBuffer.c_str();
        if ( bChanged == false && _bRowsDirty == SW_FALSE )
            return;
        vector<ValidationIssue> listIssue;
        ValidationIssueLog::get().collectIssues( listIssue );
        MapCheckFilter filter{};
        filter._search        = _searchBuffer.c_str();
        filter._bShowErrors   = _bShowErrors == SW_TRUE;
        filter._bShowWarnings = _bShowWarnings == SW_TRUE;
        MapCheckRows::populate( listIssue, filter, _listRow, _counts );
        _builtRevision = revision;
        _builtSearch   = _searchBuffer.c_str();
        _bRowsDirty    = SW_FALSE;
    }

    void MapCheckPanel::drawContent()
    {
        syncRows();

        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kRefresh, "Check Map" ) ) )
            (void)validateActiveScene(); // 검사한 수는 로그에 남는다
        EditorSelfTestMarks::note( "mapCheck.checkMap" );
        EditorWidgets::drawTooltip( "활성 씬의 모든 오브젝트를 다시 검증합니다" );

        ImGui::SameLine();
        bool                                 bShowErrors = _bShowErrors == SW_TRUE;
        fixed_string<constant::kMaxBuffer64> errorLabel;
        formatstring( errorLabel.data(), errorLabel.capacity(), "Errors (%#)", _counts._errorCount );
        if ( ImGui::Checkbox( errorLabel.c_str(), &bShowErrors ) )
        {
            _bShowErrors = bShowErrors ? SW_TRUE : SW_FALSE;
            _bRowsDirty  = SW_TRUE;
        }
        ImGui::SameLine();
        bool                                 bShowWarnings = _bShowWarnings == SW_TRUE;
        fixed_string<constant::kMaxBuffer64> warningLabel;
        formatstring( warningLabel.data(), warningLabel.capacity(), "Warnings (%#)", _counts._warningCount );
        if ( ImGui::Checkbox( warningLabel.c_str(), &bShowWarnings ) )
        {
            _bShowWarnings = bShowWarnings ? SW_TRUE : SW_FALSE;
            _bRowsDirty    = SW_TRUE;
        }
        ImGui::SameLine();
        EditorWidgets::drawSearchField( "##mapcheck_search", _searchBuffer, "Search", 0.0f, false );

        if ( _listRow.empty() )
        {
            const EditorListFilter filter{ _searchBuffer.c_str() };
            if ( filter.isActive() )
                EditorWidgets::drawNoSearchResultHint( filter.getText() );
            else
                EditorWidgets::drawEmptyHint( "No validation issues. Check Map validates every object of the active scene." );
            return;
        }

        constexpr ImGuiTableFlags kTableFlags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##mapcheck_rows", 5, kTableFlags, ImGui::GetContentRegionAvail() ) == false )
            return;
        const float32 dpiScale = EditorThemeUtil::getDpiScale();
        ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, 24.0f * dpiScale );
        ImGui::TableSetupColumn( "Object", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableSetupColumn( "Property", ImGuiTableColumnFlags_WidthStretch, 0.8f );
        ImGui::TableSetupColumn( "Message", ImGuiTableColumnFlags_WidthStretch, 2.4f );
        ImGui::TableHeadersRow();

        EditorContext*    pContext = EditorContext::get();
        const GameObject* pPrimary = pContext != nullptr ? pContext->getEditorSelection().getPrimaryObject() : nullptr;
        for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( _listRow.size() ); ++rowIndex )
        {
            const ValidationIssue& issue   = _listRow[rowIndex];
            GameObject*            pObject = MapCheckPanelInternal::findIssueObject( issue );
            ImGui::PushID( static_cast<int32>( rowIndex ) );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            const bool   bError = issue._severity == ValidationSeverity::Error;
            const Color4 color  = bError ? EditorThemeUtil::getErrorColor() : EditorThemeUtil::getWarningColor();
            ImGui::TextColored( ImVec4( color._r, color._g, color._b, color._a ), "%s", bError ? editoricon::kError : editoricon::kWarning );

            ImGui::TableSetColumnIndex( 1 );
            const bool bSelected = pObject != nullptr && pObject == pPrimary;
            if ( ImGui::Selectable( issue._sourceLabel.empty() ? "(unnamed)" : issue._sourceLabel.c_str(), bSelected,
                                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick ) )
            {
                (void)selectRowObject( rowIndex ); // 활성 씬에 없는 오브젝트(지운 것)면 고를 것이 없다
                SceneComponent* pSceneComponent = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
                if ( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) && pSceneComponent != nullptr )
                    (void)EditorSceneViewUtil::focusOn( pSceneComponent->getWorldPosition(), MapCheckPanelInternal::kFocusRadius ); // 씬 뷰가 없으면 고르기만 한다
            }
            if ( EditorSelfTestMarks::isEnabled() )
            {
                fixed_string<constant::kMaxBuffer64> mark;
                formatstring( mark.data(), mark.capacity(), "mapCheck.row.%#", rowIndex );
                EditorSelfTestMarks::note( mark.c_str() );
            }
            if ( pObject == nullptr )
                EditorWidgets::drawTooltip( "This object is not in the active scene" );

            ImGui::TableSetColumnIndex( 2 );
            ImGui::TextUnformatted( issue._typeName.c_str() );
            ImGui::TableSetColumnIndex( 3 );
            ImGui::TextUnformatted( issue._propertyName.c_str() );
            ImGui::TableSetColumnIndex( 4 );
            ImGui::TextUnformatted( issue._message.c_str() );
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
} // namespace sw::editor
