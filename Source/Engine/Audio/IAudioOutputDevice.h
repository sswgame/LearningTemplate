/**
 * @file IAudioOutputDevice.h
 * @brief 플랫폼 오디오 출력 장치(XAudio2 · ALSA · AAudio · AudioWorklet)의 계약입니다 — 장치를 열고, 서비스가 정한 레이아웃으로 엔진을 당기고, 닫는 일만 합니다.
 * @details 예전에는 백엔드가 724 줄 구현을 가진 서비스(`AudioSubsystem`)를 상속해 가상 넷만 바꿨다 — 백엔드가 서비스의 모든 상태와 공개 API 를 물려받아 이름만
 *          인터페이스였다. 이제 서비스(`AudioSubsystem`, final)가 장치를 멤버로 든다(합성 — 언리얼 `FAudioMixerPlatformInterface` 가 `FMixerDevice` 에 들린 자리).
 *          장치는 만들 때 받은 서비스로 엔진을 당기고(`AudioSubsystem::getEngine().render`) 유실을 알린다(`AudioSubsystem::notifyOutputLost`).
 *          장치가 없는 구성(전용 서버 · 테스트)은 장치를 두지 않는다 — 서비스가 `update` 에서 오프라인으로 렌더한다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Audio/AudioChannelLayout.h"

namespace sw
{
    /** @brief 장치가 연 출력의 형식입니다. 채널 수 0 은 "장치 없음"입니다. */
    struct AudioOutputDevice
    {
        uint32 _channelCount{ 0 }; ///< 장치의 채널 수다.
        uint32 _channelMask{ 0 };  ///< 장치의 WAV 채널 마스크다(`AudioChannelLayoutUtil::kSpeaker*`).
    };

    /**
     * @class IAudioOutputDevice
     * @brief 출력 장치 하나입니다(파일 머리말). 서비스가 게임 스레드에서 부릅니다. 장치 스레드에서 엔진을 당기는 것은 `startOutput` 과 `stopOutput` 사이뿐입니다.
     */
    class SW_API IAudioOutputDevice
    {
    public:
        virtual ~IAudioOutputDevice() = default;

        /**
         * @brief 출력 장치를 열고 그 형식(채널 수 · 마스크)을 @p outDevice 에 적습니다. 아직 엔진을 당기지 않습니다.
         * @return 장치가 없으면 false 를 반환합니다(그때는 서비스의 `update` 가 렌더합니다). 실패하면 잡은 것을 스스로 놓습니다.
         */
        [[nodiscard]] virtual bool openDevice( AudioOutputDevice& outDevice ) = 0;
        /** @brief 열린 장치에 @p layout 형식의 스트림을 열고, 장치 스레드에서 엔진의 `render` 를 부르기 시작합니다. 실패하면 false 입니다. */
        [[nodiscard]] virtual bool startOutput( AudioChannelLayout layout ) = 0;
        /** @brief 스트림을 닫습니다. 돌아온 뒤에는 장치 스레드가 `render` 를 부르지 않습니다(장치는 열려 있습니다). */
        virtual void stopOutput() = 0;
        /** @brief 장치를 닫습니다(`stopOutput` 뒤에 부릅니다). */
        virtual void closeDevice() = 0;
        /**
         * @brief 지금 스트림이 연 뒤로 장치가 알린 언더런(xrun · 글리치) 수입니다. 게임 스레드가 아무 때나 읽습니다(감시 값 `Audio.OutputUnderruns`).
         * @details 끊김은 평균이 아니라 최악 값의 문제라 프레임 시간에 드러나지 않는다 — 장치의 셈을 그대로 낸다(XAudio2 글리치 · AAudio xrun · ALSA 복원 수).
         */
        virtual uint64 getUnderrunCount() const = 0;

    protected:
        IAudioOutputDevice()                                       = default;
        IAudioOutputDevice( const IAudioOutputDevice& )            = delete;
        IAudioOutputDevice& operator=( const IAudioOutputDevice& ) = delete;
    };
} // namespace sw
