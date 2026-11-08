#include "pch.h"

#include "Editor/Panels/Mesh/MeshPreviewPanel.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct MeshPreviewPanelInternal
        {
            /// @brief 메시를 읽기 전 카메라가 담을 반지름(미터)입니다 — 읽힌 뒤 메시 경계로 다시 맞춘다.
            static constexpr float32 kDefaultFrameRadius = 1.0f;
            /// @brief 뷰포트의 최소 높이(96 DPI 기준 픽셀)입니다. 창이 낮아도 이만큼은 그린다.
            static constexpr float32 kMinViewportHeight = 200.0f;

            /** @brief 열린 메시 미리보기 창입니다. 닫혔으면 nullptr 입니다. */
            [[nodiscard]] static const MeshPreviewPanel* findOpenPanel()
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                if ( pPanels == nullptr )
                    return nullptr;
                const MeshPreviewPanel* pPanel = pPanels->findPanel<MeshPreviewPanel>();
                return pPanel != nullptr && pPanel->isOpen() ? pPanel : nullptr;
            }

            [[nodiscard]] static bool readVertexCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MeshPreviewPanel* pPanel = findOpenPanel();
                uint32                  count{ 0 };
                if ( pPanel == nullptr || pPanel->findPreviewVertexCount( count ) == false )
                    return false;
                outValue = static_cast<float64>( count );
                return true;
            }

            [[nodiscard]] static bool readDrawnLOD( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MeshPreviewPanel* pPanel = findOpenPanel();
                uint32                  lod{ 0 };
                if ( pPanel == nullptr || pPanel->findPreviewDrawnLOD( lod ) == false )
                    return false;
                outValue = static_cast<float64>( lod );
                return true;
            }

            [[nodiscard]] static bool readPreviewSceneFrames( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MeshPreviewPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getPreviewViewport().getSubmittedFrameCount() );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( MeshPreviewPanel, MeshPreviewPanel::kPanelID, EditorPanelCategory::Tool, 1320 );
    SW_AUTOMATION_WATCH( editorMeshPreviewVertexCount, "Editor.MeshPreviewVertexCount",
                         "LOD 0 vertices of the mesh in the Mesh Preview panel (fails while the panel is closed, has no preview scene or the mesh is not read yet)",
                         &MeshPreviewPanelInternal::readVertexCount );
    SW_AUTOMATION_WATCH( editorMeshPreviewDrawnLOD, "Editor.MeshPreviewDrawnLOD", "LOD level the Mesh Preview panel mesh draws now (fails while the mesh is not read yet)",
                         &MeshPreviewPanelInternal::readDrawnLOD );
    SW_AUTOMATION_WATCH( editorMeshPreviewSceneFrames, "Editor.MeshPreviewSceneFrames", "Frames the Mesh Preview panel asked the renderer to draw its preview scene",
                         &MeshPreviewPanelInternal::readPreviewSceneFrames );

    MeshPreviewPanel::MeshPreviewPanel()
        : EditorPreviewPanel{ EditorAssetType{ MeshAssetFormat::kAssetType }, false }
        , _model{}
        , _frameCenter{}
        , _frameRadius{ MeshPreviewPanelInternal::kDefaultFrameRadius }
        , _lodState{}
    {
    }

    void MeshPreviewPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        beginPreviewFrame();
        drawDocumentOpenBar( "mesh" );

        if ( getLoadedAssetPath().empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdLabel", "OpenAMeshAssetToLookAtIt" ) );
            return;
        }

        ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Reload" ) ) )
        {
            reloadDocument();
            _model.requestResync(); // 다시 임포트한 메시를 미리보기에 다시 건다
        }
        EditorSelfTestMarks::record( "mesh.reload" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "ReadTheMeshFileAgainAfterImporting" ) );

        syncPreviewSubject();
        MeshPreviewModel::gatherLOD( getPreviewScene(), _lodState );
        if ( _lodState._bHasSubject != SW_FALSE )
            drawLODTable();
        _model.applyLODChoice( getPreviewScene() );

        const float2 viewSize = computeViewportFillSize( MeshPreviewPanelInternal::kMinViewportHeight * EditorThemeUtil::getDPIScale() );
        drawPreviewViewport( viewSize, false ); // 기즈모가 없다
        EditorSelfTestMarks::record( "mesh.previewScene" );
        EditorWidgets::drawPanelStatus( _model.getStatus().c_str() );
    }

    void MeshPreviewPanel::drawLODTable()
    {
        if ( _lodState._bMeshRead == SW_FALSE )
        {
            ImGui::TextDisabled( _lodState._bLoading != SW_FALSE ? "Reading the mesh..." : "The mesh could not be read" );
            return;
        }

        ImGui::RadioButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "Auto" ), "Auto" ), &_model.editLODChoice(), MeshPreviewModel::kAutoLOD );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "PickTheLevelByTheScreenSize" ) );
        const uint32 lodCount = _lodState._lodCount;
        for ( uint32 lod = 0; lod < lodCount; ++lod )
        {
            fixed_string<constant::buffer::kMax32> label;
            formatstring( label.data(), label.capacity(), "LOD %u", lod );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( label.c_str() ) );
            ImGui::RadioButton( label.c_str(), &_model.editLODChoice(), static_cast<int32>( lod ) );
        }
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "DrawingLOD", TextArgumentList().addInteger( "lod", static_cast<int64>( _lodState._drawnLOD ) ).addInteger( "count", lodCount ) ).c_str() );

        ImGui::BulletText( "%s", SW_LOCKEYFORMAT( "EdFormat", "LODVerticesTriangles", TextArgumentList().addInteger( "vertexCount", static_cast<int64>( _lodState._vertexCount ) ).addInteger( "indexCount", static_cast<int64>( _lodState._triangleCount ) ) ).c_str() );
        uint32 lod = 1;
        for ( const MeshPreviewLODRow& level : _lodState._listLOD )
        {
            ImGui::BulletText( "%s", SW_LOCKEYFORMAT( "EdFormat", "LODVerticesTrianglesFrom", TextArgumentList().addInteger( "lod", static_cast<int64>( lod ) ).addInteger( "vertexCount", static_cast<int64>( level._vertexCount ) ).addInteger( "indexCount", static_cast<int64>( level._triangleCount ) ).addDecimal( "screenSize", static_cast<float64>( level._screenSize ), 3 ) ).c_str() );
            ++lod;
        }
        if ( lodCount == 1 )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "ThisMeshHasOneLevelSetLODRatios" ) );
    }

    bool MeshPreviewPanel::findPreviewVertexCount( uint32& outCount ) const
    {
        MeshPreviewLODState state;
        MeshPreviewModel::gatherLOD( getPreviewScene(), state );
        if ( state._bMeshRead == SW_FALSE )
            return false;
        outCount = state._vertexCount;
        return true;
    }

    bool MeshPreviewPanel::findPreviewDrawnLOD( uint32& outLOD ) const
    {
        MeshPreviewLODState state;
        MeshPreviewModel::gatherLOD( getPreviewScene(), state );
        if ( state._bMeshRead == SW_FALSE )
            return false;
        outLOD = state._drawnLOD;
        return true;
    }

    bool MeshPreviewPanel::acceptsDocumentPath( string_view assetPath ) const
    {
        return StringUtil::endsWith( assetPath, MeshAssetFormat::kExtension );
    }

    ToolAssetLoadResult MeshPreviewPanel::loadDocument()
    {
        const string path = acceptDocumentToLoad();
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        _model.resetForDocument();
        return ToolAssetLoadResult::Loaded;
    }

    EditorPreviewEnvironment MeshPreviewPanel::getPreviewEnvironment() const
    {
        EditorPreviewEnvironment environment{};
        environment._bFloor = false;
        return environment;
    }

    void MeshPreviewPanel::populatePreviewScene( Scene& scene )
    {
        _model.populateScene( scene );
        _frameCenter = float3{};
        _frameRadius = MeshPreviewPanelInternal::kDefaultFrameRadius;
    }

    void MeshPreviewPanel::syncPreviewSubject()
    {
        const bool bDocumentReady = getLoadedAssetPath().empty() == false && isDocumentLoadFailed() == false;
        if ( bDocumentReady == false )
            return;
        float3  center{};
        float32 radius{ 0.0f };
        if ( _model.syncSubject( getPreviewScene(), getLoadedAssetPath(), center, radius ) == false )
            return;
        _frameCenter = center;
        _frameRadius = radius;
        getPreviewViewport().frameSphere( center, radius );
    }

    bool MeshPreviewPanel::findPreviewFrameSphere( float3& outCenter, float32& outRadius ) const
    {
        outCenter = _frameCenter;
        outRadius = _frameRadius;
        return _frameRadius > 0.0f;
    }
} // namespace sw::editor
