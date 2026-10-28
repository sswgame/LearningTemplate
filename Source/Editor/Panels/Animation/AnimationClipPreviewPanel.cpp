#include "pch.h"

#include "Editor/Panels/Animation/AnimationClipPreviewPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Probe/AutomationProbe.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Scene/Scene.h"

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
    SW_AUTOMATION_PROBE( editorClipPreviewDuration, "Editor.ClipPreviewDuration", "Seconds of the clip the Clip Preview panel body plays (fails while the panel is closed, has no body or the clip is not read)",
                         &AnimationClipPreviewPanelInternal::readClipDuration );
    SW_AUTOMATION_PROBE( editorClipPreviewSceneFrames, "Editor.ClipPreviewSceneFrames", "Frames the Clip Preview panel asked the renderer to draw its preview scene",
                         &AnimationClipPreviewPanelInternal::readPreviewSceneFrames );

    AnimationClipPreviewPanel::AnimationClipPreviewPanel()
        : EditorPreviewPanel{ EditorAssetType::AnimationClip, false }
        , _bodyPrefabPath{}
        , _activePrefabPath{}
        , _spawnedPrefabPath{}
        , _playedClipPath{}
        , _status{}
        , _body{}
        , _clipSeconds{ 0.0f }
        , _playRate{ 1.0f }
        , _bLoop{ true }
        , _bPaused{ false }
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
        (void)EditorWidgets::drawTextField( "##clipPreview.body", _bodyPrefabPath, -1.0f );
        EditorSelfTestMarks::note( "clip.bodyField" );
        if ( ImGui::Button( "Spawn" ) )
        {
            _activePrefabPath = _bodyPrefabPath;
            _spawnedPrefabPath.clear(); // 다음 그리기가 몸을 다시 놓는다
        }
        EditorSelfTestMarks::note( "clip.spawn" );
        EditorWidgets::drawTooltip( "Place the body in the preview scene and play the clip on it" );

        SkeletalAnimatorComponent* pAnimator = findBodyAnimator();
        if ( pAnimator != nullptr )
        {
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( _bPaused ? "Play" : "Pause" ) );
            if ( ImGui::Button( _bPaused ? "Play" : "Pause" ) )
                _bPaused = !_bPaused;
            EditorSelfTestMarks::note( "clip.pause" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Restart" ) );
            if ( ImGui::Button( "Restart" ) )
                _playedClipPath.clear(); // 다음 그리기가 처음부터 다시 재생한다
            EditorSelfTestMarks::note( "clip.restart" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Loop" ) );
            if ( ImGui::Checkbox( "Loop", &_bLoop ) )
                _playedClipPath.clear();
            ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x * 0.5f );
            (void)ImGui::SliderFloat( "Speed", &_playRate, 0.0f, AnimationClipPreviewPanelInternal::kMaxPlayRate );
            pAnimator->setPlayRate( _bPaused ? 0.0f : _playRate );
        }

        syncPreviewBody();
        drawClipInfo();

        const float2  available = float2{ ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y };
        const float32 minHeight = AnimationClipPreviewPanelInternal::kMinViewportHeight * EditorThemeUtil::getDpiScale();
        const float2  viewSize{ available._x, available._y > minHeight ? available._y : minHeight };
        drawPreviewViewport( viewSize, false ); // 기즈모가 없다
        EditorSelfTestMarks::note( "clip.previewScene" );
        EditorWidgets::drawPanelStatus( _status.c_str() );
    }

    void AnimationClipPreviewPanel::drawClipInfo()
    {
        SkeletalAnimatorComponent* pAnimator = findBodyAnimator();
        if ( pAnimator == nullptr )
        {
            ImGui::TextDisabled( "No body yet - enter a prefab path and press Spawn" );
            return;
        }
        string_view directory;
        string_view fileName;
        FileUtil::splitPath( getLoadedAssetPath(), directory, fileName );
        string_view stem;
        FileUtil::removeExtension( fileName, stem );
        const AnimationClip* pClip = pAnimator->findClip( hashed_string( stem ) );
        if ( pClip == nullptr )
            return; // 읽지 못했다 — 상태 줄이 알렸다
        ImGui::Text( "Duration %.2f s  Tracks %u  Curves %u  Default loop %s  Root motion %s  Notifies %s", pClip->getDuration(), pClip->getTrackCount(),
                     static_cast<uint32>( pClip->getCurves().size() ), pClip->isLoopingByDefault() ? "yes" : "no", pClip->getRootMotionTrack() >= 0 ? "yes" : "no",
                     pClip->findNotifyTrack() != nullptr ? "yes" : "no" );
    }

    bool AnimationClipPreviewPanel::findPreviewClipDuration( float32& outSeconds ) const
    {
        if ( _clipSeconds <= 0.0f || findBodyAnimator() == nullptr )
            return false;
        outSeconds = _clipSeconds;
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

        // 클립은 몸의 애니메이터가 읽는다 — 읽기 실패는 playOpenClip 이 상태 줄로 알린다. 다른 클립을 열었으니 몸에 다시 걸게 한다.
        _playedClipPath.clear();
        _clipSeconds = 0.0f;
        _status      = "Loaded";
        return ToolAssetLoadResult::Loaded;
    }

    void AnimationClipPreviewPanel::populatePreviewScene( Scene& /*scene*/ )
    {
        getPreviewViewport().frameSphere( float3{ 0.0f, AnimationClipPreviewPanelInternal::kFrameCenterHeight, 0.0f }, AnimationClipPreviewPanelInternal::kFrameRadius );
        _spawnedPrefabPath.clear(); // 새 씬에는 몸이 없다
        _playedClipPath.clear();
        _body = GameObjectHandle{};
    }

    SkeletalAnimatorComponent* AnimationClipPreviewPanel::findBodyAnimator() const
    {
        Scene*             pScene   = getPreviewScene();
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        GameObject*        pBody    = pManager != nullptr ? pManager->resolveGameObject( _body ) : nullptr;
        return pBody != nullptr ? pBody->getComponent<SkeletalAnimatorComponent>() : nullptr;
    }

    void AnimationClipPreviewPanel::syncPreviewBody()
    {
        const bool bDocumentReady = getLoadedAssetPath().empty() == false && isDocumentLoadFailed() == false;
        Scene*     pScene         = getPreviewScene();
        if ( bDocumentReady == false || pScene == nullptr || _activePrefabPath.empty() )
            return;
        GameObjectManager* pManager = pScene->getObjectManager();
        if ( pManager == nullptr )
            return;

        if ( _spawnedPrefabPath != _activePrefabPath )
        {
            GameObject* pOld = pManager->resolveGameObject( _body );
            if ( pOld != nullptr )
                pManager->destroyObject( pOld );
            _body        = GameObjectHandle{};
            _clipSeconds = 0.0f;
            _playedClipPath.clear();
            _spawnedPrefabPath     = _activePrefabPath; // 실패해도 같은 경로로 매 프레임 다시 놓지 않는다(경로를 고치고 Spawn 을 다시 누른다)
            AssetManager* pAssets  = pManager->findAssetManager();
            GameObject*   pSpawned = pAssets != nullptr ? pAssets->getAssetCache<PrefabCache>().spawn( pManager, _activePrefabPath ) : nullptr;
            if ( pSpawned == nullptr )
            {
                _status = string( "Prefab '" ) + _activePrefabPath + "' could not be spawned - see the log";
                return;
            }
            _body = pSpawned->getHandle();
        }

        SkeletalAnimatorComponent* pAnimator = findBodyAnimator();
        if ( pAnimator == nullptr )
        {
            if ( pManager->resolveGameObject( _body ) != nullptr )
                _status = string( "Prefab '" ) + _activePrefabPath + "' has no skeletal animator on its root - the clip cannot play";
            return;
        }
        if ( _playedClipPath != getLoadedAssetPath() )
            playOpenClip( *pAnimator );
    }

    void AnimationClipPreviewPanel::playOpenClip( SkeletalAnimatorComponent& animator )
    {
        _playedClipPath = getLoadedAssetPath(); // 실패해도 같은 클립을 매 프레임 다시 읽지 않는다(Restart 나 다른 클립)
        _clipSeconds    = 0.0f;

        string_view directory;
        string_view fileName;
        FileUtil::splitPath( getLoadedAssetPath(), directory, fileName );
        string_view stem;
        FileUtil::removeExtension( fileName, stem );
        while ( directory.empty() == false && directory.back() == '/' )
        {
            directory.remove_suffix( 1 );
        }

        const hashed_string clipName{ stem };
        animator.setClipFolder( directory );
        const AnimationClip* pClip = animator.preloadClip( clipName ) ? animator.findClip( clipName ) : nullptr;
        if ( pClip == nullptr )
        {
            _status = string( "The clip '" ) + string{ stem } + "' could not be read from its folder - see the log";
            return;
        }
        if ( animator.play( clipName, _bLoop, 0.0f ) == false )
        {
            _status = string( "The body could not play the clip '" ) + string{ stem } + "' - see the log";
            return;
        }
        _clipSeconds = pClip->getDuration();
        _status      = "Playing";
        SW_LOG_INFO( "Clip Preview playing '%#' on '%#'", getLoadedAssetPath(), _activePrefabPath );
    }
} // namespace sw::editor
