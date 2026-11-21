/**
 * @file RHIHandleTable.h
 * @brief index|generation 불투명 핸들 테이블입니다. COM, Vk 포인터를 정수로 캐스팅하지 않습니다.
 */
#pragma once
#include "Core/Container/SlotHandle.h"
#include "Core/Container/SlotHandleTable.h"

#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    /**
     * @brief T 슬롯을 세대(generation)와 함께 보관합니다. 핸들 0 은 무효입니다.
     * @details 표는 자기 핸들 종류(@p Handle — `RHIBufferHandle` · `RHITextureHandle` · …)만 내고 받는다. 버퍼 표에 텍스처 핸들을 넣으면 컴파일이 멈춘다.
     */
    template <typename Handle, typename T>
    class RHIHandleTable
    {
    public:
        /** @brief 값을 넣고 불투명 핸들을 반환합니다. */
        Handle insert( T value ) { return Handle{ _table.insert( std::move( value ) ).packed() }; }

        /** @brief 핸들이 유효하면 슬롯 포인터를, 아니면 nullptr 를 반환합니다. */
        T* get( Handle handle ) { return _table.get( SlotHandle::fromPacked( handle.getPacked() ) ); }

        /** @brief 핸들이 유효하면 슬롯 포인터를, 아니면 nullptr 를 반환합니다. */
        const T* get( Handle handle ) const { return _table.get( SlotHandle::fromPacked( handle.getPacked() ) ); }

        /** @brief 슬롯을 비우고 세대를 증가시킵니다. 꺼낸 값은 outValue 로 받고, 핸들이 유효했으면 true 를 반환합니다. */
        [[nodiscard]] bool take( Handle handle, T& outValue ) { return _table.take( SlotHandle::fromPacked( handle.getPacked() ), outValue ); }

        /** @brief 핸들을 무효화합니다. 핸들이 유효했으면 true 입니다(장치의 같은 핸들 두 번 해제 검사가 본다). */
        bool erase( Handle handle )
        {
            T discarded{};
            return take( handle, discarded );
        }

        /** @brief 점유된 슬롯마다 fn(T&) 를 부릅니다. */
        template <typename Fn>
        void forEach( Fn&& fn ) { _table.forEach( std::forward<Fn>( fn ) ); }

        /** @brief 모든 슬롯을 비웁니다. */
        void clear() { _table.clear(); }

    private:
        SlotHandleTable<T> _table;
    };
} // namespace sw
