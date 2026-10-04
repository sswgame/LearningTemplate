/**
 * @file SpriteClipPlayable.h
 * @brief 스프라이트 클립의 이름 붙은 프레임 구간 하나를 재생할 것(`IAnimPlayable`)으로 보입니다 — 2D 와 3D 가 같은 재생 · 상태 기계를 쓰는 다리입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Animation/AnimPlayback.h"

namespace sw
{
    class SpriteClipAsset;

    /**
     * @class SpriteClipPlayable
     * @brief 구간(시작 프레임 · 프레임 수 · 반복)의 길이는 프레임 시간의 합이고, 재생 시각은 구간 안의 프레임과 클립 타임라인 시각으로 풀립니다.
     * @details 스켈레탈 클립과 다른 것은 "샘플이 무엇을 내는가" 뿐입니다(프레임 번호 · 트랜스폼 키 시각 ↔ 본 포즈). 시간 흐름 · 반복 · 끝 · 다음 상태는
     *          `AnimPlayer` · `AnimGraphPlayer` 가 두 쪽에 똑같이 합니다. 클립이 없으면 길이가 대체 시간인 프레임 하나입니다.
     */
    class SW_API SpriteClipPlayable final : public IAnimPlayable
    {
    public:
        SpriteClipPlayable();

        /**
         * @brief 구간을 정합니다. 클립 · 구간이 바뀔 때마다 부릅니다(길이를 다시 셉니다).
         * @param fallbackSeconds 시간이 없는 프레임(0 ms)이 머무는 시간입니다.
         */
        void configure( const SpriteClipAsset* pClip, int32 firstFrame, int32 frameCount, bool bLoop, float32 fallbackSeconds );

        float32 getPlayLength() const override { return _playLength; }
        bool    isLoopingByDefault() const override { return _bLoop == SW_TRUE; }
        /** @brief (IAnimPlayable) 지금 구간의 알림 트랙입니다(클립의 구간 `notifies`). 없으면 nullptr 입니다. */
        const AnimNotifyTrack* findNotifyTrack() const override { return _notifyTrack.isEmpty() ? nullptr : &_notifyTrack; }

        const SpriteClipAsset* getClip() const { return _pClip; }
        int32                  getFirstFrame() const { return _firstFrame; }
        int32                  getFrameCount() const { return _frameCount; }
        /** @brief 구간 안 프레임 하나가 머무는 시간(초)입니다. */
        float32 getFrameDuration( int32 frameInRange ) const;
        /** @brief 구간 안 프레임이 시작하는 재생 시각(초)입니다. */
        float32 computeFrameStart( int32 frameInRange ) const;
        /** @brief 재생 시각의 구간 안 프레임입니다. 길이 이상이면 마지막 프레임입니다. */
        int32 findFrameAtTime( float32 time ) const;
        /** @brief 재생 시각을 클립 타임라인 시각(트랜스폼 키의 시각)으로 옮깁니다 — 구간 시작 시각 + 재생 시각(길이로 자름). */
        float32 computeClipTime( float32 time ) const;

    private:
        AnimNotifyTrack        _notifyTrack; ///< 지금 구간의 알림(구간을 정할 때 클립에서 베낀다)
        const SpriteClipAsset* _pClip;
        float32                _fallbackSeconds;
        float32                _playLength;
        float32                _rangeStartSeconds;
        int32                  _firstFrame;
        int32                  _frameCount;
        uint8                  _bLoop;
    };
} // namespace sw
