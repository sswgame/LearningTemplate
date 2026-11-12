/**
 * @file ObjectIDAllocator.h
 * @brief 오브젝트 id 와 컴포넌트 id 를 내는 번호 공간입니다. 엔진 범위가 하나 들고 그 엔진의 모든 씬 · 오브젝트 매니저가 같이 씁니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"

namespace sw
{
    /**
     * @class ObjectIDAllocator
     * @brief 오브젝트 id(`GameObject::getObjectID`) · 컴포넌트 id(`Component::getComponentID`)의 발급기입니다(서비스 범위의 서브시스템 — `EngineSubsystemSet`).
     * @details 씬을 넘어 옮긴 오브젝트(`SceneManager::markPersistent`) · 편집 씬에서 플레이 씬으로 복사한 오브젝트 · 세이브에서 되살린 오브젝트가 같은 id 를 유지하려면
     *          한 엔진의 매니저들이 한 공간을 나눠야 한다 — 그래서 매니저가 아니라 엔진 범위가 든다(`SceneEngineServices::_pObjectIDAllocator` 로 내려간다).
     *          예전에는 프로세스 정적 카운터였다 — 엔진을 다시 세우거나 테스트가 엔진을 여러 번 세우면 번호가 이어져, 같은 입력에서 다른 id 가 나왔다. 엔진 범위면 엔진마다
     *          1 부터라 같은 입력이 같은 id 를 낸다. 엔진 없이 만든 매니저(테스트)는 자기 공간을 든다(`GameObjectManager`). 언리얼 `GUObjectArray`(객체 번호 공간)가
     *          엔진 코어에 하나인 자리를 엔진 범위 서비스로 둔다. id 를 직접 정해 되살릴 때는 `reserve…` 가 공간을 그 뒤로 밀어 앞으로의 발급과 겹치지 않게 한다.
     *          워커(비동기 씬 로드)에서도 발급하므로 원자값이다.
     */
    class SW_API ObjectIDAllocator final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "ObjectIDAllocator";

        ObjectIDAllocator();

        // ISubsystem — 세울 것이 없다.
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 새 오브젝트 id 입니다(0 은 쓰지 않는다). */
        uint64 allocateObjectID() { return _nextObjectID.fetch_add( 1, std::memory_order_relaxed ); }
        /** @brief 정해 둔 오브젝트 id(세이브 · 복사본)를 쓰겠다고 알립니다. 공간을 그 뒤로 밀고 @p objectID 를 그대로 돌려줍니다. */
        uint64 reserveObjectID( uint64 objectID );
        /** @brief 새 컴포넌트 id 입니다(0 은 쓰지 않는다). */
        uint64 allocateComponentID() { return _nextComponentID.fetch_add( 1, std::memory_order_relaxed ); }
        /** @brief 정해 둔 컴포넌트 id(상태 복원)를 쓰겠다고 알립니다. 공간을 그 뒤로 밀고 @p componentID 를 그대로 돌려줍니다. */
        uint64 reserveComponentID( uint64 componentID );

    private:
        atomic<uint64> _nextObjectID;    ///< 다음 오브젝트 id
        atomic<uint64> _nextComponentID; ///< 다음 컴포넌트 id

        SW_SERVICE_SLOT( ObjectIDAllocator, Required );
    };
} // namespace sw
