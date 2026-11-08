/**
 * @file SlotHandleTable.h
 * @brief SlotHandle 로 T 슬롯을 보관하는 테이블입니다. 핸들의 기본값은 무효입니다.
 * @details 핸들 표 한 벌: 빈 칸 재사용 정책은 `IndexFreeList`(LIFO), 낡은 핸들 가리기는 슬롯의 세대, 주소 고정은 `PagedArray` 가 맡습니다.
 *          맨 인덱스만 필요하면(셰이더에 넘기는 bindless 번호 · 결정적 저장 바이트에 드는 슬롯 순서) `IndexFreeList` 를 바로 쓰고,
 *          다시 쓰면 안 되는 id 는 `IDAllocator` 입니다(언리얼 `TSparseArray` + 세대 핸들 · 유니티 `Entity`(index, version) 와 같은 자리).
 *          잠금 정책(`SlotTableSync`)이 둘입니다: `Internal`(기본 — 표가 뮤텍스를 들고 `get` 은 락 없이 원자값으로 읽는다)과
 *          `External`(부르는 쪽이 제 잠금 · 스레드 하나로 지킨다 — 뮤텍스 · 원자값 없이 같은 규칙). 칸 번호 + 세대를 손으로 짜지 않는다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/IndexFreeList.h"
#include "Core/Container/PagedArray.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 핸들 표를 누가 지키는지입니다(`SlotHandleTable` 의 정책). */
    enum class SlotTableSync : uint8
    {
        Internal, ///< 표가 제 뮤텍스로 고치기를 직렬화하고, `get` 은 락 없이 동시에 읽는다(RHI 자원 표 — 렌더 스레드가 읽는 동안 게임 스레드가 만든다)
        External, ///< 부르는 쪽이 제 잠금 · 스레드 하나로 모든 호출을 지킨다 — 표는 잠그지 않는다(전송의 망 잠금 · 경로 줄의 잠금 아래에서 쓰는 표)
    };

    /**
     * @brief T 슬롯을 세대(generation)와 함께 보관합니다. @p kSync 가 `Internal`(기본)인 정의입니다 — `External` 은 아래 특수화입니다.
     * @details 슬롯 저장소로 `PagedArray` 를 쓰므로 슬롯이 늘어나도 이미 돌려준 원소 주소가 바뀌지 않습니다. 주의: `vector` 로
     *          바꾸면 한 스레드가 `insert()` 로 재할당을 일으킬 때 다른 스레드가 `get()` 으로 받아 둔 포인터가 댕글링 포인터가
     *          됩니다(RHI 백엔드에서 렌더 스레드가 리소스를 그리는 동안 게임 스레드가 리소스를 만들면 바로 이 상황입니다).
     *
     *          **동시성 계약**: `get` 은 락 없이 동시에 불러도 안전합니다(드로우마다 불리는 뜨거운 경로라 일부러 락을 두지
     *          않았습니다). 나머지(`insert` / `erase` / `take` / `clear` / `forEach*` / `size`)는 내부 뮤텍스로 서로 직렬화됩니다.
     *          락 없는 `get` 이 성립하는 이유는 두 가지입니다. 새 청크는 슬롯을 모두 초기화한 뒤에 발행되고(`PagedArray::ensure`),
     *          슬롯의 점유 여부와 세대는 원자값 하나에 함께 들어 있습니다. 다만 "쓰는 중인 슬롯의 값을 동시에 읽는 것" 까지
     *          막아 주지는 않습니다. 사용 중인 자원을 파괴하지 않을 책임(예: GPU 펜스 기반 지연 해제)은 상위 계층에 있습니다.
     */
    template <typename T, SlotTableSync kSync = SlotTableSync::Internal>
    class SlotHandleTable
    {
    public:
        /** @brief 값을 넣고 핸들을 반환합니다. */
        SlotHandle insert( T value )
        {
            ScopedLock<mutex> lock{ _mutex };

            uint32 index{ 0 };
            if ( _freeSlot.tryPop( index ) )
            {
                Slot* pSlot = _listSlot.find( index );
                if ( pSlot == nullptr )
                    return SlotHandle{};
                // 세대는 freeSlot 에서 이미 올려 두었으므로 그대로 쓴다. 옛 핸들은 계속 무효다.
                const uint32 generation = pSlot->generation( std::memory_order_relaxed );
                pSlot->_value           = std::move( value );
                pSlot->_state.store( Slot::kOccupiedBit | generation, std::memory_order_release );
                return SlotHandle::make( index, generation );
            }

            // 새 슬롯은 청크와 함께 초기화된 상태(비어 있음 · 세대 1)로 이미 발행돼 있을 수 있다. 값을 채운 뒤 점유 비트를
            // release 로 켜야, 락 없이 읽는 쪽이 채우는 중인 값을 받지 않는다.
            index       = _slotCount;
            Slot* pSlot = _listSlot.ensure( index );
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
            ScopedLock<mutex> lock{ _mutex };
            Slot*             pSlot = findOccupiedSlot( handle );
            if ( pSlot == nullptr )
                return false;
            outValue = std::move( pSlot->_value );
            freeSlot( handle.index(), *pSlot );
            return true;
        }

        /** @brief 핸들이 가리키는 슬롯을 비워 핸들을 무효로 만듭니다. */
        void erase( SlotHandle handle )
        {
            ScopedLock<mutex> lock{ _mutex };
            Slot*             pSlot = findOccupiedSlot( handle );
            if ( pSlot == nullptr )
                return;
            freeSlot( handle.index(), *pSlot );
        }

        /** @brief 점유된 슬롯마다 fn(T&) 를 부릅니다. */
        template <typename Fn>
        void forEach( Fn&& fn )
        {
            ScopedLock<mutex> lock{ _mutex };
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
            ScopedLock<mutex> lock{ _mutex };
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
            ScopedLock<mutex> lock{ _mutex };
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
            ScopedLock<mutex> lock{ _mutex };
            return _slotCount - static_cast<uint32>( _freeSlot.size() );
        }

        /** @brief 테이블이 비어 있는지 반환합니다. */
        bool empty() const { return size() == 0; }

        /**
         * @brief 모든 슬롯을 비웁니다. **세대는 이어 갑니다** — 비우기 전에 받은 핸들은 비운 뒤 새로 넣은 값을 가리키지 않습니다.
         * @details 주의: 슬롯 수를 0 으로 돌려 세대를 처음부터 다시 발행하면 비우기 전의 `(0, 1)` 핸들이 새 값으로 풀립니다 — 핸들이
         *          막으려던 바로 그 일입니다. 청크는 남깁니다(다시 채울 때 씁니다).
         */
        void clear()
        {
            ScopedLock<mutex> lock{ _mutex };
            for ( uint32 slotIndex = 0; slotIndex < _slotCount; ++slotIndex )
            {
                Slot* pSlot = _listSlot.find( slotIndex );
                if ( pSlot != nullptr && pSlot->isOccupied( std::memory_order_relaxed ) )
                    freeSlot( slotIndex, *pSlot );
            }
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
        Slot* findOccupiedSlot( SlotHandle handle ) SW_REQUIRES_LOCK( _mutex )
        {
            if ( handle.isValid() == false )
                return nullptr;
            Slot* pSlot = _listSlot.find( handle.index() );
            if ( pSlot == nullptr || pSlot->matches( handle.generation(), std::memory_order_relaxed ) == false )
                return nullptr;
            return pSlot;
        }

        /** @brief 슬롯을 비우고 세대를 올린 뒤 프리 리스트에 넣습니다(뮤텍스를 잡은 상태에서 부릅니다). */
        void freeSlot( uint32 index, Slot& slot ) SW_REQUIRES_LOCK( _mutex )
        {
            slot._value = T{};
            // 점유 해제와 세대 증가를 한 번의 store 로 발행한다. 둘로 나누면 그 사이에 락 없이 읽는 쪽이
            // "점유 중 + 옛 세대" 를 보고 비워지는 중인 값의 주소를 받아 간다.
            slot._state.store( SlotHandle::nextGeneration( slot.generation( std::memory_order_relaxed ), Slot::kGenerationMask ), std::memory_order_release );
            _freeSlot.push( index ); // 세대를 올린 바로 이 자리가 반납이다 — 이중 반납은 `findOccupiedSlot` 의 세대 검사가 막는다
        }

    private:
        PagedArray<Slot>        _listSlot;
        IndexFreeList _freeSlot SW_GUARDED_BY( _mutex );
        mutable mutex           _mutex;
        uint32 _slotCount       SW_GUARDED_BY( _mutex ){ 0 }; ///< 지금까지 쓴 슬롯 수. 뮤텍스 안에서만 읽고 쓴다
    };

    /**
     * @brief 부르는 쪽이 지키는 핸들 표입니다(`SlotTableSync::External`). 칸 재사용 · 세대 규칙은 기본 정의와 같고 잠금 · 원자값이 없습니다.
     * @details 모든 호출(읽기 포함)을 부르는 쪽의 잠금 하나 또는 스레드 하나가 직렬화해야 합니다. 칸은 `vector` 라 `insert` 가 앞서 받은 `get` 포인터를
     *          무효로 만들 수 있습니다 — 포인터는 그 잠금 구간 안에서만 씁니다. 언리얼 `TSparseArray`(잠금 없음) + 세대 핸들과 같은 자리입니다.
     */
    template <typename T>
    class SlotHandleTable<T, SlotTableSync::External>
    {
    public:
        /** @brief 값을 넣고 핸들을 반환합니다. 빈 칸이 있으면 마지막에 놓은 칸부터 다시 쓰고, 그 칸의 세대는 놓을 때 이미 올랐습니다. */
        SlotHandle insert( T value )
        {
            const uint32 index = _freeSlot.reserve( _listSlot );
            if ( index == _listSlot.size() )
                _listSlot.push_back( Slot{} );
            Slot& slot  = _listSlot[index];
            slot._value = std::move( value );
            slot._state = Slot::kOccupiedBit | slot.generation();
            return SlotHandle::make( index, slot.generation() );
        }

        /** @brief 핸들이 점유된 칸의 지금 세대를 가리키면 값 포인터를, 아니면 nullptr 을 반환합니다. */
        T* get( SlotHandle handle ) { return const_cast<T*>( static_cast<const SlotHandleTable*>( this )->get( handle ) ); }

        /** @brief 핸들이 점유된 칸의 지금 세대를 가리키면 값 포인터를, 아니면 nullptr 을 반환합니다. */
        const T* get( SlotHandle handle ) const
        {
            const Slot* pSlot = findOccupiedSlot( handle );
            return pSlot != nullptr ? std::addressof( pSlot->_value ) : nullptr;
        }

        /** @brief 칸을 비우고 세대를 올린 뒤, 꺼낸 값을 outValue 에 담습니다. 핸들이 무효면 false 입니다. */
        bool take( SlotHandle handle, T& outValue )
        {
            Slot* const pSlot = const_cast<Slot*>( findOccupiedSlot( handle ) );
            if ( pSlot == nullptr )
                return false;
            outValue = std::move( pSlot->_value );
            freeSlot( handle.index(), *pSlot );
            return true;
        }

        /** @brief 핸들이 가리키는 칸을 비워 핸들을 무효로 만듭니다. 이미 무효면 할 일이 없습니다(이중 반납은 세대 검사가 막는다). */
        void erase( SlotHandle handle )
        {
            Slot* const pSlot = const_cast<Slot*>( findOccupiedSlot( handle ) );
            if ( pSlot != nullptr )
                freeSlot( handle.index(), *pSlot );
        }

        /** @brief 점유된 칸마다 fn(T&) 를 칸 번호 순으로 부릅니다. 도는 동안 표를 고치지 않습니다. */
        template <typename Fn>
        void forEach( Fn&& fn )
        {
            for ( Slot& slot : _listSlot )
            {
                if ( slot.isOccupied() )
                    fn( slot._value );
            }
        }

        /** @brief 점유된 칸마다 fn(SlotHandle, T&) 를 칸 번호 순으로 부릅니다. 도는 동안 표를 고치지 않습니다. */
        template <typename Fn>
        void forEachHandle( Fn&& fn )
        {
            for ( uint32 slotIndex = 0; slotIndex < static_cast<uint32>( _listSlot.size() ); ++slotIndex )
            {
                Slot& slot = _listSlot[slotIndex];
                if ( slot.isOccupied() )
                    fn( SlotHandle::make( slotIndex, slot.generation() ), slot._value );
            }
        }

        /** @brief 지금 점유된 칸 수를 반환합니다. */
        uint32 size() const { return static_cast<uint32>( _listSlot.size() - _freeSlot.size() ); }

        /** @brief 표가 비어 있는지 반환합니다. */
        bool empty() const { return size() == 0; }

        /** @brief 모든 칸을 비웁니다. **세대는 이어 갑니다** — 비우기 전에 받은 핸들은 비운 뒤 새로 넣은 값을 가리키지 않습니다(기본 정의의 `clear` 와 같다). */
        void clear()
        {
            for ( uint32 slotIndex = 0; slotIndex < static_cast<uint32>( _listSlot.size() ); ++slotIndex )
            {
                if ( _listSlot[slotIndex].isOccupied() )
                    freeSlot( slotIndex, _listSlot[slotIndex] );
            }
        }

    private:
        /** @brief 칸 하나의 값과 상태입니다. 상태는 기본 정의와 같은 모양(점유 비트 | 세대)이라 두 정책의 세대 규칙이 같습니다. */
        struct Slot
        {
            static constexpr uint32 kOccupiedBit    = 0x8000'0000u;
            static constexpr uint32 kGenerationMask = 0x7FFF'FFFFu;

            T      _value{};
            uint32 _state{ 1 }; /**< kOccupiedBit | generation */

            uint32 generation() const { return _state & kGenerationMask; }
            bool   isOccupied() const { return ( _state & kOccupiedBit ) != 0; }
        };

        /** @brief 핸들이 가리키는 점유된 칸입니다. 없으면 nullptr 입니다. */
        const Slot* findOccupiedSlot( SlotHandle handle ) const
        {
            if ( handle.isValid() == false || handle.index() >= _listSlot.size() )
                return nullptr;
            const Slot& slot = _listSlot[handle.index()];
            return slot.isOccupied() && slot.generation() == handle.generation() ? &slot : nullptr;
        }

        /** @brief 칸을 비우고 세대를 올린 뒤 빈 칸 스택에 넣습니다. */
        void freeSlot( uint32 index, Slot& slot )
        {
            slot._value = T{};
            slot._state = SlotHandle::nextGeneration( slot.generation(), Slot::kGenerationMask );
            _freeSlot.push( index );
        }

    private:
        vector<Slot>  _listSlot; ///< 칸(번호 = 핸들의 index)
        IndexFreeList _freeSlot; ///< 놓은 칸 번호(마지막에 놓은 것부터 다시 쓴다)
    };
} // namespace sw
