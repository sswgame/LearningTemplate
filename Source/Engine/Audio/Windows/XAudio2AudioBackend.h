/**
 * @file XAudio2AudioBackend.h
 * @brief Windows 오디오 출력입니다. XAudio2 소스 보이스 하나로, 엔진이 렌더한 스테레오 float 를 스트리밍합니다.
 *
 * @note 믹스는 전부 엔진(`AudioEngine`)이 합니다. 이 백엔드는 장치를 열고, XAudio2 처리 스레드의 버퍼 끝 콜백에서 다음 버퍼를 렌더해 제출할 뿐입니다.
 *       공통 디코더가 다루지 않는 형식(MP3, ADPCM WAV)은 Media Foundation 대체 디코더로 디코드합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Audio/IAudioService.h"

namespace sw
{
    struct XAudio2AudioBackendImpl;

    /**
     * @class XAudio2AudioBackend
     * @brief Windows 오디오 백엔드입니다.
     */
    class SW_API XAudio2AudioBackend : public IAudioService
    {
    public:
        /** @brief 장치를 잡지 않은 채로 만듭니다(대체 디코더만 겁니다). */
        XAudio2AudioBackend();
        /** @brief 장치를 닫고 엔진을 내립니다. */
        ~XAudio2AudioBackend() override;

    protected:
        /** @brief COM, Media Foundation, XAudio2 를 초기화하고 스트리밍 보이스를 시작합니다. 장치가 없으면 false 를 반환합니다. */
        [[nodiscard]] bool openOutput() override;
        /** @brief 스트리밍 보이스를 멈추고 장치를 놓습니다. */
        void closeOutput() override;

    private:
        unique_ptr<XAudio2AudioBackendImpl> _impl; /**< Windows 전용 상태입니다. 헤더에서 XAudio2 를 가립니다. */
    };
} // namespace sw
