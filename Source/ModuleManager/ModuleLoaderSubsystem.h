/**
 * @file ModuleManager/ModuleLoaderSubsystem.h
 * @brief 호스트 범위의 서브시스템입니다 — 엔진의 모듈 라이브러리 창구(`IModuleLoader`)를 호스트가 정한 두 델리게이트로 구현합니다.
 * @details App 과 Server 가 같은 모양으로 씁니다. 엔진 범위가 서기 전에 호스트 범위에 걸어 두고, 엔진의 `ModuleLoadStage` · `ModuleUnloadStage` 단계가 부모 범위에서 찾아 부릅니다.
 *          로더(타입 공급자 라이브러리 로드)는 만들 때 받고, 씬 뒤 훅(모듈 DLL 내리기)은 모듈 매니저가 선 뒤에 걸 수 있다(Dev 전용이라 Shipping 은 걸지 않는다).
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/DelegateSlot.h"
#include "Core/Foundation/Macros.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Foundation/HostSubsystemNames.h"
#include "Engine/Module/HostServices.h"

namespace sw
{
    class SubsystemScope;

    /** @brief 호스트의 타입 공급자 로더입니다. 라이브러리를 올리고 타입을 등록하면 true 입니다. */
    SW_DECLARE_DELEGATE( bool, ModuleTypeLoaderDelegate, void );
} // namespace sw

namespace sw
{
    /**
     * @class ModuleLoaderSubsystem
     * @brief 호스트 범위에서 모듈 라이브러리 창구를 맡는 서브시스템입니다.
     */
    class ModuleLoaderSubsystem final : public IModuleLoader
    {
    public:
        /** @brief 서브시스템 이름입니다(같은 범위의 다른 서브시스템이 의존 칸에 적는다). */
        static constexpr const utf8* kSubsystemName = HostSubsystemNames::kModuleLoader;

        /** @brief @p moduleTypeLoader 는 타입 공급자 로더입니다. 묶이지 않았으면 올릴 것이 없습니다. */
        explicit ModuleLoaderSubsystem( ModuleTypeLoaderDelegate moduleTypeLoader );

        /**
         * @brief 호스트 범위를 세웁니다 — 이 창구를 @p hostScope 에 더해 `IModuleLoader` 로 드러내고, 범위의 서브시스템을 모두 초기화합니다. 엔진을 세우기 전에 부릅니다.
         * @details App 과 Server 가 같은 순서로 세우는 곳은 여기 하나입니다. 호스트가 더 넣을 서브시스템(App 의 자동화 구동기)은 이 함수 **전에** 범위에 더합니다.
         *          로거가 서기 전이라 실패는 표준 오류에 @p pHostName 을 붙여 남깁니다.
         * @return 세운 창구(범위 소유 — 빌려 쓴다)입니다. 초기화에 실패하면 nullptr 입니다.
         */
        [[nodiscard]] static ModuleLoaderSubsystem* initializeHostScope( SubsystemScope& hostScope, ModuleTypeLoaderDelegate moduleTypeLoader, const utf8* pHostName );

        /** @brief 씬이 사라진 뒤 불릴 훅(모듈 DLL 내리기)을 겁니다. 엔진 범위가 종료하기 전에 겁니다. 주인은 하나다(이미 걸려 있으면 단언). */
        [[nodiscard]] Subscription bindScenesReleasedHook( Delegate<void()> hook );

        [[nodiscard]] bool loadTypeProviders() override;
        void               onScenesReleased() override;

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

    private:
        ModuleTypeLoaderDelegate       _moduleTypeLoader;
        DelegateSlot<Delegate<void()>> _scenesReleasedHook;
    };
} // namespace sw
