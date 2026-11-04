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
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
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
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdLabel", "OpenAAnimclipClipPickABodyPrefab" ) );
            return;
        }

        ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "BodyPrefabSkeletalAnimatorOnItsRoot" ) );
        (void)EditorWidgets::drawTextField( "##clipPreview.body", _model.editBodyPrefabPath(), -1.0f );
        EditorSelfTestMarks::note( "clip.bodyField" );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Spawn" ) ) )
            _model.spawnFromField();
        EditorSelfTestMarks::note( "clip.spawn" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "PlaceTheBodyInThePreviewScene" ) );

        SkeletalAnimatorComponent* pAnimator = _model.findAnimator( getPreviewScene() );
        if ( pAnimator != nullptr )
        {
            // 재생 · 멈춤 · 속도 · 문지르기는 뷰포트 위 공통 재생 막대가 맡는다(EditorPreviewPanel).
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Restart" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "Restart" ) ) )
                _model.requestReplay(); // 다음 그리기가 처음부터 다시 재생한다
            EditorSelfTestMarks::note( "clip.restart" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdLabel", "Loop" ) ) );
            if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Loop" ), &_model.editLoop() ) )
                _model.requestReplay();
        }

        if ( isDocumentLoadFailed() == false ) // 읽지 못한 클립은 걸지 않는다
            _model.syncBody( getPreviewScene(), getLoadedAssetPath() );
        drawClipInfo();

        const float2 viewSize = computeViewportFillSize( AnimationClipPreviewPanelInternal::kMinViewportHeight * EditorThemeUtil::getDPIScale() );
        drawPreviewViewport( viewSize, false ); // 기즈모가 없다
        EditorSelfTestMarks::note( "clip.previewScene" );
        EditorWidgets::drawPanelStatus( _model.getStatus().c_str() );
    }

    void AnimationClipPreviewPanel::drawClipInfo()
    {
        const SkeletalAnimatorComponent* pAnimator = _model.findAnimator( getPreviewScene() );
        if ( pAnimator == nullptr )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "NoBodyYetEnterAPrefabPathAnd" ) );
            return;
        }
        const AnimationClip* pClip = AnimationClipPreviewModel::findOpenClip( *pAnimator, getLoadedAssetPath() );
        if ( pClip == nullptr )
            return; // 읽지 못했다 — 상태 줄이 알렸다
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "DurationSTracksCurves", TextArgumentList().addDecimal( "duration", static_cast<float64>( pClip->getDuration() ), 2 ).addInteger( "trackCount", static_cast<int64>( pClip->getTrackCount() ) ).addInteger( "curvesCount", static_cast<int64>( pClip->getCurves().size() ) ).addText( "loopingByDefault", pClip->isLoopingByDefault() ? "yes" : "no" ).addText( "rootMotionTrack", pClip->getRootMotionTrack() >= 0 ? "yes" : "no" ).addText( "findNotifyTrack", pClip->findNotifyTrack() != nullptr ? "yes" : "no" ) ).c_str() );
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
        const string path = acceptDocumentToLoad();
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        _model.resetForDocument();
        return ToolAssetLoadResult::Loaded;
    }

    void AnimationClipPreviewPanel::populatePreviewScene( Scene& /*scene*/ )
    {
        _model.resetForNewScene();
    }

    bool AnimationClipPreviewPanel::findPreviewFrameSphere( float3& outCenter, float32& outRadius ) const
    {
        outCenter = float3{ 0.0f, AnimationClipPreviewPanelInternal::kFrameCenterHeight, 0.0f };
        outRadius = AnimationClipPreviewPanelInternal::kFrameRadius;
        return true;
    }

    bool AnimationClipPreviewPanel::readPreviewTimeline( float32& outTime, float32& outDuration ) const
    {
        const SkeletalAnimatorComponent* pAnimator = _model.findAnimator( getPreviewScene() );
        const float32                    duration  = _model.getClipSeconds();
        if ( pAnimator == nullptr || duration <= 0.0f )
            return false;
        outDuration = duration;
        outTime     = MathUtil::fmod( pAnimator->getStateTime(), duration ); // 반복 재생은 상태 시간이 길이를 넘는다
        return true;
    }

    void AnimationClipPreviewPanel::seekPreviewTimeline( float32 seconds )
    {
        SkeletalAnimatorComponent* pAnimator = _model.findAnimator( getPreviewScene() );
        if ( pAnimator != nullptr )
            (void)pAnimator->seekCurrentTime( MathUtil::clamp( seconds, 0.0f, _model.getClipSeconds() ) ); // 범위 밖이면 애니메이터가 알린다
    }
} // namespace sw::editor
