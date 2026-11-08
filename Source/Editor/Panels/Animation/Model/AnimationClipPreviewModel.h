/**
 * @file AnimationClipPreviewModel.h
 * @brief 클립 미리보기 창의 모델 — 입힐 몸(프리팹) 경로 · Spawn 확정, 미리보기 씬의 몸 놓기 · 열린 클립 재생, 재생 속도 · 반복 · 멈춤, 상태 줄. ImGui 를 모릅니다.
 * @details 패널(`AnimationClipPreviewPanel`)은 미리보기 씬 · 뷰포트를 쥐고(`EditorPreviewPanel`) 단추 · 클립 정보를 그리기만 한다 — 래그돌 미리보기(`RagdollPreviewModel`)와 같은 선이다
 *          (언리얼 애니메이션 시퀀스 에디터의 미리보기 씬 상태와 뷰포트가 나뉜 것과 같다). 씬은 패널이 넘긴다(모델은 씬을 갖지 않는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"

namespace sw
{
    class AnimationClip;
    class Scene;
    class SkeletalAnimatorComponent;
} // namespace sw

namespace sw::editor
{
    /** @brief 몸이 읽은 클립의 요약입니다(값만 — 창은 이 값만 그린다, MVVM 계약). */
    struct AnimationClipPreviewInfo
    {
        float32 _durationSeconds{ 0.0f };
        uint32  _trackCount{ 0 };
        uint32  _curveCount{ 0 };
        uint8   _bLoopingByDefault{ SW_FALSE };
        uint8   _bHasRootMotion{ SW_FALSE };
        uint8   _bHasNotifyTrack{ SW_FALSE };
    };

    /**
     * @class AnimationClipPreviewModel
     * @brief 클립 미리보기 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class AnimationClipPreviewModel
    {
    public:
        AnimationClipPreviewModel();

        /** @brief 다른 클립을 열었다 — 다음 `syncBody` 가 몸에 다시 건다. 클립은 몸의 애니메이터가 읽고, 읽기 실패는 상태 줄이 알린다. */
        void resetForDocument();
        /** @brief 새 미리보기 씬에는 몸이 없다 — 놓은 몸과 재생한 클립을 잊습니다. */
        void resetForNewScene();
        /** @brief 입력란의 몸 경로를 확정하고 다음 `syncBody` 가 몸을 다시 놓게 합니다(Spawn 단추). */
        void spawnFromField();
        /** @brief 다음 `syncBody` 가 클립을 처음부터 다시 재생하게 합니다(Restart · 반복 바꾸기). */
        void requestReplay() { _playedClipPath.clear(); }
        /**
         * @brief 확정한 몸을 @p pScene 에 놓고(경로가 바뀌었을 때만) 클립 @p clipPath 를 재생합니다(클립이 바뀌었을 때만).
         * @details 클립을 읽지 못했으면 부르는 쪽이 부르지 않는다. 실패해도 같은 경로 · 클립으로 매 프레임 다시 놓거나 읽지 않는다.
         */
        void syncBody( Scene* pScene, string_view clipPath );
        /** @brief @p pScene 에 놓은 몸의 애니메이터입니다. 몸이 없으면 nullptr 입니다(이 호출 안에서만 씁니다). */
        [[nodiscard]] SkeletalAnimatorComponent* findAnimator( Scene* pScene ) const;
        /** @brief @p animator 가 읽은 클립 @p clipPath 입니다. 아직 읽지 않았거나 읽지 못했으면 nullptr 입니다. */
        [[nodiscard]] static const AnimationClip* findOpenClip( const SkeletalAnimatorComponent& animator, string_view clipPath );
        /** @brief @p pScene 에 몸(애니메이터)이 있으면 true 입니다. */
        [[nodiscard]] bool hasBody( Scene* pScene ) const { return findAnimator( pScene ) != nullptr; }
        /** @brief 몸이 읽은 클립 @p clipPath 의 요약을 채웁니다. 몸이 없거나 클립을 아직(또는 끝내) 읽지 못했으면 false 입니다. */
        [[nodiscard]] bool gatherClipInfo( Scene* pScene, string_view clipPath, AnimationClipPreviewInfo& outInfo ) const;
        /** @brief 몸의 재생 시각(초, 반복 재생은 길이로 접는다)입니다. 몸이 없거나 재생을 걸지 못했으면 false 입니다. */
        [[nodiscard]] bool readPlayTime( Scene* pScene, float32& outSeconds ) const;
        /** @brief 몸의 재생 시각을 @p seconds 로 옮깁니다(0 ~ 길이로 자른다). 몸이 없으면 아무것도 하지 않습니다. 미리보기 씬이라 되돌리기에 남지 않는다. */
        void seek( Scene* pScene, float32 seconds ) const;

        /** @brief 몸이 재생하는 클립의 길이(초)입니다. 재생을 걸지 못했으면 0 입니다. */
        float32       getClipSeconds() const { return _clipSeconds; }
        const string& getStatus() const { return _status; }
        /** @brief 입력란이 직접 고치는 몸 프리팹 경로입니다(Spawn 을 눌러야 적용한다). */
        string& editBodyPrefabPath() { return _bodyPrefabPath; }
        /** @brief 반복 체크 상자가 직접 고치는 값입니다. 바꿨으면 `requestReplay` 를 부릅니다(재생할 때 건다). */
        bool& editLoop() { return _bLoop; }

    private:
        /** @brief 몸 루트의 애니메이터에 클립 @p clipPath 를 읽혀 재생합니다. 읽지 못하면 상태 줄이 알린다. */
        void playClip( SkeletalAnimatorComponent& animator, string_view clipPath );

    private:
        string           _bodyPrefabPath;    ///< 입력란의 몸 프리팹 경로(Spawn 을 눌러야 적용한다)
        string           _activePrefabPath;  ///< Spawn 으로 확정한 몸 프리팹 경로
        string           _spawnedPrefabPath; ///< 미리보기 씬에 놓은 몸의 프리팹 경로(비면 아직 놓지 않았다)
        string           _playedClipPath;    ///< 몸이 재생하게 한 클립 경로(비면 다음 `syncBody` 가 다시 건다)
        string           _status;
        GameObjectHandle _body;
        float32          _clipSeconds;
        bool             _bLoop; ///< NOLINT(sw-bool-flag): 참조(`bool&`)로 넘긴다
    };
} // namespace sw::editor
