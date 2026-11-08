/**
 * @file DevTools/Client/Audio/AudioDumpListener.h
 * @brief `-gv_audioDumpPath` 개발 리스너가 쌓는 오프라인 렌더 버퍼입니다(구현 `AudioDumpListener.cpp`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    // 개발 도구 클라이언트가 기동 범위에 세운다(`DevToolsClient::addStartupSubsystems`) — 엔진 서비스 칸(SW_SERVICE_SLOT)은 Engine 이미지의 정적 멤버라 DevTools 가 정의할 수 없다.
    /**
     * @struct AudioDumpBuffer
     * @brief 쌓은 오프라인 렌더(채널 교차)입니다. 엔진 범위 서비스라 엔진 범위와 함께 생기고 사라진다 — 리스너(정적 등록 항목)는 칸을 들지 않는다.
     */
    class AudioDumpBuffer final : public ISubsystem
    {
    public:
        vector<float32> _listSample;

        static constexpr const utf8* kSubsystemName = "AudioDumpBuffer";
        SubsystemInitResult          initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                         shutdownSubsystem() override {}
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
    };
} // namespace sw
