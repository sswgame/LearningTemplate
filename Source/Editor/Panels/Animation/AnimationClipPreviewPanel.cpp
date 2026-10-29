#include "pch.h"

#include "Editor/Panels/Animation/AnimationClipPreviewPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct AnimationClipPreviewPanelInternal
        {
            /// @brief 몸을 중심으로 화면에 담을 높이(미터)입니다 — 사람 크기 몸 하나가 들어간다.
            static constexpr float32 kFrameCenterHeight = 1.0f;
            /// @brief 화면에 담을 반지름(미터)입니다.
            static constexpr float32 kFrameRadius = 2.0f;
            /// @brief 뷰포트의 최소 높이(96 DPI 기준 픽셀)입니다. 창이 낮아도 이만큼은 그린다.
            static constexpr float32 kMinViewportHeight = 200.0f;
            /// @brief 재생 속도 슬라이더의 위쪽 끝(배속)입니다 — 애니메이터의 한도(`Play Rate` 의 최대 10)보다 낮게 둔다(클립을 눈으로 따라가려는 창이다).
            static constexpr float32 kMaxPlayRate = 2.0f;

            /** @brief 열린 클립 미리보기 창입니다. 닫혔으면 nullptr 입니다. */
            [[nodiscard]] static const AnimationClipPreviewPanel* findOpenPanel()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return nullptr;
                const AnimationClipPreviewPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<AnimationClipPreviewPanel>();
                return pPanel != nullptr && pPanel->isOpen() ? pPanel : nullptr;
            }

            [[nodiscard]] static bool readClipDuration( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AnimationClipPreviewPanel* pPanel = findOpenPanel();
                float32                          seconds{ 0.0f };
                if ( pPanel == nullptr || pPanel->findPreviewClipDuration( seconds ) == false )
                    return false;
                outValue = static_cast<float64>( seconds );
                return true;
            }

            [[nodiscard]] static bool readPreviewSceneFrames( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AnimationClipPreviewPanel* pPanel = findOpenPanel();
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
    SW_LOG_CALLER( "AnimationClipPreviewPanel" );
    SW_EDITOR_PANEL( AnimationClipPreviewPanel, AnimationClipPreviewPanel::kPanelID, EditorPanelCategory::Tool, 1330 );
    SW_AUTOMATION_WATCH( editorClipPreviewDuration, "Editor.ClipPreviewDuration", "Seconds of the clip the Clip Preview panel body plays (fails while the panel is closed, has no body or the clip is not read)",
                         &AnimationClipPreviewPanelInternal::readClipDuration );
    SW_AUTOMATION_WATCH( editorClipPreviewSceneFrames, "Editor.ClipPreviewSceneFrames", "Frames the Clip Preview panel asked the renderer to draw its preview scene",
                         &AnimationClipPreviewPanelInternal::readPreviewSceneFrames );

    AnimationClipPreviewPanel::AnimationClipPreviewPanel()
        : EditorPreviewPanel{ EditorAssetType{ AnimationClip::kAssetType }, false }
        , _model{}
    {
    }

    void AnimationClipPreviewPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        beginPreviewFrame();
        drawDocumentOpenBar( "clip" );

        if ( getLoadedAssetPath().empty() )
        {
            EditorWidgets::drawEmptyHint( "Open a .animclip clip, pick a body prefab and play it here." );
            return;
        }

        ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
        ImGui::TextUnformatted( "Body prefab (skeletal animator on its root)" );
        (void)EditorWidgets::drawTextField( "##clipPreview.body", _model.editBodyPrefabPath(), -1.0f );
        EditorSelfTestMarks::note( "clip.bodyField" );
        if ( ImGui::Button( "Spawn" ) )
            _model.spawnFromField();
        EditorSelfTestMarks::note( "clip.spawn" );
        EditorWidgets::drawTooltip( "Place the body in the preview scene and play the clip on it" );

        SkeletalAnimatorComponent* pAnimator = _model.findAnimator( getPreviewScene() );
        if ( pAnimator != nullptr )
        {
            const bool bPaused = _model.isPaused();
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( bPaused ? "Play" : "Pause" ) );
            if ( ImGui::Button( bPaused ? "Play" : "Pause" ) )
                _model.togglePause();
            EditorSelfTestMarks::note( "clip.pause" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Restart" ) );
            if ( ImGui::Button( "Restart" ) )
                _model.requestReplay(); // 다음 그리기가 처음부터 다시 재생한다
            EditorSelfTestMarks::note( "clip.restart" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Loop" ) );
            if ( ImGui::Checkbox( "Loop", &_model.editLoop() ) )
                _model.requestReplay();
            ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x * 0.5f );
            (void)ImGui::SliderFloat( "Speed", &_model.editPlayRate(), 0.0f, AnimationClipPreviewPanelInternal::kMaxPlayRate );
            _model.applyPlayRate( *pAnimator );
        }

        if ( isDocumentLoadFailed() == false ) // 읽지 못한 클립은 걸지 않는다
            _model.syncBody( getPreviewScene(), getLoadedAssetPath() );
        drawClipInfo();

        const float2  available = float2{ ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y };
        const float32 minHeight = AnimationClipPreviewPanelInternal::kMinViewportHeight * EditorThemeUtil::getDPIScale();
        const float2  viewSize{ available._x, available._y > minHeight ? available._y : minHeight };
        drawPreviewViewport( viewSize, false ); // 기즈모가 없다
        EditorSelfTestMarks::note( "clip.previewScene" );
        EditorWidgets::drawPanelStatus( _model.getStatus().c_str() );
    }

    void AnimationClipPreviewPanel::drawClipInfo()
    {
        const SkeletalAnimatorComponent* pAnimator = _model.findAnimator( getPreviewScene() );
        if ( pAnimator == nullptr )
        {
            ImGui::TextDisabled( "No body yet - enter a prefab path and press Spawn" );
            return;
        }
        const AnimationClip* pClip = AnimationClipPreviewModel::findOpenClip( *pAnimator, getLoadedAssetPath() );
        if ( pClip == nullptr )
            return; // 읽지 못했다 — 상태 줄이 알렸다
        ImGui::Text( "Duration %.2f s  Tracks %u  Curves %u  Default loop %s  Root motion %s  Notifies %s", pClip->getDuration(), pClip->getTrackCount(),
                     static_cast<uint32>( pClip->getCurves().size() ), pClip->isLoopingByDefault() ? "yes" : "no", pClip->getRootMotionTrack() >= 0 ? "yes" : "no",
                     pClip->findNotifyTrack() != nullptr ? "yes" : "no" );
    }

    bool AnimationClipPreviewPanel::findPreviewClipDuration( float32& outSeconds ) const
    {
        if ( _model.getClipSeconds() <= 0.0f || _model.findAnimator( getPreviewScene() ) == nullptr )
            return false;
        outSeconds = _model.getClipSeconds();
        return true;
    }

    bool AnimationClipPreviewPanel::acceptsDocumentPath( string_view assetPath ) const
    {
        return StringUtil::endsWith( assetPath, AnimationClip::kExtension );
    }

    ToolAssetLoadResult AnimationClipPreviewPanel::loadDocument()
    {
        string path = getLoadedAssetPath();
        if ( path.empty() )
            path = string{ getMatchingFocusedPath() };
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        if ( getLoadedAssetPath().empty() )
            acceptFocusedDocument();
        _model.resetForDocument();
        return ToolAssetLoadResult::Loaded;
    }

    void AnimationClipPreviewPanel::populatePreviewScene( Scene& /*scene*/ )
    {
        getPreviewViewport().frameSphere( float3{ 0.0f, AnimationClipPreviewPanelInternal::kFrameCenterHeight, 0.0f }, AnimationClipPreviewPanelInternal::kFrameRadius );
        _model.resetForNewScene();
    }
} // namespace sw::editor
