/**
 * @file HandleTable.h
 * @brief ObjectHandle로 T 슬롯을 보관합니다. 핸들 기본값은 무효입니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/ObjectHandle.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @brief T 슬롯을 generation과 함께 보관합니다.
     */
    template <typename T>
    class HandleTable
    {
    public:
        /** @brief 값을 넣고 핸들을 반환합니다. */
        ObjectHandle insert( T value )
        {
            uint32 index{ 0 };
            uint32 gen{ 1 };
            if ( _listFree.empty() == false )
            {
                index = _listFree.back();
                _listFree.pop_back();
                gen                         = _listSlot[index]._generation;
                _listSlot[index]._value     = std::move( value );
                _listSlot[index]._bOccupied = true;
            }
            else
            {
                index = static_cast<uint32>( _listSlot.size() );
                _listSlot.push_back( Slot{ std::move( value ), 1u, true } );
                gen = 1;
            }
            return ObjectHandle::make( index, gen );
        }

        /** @brief 핸들이 유효하면 슬롯 포인터, 아니면 nullptr. */
        T* get( ObjectHandle handle ) { return const_cast<T*>( static_cast<const HandleTable*>( this )->get( handle ) ); }

        /** @brief 핸들이 유효하면 슬롯 포인터, 아니면 nullptr. */
        const T* get( ObjectHandle handle ) const
        {
            if ( handle.isValid() == false )
                return nullptr;
            const uint32 index = handle.index();
            if ( index >= _listSlot.size() || _listSlot[index]._bOccupied == false || _listSlot[index]._generation != handle.generation() )
                return nullptr;
            return std::addressof( _listSlot[index]._value );
        }

        /** @brief 슬롯을 비우고 generation을 올립니다. 꺼낸 값을 반환합니다. */
        bool take( ObjectHandle handle, T& outValue )
        {
            T* pSlot = get( handle );
            if ( pSlot == nullptr )
                return false;
            outValue = std::move( *pSlot );
            erase( handle );
            return true;
        }

        /** @brief 핸들을 무효화합니다. */
        void erase( ObjectHandle handle )
        {
            T* pSlot = get( handle );
            if ( pSlot == nullptr )
                return;
            const uint32 index           = handle.index();
            _listSlot[index]._value      = T{};
            _listSlot[index]._bOccupied  = false;
            _listSlot[index]._generation = _listSlot[index]._generation + 1u;
            if ( _listSlot[index]._generation == 0 )
                _listSlot[index]._generation = 1;
            _listFree.push_back( index );
        }

        /** @brief 모든 점유 슬롯에 fn(T&)를 호출합니다. */
        template <typename Fn>
        void forEach( Fn&& fn )
        {
            for ( Slot& slot : _listSlot )
            {
                if ( slot._bOccupied )
                    fn( slot._value );
            }
        }

        /** @brief 모든 점유 슬롯에 fn(ObjectHandle, T&)를 호출합니다. */
        template <typename Fn>
        void forEachHandle( Fn&& fn )
        {
            for ( uint32 slotIndex = 0; slotIndex < static_cast<uint32>( _listSlot.size() ); ++slotIndex )
            {
                Slot& slot = _listSlot[slotIndex];
                if ( slot._bOccupied )
                    fn( ObjectHandle::make( slotIndex, slot._generation ), slot._value );
            }
        }

        /** @brief 모든 점유 슬롯에 fn(ObjectHandle, const T&)를 호출합니다. */
        template <typename Fn>
        void forEachHandle( Fn&& fn ) const
        {
            for ( uint32 slotIndex = 0; slotIndex < static_cast<uint32>( _listSlot.size() ); ++slotIndex )
            {
                const Slot& slot = _listSlot[slotIndex];
                if ( slot._bOccupied )
                    fn( ObjectHandle::make( slotIndex, slot._generation ), slot._value );
            }
        }

        /** @brief 현재 활성화된 슬롯 개수를 반환합니다. */
        uint32 size() const { return static_cast<uint32>( _listSlot.size() - _listFree.size() ); }

        /** @brief 테이블이 비어 있는지 반환합니다. */
        bool empty() const { return size() == 0; }

        /** @brief 모든 슬롯을 비웁니다. */
        void clear()
        {
            _listSlot.clear();
            _listFree.clear();
        }

    private:
        struct Slot
        {
            T      _value{};
            uint32 _generation{ 1 };
            bool   _bOccupied{ false };
        };

        vector<Slot>   _listSlot;
        vector<uint32> _listFree;
    };
} // namespace sw
