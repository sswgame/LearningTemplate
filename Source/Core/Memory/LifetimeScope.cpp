#include "pch.h"

#include "Core/Memory/LifetimeScope.h"

namespace sw
{
    namespace
    {
        /** @brief 전역 범위입니다. 상수 초기화되고 소멸자가 하는 일이 없어 정적 소멸 순서와 무관하게 쓸 수 있습니다. */
        LifetimeScope s_globalLifetimeScope;
        static_assert( std::is_trivially_destructible_v<LifetimeScope>, "The global lifetime scope must outlive every static that detaches from it" );
    } // namespace
} // namespace sw

namespace sw
{
    LifetimeScope& LifetimeScope::getGlobal()
    {
        return s_globalLifetimeScope;
    }

    void LifetimeScope::close()
    {
        LifetimeScopeEntry* pEntry = nullptr;
        {
            ScopedLock<SpinLock> lock{ _lock };
            const bool           bWasOpen = _bClosed.load( std::memory_order_relaxed ) == false;
            SW_ASSERT( bWasOpen );
            if ( bWasOpen == false )
                return;
            _bClosed.store( true, std::memory_order_release );
            pEntry = _pHead;
            _pHead = nullptr;
        }
        // 락 밖에서 소멸시킨다 — 값의 소멸자가 다른 값을 찾아도(`find`) 멈추지 않는다.
        while ( pEntry != nullptr )
        {
            LifetimeScopeEntry* const pNext = pEntry->_pNext;
            pEntry->_pNext                  = nullptr;
            pEntry->_pDestroyValue( *pEntry );
            pEntry = pNext;
        }
    }

    void LifetimeScope::open()
    {
        ScopedLock<SpinLock> lock{ _lock };
        // 닫을 때 목록을 비웠다 — 다시 여는 범위는 빈 범위다.
        SW_ASSERT( _pHead == nullptr || _bClosed.load( std::memory_order_relaxed ) == false );
        _bClosed.store( false, std::memory_order_release );
    }

    bool LifetimeScope::attach( LifetimeScopeEntry& entry )
    {
        ScopedLock<SpinLock> lock{ _lock };
        if ( _bClosed.load( std::memory_order_relaxed ) )
            return false;
        entry._pNext = _pHead;
        _pHead       = &entry;
        return true;
    }

    void LifetimeScope::detach( LifetimeScopeEntry& entry )
    {
        ScopedLock<SpinLock> lock{ _lock };
        LifetimeScopeEntry** ppLink = &_pHead;
        while ( *ppLink != nullptr )
        {
            if ( *ppLink == &entry )
            {
                *ppLink      = entry._pNext;
                entry._pNext = nullptr;
                return;
            }
            ppLink = &( *ppLink )->_pNext;
        }
    }
} // namespace sw
