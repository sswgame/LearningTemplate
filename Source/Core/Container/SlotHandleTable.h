/**
 * @file SlotHandleTable.h
 * @brief SlotHandle 로 T 슬롯을 보관하는 표입니다. 핸들의 기본값은 무효입니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/PagedArray.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @brief T 슬롯을 세대(generation)와 함께 보관합니다.
     * @details 슬롯 저장소로 `PagedArray` 를 쓰므로 슬롯이 늘어나도 이미 돌려준 원소 주소가 바뀌지 않습니다. 예전에는
     *          `vector` 여서, 한 스레드가 `insert()` 로 재할당을 일으키면 다른 스레드가 `get()` 으로 받아 둔 포인터가 그대로
     *          댕글링 포인터가 됐습니다(RHI 백엔드에서 렌더 스레드가 리소스를 그리는 동안 게임 스레드가 리소스를 만들면 바로
     *          이 상황이 됩니다).
     *
     *          **동시성 계약**: `get` 은 락 없이 동시에 불러도 안전합니다(드로우마다 불리는 뜨거운 경로라 일부러 락을 두지
     *          않았습니다). 나머지(`insert` / `erase` / `take` / `clear` / `forEach*` / `size`)는 내부 뮤텍스로 서로 직렬화됩니다.
     *          락 없는 `get` 이 성립하는 이유는 두 가지입니다. 새 청크는 슬롯을 모두 초기화한 뒤에 발행되고(`PagedArray::ensure`),
     *          슬롯의 점유 여부와 세대는 원자값 하나에 함께 들어 있습니다. 다만 "쓰는 중인 슬롯의 값을 동시에 읽는 것" 까지
     *          막아 주지는 않습니다. 사용 중인 자원을 파괴하지 않을 책임(예: GPU 펜스 기반 지연 해제)은 상위 계층에 있습니다.
     */
    template <typename T>
    class SlotHandleTable
    {
    public:
        /** @brief 값을 넣고 핸들을 반환합니다. */
        SlotHandle insert( T value )
        {
            std::scoped_lock<mutex> lock{ _mutex };

            if ( _listFree.empty() == false )
            {
                const uint32 index = _listFree.back();
                _listFree.pop_back();
                Slot* pSlot = _listSlot.find( index );
                if ( pSlot == nullptr )
                    return SlotHandle{};
                // 세대는 retireSlot 에서 이미 올려 두었으므로 그대로 쓴다. 옛 핸들은 계속 무효다.
                const uint32 generation = pSlot->generation( std::memory_order_relaxed );
                pSlot->_value           = std::move( value );
                pSlot->_state.store( Slot::kOccupiedBit | generation, std::memory_order_release );
                return SlotHandle::make( index, generation );
            }

            // 새 슬롯은 청크와 함께 초기화된 상태(비어 있음 · 세대 1)로 이미 발행돼 있을 수 있다. 값을 채운 뒤 점유 비트를
            // release 로 켜야, 락 없이 읽는 쪽이 채우는 중인 값을 받지 않는다.
            const uint32 index = _slotCount;
            Slot*        pSlot = _listSlot.ensure( index );
            if ( pSlot == nullptr )
                return SlotHandle{};
            pSlot->_value = std::move( value );
            pSlot->_state.store( Slot::kOccupiedBit | 1u, std::memory_order_release );
            ++_slotCount;
            return SlotHandle::make( index, 1u );
        }

        /** @brief 핸들이 유효하면 슬롯 포인터를, 아니면 nullptr 을 반환합니다. 락을 잡지 않습니다. */
        T* get( SlotHandle handle ) { return const_cast<T*>( static_cast<const SlotHandleTable*>( this )->get( handle ) ); }

        /** @brief 핸들이 유효하면 슬롯 포인터를, 아니면 nullptr 을 반환합니다. 락을 잡지 않습니다. */
        const T* get( SlotHandle handle ) const
        {
            if ( handle.isValid() == false )
                return nullptr;
            const Slot* pSlot = _listSlot.find( handle.index() );
            if ( pSlot == nullptr )
                return nullptr;
            // 점유 여부와 세대를 한 번에 읽는다. 둘로 나눠 읽으면 그 사이에 erase 가 끼어들어 "점유 중 + 옛 세대" 라는
            // 조합이 보이고, 비워지는 중인 값의 주소를 반환하게 된다.
            if ( pSlot->matches( handle.generation(), std::memory_order_acquire ) == false )
                return nullptr;
            return std::addressof( pSlot->_value );
        }

        /** @brief 슬롯을 비우고 세대를 올린 뒤, 꺼낸 값을 outValue 에 담습니다. 핸들이 무효면 false 입니다. */
        bool take( SlotHandle handle, T& outValue )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            Slot*                   pSlot = findOccupiedSlot( handle );
            if ( pSlot == nullptr )
                return false;
            outValue = std::move( pSlot->_value );
            retireSlot( handle.index(), *pSlot );
            return true;
        }

        /** @brief 핸들이 가리키는 슬롯을 비워 핸들을 무효로 만듭니다. */
        void erase( SlotHandle handle )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            Slot*                   pSlot = findOccupiedSlot( handle );
            if ( pSlot == nullptr )
                return;
            retireSlot( handle.index(), *pSlot );
        }

        /** @brief 점유된 슬롯마다 fn(T&) 를 부릅니다. */
        template <typename Fn>
        void forEach( Fn&& fn )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            for ( uint32 slotIndex = 0; slotIndex < _slotCount; ++slotIndex )
            {
                Slot* pSlot = _listSlot.find( slotIndex );
                if ( pSlot != nullptr && pSlot->isOccupied( std::memory_order_relaxed ) )
                    fn( pSlot->_value );
            }
        }

        /** @brief 점유된 슬롯마다 fn(SlotHandle, T&) 를 부릅니다. */
        template <typename Fn>
        void forEachHandle( Fn&& fn )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            for ( uint32 slotIndex = 0; slotIndex < _slotCount; ++slotIndex )
            {
                Slot* pSlot = _listSlot.find( slotIndex );
                if ( pSlot != nullptr && pSlot->isOccupied( std::memory_order_relaxed ) )
                    fn( SlotHandle::make( slotIndex, pSlot->generation( std::memory_order_relaxed ) ), pSlot->_value );
            }
        }

        /** @brief 점유된 슬롯마다 fn(SlotHandle, const T&) 를 부릅니다. */
        template <typename Fn>
        void forEachHandle( Fn&& fn ) const
        {
            std::scoped_lock<mutex> lock{ _mutex };
            for ( uint32 slotIndex = 0; slotIndex < _slotCount; ++slotIndex )
            {
                const Slot* pSlot = _listSlot.find( slotIndex );
                if ( pSlot != nullptr && pSlot->isOccupied( std::memory_order_relaxed ) )
                    fn( SlotHandle::make( slotIndex, pSlot->generation( std::memory_order_relaxed ) ), pSlot->_value );
            }
        }

        /** @brief 지금 점유된 슬롯 수를 반환합니다. */
        uint32 size() const
        {
            std::scoped_lock<mutex> lock{ _mutex };
            return _slotCount - static_cast<uint32>( _listFree.size() );
        }

        /** @brief 테이블이 비어 있는지 반환합니다. */
        bool empty() const { return size() == 0; }

        /** @brief 모든 슬롯을 비웁니다. 락 없이 `get` 하는 스레드가 없을 때만 부릅니다(청크를 해제합니다). */
        void clear()
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _listSlot.releaseChunks();
            _slotCount = 0;
            _listFree.clear();
        }

    private:
        /**
         * @brief 슬롯 하나의 값과 상태입니다.
         * @details **점유 여부와 세대는 한 원자값에 함께 둡니다.** 따로 두면 락 없이 읽는 쪽이 둘을 두 번에 나눠 읽게 되고,
         *          그 사이에 지우는 쪽이 끼어들면 "점유 중 + 옛 세대" 라는 있어서는 안 되는 조합을 보게 됩니다. 이미 비워지는
         *          중인 슬롯의 값을 가리키는 포인터가 그대로 반환되는 것입니다. x86 의 메모리 모델에서는 이 틈이 거의 닫히지만
         *          arm64(맥 타깃)에서는 그렇지 않습니다. 하나로 합치면 load 한 번으로 둘 다 알 수 있고, 더 빠르기도 합니다.
         */
        struct Slot
        {
            /** @brief `_state` 의 최상위 비트입니다. 켜져 있으면 점유 중입니다. */
            static constexpr uint32 kOccupiedBit = 0x8000'0000u;
            /** @brief `_state` 의 나머지 31비트가 세대입니다. 0 은 무효 세대입니다. */
            static constexpr uint32 kGenerationMask = 0x7FFF'FFFFu;

            T              _value{};
            atomic<uint32> _state{ 1 }; /**< kOccupiedBit | generation */

            /** @brief 점유 중이고 세대가 맞으면 true 입니다(원자값을 한 번만 읽습니다). */
            bool matches( uint32 generation, std::memory_order order ) const
            {
                const uint32 state = _state.load( order );
                return ( state & kOccupiedBit ) != 0 && ( state & kGenerationMask ) == generation;
            }

            /** @brief 현재 세대입니다(점유 여부와 관계없음). */
            uint32 generation( std::memory_order order ) const { return _state.load( order ) & kGenerationMask; }

            /** @brief 점유 중인지 반환합니다. */
            bool isOccupied( std::memory_order order ) const { return ( _state.load( order ) & kOccupiedBit ) != 0; }
        };

        /** @brief 핸들이 가리키는 점유된 슬롯을 찾습니다(뮤텍스를 잡은 상태에서 부릅니다). */
        Slot* findOccupiedSlot( SlotHandle handle )
        {
            if ( handle.isValid() == false )
                return nullptr;
            Slot* pSlot = _listSlot.find( handle.index() );
            if ( pSlot == nullptr || pSlot->matches( handle.generation(), std::memory_order_relaxed ) == false )
                return nullptr;
            return pSlot;
        }

        /** @brief 슬롯을 비우고 세대를 올린 뒤 프리 리스트에 넣습니다(뮤텍스를 잡은 상태에서 부릅니다). */
        void retireSlot( uint32 index, Slot& slot )
        {
            slot._value = T{};
            // 점유 해제와 세대 증가를 한 번의 store 로 발행한다. 둘로 나누면 그 사이에 락 없이 읽는 쪽이
            // "점유 중 + 옛 세대" 를 보고 비워지는 중인 값의 주소를 받아 간다.
            uint32 nextGeneration = ( slot.generation( std::memory_order_relaxed ) + 1u ) & Slot::kGenerationMask;
            if ( nextGeneration == 0 )
                nextGeneration = 1; // 세대 0 은 무효 핸들 몫이다. 한 바퀴 돌면 건너뛴다.
            slot._state.store( nextGeneration, std::memory_order_release );
            _listFree.push_back( index );
        }

        PagedArray<Slot> _listSlot;
        vector<uint32>   _listFree;
        mutable mutex    _mutex;
        uint32           _slotCount{ 0 }; ///< 지금까지 쓴 슬롯 수. 뮤텍스 안에서만 읽고 쓴다
    };
} // namespace sw
