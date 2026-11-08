#include "pch.h"

#include "Editor/Panels/VFX/VFXPreviewPanel.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
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
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                if ( pPanels == nullptr )
                    return nullptr;
                const VFXPreviewPanel* pPanel = pPanels->findPanel<VFXPreviewPanel>();
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
            [[nodiscard]] static bool readDocumentIsWatchAsset( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const VFXPreviewPanel* pPanel = findOpenPanel();
                return pPanel != nullptr && pPanel->readDocumentIsWatchAsset( outValue );
            }

            /** @brief 효과 파일이 바뀌어 창이 스스로 다시 읽은 횟수입니다(감시 값 `Editor.VFXPreviewAutoReloadCount`). */
            [[nodiscard]] static bool readAutoReloadCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const VFXPreviewPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getAutoReloadCount() );
                return true;
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
    SW_EDITOR_PANEL( VFXPreviewPanel, VFXPreviewPanel::kPanelID, EditorPanelCategory::Tool, 1310 );
    SW_AUTOMATION_WATCH( editorVFXPreviewParticleCount, "Editor.VFXPreviewParticleCount",
                         "Particles the effect in the VFX Preview panel spawned this run, CPU and GPU emitters alike (fails while the panel is closed or has no preview scene)",
                         &VFXPreviewPanelInternal::readParticleCount );
    SW_AUTOMATION_WATCH( editorVFXPreviewDocumentIsWatchAsset, "Editor.VFXPreviewDocumentIsWatchAsset",
                         "1 when the effect open in the VFX Preview panel is gv_editorWatchAsset and it loaded", &VFXPreviewPanelInternal::readDocumentIsWatchAsset );
    SW_AUTOMATION_WATCH( editorVFXPreviewAutoReloadCount, "Editor.VFXPreviewAutoReloadCount", "Times the VFX Preview panel re-read its effect because the file changed on disk",
                         &VFXPreviewPanelInternal::readAutoReloadCount );
    SW_AUTOMATION_WATCH( editorVFXPreviewSceneFrames, "Editor.VFXPreviewSceneFrames", "Frames the VFX Preview panel asked the renderer to draw its preview scene",
                         &VFXPreviewPanelInternal::readPreviewSceneFrames );

    VFXPreviewPanel::VFXPreviewPanel()
        : EditorPreviewPanel{ EditorAssetType{ EditorAssetTypeIDs::kData }, false }
        , _model{}
        , _autoReloadCount{ 0 }
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
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdLabel", "OpenAVfxXmlEffectToPlayIt" ) );
            return;
        }

        // 효과 파일이 바뀌었으면(카탈로그 편집기의 저장) 다시 읽고 다시 건다 — Reload 를 누르지 않아도 된다.
        if ( _model.pollFileChanged() )
        {
            VFXPreviewModel::applySavedAsset( getLoadedAssetPath() );
            reloadDocument();
            _model.requestResync();
            ++_autoReloadCount;
        }
        ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Reload" ) ) )
        {
            reloadDocument();
            _model.requestResync(); // 다시 읽은 효과를 미리보기에 다시 건다
        }
        EditorSelfTestMarks::record( "vfx.reload" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "ReadTheEffectFileAgainAfterSaving" ) );
        VFXComponent* pSubject = VFXPreviewModel::findSubject( getPreviewScene() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Restart" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Restart" ) ) && pSubject != nullptr )
            pSubject->play();
        EditorSelfTestMarks::record( "vfx.restart" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "PlayTheEffectAgainFromTheStart" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Stop" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Stop" ) ) && pSubject != nullptr )
            pSubject->stop();
        EditorSelfTestMarks::record( "vfx.stop" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "StopAndClearTheParticles" ) );
        ImGui::SameLine();
        // GPU 방출기의 살아 있는 입자는 GPU 버퍼에만 있다 — 이번 재생에서 만든 수(일정)와 CPU 방출기의 살아 있는 수를 따로 보인다.
        const uint32 spawnedCount = pSubject != nullptr ? static_cast<uint32>( pSubject->getSpawnedParticleCount() ) : 0u; // 표시용 — 한 재생에 40 억을 넘지 않는다
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "SpawnedCPUAlive", TextArgumentList().addInteger( "spawnedCount", static_cast<int64>( spawnedCount ) ).addInteger( "subject", static_cast<int64>( pSubject != nullptr ? pSubject->getCPUAliveParticleCount() : 0u ) ) ).c_str() );

        for ( const VFXPreviewEmitterRow& row : _model.getEmitterRows() )
        {
            ImGui::BulletText( "%s", SW_LOCKEYFORMAT( "EdFormat", "Max", TextArgumentList().addText( "name", row._name.c_str() ).addText( "blend", row._bAdditive ? "additive" : "alpha" ).addText( "looping", row._bLooping ? "yes" : "no" ).addInteger( "maxParticles", static_cast<int64>( row._maxParticles ) ).addText( "texturePath", row._texturePath.c_str() ) ).c_str() );
        }

        if ( isDocumentLoadFailed() == false ) // 읽지 못한 효과는 걸지 않는다
            _model.syncEffect( getPreviewScene(), getLoadedAssetPath() );
        const float2 viewSize = computeViewportFillSize( VFXPreviewPanelInternal::kMinViewportHeight * EditorThemeUtil::getDPIScale() );
        drawPreviewViewport( viewSize, false ); // 기즈모가 없다
        EditorSelfTestMarks::record( "vfx.previewScene" );
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
        const string path = acceptDocumentToLoad();
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        return _model.loadAsset( path );
    }

    void VFXPreviewPanel::populatePreviewScene( Scene& scene )
    {
        _model.populateScene( scene );
    }

    bool VFXPreviewPanel::findPreviewFrameSphere( float3& outCenter, float32& outRadius ) const
    {
        outCenter = float3{ 0.0f, VFXPreviewPanelInternal::kFrameRadius * 0.5f, 0.0f };
        outRadius = VFXPreviewPanelInternal::kFrameRadius;
        return true;
    }
} // namespace sw::editor
