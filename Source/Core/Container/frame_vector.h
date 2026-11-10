/**
 * @file frame_vector.h
 * @brief 프레임 아레나(`LinearAllocator`)에서 잡는 `vector` 입니다 — 한 프레임 안에서만 사는 지역 목록이 힙을 건드리지 않게 합니다.
 * @details 아레나 주인이 프레임 경계(그 아레나에서 잡은 목록이 하나도 살아 있지 않을 때)에 `LinearAllocator::reset` 을 부른다. 원소의 소멸자는 돌지만
 *          메모리는 돌려주지 않는다(`deallocate` 가 없다) — 늘어난 뒤 버린 앞 버퍼도 reset 까지 남는다. 언리얼 `TArray<T, TMemStackAllocator<>>`(FMemStack) 자리다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Memory/LinearAllocator.h"

namespace sw
{
    /**
     * @brief 아레나 하나를 가리키는 컨테이너 할당자입니다. 기본 생성자가 없어 아레나 없는 `frame_vector` 는 컴파일되지 않습니다.
     * @details 아레나가 메모리를 내주지 못하면 `std::bad_alloc` 이다(`Allocator` 와 같다 — 힙으로 대신 잡지 않는다).
     */
    template <typename T>
    class FrameAllocator
    {
    public:
        using value_type = T;

        explicit FrameAllocator( LinearAllocator& arena ) noexcept
            : _pArena{ &arena }
        {
        }
        template <typename U>
        FrameAllocator( const FrameAllocator<U>& other ) noexcept
            : _pArena{ other.getArena() }
        {
        }

        [[nodiscard]] T* allocate( size_t n )
        {
            if ( n == 0 )
                return nullptr; // 아레나는 크기 0 을 거절한다(nullptr) — 빈 버퍼는 실패가 아니다
            if ( n > ( ~size_t( 0 ) ) / sizeof( T ) )
                throw std::bad_alloc();
            void* p = _pArena->allocate( n * sizeof( T ), alignof( T ) );
            if ( p == nullptr )
                throw std::bad_alloc();
            return static_cast<T*>( p );
        }
        void deallocate( T*, size_t ) noexcept {}

        LinearAllocator* getArena() const noexcept { return _pArena; }

    private:
        LinearAllocator* _pArena;
    };

    template <typename T, typename U>
    bool operator==( const FrameAllocator<T>& left, const FrameAllocator<U>& right )
    {
        return left.getArena() == right.getArena();
    }

    template <typename T, typename U>
    bool operator!=( const FrameAllocator<T>& left, const FrameAllocator<U>& right )
    {
        return left.getArena() != right.getArena();
    }

    /** @brief 프레임 아레나의 `vector` 입니다. 만들 때 아레나를 넘긴다: `frame_vector<X> list{ FrameAllocator<X>{ arena } };` */
    template <typename T>
    using frame_vector = vector<T, FrameAllocator<T>>;
} // namespace sw
