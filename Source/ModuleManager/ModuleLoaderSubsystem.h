/**
 * @file ModuleManager/ModuleLoaderSubsystem.h
 * @brief 호스트 범위의 서브시스템입니다 — 엔진의 모듈 라이브러리 창구(`IModuleLoader`)를 호스트가 정한 두 델리게이트로 구현하고, 모듈 맡김 칸(`IModuleStateStore`)을 듭니다.
 * @details App 과 Server 가 같은 모양으로 씁니다. 엔진 범위가 서기 전에 호스트 범위에 걸어 두고, 엔진의 `ModuleLoadStage` · `ModuleUnloadStage` 단계가 부모 범위에서 찾아 부릅니다.
 *          로더(타입 공급자 라이브러리 로드)는 만들 때 받고, 씬 뒤 훅(모듈 DLL 내리기)은 모듈 매니저가 선 뒤에 걸 수 있다(Dev 전용이라 Shipping 은 걸지 않는다).
 */
#pragma once
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/DelegateSlot.h"
#include "Core/Foundation/Macros.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Foundation/HostSubsystemNames.h"
#include "Engine/Module/HostInterfaces.h"
#include "Engine/Module/ModuleCatalog.h"

#include "ModuleAPI/Service/IModuleStateStore.h"

#include "ModuleManager/ModuleManagerAPI.h"

/*
 * `IModuleStateStore` 의 호스트 구현입니다 — 프로세스 동안 이름 붙은 바이트 묶음을 든다.
 */
namespace sw
{
    /** @brief 맡김 칸입니다. 호스트가 소유하고 호스트 범위에 `IModuleStateStore` 로 건다. */
    class SW_MODULEMANAGER_API ModuleStateStore final : public IModuleStateStore
    {
    public:
        void               store( string_view key, vector<uint8>&& bytes ) override;
        [[nodiscard]] bool take( string_view key, vector<uint8>& outBytes ) override;

    private:
        map<string, vector<uint8>> _mapStored;
    };
} // namespace sw

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
    class SW_MODULEMANAGER_API ModuleLoaderSubsystem final : public IModuleLoader
    {
    public:
        /** @brief 서브시스템 이름입니다(같은 범위의 다른 서브시스템이 의존 칸에 적는다). */
        static constexpr const utf8* kSubsystemName = HostSubsystemNames::kModuleLoader;

        /** @brief @p moduleTypeLoader 는 타입 공급자 로더입니다. 묶이지 않았으면 올릴 것이 없습니다. */
        explicit ModuleLoaderSubsystem( ModuleTypeLoaderDelegate moduleTypeLoader );

        /**
         * @brief 호스트 범위를 세웁니다 — 이 창구를 @p hostScope 에 더해 `IModuleLoader` 로, 그 맡김 칸을 `IModuleStateStore` 로 드러내고, 범위의 서브시스템을 모두 초기화합니다.
         *        엔진을 세우기 전에 부릅니다.
         * @details App 과 Server 가 같은 순서로 세우는 곳은 여기 하나입니다. 호스트가 더 넣을 서브시스템(App 의 자동화 구동기)은 이 함수 **전에** 범위에 더합니다.
         *          로거가 서기 전이라 실패는 표준 오류에 @p pHostName 을 붙여 남깁니다.
         * @return 세운 창구(범위 소유 — 빌려 쓴다)입니다. 초기화에 실패하면 nullptr 입니다.
         */
        [[nodiscard]] static ModuleLoaderSubsystem* initializeHostScope( SubsystemScope& hostScope, ModuleTypeLoaderDelegate moduleTypeLoader, const utf8* pHostName );

        /** @brief 씬이 사라진 뒤 불릴 훅(모듈 DLL 내리기)을 겁니다. 엔진 범위가 종료하기 전에 겁니다. 주인은 하나다(이미 걸려 있으면 단언). */
        [[nodiscard]] Subscription bindScenesReleasedHook( Delegate<void()> hook );

        /** @brief 호스트가 해석한 모듈 목록을 받습니다(`ModuleCatalogLoader::loadAndResolve` 뒤). `isModuleActive` 가 이것을 본다. */
        void setResolution( const ModuleResolution& resolution ) { _resolution = resolution; }

        [[nodiscard]] bool loadTypeProviders() override;
        void               onScenesReleased() override;
        [[nodiscard]] bool isModuleActive( string_view name ) const override { return _resolution.isActive( name ); }

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

    private:
        ModuleTypeLoaderDelegate       _moduleTypeLoader;
        DelegateSlot<Delegate<void()>> _scenesReleasedHook;
        ModuleResolution               _resolution;
        /** @brief 모듈이 리로드 · 인스턴스 재생성 사이에 데이터를 맡기는 칸입니다(편집기의 되돌리기 기록 · 선택, 게임 상태). 예전에는 App 만 걸어 Server 의 모듈은 넘기지 못했다. */
        ModuleStateStore _stateStore;
    };
} // namespace sw
