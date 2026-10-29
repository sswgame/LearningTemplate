#include "pch.h"

#include "Editor/Panels/Mesh/MeshPreviewPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"
#include "Core/Foundation/Defines.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/MeshLODSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct MeshPreviewPanelInternal
        {
            /// @brief 미리보기 씬에 스폰하는 메시 오브젝트 이름입니다.
            static constexpr const utf8* kPreviewSubjectName = "PreviewSubject";
            /// @brief 메시를 읽기 전 카메라가 담을 반지름(미터)입니다 — 읽힌 뒤 메시 경계로 다시 맞춘다.
            static constexpr float32 kDefaultFrameRadius = 1.0f;
            /// @brief 경계 구 반지름에 곱해 화면 가장자리에 붙지 않게 하는 여유 비율입니다.
            static constexpr float32 kFrameMargin = 1.15f;
            /// @brief 뷰포트의 최소 높이(96 DPI 기준 픽셀)입니다. 창이 낮아도 이만큼은 그린다.
            static constexpr float32 kMinViewportHeight = 200.0f;
            /// @brief "Auto"(화면 크기로 고른다)를 뜻하는 고른 단계 값입니다.
            static constexpr int32 kAutoLOD = -1;

            /** @brief 열린 메시 미리보기 창입니다. 닫혔으면 nullptr 입니다. */
            [[nodiscard]] static const MeshPreviewPanel* findOpenPanel()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return nullptr;
                const MeshPreviewPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<MeshPreviewPanel>();
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
    SW_LOG_CALLER( "MeshPreviewPanel" );
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
        , _previewMeshPath{}
        , _status{}
        , _lodChoice{ MeshPreviewPanelInternal::kAutoLOD }
        , _bFramed{ false }
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
            EditorWidgets::drawEmptyHint( "Open a .mesh asset to look at it and compare its LOD levels here." );
            return;
        }

        ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
        if ( ImGui::Button( "Reload" ) )
        {
            reloadDocument();
            _previewMeshPath.clear(); // 다시 임포트한 메시를 미리보기에 다시 건다
        }
        EditorSelfTestMarks::note( "mesh.reload" );
        EditorWidgets::drawTooltip( "Read the mesh file again (after importing the model source)" );

        syncPreviewSubject();
        const MeshComponent* pSubject = findPreviewSubject();
        if ( pSubject != nullptr )
            drawLODTable( *pSubject );
        applyLODChoice();

        const float2  available = float2{ ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y };
        const float32 minHeight = MeshPreviewPanelInternal::kMinViewportHeight * EditorThemeUtil::getDPIScale();
        const float2  viewSize{ available._x, available._y > minHeight ? available._y : minHeight };
        drawPreviewViewport( viewSize, false ); // 기즈모가 없다
        EditorSelfTestMarks::note( "mesh.previewScene" );
        EditorWidgets::drawPanelStatus( _status.c_str() );
    }

    void MeshPreviewPanel::drawLODTable( const MeshComponent& subject )
    {
        const Mesh* pMesh = subject.getRawMesh();
        if ( pMesh == nullptr )
        {
            ImGui::TextDisabled( subject.isMeshLoading() ? "Reading the mesh..." : "The mesh could not be read" );
            return;
        }

        ImGui::RadioButton( "Auto", &_lodChoice, MeshPreviewPanelInternal::kAutoLOD );
        EditorWidgets::drawTooltip( "Pick the level by the screen size of the preview camera (zoom out to see it drop)" );
        const uint32 lodCount = pMesh->getLODCount();
        for ( uint32 lod = 0; lod < lodCount; ++lod )
        {
            fixed_string<constant::kMaxBuffer32> label;
            formatstring( label.data(), label.capacity(), "LOD %u", lod );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( label.c_str() ) );
            ImGui::RadioButton( label.c_str(), &_lodChoice, static_cast<int32>( lod ) );
        }
        ImGui::Text( "Drawing LOD %u of %u", subject.getLODIndex(), lodCount );

        // 흐름은 LOD 0 뒤에 낮은 단계 정점이 이어진 것이다 — 인덱스 없는 삼각형 목록이라 정점 수 / 3 이 삼각형 수다.
        ImGui::BulletText( "LOD 0  %u vertices  %u triangles", pMesh->getVertexCount(), pMesh->getVertexCount() / 3u );
        uint32 lod = 1;
        for ( const MeshLOD& level : pMesh->getLODs() )
        {
            ImGui::BulletText( "LOD %u  %u vertices  %u triangles  from screen size %.3f", lod, level._vertexCount, level._vertexCount / 3u, level._screenSize );
            ++lod;
        }
        if ( lodCount == 1 )
            ImGui::TextDisabled( "This mesh has one level - set LOD ratios in the import rule and import again." );
    }

    void MeshPreviewPanel::applyLODChoice()
    {
        Scene*             pScene   = getPreviewScene();
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        if ( pManager == nullptr )
            return;
        pManager->getSubsystem<MeshLODSystem>().setForcedLOD( _lodChoice );
    }

    bool MeshPreviewPanel::findPreviewVertexCount( uint32& outCount ) const
    {
        const MeshComponent* pSubject = findPreviewSubject();
        const Mesh*          pMesh    = pSubject != nullptr ? pSubject->getRawMesh() : nullptr;
        if ( pMesh == nullptr )
            return false;
        outCount = pMesh->getVertexCount();
        return true;
    }

    bool MeshPreviewPanel::findPreviewDrawnLOD( uint32& outLOD ) const
    {
        const MeshComponent* pSubject = findPreviewSubject();
        if ( pSubject == nullptr || pSubject->getRawMesh() == nullptr )
            return false;
        outLOD = pSubject->getLODIndex();
        return true;
    }

    bool MeshPreviewPanel::acceptsDocumentPath( string_view assetPath ) const
    {
        return StringUtil::endsWith( assetPath, MeshAssetFormat::kExtension );
    }

    ToolAssetLoadResult MeshPreviewPanel::loadDocument()
    {
        string path = getLoadedAssetPath();
        if ( path.empty() )
            path = string{ getMatchingFocusedPath() };
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        if ( getLoadedAssetPath().empty() )
            acceptFocusedDocument();

        // 메시는 미리보기 메시 컴포넌트가 읽는다(비동기) — 읽기 실패는 컴포넌트가 로그로 남기고 표(drawLODTable)가 알린다.
        _previewMeshPath.clear();
        _bFramed = false;
        _status  = "Loaded";
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
        GameObjectManager* pManager = scene.getObjectManager();
        GameObject*        pSubject = pManager != nullptr ? pManager->createGameObject( hashed_string( MeshPreviewPanelInternal::kPreviewSubjectName ) ) : nullptr;
        if ( pSubject != nullptr )
            (void)pSubject->addComponent<MeshComponent>(); // 메시 경로는 문서를 읽은 뒤 건다(syncPreviewSubject)
        getPreviewViewport().frameSphere( float3{}, MeshPreviewPanelInternal::kDefaultFrameRadius );
        _previewMeshPath.clear(); // 새 씬의 메시에는 아직 경로가 없다
        _bFramed = false;
    }

    MeshComponent* MeshPreviewPanel::findPreviewSubject() const
    {
        Scene*             pScene   = getPreviewScene();
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        GameObject*        pSubject = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( MeshPreviewPanelInternal::kPreviewSubjectName ) ) : nullptr;
        return pSubject != nullptr ? pSubject->getComponent<MeshComponent>() : nullptr;
    }

    void MeshPreviewPanel::syncPreviewSubject()
    {
        const bool bDocumentReady = getLoadedAssetPath().empty() == false && isDocumentLoadFailed() == false;
        if ( bDocumentReady == false )
            return;
        MeshComponent* pMesh = findPreviewSubject();
        if ( pMesh == nullptr )
            return;
        if ( _previewMeshPath != getLoadedAssetPath() )
        {
            pMesh->setMeshID( getLoadedAssetPath() );
            _previewMeshPath = getLoadedAssetPath();
            _bFramed         = false;
            SW_LOG_INFO( "Mesh Preview showing '%#'", _previewMeshPath );
        }
        if ( _bFramed )
            return;
        const Mesh* pLoaded = pMesh->getRawMesh();
        if ( pLoaded == nullptr )
        {
            if ( pMesh->isMeshLoading() == false )
                _status = "The mesh could not be read - see the log";
            return; // 읽는 중 — 읽힌 뒤 한 번 카메라를 맞춘다
        }
        const float3& boundsMin = pLoaded->getLocalBoundsMin();
        const float3& boundsMax = pLoaded->getLocalBoundsMax();
        const float3  center{ ( boundsMin._x + boundsMax._x ) * 0.5f, ( boundsMin._y + boundsMax._y ) * 0.5f, ( boundsMin._z + boundsMax._z ) * 0.5f };
        getPreviewViewport().frameSphere( center, MathUtil::max( pLoaded->getBoundingRadius(), 0.01f ) * MeshPreviewPanelInternal::kFrameMargin );
        _bFramed = true;
        _status  = "Loaded";
    }
} // namespace sw::editor
