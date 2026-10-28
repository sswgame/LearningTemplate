/**
 * @file AnimationClipPreviewPanel.h
 * @brief 애니메이션 클립(`*.animclip`)을 고른 몸(프리팹)에 입혀 미리보기 씬에서 재생해 보는 창입니다(유니티 Animation 창 미리보기, 언리얼 Animation Sequence 에디터에 해당합니다).
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/EditorPreviewPanel.h"

namespace sw
{
    class SkeletalAnimatorComponent;
} // namespace sw

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
        bool                acceptsDocumentPath( string_view assetPath ) const override;
        ToolAssetLoadResult loadDocument() override;
        /** @brief 미리보기 씬은 조명과 바닥뿐이다 — 몸은 "Spawn" 을 누르면 놓는다. */
        void populatePreviewScene( Scene& scene ) override;
        /** @brief 미리보기 몸의 애니메이터입니다. 몸이 없으면 nullptr 입니다(이 호출 안에서만 씁니다). */
        SkeletalAnimatorComponent* findBodyAnimator() const;
        /** @brief 고른 몸을 놓고(경로가 바뀌었을 때만) 연 클립을 재생합니다(클립이 바뀌었을 때만). */
        void syncPreviewBody();
        /** @brief 몸 루트의 애니메이터에 열린 클립을 읽혀 재생합니다. 읽지 못하면 상태 줄이 알립니다. */
        void playOpenClip( SkeletalAnimatorComponent& animator );
        void drawClipInfo();

        string           _bodyPrefabPath;    ///< 입력란의 몸 프리팹 경로(Spawn 을 눌러야 적용한다)
        string           _activePrefabPath;  ///< Spawn 으로 확정한 몸 프리팹 경로
        string           _spawnedPrefabPath; ///< 미리보기 씬에 놓은 몸의 프리팹 경로(비면 아직 놓지 않았다)
        string           _playedClipPath;    ///< 몸이 재생하게 한 클립 경로(비면 다음 그리기가 다시 건다)
        string           _status;
        GameObjectHandle _body;
        float32          _clipSeconds;
        float32          _playRate;
        bool             _bLoop;
        bool             _bPaused;
    };
} // namespace sw::editor
