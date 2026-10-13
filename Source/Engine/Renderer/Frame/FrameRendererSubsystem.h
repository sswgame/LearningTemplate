/**
 * @file Engine/Renderer/Frame/FrameRendererSubsystem.h
 * @brief 기동 순서의 FrameRenderer 자리입니다 — 렌더러와 GT 쪽 패킷 빌더(씬 스냅샷 · 업로드 큐)를 RHI 디바이스에 묶고 둘을 소유합니다.
 * @details 백엔드 교체로 다시 설 때는 있는 객체를 그대로 쓰고 디바이스에 매인 설정만 새로 건다. GT 쪽 GPUScene 도 스냅샷의 소유(머티리얼 · 인스턴스)를
 *          들고 있어 렌더러와 같은 시점에, 디바이스가 살아 있을 때(종료) 놓는다. 소멸은 패킷 빌더(해제 `destroy` 뒤) → 렌더러 순이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    class FrameRenderer;
    class RenderPacketBuilder;
    class RHISubsystem;

    /**
     * @class FrameRendererSubsystem
     * @brief 렌더러 · 패킷 빌더를 디바이스에 묶는 서브시스템입니다.
     */
    class SW_API FrameRendererSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "FrameRenderer";

        /** @brief @p frameRenderer 를 넘겨받고(서비스 표에는 호스트가 이미 묶었다) 패킷 빌더를 만듭니다. @p rhiSubsystem 은 이것보다 오래 산다. */
        FrameRendererSubsystem( unique_ptr<FrameRenderer> frameRenderer, const RHISubsystem& rhiSubsystem );
        ~FrameRendererSubsystem() override;

        FrameRendererSubsystem( const FrameRendererSubsystem& )            = delete;
        FrameRendererSubsystem& operator=( const FrameRendererSubsystem& ) = delete;

        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override;
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
        SubsystemHostMask       getSubsystemHost() const override;
        MemoryTag               getSubsystemMemoryTag() const override { return MemoryTag::RenderCPU; }

        FrameRenderer&       getFrameRenderer() const { return *_frameRenderer; }
        RenderPacketBuilder& getPacketBuilder() const { return *_packetBuilder; }

    private:
        unique_ptr<FrameRenderer>       _frameRenderer;
        unique_ptr<RenderPacketBuilder> _packetBuilder;
        const RHISubsystem&             _rhiSubsystem;
    };
} // namespace sw
