/**
 * @file InlineAllocator.h
 * @brief SBO(Small Buffer Optimization)용 인라인 버퍼 할당자입니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @brief 내부 버퍼를 가진 할당자입니다.
     * @details sw::vector 가 이 할당자를 알아보고 SBO 를 켭니다. 버퍼가 모자라면 컨테이너 기본 할당자(`ContainerAllocator`)로 힙에서 잡습니다.
     *          `vector` 가 두 구성(`SW_ENABLE_STL_CONTAINER`)에서 같은 구현이라 이동할 때 인라인 버퍼를 옮기는 규칙도 같고, 구성마다 일반 할당자로 갈아 끼우지 않습니다.
     */
    template <typename T, size_t N>
    struct InlineAllocator
    {
        using value_type = T;

        alignas( T ) uint8 _buffer[N * sizeof( T )];
        bool _isUsed = false;

        InlineAllocator()                                        = default;
        InlineAllocator( const InlineAllocator& )                = default;
        InlineAllocator( InlineAllocator&& ) noexcept            = default;
        InlineAllocator& operator=( const InlineAllocator& )     = default;
        InlineAllocator& operator=( InlineAllocator&& ) noexcept = default;

        template <class U>
        struct rebind
        {
            using other = InlineAllocator<U, N>;
        };

        T* allocate( size_t n )
        {
            if ( _isUsed == false && n <= N )
            {
                _isUsed = true;
                return reinterpret_cast<T*>( _buffer );
            }
            return ContainerAllocator<T>().allocate( n );
        }

        void deallocate( T* p, size_t n ) noexcept
        {
            if ( p == reinterpret_cast<T*>( _buffer ) )
                _isUsed = false;
            else
                ContainerAllocator<T>().deallocate( p, n );
        }

        // sw::vector 가 SBO 여부를 알아볼 수 있도록 버퍼 포인터를 내준다
        T*     get_inline_buffer() { return reinterpret_cast<T*>( _buffer ); }
        size_t get_inline_capacity() const { return N; }
    };
} // namespace sw
