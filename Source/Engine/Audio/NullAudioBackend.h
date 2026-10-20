/**
 * @file NullAudioBackend.h
 * @brief 출력 장치를 열지 않는 오디오 시스템입니다(전용 서버, 테스트). 엔진은 그대로 돌고, `update` 가 흐른 시간만큼 버퍼에 렌더합니다.
 *
 * @note 클라이언트(Windows XAudio2 · 리눅스 ALSA)에서는 `createNull` 로만 쓰지만 **항상 컴파일됩니다**(`IAudioService.cpp` 가 무조건 include 합니다).
 *       `#else` 안에만 두면 한 번도 컴파일되지 않아 조용히 썩습니다.
 */
#pragma once
#include "Engine/Audio/IAudioService.h"

namespace sw
{
    /**
     * @class NullAudioBackend
     * @brief 소리를 내지 않는 오디오 시스템입니다. 장치를 열지 않으므로(`openDevice` 가 false) 재생 상태는 `update` 의 오프라인 렌더로 진행합니다.
     * @details 장치가 없으니 명시 스피커 모드(`setSpeakerMode`)는 그 레이아웃 그대로 렌더한다. 5.1 · 7.1 의 결정적 오프라인 렌더와 덤프(`gv_audioDumpPath`)가 이 길을 쓴다.
     */
    class NullAudioBackend : public IAudioService
    {
    public:
        /** @brief 출력 장치 없이 만듭니다. */
        NullAudioBackend() = default;
        /** @brief 내립니다. */
        ~NullAudioBackend() override { shutdown(); }
    };
} // namespace sw
