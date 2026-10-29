/**
 * @file TickBatchRegistry.cpp
 * @brief 타입별 일괄 틱 레지스트리 구현입니다(프로세스 전역 목록 · 세대).
 */
#include "pch.h"

#include "Engine/Object/GameObject/Tick/TickBatchRegistry.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/String/RegistrationList.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 등록 줄 목록과 그 잠금 · 세대입니다.
         * @details 모듈의 정적 초기화가 이 번역 단위의 초기화보다 먼저 등록할 수 있다(링크 때 등록 표). 그래서 함수 안 정적으로 두어 첫 등록이 만든다 — 정적 등록부는 모두
         *          `RegistrationList` 하나로 쓴다(`BuiltInAssetCacheRegistry` 와 같은 모양). 엔진 범위가 서기 전에 올라가고 엔진을 다시 세워도 남아야 하는 표라
         *          엔진 수명 범위(`ScopeOwned`)의 소유로 옮길 수 없다. 조회는 나중에 오른 줄이 먼저다(같은 타입의 테스트 지역 등록이 정적 등록을 덮는다).
         */
        struct TickBatchRegistryState
        {
            mutex                                                 _mutex;
            RegistrationList<TickBatchRegistry::Entry> _listEntry SW_GUARDED_BY( _mutex ){ "TickBatchRegistry" };
            atomic<uint32>                                        _generation{ 0 };
        };

        TickBatchRegistryState& getTickBatchRegistryState()
        {
            static TickBatchRegistryState s_state;
            return s_state;
        }
    } // namespace
} // namespace sw

namespace sw
{
    void TickBatchRegistry::registerType( Entry& entry )
    {
        if ( entry._pStaticType == nullptr || entry._pFunction == nullptr )
            return;
        TickBatchRegistryState& state = getTickBatchRegistryState();
        {
            ScopedLock<mutex> lock{ state._mutex };
            (void)state._listEntry.addLogged( &entry, "Tick batch entry" ); // 같은 줄을 다시 올리면 `AlreadyPresent` 라 할 일이 없다
        }
        state._generation.fetch_add( 1, std::memory_order_acq_rel );
    }

    void TickBatchRegistry::unregisterType( Entry& entry )
    {
        TickBatchRegistryState& state    = getTickBatchRegistryState();
        bool                    bRemoved = false;
        {
            ScopedLock<mutex> lock{ state._mutex };
            bRemoved = state._listEntry.remove( &entry );
        }
        if ( bRemoved )
            state._generation.fetch_add( 1, std::memory_order_acq_rel );
    }

    TickBatchFunction TickBatchRegistry::findFunction( const TypeInfo* pType )
    {
        if ( pType == nullptr )
            return nullptr;
        TickBatchRegistryState& state = getTickBatchRegistryState();
        ScopedLock<mutex>       lock{ state._mutex };
        const vector<Entry*>&   listEntry = state._listEntry.getItems();
        for ( size_t index = listEntry.size(); index > 0; --index )
        {
            // 주소가 아니라 지금의 `StaticType` 과 비교한다 — 모듈을 다시 올리면 타입 정보가 새로 만들어질 수 있다.
            const Entry* const pEntry = listEntry[index - 1];
            if ( pEntry->_pStaticType() == pType )
                return pEntry->_pFunction;
        }
        return nullptr;
    }

    uint32 TickBatchRegistry::getGeneration()
    {
        return getTickBatchRegistryState()._generation.load( std::memory_order_acquire );
    }

    uint32 TickBatchRegistry::getRegisteredCount()
    {
        TickBatchRegistryState& state = getTickBatchRegistryState();
        ScopedLock<mutex>       lock{ state._mutex };
        return state._listEntry.getCount();
    }
} // namespace sw
