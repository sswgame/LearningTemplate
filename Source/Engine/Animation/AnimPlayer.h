/**
 * @file AnimPlayer.h
 * @brief AnimClip 재생과 두 클립 사이의 크로스페이드입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Animation/AnimClip.h"

namespace sw
{
    /**
     * @class AnimPlayer
     * @brief 선형 크로스페이드를 하는 두 칸짜리 클립 플레이어입니다.
     */
    class SW_API AnimPlayer
    {
    public:
        /** @brief 빈 플레이어로 만듭니다. */
        AnimPlayer() = default;

        /** @brief 클립을 즉시 재생합니다. */
        void play( const AnimClip* pClip, bool bLooping = true );
        /** @brief 클립으로 크로스페이드합니다. */
        void crossfade( const AnimClip* pClip, float32 fadeSeconds, bool bLooping = true );

        /** @brief 재생 시각과 페이드를 갱신합니다. */
        void update( float32 deltaSeconds );
        /** @brief 현재 샘플을 평가합니다. 페이드 중이면 두 클립을 섞은 값입니다. */
        AnimSample evaluate() const;

        /**
         * @brief 재생 속도 배율을 설정합니다(1.0 = 보통 속도, 0.0 = 일시정지, 2.0 = 2배속).
         * @details 역재생은 지원하지 않습니다. 음수는 0(일시정지)으로 막습니다. 음수를 그대로
         *          흘리면 크로스페이드 경과 시간이 뒤로 흘러 페이드가 영원히 끝나지 않습니다.
         *          `update` 가 음수 델타를 0 으로 막는 것과 같은 이유입니다.
         */
        void setSpeed( float32 speed ) { _playSpeed = ( speed > 0.0f ) ? speed : 0.0f; }
        /** @brief 현재 재생 속도 배율을 반환합니다. */
        float32 getSpeed() const { return _playSpeed; }

        /** @brief 현재 클립을 반환합니다. */
        const AnimClip* getCurrentClip() const { return _pCurrent; }
        /** @brief 페이드 대상 클립을 반환합니다. */
        const AnimClip* getNextClip() const { return _pNext; }
        /** @brief 크로스페이드 중인지 반환합니다. */
        bool isCrossfading() const { return _fadeDuration > 0.0f && _pNext != nullptr; }
        /** @brief 현재 클립이 루프 없이 끝까지 재생됐으면 true 입니다. 클립이 없어도 true 입니다. */
        bool hasFinished() const;
        /** @brief 현재 클립 재생 시각(초)을 반환합니다. */
        float32 getCurrentTime() const { return _currentTime; }

    private:
        const AnimClip* _pCurrent{ nullptr };  /**< 지금 재생 중인 클립입니다. 소유하지 않습니다. */
        const AnimClip* _pNext{ nullptr };     /**< 크로스페이드 대상 클립입니다. 소유하지 않습니다. */
        float32         _currentTime{ 0.0f };  /**< `_pCurrent` 의 재생 시각(초)입니다. */
        float32         _nextTime{ 0.0f };     /**< `_pNext` 의 재생 시각(초)입니다. */
        float32         _fadeDuration{ 0.0f }; /**< 크로스페이드 길이(초)입니다. 0 이면 페이드 중이 아닙니다. */
        float32         _fadeElapsed{ 0.0f };  /**< 크로스페이드 경과 시간(초)입니다. */
        float32         _playSpeed{ 1.0f };    /**< 재생 속도 배율입니다. 음수가 될 수 없습니다. */
        bool            _bCurrentLoop{ true }; /**< `_pCurrent` 를 루프 재생하는지 여부입니다. */
        bool            _bNextLoop{ true };    /**< `_pNext` 를 루프 재생하는지 여부입니다. */
    };
} // namespace sw
