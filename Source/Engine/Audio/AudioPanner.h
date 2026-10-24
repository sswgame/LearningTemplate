/**
 * @file AudioPanner.h
 * @brief 방향을 채널별 게인으로 바꾸는 패너입니다. 레이아웃(채널 집합)과 패너(방향 → 게인)를 나눠, 보이스는 방향만 내고 패너가 레이아웃에 맞춰 게인을 고릅니다.
 * @details 방향(`AudioPanDirection`)은 리스너 수평면의 (오른쪽, 앞) 성분이고, 길이가 1 보다 짧을수록 모든 스피커로 퍼진다(리스너 머리 근처의 소리).
 *          - 스테레오 레이아웃은 오른쪽 성분을 팬 값으로 쓴 등전력 법칙이다(θ = (팬 + 1)·π/4, L = cos θ, R = sin θ). 앞뒤는 구분하지 않는다.
 *          - 5.1 · 7.1 의 `Surround` 모드는 방위가 놓인 두 스피커 사이를 등전력으로 나눈다(쌍별 패닝, 2D VBAP). LFE 는 직접 패닝하지 않는다.
 *            퍼짐은 점 게인과 균등 게인(1/√N, N = LFE 를 뺀 채널 수)을 전력으로 섞어(점 L², 균등 1 − L²) 합 전력이 1 로 일정하다.
 *          - `FrontStereo` 모드는 어느 레이아웃에서나 앞 두 채널(FL FR)에 스테레오 법칙으로 낸다. 2D 소리와 화면 평면(Screen2D) 리스너가 쓴다(FMOD · Unity 의 2D 소리와 같다).
 *          고도는 아직 쓰지 않는다. 높이 채널이 없는 레이아웃만 다루기 때문이며, HRTF 패너가 같은 입력에 고도를 더해 같은 자리에 올라온다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Audio/AudioChannelLayout.h"

namespace sw
{
    /** @brief 방향을 어느 스피커 집합에 낼지입니다. */
    enum class AudioPanMode : uint8
    {
        FrontStereo, ///< 앞 두 채널(FL FR)에 스테레오 등전력 법칙으로 낸다(2D 소리, 화면 평면 리스너).
        Surround,    ///< 레이아웃의 스피커 쌍 사이에 방위대로 낸다(3D 리스너).
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AudioPanDirection
     * @brief 보이스가 패너에 넘기는 방향입니다. (오른쪽, 앞) 의 길이는 1 이하이며 짧을수록 퍼집니다.
     */
    struct SW_API AudioPanDirection
    {
        float32      _right{ 0.0f };                     ///< 리스너 오른쪽 성분([-1, 1]). 스테레오 레이아웃의 팬 값이다.
        float32      _front{ 0.0f };                     ///< 리스너 앞 성분([-1, 1]). 뒤는 음수다.
        AudioPanMode _mode{ AudioPanMode::FrontStereo }; ///< 낼 스피커 집합이다.

        /** @brief 스테레오 팬 값([-1, 1])만 있는 방향을 만듭니다(2D 소리, `FrontStereo`). */
        static AudioPanDirection makeStereoPan( float32 pan );
        /** @brief 사용자 팬(클립 재생 인자)을 오른쪽 성분에 더합니다. 길이가 1 을 넘으면 앞 성분을 줄여 1 로 맞춥니다(스테레오 결과는 팬의 합을 [-1, 1] 로 자른 것과 같다). */
        void addPan( float32 pan );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AudioPanner
     * @brief 방향과 레이아웃으로 채널별 게인을 구하는 순수 함수입니다. 상태가 없어 테스트가 방위마다 바로 측정합니다.
     */
    struct SW_API AudioPanner
    {
        /**
         * @brief 모노 소스 하나의 채널별 게인입니다. @p outArrGain 의 앞 `getChannelCount( layout )` 칸을 채우고 나머지는 0 입니다.
         * @details 게인 제곱의 합은 1 입니다(에너지 보존). LFE 칸은 항상 0 입니다.
         */
        static void computeGains( AudioChannelLayout layout, const AudioPanDirection& direction, float32 ( &outArrGain )[audio::kMaxChannelCount] );
    };
} // namespace sw
