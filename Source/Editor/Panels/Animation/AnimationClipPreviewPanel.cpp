/**
 * @file AnimationClipPreviewPanel.cpp
 * @brief 애니메이션 클립(`*.animclip`)을 고른 몸(프리팹)에 입혀 미리보기 씬에서 재생해 보는 창입니다(유니티 Animation 창 미리보기, 언리얼 Animation Sequence 에디터에 해당합니다).
 * @details 몸 놓기 · 클립 재생 · 속도 · 반복 · 멈춤은 `AnimationClipPreviewViewModel`(Model/, ImGui 없음)이 들고, 이 창은 그리기와 미리보기 씬 · 뷰포트만 쥔다.
 */
#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Animation/Model/AnimationClipPreviewViewModel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    /**
     * @class AnimationClipPreviewPanel
     * @brief 연 클립을 미리보기 씬의 몸 오브젝트(프리팹 루트의 `SkeletalAnimatorComponent`)로 재생하고, 길이 · 트랙 수 · 곡선 수를 보입니다.
     * @details **보기 전용**이다. `.animclip` 은 애니메이션 임포트의 산출물이라 이 창이 저장하지 않는다. 클립은 스켈레톤을 모르므로 입힐 몸(프리팹)을 사람이 고른다 —
     *          몸의 루트에 `SkeletalAnimatorComponent` 가 있어야 하고, 클립의 트랙 이름이 그 스켈레톤의 뼈와 맞아야 움직인다(맞지 않으면 애니메이터가 오류를 남긴다).
     *          창은 몸의 클립 폴더를 열린 클립이 든 폴더로 바꾸고 클립 파일 이름(확장자 앞)으로 재생한다.
     */
    class AnimationClipPreviewPanel : public EditorPreviewPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "animation_clip_preview";

        AnimationClipPreviewPanel();

        const utf8*        getPanelTitle() const override { return "Clip Preview"; }
        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override { return false; }
        bool               canSaveDocument() const override { return false; }

        /** @brief 몸이 지금 재생하는 클립의 길이(초)입니다. 몸이 없거나 클립을 읽지 못했으면 false 입니다(감시 값 `Editor.ClipPreviewDuration`). */
        [[nodiscard]] bool findPreviewClipDuration( float32& outSeconds ) const;

    private:
        bool                canAcceptDocumentPath( string_view assetPath ) const override;
        ToolAssetLoadResult loadDocument() override;
        /** @brief 미리보기 씬은 조명과 바닥뿐이다 — 몸은 "Spawn" 을 누르면 놓는다. */
        void populatePreviewScene( Scene& scene ) override;
        /** @brief 맞춤(F)과 창을 처음 열 때의 카메라가 담을 구입니다. */
        bool findPreviewFrameSphere( float3& outCenter, float32& outRadius ) const override;
        /** @brief 문지르기 막대의 시각 · 길이 — 몸이 재생하는 클립입니다. */
        bool readPreviewTimeline( float32& outTime, float32& outDuration ) const override;
        /** @brief 문지르기 막대가 고른 시각으로 몸의 클립을 옮깁니다. */
        void seekPreviewTimeline( float32 seconds ) override;
        void drawClipInfo();

    private:
        AnimationClipPreviewViewModel _model;
    };

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
                return EditorPanelManager::findOpenPanelInEditorContext<AnimationClipPreviewPanel>();
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
        EditorSelfTestMarks::record( "clip.bodyField" );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Spawn" ) ) )
            _model.spawnFromField();
        EditorSelfTestMarks::record( "clip.spawn" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "PlaceTheBodyInThePreviewScene" ) );

        if ( _model.hasBody( getPreviewScene() ) )
        {
            // 재생 · 멈춤 · 속도 · 문지르기는 뷰포트 위 공통 재생 막대가 맡는다(EditorPreviewPanel).
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Restart" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "Restart" ) ) )
                _model.requestReplay(); // 다음 그리기가 처음부터 다시 재생한다
            EditorSelfTestMarks::record( "clip.restart" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdLabel", "Loop" ) ) );
            if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Loop" ), &_model.editLoop() ) )
                _model.requestReplay();
        }

        if ( isDocumentLoadFailed() == false ) // 읽지 못한 클립은 걸지 않는다
            _model.syncBody( getPreviewScene(), getLoadedAssetPath() );
        drawClipInfo();

        const float2 viewSize = computeViewportFillSize( AnimationClipPreviewPanelInternal::kMinViewportHeight * EditorThemeUtil::getDPIScale() );
        drawPreviewViewport( viewSize, false ); // 기즈모가 없다
        EditorSelfTestMarks::record( "clip.previewScene" );
        EditorWidgets::drawPanelStatus( _model.getStatus().c_str() );
    }

    void AnimationClipPreviewPanel::drawClipInfo()
    {
        if ( _model.hasBody( getPreviewScene() ) == false )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "NoBodyYetEnterAPrefabPathAnd" ) );
            return;
        }
        AnimationClipPreviewInfo info;
        if ( _model.gatherClipInfo( getPreviewScene(), getLoadedAssetPath(), info ) == false )
            return; // 읽지 못했다 — 상태 줄이 알렸다
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "DurationSTracksCurves", TextArgumentList().addDecimal( "duration", static_cast<float64>( info._durationSeconds ), 2 ).addInteger( "trackCount", static_cast<int64>( info._trackCount ) ).addInteger( "curvesCount", static_cast<int64>( info._curveCount ) ).addText( "loopingByDefault", info._bLoopingByDefault != SW_FALSE ? "yes" : "no" ).addText( "rootMotionTrack", info._bHasRootMotion != SW_FALSE ? "yes" : "no" ).addText( "findNotifyTrack", info._bHasNotifyTrack != SW_FALSE ? "yes" : "no" ) ).c_str() );
    }

    bool AnimationClipPreviewPanel::findPreviewClipDuration( float32& outSeconds ) const
    {
        if ( _model.getClipSeconds() <= 0.0f || _model.hasBody( getPreviewScene() ) == false )
            return false;
        outSeconds = _model.getClipSeconds();
        return true;
    }

    bool AnimationClipPreviewPanel::canAcceptDocumentPath( string_view assetPath ) const
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
        if ( _model.readPlayTime( getPreviewScene(), outTime ) == false )
            return false;
        outDuration = _model.getClipSeconds();
        return true;
    }

    void AnimationClipPreviewPanel::seekPreviewTimeline( float32 seconds )
    {
        _model.seek( getPreviewScene(), seconds );
    }
} // namespace sw::editor
