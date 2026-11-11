/**
 * @file AAudioAudioBackend.h
 * @brief Android 오디오 장치 백엔드입니다(AAudio 데이터 콜백 — API 26+, 이 저장소의 최소 API 는 29).
 * @details 예전에는 Android 클라이언트에 등록된 백엔드가 없어 기동에서 `IAudioService::create` 가 치명 오류였다(감사 P0-11).
 *          언리얼 AudioMixer 의 Android 백엔드(AAudio · OpenSL)와 같은 자리다. 장치가 콜백 스레드에서 프레임을 달라고 하면 엔진이 그 자리에 렌더한다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Audio/IAudioService.h"

namespace sw
{
    struct AAudioAudioBackendImpl;

    /**
     * @class AAudioAudioBackend
     * @brief Android 오디오 백엔드입니다.
     */
    class SW_API AAudioAudioBackend : public IAudioService
    {
    public:
        /** @brief 장치를 잡지 않은 채로 만듭니다. */
        AAudioAudioBackend();
        /** @brief 장치를 닫고 엔진을 내립니다. */
        ~AAudioAudioBackend() override;

    protected:
        /** @brief 출력 스트림을 열 수 있는지 봅니다. 스피커 배치는 알려 주지 않으므로 스테레오를 알린다. */
        [[nodiscard]] bool openDevice( AudioOutputDevice& outDevice ) override;
        /** @brief @p layout 의 채널 수 · 48 kHz · float 로 스트림을 열고 콜백을 시작합니다. */
        [[nodiscard]] bool startOutput( AudioChannelLayout layout ) override;
        /** @brief 스트림을 멈추고 닫습니다. */
        void stopOutput() override;
        /** @brief 장치를 놓습니다. */
        void closeDevice() override;

    private:
        /** @brief 장치 콜백 본문입니다 — @p frameCount 프레임을 @p pOutput 에 렌더합니다. */
        void renderCallback( float32* pOutput, uint32 frameCount );

    private:
        friend struct AAudioAudioBackendCallback;
        unique_ptr<AAudioAudioBackendImpl> _impl; /**< Android 전용 상태입니다. 헤더에서 AAudio 형식을 가립니다. */
    };
} // namespace sw
