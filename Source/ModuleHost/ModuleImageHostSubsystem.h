/**
 * @file ModuleHost/ModuleImageHostSubsystem.h
 * @brief 호스트 범위의 서브시스템입니다 — 엔진의 모듈 이미지 창구(`IModuleImageHost`)를 호스트가 정한 두 델리게이트로 구현합니다.
 * @details App 과 Server 가 같은 모양으로 씁니다. 엔진 범위가 서기 전에 호스트 범위에 걸어 두고, 엔진의 `ModuleTypes` · `ModuleImages` 단계가 부모 범위에서 찾아 부릅니다.
 *          로더(타입 공급자 이미지 로드)는 만들 때 받고, 씬 뒤 훅(모듈 DLL 내리기)은 모듈 매니저가 선 뒤에 걸 수 있다(Dev 전용이라 Shipping 은 걸지 않는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Module/ModuleImageHost.h"

namespace sw
{
    /** @brief 호스트의 타입 공급자 로더입니다. 이미지를 올리고 타입을 등록하면 true 입니다. */
    SW_DECLARE_DELEGATE( bool, ModuleTypeLoaderDelegate, void );
} // namespace sw

namespace sw
{
    /**
     * @class ModuleImageHostSubsystem
     * @brief 호스트 범위에서 모듈 이미지 창구를 맡는 서브시스템입니다.
     */
    class ModuleImageHostSubsystem final : public ISubsystem, public IModuleImageHost
    {
    public:
        /** @brief 서브시스템 이름입니다(같은 범위의 다른 서브시스템이 의존 칸에 적는다). */
        static constexpr const utf8* kSubsystemName = "ModuleImageHost";

        /** @brief @p moduleTypeLoader 는 타입 공급자 로더입니다. 묶이지 않았으면 올릴 것이 없습니다. */
        explicit ModuleImageHostSubsystem( ModuleTypeLoaderDelegate moduleTypeLoader );

        /** @brief 씬이 사라진 뒤 불릴 훅(모듈 DLL 내리기)을 겁니다. 엔진 범위가 종료하기 전에 겁니다. */
        void setScenesReleasedHook( Delegate<void()> hook );

        [[nodiscard]] bool loadTypeProviders() override;
        void               onScenesReleased() override;

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }
        /**
         * @brief 이 창구보다 뒤에 서야 하는 호스트 범위 서브시스템 — 자동화 구동기입니다(시나리오는 모듈이 올라온 뒤에 시작한다).
         * @details 그래서 `shutdownDependentsOf( 이 서브시스템 )` 이 구동기만 엔진 종료 앞에서 내린다. 이 창구는 엔진 범위가 종료하는 동안(씬 뒤 훅) 살아 있어야 한다.
         */
        span<const utf8* const> getSubsystemDependents() const override;

    private:
        ModuleTypeLoaderDelegate _moduleTypeLoader;
        Delegate<void()>         _scenesReleasedHook;
    };
} // namespace sw
