/**
 * @file TickBatchRegistry.cpp
 * @brief 타입별 일괄 틱 등록부 구현입니다(프로세스 전역 목록 · 세대).
 */
#include "pch.h"

#include "Engine/Object/GameObject/TickBatchRegistry.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"

namespace sw
{
    namespace
    {
        struct TickBatchRegistryInternal
        {
            /** @brief 등록 하나입니다. */
            struct Entry
            {
                TickBatchRegistry::StaticTypeFunction _pStaticType{ nullptr };
                TickBatchFunction                     _pFunction{ nullptr };
            };

            /** @brief 등록 목록과 그 잠금 · 세대입니다. 모듈의 정적 초기화가 먼저 부를 수 있어 함수 지역 정적으로 둡니다. */
            struct State
            {
                mutex          _mutex;
                vector<Entry>  _listEntry;
                atomic<uint32> _generation{ 0 };
            };

            static State& getState()
            {
                static State s_state;
                return s_state;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void TickBatchRegistry::registerType( StaticTypeFunction pStaticType, TickBatchFunction pFunction )
    {
        if ( pStaticType == nullptr || pFunction == nullptr )
            return;
        TickBatchRegistryInternal::State& state = TickBatchRegistryInternal::getState();
        {
            std::scoped_lock<mutex> lock{ state._mutex };
            bool                    bReplaced = false;
            for ( TickBatchRegistryInternal::Entry& entry : state._listEntry )
            {
                if ( entry._pFunction != pFunction )
                    continue;
                entry._pStaticType = pStaticType;
                bReplaced          = true;
                break;
            }
            if ( bReplaced == false )
                state._listEntry.push_back( TickBatchRegistryInternal::Entry{ pStaticType, pFunction } );
        }
        state._generation.fetch_add( 1, std::memory_order_acq_rel );
    }

    void TickBatchRegistry::unregisterType( TickBatchFunction pFunction )
    {
        TickBatchRegistryInternal::State& state = TickBatchRegistryInternal::getState();
        {
            std::scoped_lock<mutex> lock{ state._mutex };
            for ( size_t index = 0; index < state._listEntry.size(); ++index )
            {
                if ( state._listEntry[index]._pFunction != pFunction )
                    continue;
                state._listEntry.erase( state._listEntry.begin() + static_cast<ptrdiff_t>( index ) );
                break;
            }
        }
        state._generation.fetch_add( 1, std::memory_order_acq_rel );
    }

    TickBatchFunction TickBatchRegistry::findFunction( const TypeInfo* pType )
    {
        if ( pType == nullptr )
            return nullptr;
        TickBatchRegistryInternal::State& state = TickBatchRegistryInternal::getState();
        std::scoped_lock<mutex>           lock{ state._mutex };
        for ( const TickBatchRegistryInternal::Entry& entry : state._listEntry )
        {
            // 주소가 아니라 지금의 `StaticType` 과 견준다 — 모듈을 다시 올리면 타입 정보가 새로 만들어질 수 있다.
            if ( entry._pStaticType() == pType )
                return entry._pFunction;
        }
        return nullptr;
    }

    uint32 TickBatchRegistry::getGeneration()
    {
        return TickBatchRegistryInternal::getState()._generation.load( std::memory_order_acquire );
    }

    uint32 TickBatchRegistry::getRegisteredCount()
    {
        TickBatchRegistryInternal::State& state = TickBatchRegistryInternal::getState();
        std::scoped_lock<mutex>           lock{ state._mutex };
        return static_cast<uint32>( state._listEntry.size() );
    }
} // namespace sw
