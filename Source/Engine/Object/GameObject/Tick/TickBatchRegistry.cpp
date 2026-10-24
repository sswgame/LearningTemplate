/**
 * @file TickBatchRegistry.cpp
 * @brief 타입별 일괄 틱 레지스트리 구현입니다(프로세스 전역 목록 · 세대).
 */
#include "pch.h"

#include "Engine/Object/GameObject/Tick/TickBatchRegistry.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 등록 줄의 연결 목록 머리와 그 잠금 · 세대입니다.
         * @details 모듈의 정적 초기화가 이 번역 단위의 초기화보다 먼저 등록할 수 있다(링크 때 등록 표). 그래서 지연 생성하는 접근자를 두지 않고, 줄이 등록 객체의 멤버인
         *          침입 목록과 상수 초기화되는 머리(할당 없음)로 둔다 — `RegistrationListBase` 의 전역 목록과 같은 모양이다. 엔진 범위가 서기 전에 올라가고 엔진을 다시 세워도
         *          남아야 하는 표라 엔진 수명 범위(`ScopeOwned`)의 소유로 옮길 수 없다.
         */
        struct TickBatchRegistryState
        {
            mutex                     _mutex;
            TickBatchRegistry::Entry* _pHead{ nullptr };
            uint32                    _count{ 0 }; ///< 오른 줄 수(`_mutex` 가 지킨다)
            atomic<uint32>            _generation{ 0 };
        };

        TickBatchRegistryState s_state;
    } // namespace
} // namespace sw

namespace sw
{
    void TickBatchRegistry::registerType( Entry& entry )
    {
        if ( entry._pStaticType == nullptr || entry._pFunction == nullptr )
            return;
        {
            std::scoped_lock<mutex> lock{ s_state._mutex };
            entry._pNext   = s_state._pHead;
            s_state._pHead = &entry;
            ++s_state._count;
        }
        s_state._generation.fetch_add( 1, std::memory_order_acq_rel );
    }

    void TickBatchRegistry::unregisterType( Entry& entry )
    {
        bool bRemoved = false;
        {
            std::scoped_lock<mutex> lock{ s_state._mutex };
            for ( Entry** ppLink = &s_state._pHead; *ppLink != nullptr; ppLink = &( *ppLink )->_pNext )
            {
                if ( *ppLink != &entry )
                    continue;
                *ppLink      = entry._pNext;
                entry._pNext = nullptr;
                --s_state._count;
                bRemoved = true;
                break;
            }
        }
        if ( bRemoved )
            s_state._generation.fetch_add( 1, std::memory_order_acq_rel );
    }

    TickBatchFunction TickBatchRegistry::findFunction( const TypeInfo* pType )
    {
        if ( pType == nullptr )
            return nullptr;
        std::scoped_lock<mutex> lock{ s_state._mutex };
        for ( const Entry* pEntry = s_state._pHead; pEntry != nullptr; pEntry = pEntry->_pNext )
        {
            // 주소가 아니라 지금의 `StaticType` 과 비교한다 — 모듈을 다시 올리면 타입 정보가 새로 만들어질 수 있다.
            if ( pEntry->_pStaticType() == pType )
                return pEntry->_pFunction;
        }
        return nullptr;
    }

    uint32 TickBatchRegistry::getGeneration()
    {
        return s_state._generation.load( std::memory_order_acquire );
    }

    uint32 TickBatchRegistry::getRegisteredCount()
    {
        std::scoped_lock<mutex> lock{ s_state._mutex };
        return s_state._count;
    }
} // namespace sw
