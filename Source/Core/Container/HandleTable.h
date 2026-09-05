/**
 * @file HandleTable.h
 * @brief ObjectHandle로 T 슬롯을 보관합니다. 핸들 기본값은 무효입니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/ObjectHandle.h"
#include "Core/Container/PagedArray.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @brief T 슬롯을 generation과 함께 보관합니다.
     * @details 슬롯 저장소로 `PagedArray`를 쓰기 때문에 슬롯이 늘어나도 이미 돌려준 원소 주소가
     *          변하지 않는다. 예전엔 `vector`라서, 한 스레드가 `insert()`로 재할당을 일으키면 다른
     *          스레드가 `get()`으로 받아둔 포인터가 그대로 dangling 이 됐다(RHI 백엔드에서 렌더
     *          스레드가 리소스를 그리는 동안 게임 스레드가 리소스를 만들면 바로 이 상황이 된다).
     *
     *          **동시성 계약**: `get`/`size`/`empty`는 락 없이 동시 호출해도 안전하다(드로우마다
     *          불리는 뜨거운 경로라 의도적으로 락을 두지 않았다). 구조를 바꾸는
     *          `insert`/`erase`/`take`/`clear`/`forEach*`는 내부 뮤텍스로 서로 직렬화된다.
     *          다만 "쓰는 중인 슬롯의 값을 동시에 읽는 것"까지 막아주지는 않는다 — 사용 중인 자원을
     *          파괴하지 않는 책임(예: GPU 펜스 기반 지연 해제)은 상위 계층에 있다.
     */
    template <typename T>
    class HandleTable
    {
    public:
        /** @brief 값을 넣고 핸들을 반환합니다. */
        ObjectHandle insert( T value )
        {
            std::scoped_lock<mutex> lock{ _mutex };

            if ( _listFree.empty() == false )
            {
                const uint32 index = _listFree.back();
                _listFree.pop_back();
                Slot* pSlot = _listSlot.at( index );
                if ( pSlot == nullptr )
                    return ObjectHandle{};
                // 세대는 retireSlot 에서 이미 올려뒀으므로 그대로 쓴다 — 옛 핸들은 계속 무효.
                pSlot->_value = std::move( value );
                pSlot->_bOccupied.store( true, std::memory_order_release );
                return ObjectHandle::make( index, pSlot->_generation.load( std::memory_order_relaxed ) );
            }

            const uint32 index = _listSlot.pushBack( Slot{} );
            if ( index == decltype( _listSlot )::kInvalidIndex )
                return ObjectHandle{};

            Slot* pSlot   = _listSlot.at( index );
            pSlot->_value = std::move( value );
            pSlot->_generation.store( 1u, std::memory_order_relaxed );
            pSlot->_bOccupied.store( true, std::memory_order_release );
            return ObjectHandle::make( index, 1u );
        }

        /** @brief 핸들이 유효하면 슬롯 포인터, 아니면 nullptr. 락이 없습니다. */
        T* get( ObjectHandle handle ) { return const_cast<T*>( static_cast<const HandleTable*>( this )->get( handle ) ); }

        /** @brief 핸들이 유효하면 슬롯 포인터, 아니면 nullptr. 락이 없습니다. */
        const T* get( ObjectHandle handle ) const
        {
            if ( handle.isValid() == false )
                return nullptr;
            const Slot* pSlot = _listSlot.at( handle.index() );
            if ( pSlot == nullptr )
                return nullptr;
            if ( pSlot->_bOccupied.load( std::memory_order_acquire ) == false ||
                 pSlot->_generation.load( std::memory_order_acquire ) != handle.generation() )
                return nullptr;
            return std::addressof( pSlot->_value );
        }

        /** @brief 슬롯을 비우고 generation을 올립니다. 꺼낸 값을 반환합니다. */
        bool take( ObjectHandle handle, T& outValue )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            Slot*                   pSlot = findOccupiedSlot( handle );
            if ( pSlot == nullptr )
                return false;
            outValue = std::move( pSlot->_value );
            retireSlot( handle.index(), *pSlot );
            return true;
        }

        /** @brief 핸들을 무효화합니다. */
        void erase( ObjectHandle handle )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            Slot*                   pSlot = findOccupiedSlot( handle );
            if ( pSlot == nullptr )
                return;
            retireSlot( handle.index(), *pSlot );
        }

        /** @brief 모든 점유 슬롯에 fn(T&)를 호출합니다. */
        template <typename Fn>
        void forEach( Fn&& fn )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            const uint32            count = _listSlot.size();
            for ( uint32 slotIndex = 0; slotIndex < count; ++slotIndex )
            {
                Slot* pSlot = _listSlot.at( slotIndex );
                if ( pSlot != nullptr && pSlot->_bOccupied.load( std::memory_order_relaxed ) )
                    fn( pSlot->_value );
            }
        }

        /** @brief 모든 점유 슬롯에 fn(ObjectHandle, T&)를 호출합니다. */
        template <typename Fn>
        void forEachHandle( Fn&& fn )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            const uint32            count = _listSlot.size();
            for ( uint32 slotIndex = 0; slotIndex < count; ++slotIndex )
            {
                Slot* pSlot = _listSlot.at( slotIndex );
                if ( pSlot != nullptr && pSlot->_bOccupied.load( std::memory_order_relaxed ) )
                    fn( ObjectHandle::make( slotIndex, pSlot->_generation.load( std::memory_order_relaxed ) ), pSlot->_value );
            }
        }

        /** @brief 모든 점유 슬롯에 fn(ObjectHandle, const T&)를 호출합니다. */
        template <typename Fn>
        void forEachHandle( Fn&& fn ) const
        {
            std::scoped_lock<mutex> lock{ _mutex };
            const uint32            count = _listSlot.size();
            for ( uint32 slotIndex = 0; slotIndex < count; ++slotIndex )
            {
                const Slot* pSlot = _listSlot.at( slotIndex );
                if ( pSlot != nullptr && pSlot->_bOccupied.load( std::memory_order_relaxed ) )
                    fn( ObjectHandle::make( slotIndex, pSlot->_generation.load( std::memory_order_relaxed ) ), pSlot->_value );
            }
        }

        /** @brief 현재 활성화된 슬롯 개수를 반환합니다. */
        uint32 size() const
        {
            std::scoped_lock<mutex> lock{ _mutex };
            return _listSlot.size() - static_cast<uint32>( _listFree.size() );
        }

        /** @brief 테이블이 비어 있는지 반환합니다. */
        bool empty() const { return size() == 0; }

        /** @brief 모든 슬롯을 비웁니다. */
        void clear()
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _listSlot.clear();
            _listFree.clear();
        }

    private:
        struct Slot
        {
            T              _value{};
            atomic<uint32> _generation{ 1 };
            atomic<bool>   _bOccupied{ false };

            /** @brief 빈 슬롯입니다. */
            Slot() = default;
            /** @brief PagedArray 저장을 위해 값만 옮깁니다(카운터는 새로 시작). */
            Slot( Slot&& other ) noexcept
                : _value{ std::move( other._value ) }
                , _generation{ other._generation.load( std::memory_order_relaxed ) }
                , _bOccupied{ other._bOccupied.load( std::memory_order_relaxed ) }
            {
            }
            /** @brief PagedArray 저장을 위해 값만 옮깁니다(카운터는 새로 시작). */
            Slot& operator=( Slot&& other ) noexcept
            {
                _value = std::move( other._value );
                _generation.store( other._generation.load( std::memory_order_relaxed ), std::memory_order_relaxed );
                _bOccupied.store( other._bOccupied.load( std::memory_order_relaxed ), std::memory_order_relaxed );
                return *this;
            }
        };

        /** @brief 핸들이 가리키는 점유 중인 슬롯을 찾습니다(뮤텍스를 이미 잡은 상태에서 호출). */
        Slot* findOccupiedSlot( ObjectHandle handle )
        {
            if ( handle.isValid() == false )
                return nullptr;
            Slot* pSlot = _listSlot.at( handle.index() );
            if ( pSlot == nullptr || pSlot->_bOccupied.load( std::memory_order_relaxed ) == false ||
                 pSlot->_generation.load( std::memory_order_relaxed ) != handle.generation() )
                return nullptr;
            return pSlot;
        }

        /** @brief 슬롯을 비우고 세대를 올린 뒤 프리리스트에 넣습니다(뮤텍스를 이미 잡은 상태에서 호출). */
        void retireSlot( uint32 index, Slot& slot )
        {
            slot._value = T{};
            // 세대를 먼저 올리고 점유 해제를 release 로 발행해, 락 없이 읽는 쪽이 "점유 중"으로 보는
            // 동안에는 항상 옛 세대와 비교되어 실패하도록 한다.
            uint32 nextGeneration = slot._generation.load( std::memory_order_relaxed ) + 1u;
            if ( nextGeneration == 0 )
                nextGeneration = 1;
            slot._generation.store( nextGeneration, std::memory_order_relaxed );
            slot._bOccupied.store( false, std::memory_order_release );
            _listFree.push_back( index );
        }

        PagedArray<Slot> _listSlot;
        vector<uint32>   _listFree;
        mutable mutex    _mutex;
    };
} // namespace sw
