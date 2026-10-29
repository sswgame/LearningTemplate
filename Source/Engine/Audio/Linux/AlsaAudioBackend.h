/**
 * @file AlsaAudioBackend.h
 * @brief 리눅스 오디오 출력입니다. ALSA(`libasound.so.2`)의 기본 장치(`default`, PulseAudio · PipeWire 의 ALSA 플러그인 포함)에 엔진 출력을 씁니다.
 *
 * @note 믹스는 전부 엔진(`AudioEngine`)이 합니다. 이 백엔드는 장치를 열고, 자기 스트림 스레드에서 블록을 렌더해 `snd_pcm_writei` 로 씁니다(장치가 속도를 정합니다).
 *       `libasound` 는 링크하지 않고 실행 중에 엽니다 — 없는 기계(서버, 컨테이너)에서도 프로세스가 뜨고 오프라인 렌더로 진행합니다.
 *       ALSA 기본 장치는 스피커 배치를 알려 주지 않으므로 장치 마스크는 스테레오로 알립니다(`Auto` 는 스테레오). 5.1 · 7.1 은 사용자가 고르면 장치의 최대 채널 수 안에서 열고,
 *       채널 순서는 ALSA 순서(FL FR RL RR FC LFE SL SR)로 바꿔 씁니다.
 */
#pragma once
#include "Core/Concurrency/StopToken.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Audio/IAudioService.h"

namespace sw
{
    struct AlsaAudioBackendImpl;

    /**
     * @class AlsaAudioBackend
     * @brief 리눅스 오디오 백엔드입니다.
     */
    class SW_API AlsaAudioBackend : public IAudioService
    {
    public:
        /** @brief 장치를 잡지 않은 채로 만듭니다. */
        AlsaAudioBackend();
        /** @brief 장치를 닫고 엔진을 내립니다. */
        ~AlsaAudioBackend() override;

    protected:
        /** @brief `libasound` 를 열고 기본 재생 장치를 엽니다. 최대 채널 수를 알립니다. 라이브러리나 장치가 없으면 false 입니다. */
        [[nodiscard]] bool openDevice( AudioOutputDevice& outDevice ) override;
        /** @brief @p layout 의 채널 수 · 48 kHz · float 로 장치를 맞추고 스트림 스레드를 시작합니다. */
        [[nodiscard]] bool startOutput( AudioChannelLayout layout ) override;
        /** @brief 스트림 스레드를 멈추고 기다립니다. */
        void stopOutput() override;
        /** @brief 장치를 닫고 라이브러리를 놓습니다. */
        void closeDevice() override;

    private:
        /** @brief 스트림 스레드 본문입니다. 멈추라는 표시가 설 때까지 블록을 렌더해 씁니다. */
        void runStream( StopToken stopToken );

    private:
        unique_ptr<AlsaAudioBackendImpl> _impl; /**< 리눅스 전용 상태입니다. 헤더에서 ALSA 형식을 가립니다. */
    };
} // namespace sw
