/**
 * @file Engine/Renderer/SceneRHISubsystem.h
 * @brief 기동 순서에서 SceneRHI 를 맡는 서브시스템입니다. 씬 매니저에 디바이스를 연결합니다. 렌더러 쪽 서브시스템 가운데 종료가 가장 먼저라 씬에서 디바이스를 먼저 뗍니다.
 * @details 백엔드 교체 뒤 다시 시작하며 새 디바이스를 연결합니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    class IRHIDevice;
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

        SubsystemInitResult        initializeSubsystem() override;
        void                       shutdownSubsystem() override;
        const utf8*                getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const>    getSubsystemDependencies() const override;
        SubsystemHostMask          getSubsystemHost() const override;
        static constexpr MemoryTag kSubsystemMemoryTag = MemoryTag::RenderCPU;

        /**
         * @brief GPU 자원을 가진 에셋 캐시(텍스처 · 머티리얼 · 메시)에 올릴 디바이스를 붙이거나(nullptr 면) 뗍니다.
         * @details 기동 · 종료가 부르고, 백엔드 교체는 자원 다시 올리기(`RHIRenderResource::initAllFor`) **앞에서** 부른다 — 머티리얼을 다시 올리면
         *          텍스처를 요청하는데 이 단계는 그 뒤(`restartStopped`)에야 다시 선다. 붙지 않은 캐시는 요청을 실패시킨다(텍스처 머티리얼이 체커가 된다).
         */
        static void bindAssetCaches( IRHIDevice* pDevice );

    private:
        const RHISubsystem& _rhiSubsystem;
    };
} // namespace sw
