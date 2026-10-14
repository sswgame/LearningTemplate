/**
 * @file Engine/Module/ModuleTypesSubsystem.h
 * @brief 기동 순서의 ModuleTypes 자리입니다 — 호스트가 타입 공급자(GameFramework · 킷 · 게임 모듈)를 올려 등록을 끝냅니다. 씬을 읽는 일은 이 뒤다.
 * @details 로더가 없으면 올릴 것이 없다(배포 구성은 정적 링크라 Reflection 이 이미 다 모았다). 어느 쪽이든 끝나면 모든 타입이 등록된 것으로 적고
 *          (`TypeRegistry::markAllModuleTypesRegistered`), 그때부터 씬을 읽을 수 있다 — 헤드리스 쿠킹이 이것에 의존한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    /**
     * @brief 호스트가 동적으로 올리는 타입 공급자(GameFramework · 킷 · 게임 모듈)를 올려 그 타입을 등록합니다. 실패하면 false 입니다.
     * @details 기동 단계 `ModuleTypes` 가 부릅니다. 엔진 루프가 받고(`Engine::setModuleTypeLoader`) 이 서브시스템이 부른다.
     */
    SW_DECLARE_DELEGATE( bool, ModuleTypeLoaderDelegate, void );
} // namespace sw

namespace sw
{
    /**
     * @class ModuleTypesSubsystem
     * @brief 타입 공급자 모듈을 올려 타입 등록을 끝내는 서브시스템입니다.
     */
    class SW_API ModuleTypesSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "ModuleTypes";

        /** @brief @p pModuleTypeLoader 는 호스트의 로더입니다(호스트 소유 — 이것보다 오래 산다). nullptr 이거나 묶이지 않았으면 올릴 것이 없습니다. */
        explicit ModuleTypesSubsystem( const ModuleTypeLoaderDelegate* pModuleTypeLoader = nullptr );

        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override {}
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
        MemoryTag               getSubsystemMemoryTag() const override { return MemoryTag::Reflection; }

    private:
        const ModuleTypeLoaderDelegate* _pModuleTypeLoader;
    };
} // namespace sw
