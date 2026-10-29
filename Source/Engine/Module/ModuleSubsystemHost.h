/**
 * @file Engine/Module/ModuleSubsystemHost.h
 * @brief 모듈 서브시스템(`SW_MODULE_SUBSYSTEM`)을 모듈 이름의 자식 범위로 세우고 걷는 자리, 그리고 모듈이 공개한 것만 보는 조회 창구입니다.
 * @details 엔진이 서비스 범위에 둔다(`Engine::initialize`). 모듈을 올리는 길(`engine::registerModuleTypes`)이 그 모듈의 등록자 사슬을 넘기면 고른 범위
 *          (프로세스 · 엔진)에 모듈 이름의 자식 범위를 매달아 서브시스템을 세우고, 내리는 길(`engine::unregisterModuleTypes`)이 그 자식 범위째 걷는다.
 *          Shipping 은 모듈을 내리지 않는다 — 자식 범위는 그것을 매단 범위가 사라질 때 함께 사라진다(`SubsystemScope::destroyAll` 이 자식을 먼저 지운다).
 *          조회 창구(`ISubsystemLocator`)는 매단 자식이 공개한 칸만 본다 — 모듈에 넘기는 서비스 범위가 이것을 부모로 두어(`GameModuleManager::fillModuleServiceScope`)
 *          게임 · 에디터 모듈이 호스트 전용 서비스는 못 보고 모듈이 공개한 구현(암호 제공자 …)만 찾게 한다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/SubsystemScope.h"

namespace sw
{
    class ModuleSubsystemRegistrar;

    /**
     * @class ModuleSubsystemHost
     * @brief 모듈 서브시스템의 자식 범위를 매달고 떼는 서브시스템이자, 매단 자식이 공개한 칸의 조회 창구입니다. 스레드: 모듈을 올리고 내리는 스레드만 부른다.
     */
    class SW_API ModuleSubsystemHost final : public ISubsystem, public ISubsystemLocator
    {
    public:
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "ModuleSubsystemHost";

        /**
         * @param processScope 프로세스 범위 — 범위 사슬의 뿌리입니다(엔진 기동 단계보다 오래 산다).
         * @param engineScope 엔진 범위(기동 단계)입니다.
         * @param hostMask 자식 범위의 서브시스템을 세울 호스트 종류입니다(엔진 범위와 같다).
         */
        ModuleSubsystemHost( SubsystemScope& processScope, SubsystemScope& engineScope, SubsystemHostMask hostMask );

        /**
         * @brief @p pHead 사슬의 등록자를 만든 순서로 걷어, 고른 범위마다 모듈 @p moduleName 의 자식 범위를 매달고 그 서브시스템을 세웁니다.
         * @return 자식 범위의 초기화가 실패했거나 같은 모듈의 자식 범위가 이미 있으면 false 입니다(오류를 남긴다 — 그 서브시스템을 찾는 쪽은 없다는 오류로 멈춘다).
         */
        [[nodiscard]] bool mountModule( string_view moduleName, const ModuleSubsystemRegistrar* pHead );
        /** @brief 모듈 @p moduleName 의 자식 범위를 모든 범위에서 떼고 지웁니다(그 서브시스템을 종료 · 역순 소멸). 없으면 아무것도 하지 않습니다. */
        void unmountModule( string_view moduleName );

        // ISubsystem — 세울 것이 없다(자식 범위는 매단 범위가 소유한다).
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        // ISubsystemLocator — 매단 자식이 공개한 칸만 본다(프로세스 범위의 자식이 먼저).
        void*                    findInScope( uint64 typeHash ) const override;
        const ISubsystemLocator* getParentScope() const override { return nullptr; }

    private:
        /** @brief @p scope 의 범위입니다. */
        SubsystemScope& resolveScope( uint8 scope ) const;

    private:
        SubsystemScope*   _pProcessScope; ///< 프로세스 범위(엔진보다 오래 산다)
        SubsystemScope*   _pEngineScope;  ///< 엔진 범위(엔진 기동 단계)
        SubsystemHostMask _hostMask;      ///< 자식 서브시스템을 세울 호스트 종류
    };
} // namespace sw
