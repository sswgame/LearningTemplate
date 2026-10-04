/**
 * @file NullAudioSystem.h
 * @brief 출력 장치가 없는 플랫폼용 오디오 시스템입니다. 엔진은 그대로 돌고, `update` 가 흐른 시간만큼 버퍼에 렌더합니다.
 *
 * @note Windows 에서는 쓰이지 않지만 **항상 컴파일됩니다**(`IAudioSystem.cpp` 가 무조건 include 합니다).
 *       `#else` 안에만 두면 Windows 에서 한 번도 컴파일되지 않아 조용히 썩습니다.
 */
#pragma once
#include "Engine/Audio/IAudioSystem.h"

namespace sw
{
    /**
     * @class NullAudioSystem
     * @brief 소리를 내지 않는 오디오 시스템입니다. 장치를 열지 않으므로(`openOutput` 이 false) 재생 상태는 `update` 의 오프라인 렌더로 진행합니다.
     */
    class NullAudioSystem : public IAudioSystem
    {
    public:
        /** @brief 출력 장치 없이 만듭니다. */
        NullAudioSystem() = default;
        /** @brief 내립니다. */
        ~NullAudioSystem() override { shutdown(); }
    };
} // namespace sw
