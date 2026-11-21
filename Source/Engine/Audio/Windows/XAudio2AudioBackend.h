/**
 * @file XAudio2AudioBackend.h
 * @brief Windows 오디오 출력입니다. XAudio2 소스 보이스 하나로, 엔진이 렌더한 float(레이아웃의 채널 수와 채널 마스크)를 스트리밍합니다.
 * @details 마스터링 보이스는 장치 채널 수 그대로 열고 채널 마스크를 서비스에 알린다(`openDevice`). 서비스가 정한 레이아웃으로 소스 보이스를 연다(`startOutput`).
 *
 * @note 믹스는 전부 엔진(`AudioEngine`)이 합니다. 이 백엔드는 장치를 열고, XAudio2 처리 스레드의 버퍼 끝 콜백에서 다음 버퍼를 렌더해 제출할 뿐입니다.
 *       공통 디코더가 다루지 않는 형식(MP3, ADPCM WAV)은 Media Foundation 대체 디코더로 디코드합니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Audio/IAudioOutputDevice.h"

namespace sw
{
    class AudioService;
    struct XAudio2AudioBackendImpl;

    /**
     * @class XAudio2AudioBackend
     * @brief Windows 오디오 백엔드입니다.
     */
    class SW_API XAudio2AudioBackend final : public IAudioOutputDevice
    {
    public:
        /** @brief 장치를 잡지 않은 채로 만듭니다(대체 디코더만 겁니다). */
        explicit XAudio2AudioBackend( AudioService& service );
        /** @brief 장치를 닫고 엔진을 내립니다. */
        ~XAudio2AudioBackend() override;

        /** @brief COM, Media Foundation, XAudio2 를 초기화하고 장치 채널 수의 마스터링 보이스를 엽니다. 장치가 없으면 false 를 반환합니다. */
        [[nodiscard]] bool openDevice( AudioOutputDevice& outDevice ) override;
        /** @brief @p layout 형식(채널 마스크 포함)의 스트리밍 보이스를 열고 시작합니다. */
        [[nodiscard]] bool startOutput( AudioChannelLayout layout ) override;
        /** @brief 스트리밍 보이스를 멈추고 파괴합니다. */
        void stopOutput() override;
        /** @brief 스트리밍 보이스와 장치를 놓습니다. */
        void closeDevice() override;
        /** @brief XAudio2 의 글리치 수(`XAUDIO2_PERFORMANCE_DATA::GlitchesSinceEngineStarted`)입니다. 장치가 없으면 0 입니다. */
        uint64 getUnderrunCount() const override;

    private:
        AudioService&                       _service; ///< 이 장치가 당기는 서비스(엔진 · 유실 알림) — 서비스가 장치를 들고 먼저 내린다
        unique_ptr<XAudio2AudioBackendImpl> _impl;    /**< Windows 전용 상태입니다. 헤더에서 XAudio2 를 가립니다. */
    };
} // namespace sw
