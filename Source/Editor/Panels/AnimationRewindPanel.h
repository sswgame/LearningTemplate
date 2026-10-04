/**
 * @file AnimationRewindPanel.h
 * @brief 애니메이션 되감기 창 — 기록을 켜고, 고른 캐릭터의 기록을 시간 막대로 훑으며 뼈대(디버그 드로우) · 상태 · 알림 · 커브 · 루트 모션을 봅니다.
 * @details 언리얼 Rewind Debugger 의 자리입니다. 훑으면 PIE 를 멈추고(`EditorPlaySession::pause`) 씬의 애니메이션을 기록된 시각으로 겁니다.
 *          "Resume" 이 되감기를 풀고 플레이를 잇습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw
{
    struct AnimationRewindFrame;
    struct AnimationRewindTrack;

    class AnimationRewindRecorder;
} // namespace sw

namespace sw::editor
{
    /** @brief 애니메이션 되감기 도구 창입니다. */
    class AnimationRewindPanel : public IEditorPanel
    {
    public:
        AnimationRewindPanel();
        virtual ~AnimationRewindPanel() override = default;

        const utf8* getPanelTitle() const override { return "Animation Rewind"; }
        void        drawContent() override;
        bool        isToolPanel() const override { return true; }

    private:
        /** @brief 켜기 · 창 길이 · 상태 줄입니다. */
        void drawRecordingControls( const AnimationRewindRecorder& rewind );
        /** @brief 시간 막대 — 끌면 멈추고 되감는다. */
        void drawTimeline( AnimationRewindRecorder& rewind );
        /** @brief 고른 트랙의 그 시각 프레임(상태 · 알림 · 커브 · 루트 모션)과 뼈대 그리기입니다. */
        void drawFrame( const AnimationRewindTrack& track, const AnimationRewindFrame& frame );

        vector<float3>         _listScratchPosition;
        float32                _scrubSecondsAgo; ///< 시간 막대 값(가장 늦은 기록에서 몇 초 전)
        uint8                  _bDrawSkeleton : 1;
        uint8                  _bAllTracks    : 1; ///< 고른 것만이 아니라 기록된 모든 뼈대를 그린다
        [[maybe_unused]] uint8 _reserved      : 6;
    };
} // namespace sw::editor
