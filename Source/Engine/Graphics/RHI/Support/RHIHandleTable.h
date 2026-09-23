/**
 * @file RHIHandleTable.h
 * @brief index|generation 불투명 핸들 표입니다. COM · Vk 포인터를 uint64 로 캐스팅하지 않습니다.
 */
#pragma once
#include "Core/Container/SlotHandle.h"
#include "Core/Container/SlotHandleTable.h"

namespace sw
{
    /**
     * @brief T 슬롯을 세대(generation)와 함께 보관합니다. 핸들 0 은 무효입니다.
     * @details RHI 백엔드는 uint64 핸들 별칭을 그대로 쓰므로 packed 값을 주고받습니다.
     */
    template <typename T>
    class RHIHandleTable
    {
    public:
        /** @brief 값을 넣고 불투명 핸들을 반환합니다. */
        uint64 insert( T value ) { return _table.insert( std::move( value ) ).packed(); }

        /** @brief 핸들이 유효하면 슬롯 포인터를, 아니면 nullptr 를 반환합니다. */
        T* get( uint64 handle ) { return _table.get( SlotHandle::fromPacked( handle ) ); }

        /** @brief 핸들이 유효하면 슬롯 포인터를, 아니면 nullptr 를 반환합니다. */
        const T* get( uint64 handle ) const { return _table.get( SlotHandle::fromPacked( handle ) ); }

        /** @brief 슬롯을 비우고 세대를 올립니다. 꺼낸 값은 outValue 로 받고, 핸들이 유효했으면 true 를 반환합니다. */
        bool take( uint64 handle, T& outValue ) { return _table.take( SlotHandle::fromPacked( handle ), outValue ); }

        /** @brief 핸들을 무효화합니다. */
        void erase( uint64 handle ) { _table.erase( SlotHandle::fromPacked( handle ) ); }

        /** @brief 점유된 슬롯마다 fn(T&) 를 부릅니다. */
        template <typename Fn>
        void forEach( Fn&& fn ) { _table.forEach( std::forward<Fn>( fn ) ); }

        /** @brief 모든 슬롯을 비웁니다. */
        void clear() { _table.clear(); }

    private:
        SlotHandleTable<T> _table;
    };
} // namespace sw
