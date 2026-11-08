#include "pch.h"

#include "Editor/Panels/VFX/VFXPreviewPanel.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

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

            [[nodiscard]] static bool readSoloEmitter( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const VFXPreviewPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getSoloEmitter() );
                return true;
            }

            [[nodiscard]] static bool readEnabledEmitterCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const VFXPreviewPanel* pPanel = findOpenPanel();
                uint32                 count{ 0 };
                if ( pPanel == nullptr || pPanel->findEnabledEmitterCount( count ) == false )
                    return false;
                outValue = static_cast<float64>( count );
                return true;
            }

            [[nodiscard]] static bool readHasTarget( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const VFXPreviewPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = pPanel->hasPreviewTarget() ? 1.0 : 0.0;
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
    SW_AUTOMATION_WATCH( editorVFXPreviewSoloEmitter, "Editor.VFXPreviewSoloEmitter", "Emitter the VFX Preview panel isolates (-1: none)", &VFXPreviewPanelInternal::readSoloEmitter );
    SW_AUTOMATION_WATCH( editorVFXPreviewEnabledEmitterCount, "Editor.VFXPreviewEnabledEmitterCount", "Emitters of the VFX Preview effect that are enabled (isolation turns the others off)",
                         &VFXPreviewPanelInternal::readEnabledEmitterCount );
    SW_AUTOMATION_WATCH( editorVFXPreviewHasTarget, "Editor.VFXPreviewHasTarget", "1 when the VFX Preview scene has a character target the effect rides on", &VFXPreviewPanelInternal::readHasTarget );

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

        // 방출기 줄마다 격리(Solo) 토글 — 켜면 그 방출기만 돈다(언리얼 Niagara 의 Isolate). 저장되는 효과는 바뀌지 않는다.
        const vector<VFXPreviewEmitterRow>& listRow = _model.getEmitterRows();
        for ( size_t rowIndex = 0; rowIndex < listRow.size(); ++rowIndex )
        {
            const VFXPreviewEmitterRow& row = listRow[rowIndex];
            ImGui::PushID( static_cast<int32>( rowIndex ) );
            const bool bSolo = _model.getSoloEmitter() == static_cast<int32>( rowIndex );
            if ( EditorWidgets::drawToggleButton( SW_LOCKEY( "EdVFX", "Solo" ), bSolo ) )
                _model.toggleSolo( static_cast<int32>( rowIndex ) ); // 보기 상태 — 되돌리기에 남기지 않는다
            fixed_string<constant::buffer::kMax64> mark;
            formatstring( mark.data(), mark.capacity(), "vfx.solo.%#", rowIndex );
            EditorSelfTestMarks::record( mark.c_str() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdVFX", "SoloTip" ) );
            ImGui::SameLine();
            const string text = SW_LOCKEYFORMAT( "EdFormat", "Max", TextArgumentList().addText( "name", row._name.c_str() ).addText( "blend", row._bAdditive ? "additive" : "alpha" ).addText( "looping", row._bLooping ? "yes" : "no" ).addInteger( "maxParticles", static_cast<int64>( row._maxParticles ) ).addText( "texturePath", row._texturePath.c_str() ) );
            if ( _model.getSoloEmitter() != VFXPreviewModel::kNoSolo && bSolo == false )
                ImGui::TextDisabled( "%s", text.c_str() ); // 격리로 꺼진 방출기는 흐리게
            else
                ImGui::TextUnformatted( text.c_str() );
            ImGui::PopID();
        }

        // 캐릭터 타깃: 애니메이션이 도는 몸에 효과를 붙여 원을 돌린다(트레일 · 잔상 · 손 효과 — 언리얼 Niagara 미리보기의 Preview Mesh). 멈춤 · 한 프레임은 뷰포트 위 재생 막대.
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdVFX", "TargetLabel" ), SW_LOCKEY( "EdVFX", "TargetTip" ) );
        const utf8* const pSpawn = SW_LOCKEY( "EdText", "Spawn" );
        (void)EditorWidgets::drawTextField( "##vfxPreview.target", _model.editTargetPrefabPath(), ImGui::GetContentRegionAvail().x - EditorChrome::measureSmallButton( pSpawn ) - ImGui::GetStyle().ItemSpacing.x );
        string droppedTarget;
        if ( EditorWidgets::acceptAssetDrop( droppedTarget, ".prefab.xml" ) )
            _model.editTargetPrefabPath() = droppedTarget;
        EditorSelfTestMarks::record( "vfx.targetField" );
        ImGui::SameLine();
        if ( ImGui::Button( pSpawn ) )
            _model.spawnTargetFromField(); // 빈 칸이면 타깃을 치운다
        EditorSelfTestMarks::record( "vfx.targetSpawn" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdVFX", "TargetSpawnTip" ) );

        if ( isDocumentLoadFailed() == false ) // 읽지 못한 효과는 걸지 않는다
        {
            _model.syncEffect( getPreviewScene(), getLoadedAssetPath() );
            _model.syncPlayback( getPreviewScene(), ImGui::GetIO().DeltaTime );
        }
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

    bool VFXPreviewPanel::findEnabledEmitterCount( uint32& outCount ) const
    {
        const VFXComponent* pSubject = VFXPreviewModel::findSubject( getPreviewScene() );
        if ( pSubject == nullptr )
            return false;
        outCount = 0;
        for ( uint32 emitterIndex = 0; emitterIndex < pSubject->getEmitterCount(); ++emitterIndex )
        {
            if ( pSubject->isEmitterEnabled( emitterIndex ) )
                ++outCount;
        }
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
