/**
 * @file Engine/Renderer/SceneRHISubsystem.h
 * @brief 기동 순서에서 SceneRHI 를 맡는 서브시스템입니다. 씬 매니저에 디바이스를 연결합니다. 렌더러 쪽 서브시스템 가운데 종료가 가장 먼저라 씬에서 디바이스를 먼저 뗍니다.
 * @details 백엔드 교체 뒤 다시 시작하며 새 디바이스를 연결합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    class RHISubsystem;

    /**
     * @class SceneRHISubsystem
     * @brief 씬의 디바이스를 연결하는 서브시스템입니다.
     */
    class SW_API SceneRHISubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 항목이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "SceneRHI";

        /** @brief @p rhiSubsystem 은 이것보다 오래 삽니다(이것이 그것에 의존합니다). */
        explicit SceneRHISubsystem( const RHISubsystem& rhiSubsystem );

        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override;
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
        SubsystemHostMask       getSubsystemHost() const override;
        MemoryTag               getSubsystemMemoryTag() const override { return MemoryTag::RenderCPU; }

    private:
        const RHISubsystem& _rhiSubsystem;
    };
} // namespace sw
