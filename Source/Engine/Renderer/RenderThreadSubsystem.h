/**
 * @file Engine/Renderer/RenderThreadSubsystem.h
 * @brief 기동 순서에서 RenderThread 를 맡는 서브시스템입니다. 렌더 스레드를 디바이스와 렌더러에 연결하고 소유합니다.
 * @details 렌더 스레드 객체는 처음 시작할 때 만들고, 백엔드 교체로 다시 시작할 때는 같은 객체에 붙입니다. 호스트가 건 프레젠트 훅과 렌더 스레드 포인터
 *          (GameModuleManager)가 그대로 남습니다. 전용 서버는 이 서브시스템을 돌리지 않으므로 객체도 만들지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    class FrameRendererSubsystem;
    class RenderThread;
    class RHISubsystem;

    /**
     * @class RenderThreadSubsystem
     * @brief 렌더 스레드를 시작하는 서브시스템입니다.
     */
    class SW_API RenderThreadSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 항목이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "RenderThread";

        /** @brief @p rhiSubsystem 과 @p frameRendererSubsystem 은 이것보다 오래 삽니다(같은 목록에 있고, 이것이 그들에 의존합니다). */
        RenderThreadSubsystem( const RHISubsystem& rhiSubsystem, const FrameRendererSubsystem& frameRendererSubsystem );
        ~RenderThreadSubsystem() override;

        RenderThreadSubsystem( const RenderThreadSubsystem& )            = delete;
        RenderThreadSubsystem& operator=( const RenderThreadSubsystem& ) = delete;

        SubsystemInitResult initializeSubsystem() override;
        /** @brief 남은 패킷을 기다린 뒤 스레드를 멈춥니다(객체는 다시 시작할 때 같은 것을 씁니다). */
        void                    shutdownSubsystem() override;
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
        SubsystemHostMask       getSubsystemHost() const override;
        MemoryTag               getSubsystemMemoryTag() const override { return MemoryTag::RenderCPU; }

        /** @brief 렌더 스레드입니다. 한 번도 시작하지 않았으면(전용 서버, 헤드리스) nullptr 입니다. */
        RenderThread* findRenderThread() const { return _renderThread.get(); }

    private:
        unique_ptr<RenderThread>      _renderThread;
        const RHISubsystem&           _rhiSubsystem;
        const FrameRendererSubsystem& _frameRendererSubsystem;
    };
} // namespace sw
