/**
 * @file ModuleRenderPassRegistry.h
 * @brief 모듈(게임 · 키트)이 렌더 스레드 확장(`IModuleRenderExtension`)과 씬 프록시(`IModuleSceneProxy`)를 등록하는 창구입니다.
 * @details 계약은 `ModuleRenderExtension.h` 에 있습니다. 이 레지스트리는 다음을 맡습니다.
 *          - **등록 목록**: 확장 객체(소유하지 않음), 프록시(소유함), 프록시 갱신 대기열.
 *          - **flush**: 등록을 푸는 함수(`unregisterExtension` · `removeProxy`)는 렌더 스레드의 모듈 구간(`RenderScope`)이 끝나기를 기다린 뒤에야 목록을 바꾼다. 그래서 풀린 뒤에는 렌더 스레드가
 *            그 모듈 코드를 다시 부르지 않고, 모듈이 내려가기 전(등록 객체의 소멸자)에 프록시가 모듈 안에서 소멸한다(언리얼 Live Coding 이 모듈을 내리기 전에 렌더링을 flush 하는 것과 같다).
 *          잠금 순서는 늘 `_runMutex` → `_mutex` 입니다. 렌더 스레드는 `RenderScope` 로 `_runMutex` 를 프레임의 모듈 구간 내내 쥐고 목록은 `_mutex` 로 잠깐만 읽습니다 —
 *          그래서 게임 스레드의 등록 · 갱신 올리기(`_mutex` 만)는 렌더 스레드를 막지 않는다.
 *          **모듈 구간 안(확장 · 프록시 · 실행 콜백)에서 이 레지스트리의 등록 해제 함수를 부르면 교착합니다.**
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Renderer/Module/ModuleRenderExtension.h"

namespace sw
{
    /** @brief 프록시 하나의 번호입니다. 0 은 등록 실패입니다. */
    using ModuleSceneProxyId = uint32;
    /** @brief 프록시 갱신 함수입니다. 게임 스레드가 값을 캡처해 올리고, 렌더 스레드가 프레임 시작에 프록시에 적용합니다. */
    using ModuleProxyUpdateFn = Delegate<void( IModuleSceneProxy& )>;

    /**
     * @class ModuleRenderPassRegistry
     * @brief 엔진 서비스 범위의 서브시스템입니다. 엔진이 소유하므로 모듈이 내려가도 사라지지 않습니다.
     * @details 렌더러는 `engine::getSubsystem<ModuleRenderPassRegistry>()`, 모듈은 `game::findSubsystem<ModuleRenderPassRegistry>()` 로 찾습니다(`EngineServiceList.xxx` 의 GameVisible 행).
     */
    class SW_API ModuleRenderPassRegistry : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. 초기화하거나 종료할 것이 없고, 수명만 컬렉션이 정합니다. */
        static constexpr const utf8* kSubsystemName = "ModuleRenderPass";

        /**
         * @class RenderScope
         * @brief 렌더 스레드가 프레임의 모듈 구간(확장 훅 → 기록 → 콜백 정리) 동안 잡는 잠금입니다. 이 구간 안에서만 모듈 코드가 불립니다.
         */
        class RenderScope
        {
        public:
            explicit RenderScope( ModuleRenderPassRegistry& registry )
                : _lock{ registry._runMutex }
            {
            }
            RenderScope( const RenderScope& )            = delete;
            RenderScope& operator=( const RenderScope& ) = delete;

        private:
            ScopedLock<mutex> _lock;
        };

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }
        MemoryTag           getSubsystemMemoryTag() const override { return MemoryTag::RenderCPU; }

        /** @brief 확장 객체를 올립니다(소유하지 않는다 — 객체는 등록을 풀 때까지 살아 있어야 한다). nullptr 이거나 이미 있으면 false 입니다. */
        [[nodiscard]] bool registerExtension( IModuleRenderExtension* pExtension );
        /** @brief 확장 객체를 풉니다. 렌더 스레드의 모듈 구간이 끝나기를 기다린 뒤 뺍니다(flush). 없으면 아무것도 하지 않습니다. */
        void unregisterExtension( IModuleRenderExtension* pExtension );

        /** @brief 프록시를 올리고(소유권을 넘긴다) 번호를 돌려줍니다. nullptr 이면 0 입니다. 렌더 스레드는 다음 프레임부터 봅니다. */
        [[nodiscard]] ModuleSceneProxyId addProxy( unique_ptr<IModuleSceneProxy> pProxy );
        /** @brief 프록시를 풀고 소멸시킵니다. flush 한 뒤이므로 소멸자는 호출한 모듈 안에서 돕니다. 그 프록시에 대기 중인 갱신도 버립니다. 없는 번호면 아무것도 하지 않습니다. */
        void removeProxy( ModuleSceneProxyId id );
        /** @brief 프록시 갱신을 올립니다. 렌더 스레드가 다음 프레임 시작에 적용합니다. 없는 번호이거나 함수가 비어 있으면 false 입니다. */
        [[nodiscard]] bool enqueueProxyUpdate( ModuleSceneProxyId id, ModuleProxyUpdateFn update );

        // --- 아래는 렌더 스레드가 `RenderScope` 안에서만 부릅니다. ---

        /** @brief 대기 중인 프록시 갱신을 올린 순서대로 적용하고 비웁니다. */
        void applyPendingProxyUpdates();
        /** @brief 등록된 확장 포인터를 복사합니다(`RenderScope` 가 살아 있는 동안 유효하다). */
        void copyExtensions( vector<IModuleRenderExtension*>& outList ) const;
        /** @brief 등록된 프록시 포인터를 번호 순서로 복사합니다(`RenderScope` 가 살아 있는 동안 유효하다). */
        void copyProxies( vector<IModuleSceneProxy*>& outList ) const;
        /** @brief 그릴 수 없는 모듈 패스 · 요소의 오류를 키마다 한 번만 남기게 합니다. 처음이면 true 입니다. 키는 호출한 쪽이 정한 글입니다. */
        bool noteFailureOnce( string_view key );

    private:
        struct ProxyEntry
        {
            ModuleSceneProxyId            _id{ 0 };
            unique_ptr<IModuleSceneProxy> _pProxy;
        };
        struct PendingUpdate
        {
            ModuleSceneProxyId  _id{ 0 };
            ModuleProxyUpdateFn _update;
        };

        mutable mutex                                     _runMutex;
        mutable mutex                                     _mutex;
        vector<IModuleRenderExtension*> _listExtension    SW_GUARDED_BY( _mutex );
        vector<ProxyEntry> _listProxy                     SW_GUARDED_BY( _mutex );
        vector<PendingUpdate> _listPendingUpdate          SW_GUARDED_BY( _mutex );
        ModuleSceneProxyId _nextProxyId                   SW_GUARDED_BY( _mutex ) = 1;
        unordered_set<hashed_string> _uniqueFailureLogged SW_GUARDED_BY( _mutex );
    };
} // namespace sw

