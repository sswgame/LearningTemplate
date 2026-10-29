#include "pch.h"

#include "Editor/Panels/VFX/VFXPreviewPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/VFX/VFXComponent.h"
#include "Engine/VFX/VFXEffectDef.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct VFXPreviewPanelInternal
        {
            /// @brief 화면에 담을 반지름(미터)입니다 — 효과는 원점에 서고, 표본 효과의 불꽃이 1 m 남짓 솟는다.
            static constexpr float32 kFrameRadius = 1.5f;
            /// @brief 뷰포트의 최소 높이(96 DPI 기준 픽셀)입니다. 창이 낮아도 이만큼은 그린다.
            static constexpr float32 kMinViewportHeight = 160.0f;

            /** @brief 열린 VFX 미리보기 창입니다. 닫혔으면 nullptr 입니다. */
            [[nodiscard]] static const VFXPreviewPanel* findOpenPanel()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return nullptr;
                const VFXPreviewPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<VFXPreviewPanel>();
                return pPanel != nullptr && pPanel->isOpen() ? pPanel : nullptr;
            }

            [[nodiscard]] static bool readParticleCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const VFXPreviewPanel* pPanel = findOpenPanel();
                uint64                 count{ 0 };
                if ( pPanel == nullptr || pPanel->findPreviewSpawnedParticleCount( count ) == false )
                    return false;
                outValue = static_cast<float64>( count );
                return true;
            }

            /** @brief 창이 지금 연 문서가 `gv_editorWatchAsset`(리소스 id)이면 1 입니다. */
            [[nodiscard]] static bool readDocumentIsProbeAsset( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const VFXPreviewPanel* pPanel = findOpenPanel();
                return pPanel != nullptr && pPanel->readDocumentIsProbeAsset( outValue );
            }

            [[nodiscard]] static bool readPreviewSceneFrames( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const VFXPreviewPanel* pPanel = findOpenPanel();
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
    SW_LOG_CALLER( "VFXPreviewPanel" );
    SW_EDITOR_PANEL( VFXPreviewPanel, VFXPreviewPanel::kPanelID, EditorPanelCategory::Tool, 1310 );
    SW_AUTOMATION_WATCH( editorVFXPreviewParticleCount, "Editor.VFXPreviewParticleCount",
                         "Particles the effect in the VFX Preview panel spawned this run, CPU and GPU emitters alike (fails while the panel is closed or has no preview scene)",
                         &VFXPreviewPanelInternal::readParticleCount );
    SW_AUTOMATION_WATCH( editorVFXPreviewDocumentIsProbeAsset, "Editor.VFXPreviewDocumentIsProbeAsset",
                         "1 when the effect open in the VFX Preview panel is gv_editorWatchAsset and it loaded", &VFXPreviewPanelInternal::readDocumentIsProbeAsset );
    SW_AUTOMATION_WATCH( editorVFXPreviewSceneFrames, "Editor.VFXPreviewSceneFrames", "Frames the VFX Preview panel asked the renderer to draw its preview scene",
                         &VFXPreviewPanelInternal::readPreviewSceneFrames );

    VFXPreviewPanel::VFXPreviewPanel()
        : EditorPreviewPanel{ EditorAssetType{ EditorAssetTypeIDs::kData }, false }
        , _model{}
    {
    }

    void VFXPreviewPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        beginPreviewFrame();
        drawDocumentOpenBar( "vfx" );

        if ( getLoadedAssetPath().empty() )
        {
            EditorWidgets::drawEmptyHint( "Open a .vfx.xml effect to play it here. Edit effects in the Catalog Editor." );
            return;
        }

        ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
        if ( ImGui::Button( "Reload" ) )
        {
            reloadDocument();
            _model.requestResync(); // 다시 읽은 효과를 미리보기에 다시 건다
        }
        EditorSelfTestMarks::note( "vfx.reload" );
        EditorWidgets::drawTooltip( "Read the effect file again (after saving it in the Catalog Editor)" );
        VFXComponent* pSubject = VFXPreviewModel::findSubject( getPreviewScene() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Restart" ) );
        if ( ImGui::Button( "Restart" ) && pSubject != nullptr )
            pSubject->play();
        EditorSelfTestMarks::note( "vfx.restart" );
        EditorWidgets::drawTooltip( "Play the effect again from the start" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Stop" ) );
        if ( ImGui::Button( "Stop" ) && pSubject != nullptr )
            pSubject->stop();
        EditorSelfTestMarks::note( "vfx.stop" );
        EditorWidgets::drawTooltip( "Stop and clear the particles" );
        ImGui::SameLine();
        // GPU 방출기의 살아 있는 입자는 GPU 버퍼에만 있다 — 이번 재생에서 만든 수(일정)와 CPU 방출기의 살아 있는 수를 따로 보인다.
        const uint32 spawnedCount = pSubject != nullptr ? static_cast<uint32>( pSubject->getSpawnedParticleCount() ) : 0u; // 표시용 — 한 재생에 40 억을 넘지 않는다
        ImGui::Text( "Spawned: %u  CPU alive: %u", spawnedCount, pSubject != nullptr ? pSubject->getCPUAliveParticleCount() : 0u );

        for ( const VFXPreviewEmitterRow& row : _model.getEmitterRows() )
        {
            ImGui::BulletText( "%s  %s%s  max %u  %s", row._name.c_str(), row._bAdditive ? "Additive" : "Alpha", row._bLooping ? "  looping" : "", row._maxParticles,
                               row._texturePath.c_str() );
        }

        if ( isDocumentLoadFailed() == false ) // 읽지 못한 효과는 걸지 않는다
            _model.syncEffect( getPreviewScene(), getLoadedAssetPath() );
        const float2  available = float2{ ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y };
        const float32 minHeight = VFXPreviewPanelInternal::kMinViewportHeight * EditorThemeUtil::getDPIScale();
        const float2  viewSize{ available._x, available._y > minHeight ? available._y : minHeight };
        drawPreviewViewport( viewSize, false ); // 기즈모가 없다
        EditorSelfTestMarks::note( "vfx.previewScene" );
        EditorWidgets::drawPanelStatus( _model.getStatus().c_str() );
    }

    bool VFXPreviewPanel::findPreviewSpawnedParticleCount( uint64& outCount ) const
    {
        const VFXComponent* pSubject = VFXPreviewModel::findSubject( getPreviewScene() );
        if ( pSubject == nullptr )
            return false;
        outCount = pSubject->getSpawnedParticleCount();
        return true;
    }

    bool VFXPreviewPanel::acceptsDocumentPath( string_view assetPath ) const
    {
        return StringUtil::endsWith( assetPath, VFXEffectDef::kExtension );
    }

    ToolAssetLoadResult VFXPreviewPanel::loadDocument()
    {
        string path = getLoadedAssetPath();
        if ( path.empty() )
            path = string{ getMatchingFocusedPath() };
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        if ( getLoadedAssetPath().empty() )
            acceptFocusedDocument();
        return _model.loadAsset( path );
    }

    void VFXPreviewPanel::populatePreviewScene( Scene& scene )
    {
        _model.populateScene( scene );
        getPreviewViewport().frameSphere( float3{ 0.0f, VFXPreviewPanelInternal::kFrameRadius * 0.5f, 0.0f }, VFXPreviewPanelInternal::kFrameRadius );
    }
} // namespace sw::editor
