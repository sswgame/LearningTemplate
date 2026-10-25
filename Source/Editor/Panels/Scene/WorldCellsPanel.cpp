#include "pch.h"

#include "Editor/Panels/Scene/WorldCellsPanel.h"

#include "Core/Log/LogOnce.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/WorldCellGrid.h"
#include "Engine/Scene/WorldStreamingSubsystem.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 게임 카메라 자리에서 본 셀의 상태입니다(`WorldStreamingSubsystem::update` 의 규칙). */
        enum class WorldCellPreviewState : uint8
        {
            Load,   ///< 로딩 반경 안 — 올린다
            Keep,   ///< 반경 밖, 반경 + 여유 안 — 올라 있으면 둔다
            Unload, ///< 반경 + 여유 밖 — 내린다
            NoCamera
        };

        struct WorldCellsPanelInternal
        {
            /** @brief @p ids 의 오브젝트를 활성 씬에서 찾아 고릅니다. 없는 id(0 · 지운 것)는 건너뜁니다. */
            static void selectObjects( const vector<uint64>& listObjectID )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( pManager == nullptr )
                    return;
                vector<GameObject*> listObject;
                listObject.reserve( listObjectID.size() );
                for ( const uint64 objectID : listObjectID )
                {
                    GameObject* pObject = objectID != 0 ? pManager->findGameObjectByID( objectID ) : nullptr;
                    if ( pObject != nullptr )
                        listObject.push_back( pObject );
                }
                editor::getMainSceneSelection().selectObjects( listObject );
            }

            /** @brief 오브젝트 이름입니다. 활성 씬에 없으면 "?" 입니다. */
            static string findObjectName( uint64 objectID )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                GameObject*        pObject  = pManager != nullptr && objectID != 0 ? pManager->findGameObjectByID( objectID ) : nullptr;
                return pObject != nullptr ? string( pObject->getName().c_str() ) : string( "?" );
            }

            static WorldCellPreviewState computeState( const WorldCellGrid& grid, const WorldSettings& settings, const WorldCellCoord& cell, const CameraComponent* pCamera )
            {
                if ( pCamera == nullptr )
                    return WorldCellPreviewState::NoCamera;
                const float32 distanceSquared = grid.computeCellDistanceSquared( cell, pCamera->getWorldPosition() );
                const float32 loadRadius      = grid.getLoadingRadius();
                const float32 keepRadius      = loadRadius + settings._unloadMargin;
                if ( distanceSquared <= loadRadius * loadRadius )
                    return WorldCellPreviewState::Load;
                return distanceSquared <= keepRadius * keepRadius ? WorldCellPreviewState::Keep : WorldCellPreviewState::Unload;
            }

            static const utf8* getStateLabel( WorldCellPreviewState state )
            {
                switch ( state )
                {
                    case WorldCellPreviewState::Load:
                        return "Load";
                    case WorldCellPreviewState::Keep:
                        return "Keep";
                    case WorldCellPreviewState::Unload:
                        return "Unload";
                    case WorldCellPreviewState::NoCamera:
                        return "-";
                }
                return "-";
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( WorldCellsPanel, WorldCellsPanel::kPanelID, EditorPanelCategory::Tool, 2042 );

    WorldCellsPanel::WorldCellsPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _preview{}
        , _previewSceneGeneration{ 0 }
        , _bHasPreview{ SW_FALSE }
        , _bLastFailed{ SW_FALSE }
        , _reservedFlag{ 0 }
    {
    }

    bool WorldCellsPanel::refreshPreview()
    {
        _preview                    = StreamingCellPreview{};
        _bHasPreview                = SW_FALSE;
        Scene*        pScene        = editor::getActiveScene();
        SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr ) // 필수 서비스: 없으면 버그. 활성 씬이 없는 것(아래)은 실제로 있을 수 있다.
        {
            SW_LOG_ERROR_ONCE( "[WorldCells] SceneManager is missing - the streaming preview cannot be built" );
            return false;
        }
        if ( pScene == nullptr )
            return false; // 활성 씬이 없을 수 있다 — 패널이 빈 안내를 그린다
        const bool bSplit = SceneCooker::previewStreamingCells( *pScene, _preview );
        _bLastFailed      = bSplit ? SW_FALSE : SW_TRUE;
        if ( bSplit == false )
            return false;
        _previewSceneGeneration = pSceneManager->getSceneGeneration();
        _bHasPreview            = SW_TRUE;
        return true;
    }

    void WorldCellsPanel::drawContent()
    {
        Scene*        pScene        = editor::getActiveScene();
        SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
        const bool    bNoScene      = pScene == nullptr || pSceneManager == nullptr || pScene->getObjectManager() == nullptr;
        if ( bNoScene )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorWorldCells", "NoActiveScene" ) );
            return;
        }
        const WorldSettings& settings = pScene->getWorldSettings();
        if ( settings._bStreaming == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorWorldCells", "NotStreaming" ) );
            return;
        }
        // 쿠킹본으로 지은 씬은 이미 셀로 나뉘어 있고 문서로 쓰지 않는다 — 나눠 보지 않고 올라 있는 셀 수만 보인다.
        const WorldStreamingSubsystem& streaming = pScene->getObjectManager()->getSubsystem<WorldStreamingSubsystem>();
        if ( streaming.isStreaming() )
        {
            ImGui::Text( "streaming: %u of %u cells loaded", streaming.getLoadedCellCount(), streaming.getCellCount() );
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorWorldCells", "CookedScene" ) );
            return;
        }
        // 다른 씬의 결과는 그 씬의 오브젝트 id 라 버린다.
        if ( _bHasPreview == SW_TRUE && _previewSceneGeneration != pSceneManager->getSceneGeneration() )
        {
            _preview     = StreamingCellPreview{};
            _bHasPreview = SW_FALSE;
        }

        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kRefresh, "Preview Split" ) ) )
            (void)refreshPreview(); // 나누지 못한 이유는 쿠커가 로그에 남긴다
        EditorSelfTestMarks::note( "worldCells.preview" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorWorldCells", "PreviewTip" ) );
        ImGui::SameLine();
        ImGui::TextDisabled( "cell %.1f m, radius %.1f m, margin %.1f m, %u per frame", static_cast<float64>( settings._cellGrid._cellSize ), static_cast<float64>( settings._cellGrid._loadingRadius ), static_cast<float64>( settings._unloadMargin ),
                             settings._maxCellLoadsPerFrame );

        if ( _bLastFailed == SW_TRUE )
            ImGui::TextColored( EditorColorUtil::toImVec4( EditorThemeUtil::getErrorColor() ), "%s", SW_LOCKEY( "EditorWorldCells", "SplitFailed" ) );
        if ( _bHasPreview == SW_FALSE )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorWorldCells", "NoPreview" ) );
            return;
        }
        if ( _preview._listViolation.empty() == false )
        {
            drawViolations();
            return;
        }
        drawCells( *pScene );
    }

    void WorldCellsPanel::drawViolations()
    {
        ImGui::TextColored( EditorColorUtil::toImVec4( EditorThemeUtil::getErrorColor() ), "%s %u", SW_LOCKEY( "EditorWorldCells", "ViolationHeader" ),
                            static_cast<uint32>( _preview._listViolation.size() ) );
        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
        if ( ImGui::BeginTable( "##world_cell_violations", 2, kTableFlags, ImGui::GetContentRegionAvail() ) == false )
            return;
        ImGui::TableSetupColumn( "Object", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableSetupColumn( "Refers to (another cell)", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableHeadersRow();
        for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( _preview._listViolation.size() ); ++rowIndex )
        {
            const WorldCellReference& violation = _preview._listViolation[rowIndex];
            ImGui::PushID( static_cast<int32>( rowIndex ) );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            if ( ImGui::Selectable( WorldCellsPanelInternal::findObjectName( violation._fromObjectID ).c_str(), false, ImGuiSelectableFlags_SpanAllColumns ) )
                WorldCellsPanelInternal::selectObjects( vector<uint64>{ violation._fromObjectID, violation._toObjectID } );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorWorldCells", "ViolationTip" ) );
            ImGui::TableSetColumnIndex( 1 );
            ImGui::TextUnformatted( WorldCellsPanelInternal::findObjectName( violation._toObjectID ).c_str() );
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void WorldCellsPanel::drawCells( Scene& scene )
    {
        const WorldSettings& settings = scene.getWorldSettings();
        WorldCellGrid        grid;
        if ( WorldCellGrid::tryCreate( settings._cellGrid, grid ) == false )
            return; // 미리보기가 섰다면 격자는 쓸 수 있다 — 그 사이 설정을 고쳤다(오류는 로그에)
        const CameraComponent* pCamera = scene.getActiveGameCamera();

        if ( ImGui::SmallButton( "Always loaded" ) )
            WorldCellsPanelInternal::selectObjects( _preview._listAlwaysLoadedID );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorWorldCells", "AlwaysLoadedTip" ) );
        ImGui::SameLine();
        ImGui::Text( "%u entities, %u cells", static_cast<uint32>( _preview._listAlwaysLoadedID.size() ), static_cast<uint32>( _preview._listCell.size() ) );
        if ( pCamera == nullptr )
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorWorldCells", "NoGameCamera" ) );

        constexpr ImGuiTableFlags kTableFlags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##world_cells", 3, kTableFlags, ImGui::GetContentRegionAvail() ) == false )
            return;
        ImGui::TableSetupColumn( "Cell (x, z)", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableSetupColumn( "Entities", ImGuiTableColumnFlags_WidthStretch, 0.6f );
        ImGui::TableSetupColumn( "From game camera", ImGuiTableColumnFlags_WidthStretch, 0.8f );
        ImGui::TableHeadersRow();
        for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( _preview._listCell.size() ); ++rowIndex )
        {
            const StreamingCellPreviewCell& cell  = _preview._listCell[rowIndex];
            const WorldCellPreviewState     state = WorldCellsPanelInternal::computeState( grid, settings, cell._cell, pCamera );
            ImGui::PushID( static_cast<int32>( rowIndex ) );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            fixed_string<constant::kMaxBuffer64> label;
            formatstring( label.data(), label.capacity(), "%#, %#", cell._cell._x, cell._cell._z );
            if ( ImGui::Selectable( label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns ) )
                WorldCellsPanelInternal::selectObjects( cell._listObjectID );
            if ( EditorSelfTestMarks::isEnabled() )
            {
                fixed_string<constant::kMaxBuffer64> mark;
                formatstring( mark.data(), mark.capacity(), "worldCells.cell.%#", rowIndex );
                EditorSelfTestMarks::note( mark.c_str() );
            }
            ImGui::TableSetColumnIndex( 1 );
            ImGui::Text( "%u", static_cast<uint32>( cell._listObjectID.size() ) );
            ImGui::TableSetColumnIndex( 2 );
            const Color stateColor = state == WorldCellPreviewState::Load ? EditorThemeUtil::getSuccessColor()
                                   : state == WorldCellPreviewState::Keep ? EditorThemeUtil::getWarningColor()
                                                                          : EditorThemeUtil::getTextMutedColor();
            ImGui::TextColored( EditorColorUtil::toImVec4( stateColor ), "%s", WorldCellsPanelInternal::getStateLabel( state ) );
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
} // namespace sw::editor