namespace sw
{
    /**
     * @class ModuleRenderExtensionRegistration
     * @brief 확장 등록 하나의 수명입니다. 만들 때 올리고 소멸할 때 풉니다(flush 포함) — 모듈이 내려갈 때 렌더 스레드에 모듈 코드가 남지 않게 하는 형식입니다.
     * @details 모듈의 게임 인스턴스 · 서브시스템 멤버로 둡니다. @p extension 은 이 객체보다 오래 살아야 합니다(멤버 선언 순서로 보장한다 — 확장 객체를 먼저 선언).
     */
    class SW_API ModuleRenderExtensionRegistration
    {
    public:
        ModuleRenderExtensionRegistration( ModuleRenderPassRegistry& registry, IModuleRenderExtension& extension );
        ~ModuleRenderExtensionRegistration();

        ModuleRenderExtensionRegistration( const ModuleRenderExtensionRegistration& )            = delete;
        ModuleRenderExtensionRegistration& operator=( const ModuleRenderExtensionRegistration& ) = delete;

        bool isRegistered() const { return _bRegistered != SW_FALSE; }

    private:
        ModuleRenderPassRegistry& _registry;
        IModuleRenderExtension*   _pExtension;
        uint8                     _bRegistered;
    };

    /**
     * @class ModuleSceneProxyRegistration
     * @brief 프록시 등록 하나의 수명입니다. 만들 때 프록시를 올리고(소유권을 넘긴다) 소멸할 때 flush 하고 프록시를 소멸시킵니다.
     */
    class SW_API ModuleSceneProxyRegistration
    {
    public:
        ModuleSceneProxyRegistration( ModuleRenderPassRegistry& registry, unique_ptr<IModuleSceneProxy> pProxy );
        ~ModuleSceneProxyRegistration();

        ModuleSceneProxyRegistration( const ModuleSceneProxyRegistration& )            = delete;
        ModuleSceneProxyRegistration& operator=( const ModuleSceneProxyRegistration& ) = delete;

        bool isRegistered() const { return _id != 0; }
        /** @brief 프록시 갱신을 올립니다(값을 캡처한 함수). 렌더 스레드가 다음 프레임 시작에 프록시에 적용합니다. 등록하지 못한 등록이면 false 입니다. */
        [[nodiscard]] bool update( ModuleProxyUpdateFn update );

    private:
        ModuleRenderPassRegistry& _registry;
        ModuleSceneProxyId        _id;
    };
} // namespace sw
