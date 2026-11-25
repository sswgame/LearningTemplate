/**
 * @file MeshPreviewPanel.cpp
 * @brief 메시 에셋(`*.mesh`)을 미리보기 씬에서 돌려 보고 LOD 단계를 골라 견주는 창입니다(유니티 모델 임포터 미리보기의 LOD 보기, 언리얼 Static Mesh 에디터의 LOD 선택에 해당합니다).
 * @details 메시 걸기 · 카메라 맞출 경계 · 고른 LOD 는 `MeshPreviewViewModel`(Model/, ImGui 없음)이 들고, 이 창은 그리기와 미리보기 씬 · 뷰포트만 쥔다.
 */
#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Mesh/Model/MeshPreviewViewModel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    /**
     * @class MeshPreviewPanel
     * @brief 연 메시를 미리보기 씬의 오브젝트 하나(`MeshComponent`)로 보이고, 단계별 정점 수와 임계값 표에서 LOD 를 골라 그 단계만 그리게 합니다.
     * @details 보기 전용이다. `.mesh` 는 모델 임포트의 산출물이라 이 창이 저장하지 않는다(원본을 다시 임포트하면 덮인다). Mesh 종류 중 `.mesh` 만 문서로 받는다
     *          (`canAcceptDocumentPath`: 같은 종류에 모델 원본 `.fbx` · `.gltf` 도 있다). LOD 는 씬 단위로 고정한다(`MeshLODSystem::setForcedLOD`) — 화면 크기로 고르는 자동 선택은
     *          창이 "Auto" 일 때만 돈다.
     */
    class MeshPreviewPanel : public EditorPreviewPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "mesh_preview";

        MeshPreviewPanel();

        const utf8*        getPanelTitle() const override { return "Mesh Preview"; }
        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override { return false; }
        bool               canSaveDocument() const override { return false; }

        /** @brief 미리보기 메시의 LOD 0 정점 수입니다. 아직 읽는 중이거나 읽지 못했거나 씬이 없으면 false 입니다(감시 값 `Editor.MeshPreviewVertexCount`). */
        [[nodiscard]] bool findPreviewVertexCount( uint32& outCount ) const;
        /** @brief 미리보기 메시가 지금 그리는 LOD 단계입니다. 없으면 false 입니다(감시 값 `Editor.MeshPreviewDrawnLOD`). */
        [[nodiscard]] bool findPreviewDrawnLOD( uint32& outLOD ) const;

    private:
        bool                canAcceptDocumentPath( string_view assetPath ) const override;
        ToolAssetLoadResult loadDocument() override;
        /** @brief 구경 바닥을 끕니다 — 메시 원점이 바닥에 겹쳐 가리지 않게 한다. */
        EditorPreviewEnvironment getPreviewEnvironment() const override;
        /** @brief 미리보기 씬에 메시 오브젝트 하나(`PreviewSubject`)를 스폰합니다. */
        void populatePreviewScene( Scene& scene ) override;
        /** @brief 맞춤(F)과 창을 처음 열 때의 카메라가 담을 구입니다. */
        bool findPreviewFrameSphere( float3& outCenter, float32& outRadius ) const override;
        /** @brief 미리보기 메시가 지금 문서를 그리게 하고 읽힌 뒤 한 번 카메라를 맞춥니다. */
        void syncPreviewSubject();
        void drawLODTable();

    private:
        MeshPreviewViewModel _model;
        float3               _frameCenter; ///< 맞춤(F)이 담을 구의 중심 — 대상을 바꿀 때 그 경계 구로 정한다
        float32              _frameRadius;
        MeshPreviewLODState  _lodState; ///< 프레임마다 다시 채운다(LOD 표가 그린다)
    };

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
                return EditorPanelManager::findOpenPanelInEditorContext<MeshPreviewPanel>();
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
        MeshPreviewViewModel::gatherLOD( getPreviewScene(), _lodState );
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

        ImGui::RadioButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "Auto" ), "Auto" ), &_model.editLODChoice(), MeshPreviewViewModel::kAutoLOD );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "PickTheLevelByTheScreenSize" ) );
        const uint32 lodCount = _lodState._lodCount;
        for ( uint32 lod = 0; lod < lodCount; ++lod )
        {
            fixed_string<constant::buffer::kMax32> label;
            formatstring( label.data(), label.capacity(), "LOD %#", lod );
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
        MeshPreviewViewModel::gatherLOD( getPreviewScene(), state );
        if ( state._bMeshRead == SW_FALSE )
            return false;
        outCount = state._vertexCount;
        return true;
    }

    bool MeshPreviewPanel::findPreviewDrawnLOD( uint32& outLOD ) const
    {
        MeshPreviewLODState state;
        MeshPreviewViewModel::gatherLOD( getPreviewScene(), state );
        if ( state._bMeshRead == SW_FALSE )
            return false;
        outLOD = state._drawnLOD;
        return true;
    }

    bool MeshPreviewPanel::canAcceptDocumentPath( string_view assetPath ) const
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
