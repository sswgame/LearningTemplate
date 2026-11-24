/**
 * @file NativeStringStorage.h
 * @brief sw::basic_string 의 자체 저장 형식입니다(기본 구성). 쓰는 쪽은 `Core/Container/string.h` 를 include 합니다.
 * @details 배치는 EASTL `basic_string` · folly `fbstring` 과 같은 세 칸(64 비트 24 바이트)이다: 긴 글은 { 포인터, 글자 수, 용량 | 힙 표식 }, 짧은 글(utf8 23 자)은 그 세 칸을 통째로
 *          글자로 쓰고 마지막 칸에 "남은 칸 수" 를 둔다 — 꽉 찬 짧은 글은 남은 칸 수 0 이 곧 널 문자다. 언리얼 `FString`(TArray) · Godot `String`(CowData)은 짧은 글 버퍼가 없어
 *          이름 하나도 할당한다. 표준 라이브러리의 짧은 글 길이(MSVC · libstdc++ 15 · libc++ 22)와 크기(32 · 32 · 24)는 저마다 갈려 같은 코드의 할당 수가 플랫폼마다 다르다.
 *          자기 주소를 담은 칸이 없다(포인터는 힙 버퍼만 가리킨다 — BitwiseRelocatable.h).
 * @note 마지막 칸은 짧은 글의 남은 칸 수다 — `data()` 로 `[0, size()]` 밖(용량 안쪽)을 쓰지 않는다. 글자 수를 늘려 쓰려면 먼저 `resize` 한다(std 와 같은 계약).
 */
#pragma once
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw::internal
{
    /** @brief 글자와 글자 수를 담습니다. 널 문자를 늘 유지합니다. 레이스 · 범위 검사는 `basic_string` 이 합니다. */
    template <typename CharT, typename Traits, typename Allocator>
    class StringStorage
    {
        static_assert( std::is_empty_v<Allocator>, "sw::basic_string does not store its allocator - it must be stateless" );

    public:
        static constexpr bool kBitwiseRelocatable = true; ///< 포인터는 힙 버퍼만 가리킨다

        StringStorage() noexcept { setEmpty(); }
        StringStorage( const StringStorage& ) = delete; // 복사는 `basic_string` 이 `initialize` 로 한다
        StringStorage( StringStorage&& other ) noexcept { take( other ); }
        ~StringStorage() { release(); }
        StringStorage& operator=( const StringStorage& ) = delete;
        /** @brief @p other 의 글을 가져오고 @p other 를 빈 글로 둡니다. */
        StringStorage& operator=( StringStorage&& other ) noexcept
        {
            if ( this != &other )
            {
                release();
                take( other );
            }
            return *this;
        }

        CharT*       data() noexcept { return isHeap() ? _heap._pData : _arrSmall; }
        const CharT* data() const noexcept { return isHeap() ? _heap._pData : _arrSmall; }
        size_t       size() const noexcept { return isHeap() ? _heap._size : kSmallCapacity - static_cast<size_t>( _arrSmall[kSmallCapacity] ); }
        size_t       capacity() const noexcept { return isHeap() ? ( _heap._capacityAndFlag & ~kHeapFlag ) : kSmallCapacity; }
        /** @brief 널 문자 한 칸 · 힙 표식 한 비트를 뺀 가장 긴 글자 수입니다. */
        static constexpr size_t max_size() noexcept { return ( kHeapFlag - 1 ) / sizeof( CharT ) - 1; }

        /** @brief 빈 글을 @p count 글자로 채웁니다(생성자가 쓴다). 꼭 맞는 용량만 잡는다. */
        void initialize( const CharT* pSource, size_t count )
        {
            if ( count > kSmallCapacity )
            {
                if ( count > max_size() )
                    throw std::bad_alloc(); // 할당자와 같은 실패다
                const size_t capacity = roundCapacity( count );
                setHeap( allocate( capacity ), 0, capacity );
            }
            copyChars( data(), pSource, count );
            setSize( count );
        }

        /** @brief 용량을 @p newCapacity(지금 용량보다 크다) 이상의 힙 버퍼로 바꿉니다. */
        void reserve( size_t newCapacity )
        {
            if ( newCapacity > max_size() )
                throw std::bad_alloc();
            reallocate( roundCapacity( newCapacity ) );
        }

        /** @brief 남는 용량을 놓습니다. 짧은 글 버퍼에 들면 힙 버퍼를 놓습니다. */
        void shrink_to_fit()
        {
            if ( isHeap() == false )
                return;
            const size_t size = _heap._size;
            if ( size <= kSmallCapacity )
            {
                CharT* const pOld        = _heap._pData;
                const size_t oldCapacity = capacity();
                setEmpty();
                copyChars( _arrSmall, pOld, size );
                setSize( size );
                deallocate( pOld, oldCapacity );
            }
            else if ( roundCapacity( size ) < capacity() )
            {
                reallocate( roundCapacity( size ) );
            }
        }

        /** @brief 글자 수를 @p count(지금 글자 수 이하)로 줄입니다. 용량은 남깁니다. */
        void truncate( size_t count ) noexcept { setSize( count ); }

        void push_back( CharT ch )
        {
            const size_t oldSize = size();
            if ( oldSize == capacity() )
                reallocate( computeGrowth( oldSize + 1 ) );
            data()[oldSize] = ch;
            setSize( oldSize + 1 );
        }

        /**
         * @brief [pos, pos + removeCount) 를 @p insertCount 칸의 빈자리로 바꾸고 그 자리를 돌려줍니다 — 모든 변경이 지나는 한 길이다(뒤쪽은 많아야 한 번 옮긴다).
         * @details 자리의 글자는 정해지지 않았다(부르는 쪽이 채운다). 글자 수 · 널 문자는 맞춰 둔다.
         */
        CharT* openGap( size_t pos, size_t removeCount, size_t insertCount )
        {
            const size_t oldSize   = size();
            const size_t kept      = oldSize - removeCount;
            const size_t tailCount = kept - pos;
            if ( insertCount > max_size() - kept )
                throw std::bad_alloc();
            const size_t newSize = kept + insertCount;
            if ( newSize > capacity() )
            {
                const size_t newCapacity = computeGrowth( newSize );
                CharT* const pNew        = allocate( newCapacity );
                const CharT* pOld        = data();
                copyChars( pNew, pOld, pos );
                copyChars( pNew + pos + insertCount, pOld + pos + removeCount, tailCount );
                release();
                setHeap( pNew, newSize, newCapacity );
                return pNew + pos;
            }
            CharT* const pGap = data() + pos;
            if ( tailCount > 0 && removeCount != insertCount )
                Traits::move( pGap + insertCount, pGap + removeCount, tailCount );
            setSize( newSize );
            return pGap;
        }

        void swap( StringStorage& other ) noexcept
        {
            CharT arrTemp[kSmallCount];
            std::memcpy( arrTemp, _arrSmall, sizeof( Heap ) );
            std::memcpy( _arrSmall, other._arrSmall, sizeof( Heap ) );
            std::memcpy( other._arrSmall, arrTemp, sizeof( Heap ) );
        }

    private:
        /** @brief 긴 글의 세 칸입니다. 짧은 글은 같은 자리를 글자로 쓴다. */
        struct Heap
        {
            CharT* _pData;
            size_t _size;
            size_t _capacityAndFlag; ///< 용량(널 문자 칸을 뺀 글자 수) | `kHeapFlag`
        };
        static_assert( sizeof( Heap ) % sizeof( CharT ) == 0, "the three heap words must hold whole characters" );

        static constexpr size_t kSmallCount    = sizeof( Heap ) / sizeof( CharT );
        static constexpr size_t kSmallCapacity = kSmallCount - 1;
        static constexpr size_t kHeapFlag      = static_cast<size_t>( 1 ) << ( sizeof( size_t ) * 8 - 1 ); ///< 리틀 엔디언이라 객체 마지막 바이트의 맨 위 비트다(지원 대상이 모두 리틀 엔디언이다)
        static constexpr size_t kRoundMask     = sizeof( CharT ) < 16 ? 16 / sizeof( CharT ) - 1 : 0;      ///< 널 문자까지 16 바이트 단위로 잡는다(할당자 최소 정렬)

        // 짧은 글이면 남은 칸 수가 마지막 글자 칸(최대 23)이라 맨 위 비트가 늘 0 이다. 객체의 바이트를 보는 것이라 어느 칸이 살아 있어도 된다.
        bool isHeap() const noexcept { return static_cast<int8>( reinterpret_cast<const uint8*>( &_arrSmall )[sizeof( Heap ) - 1] ) < 0; }

        /** @brief 빈 짧은 글로 둡니다(버퍼를 놓지 않는다). */
        void setEmpty() noexcept
        {
            _heap                     = Heap{ nullptr, 0, 0 }; // 칸을 통째로 옮기므로(take · swap) 안 쓰는 글자도 정해 둔다
            _arrSmall[0]              = CharT();
            _arrSmall[kSmallCapacity] = static_cast<CharT>( kSmallCapacity );
        }

        /** @brief 글자 수를 적고 널 문자를 둡니다. @p count 는 용량 이하다. */
        void setSize( size_t count ) noexcept
        {
            if ( isHeap() )
            {
                _heap._size         = count;
                _heap._pData[count] = CharT();
                return;
            }
            _arrSmall[kSmallCapacity] = static_cast<CharT>( kSmallCapacity - count );
            _arrSmall[count]          = CharT(); // 꽉 찼으면 남은 칸 수(0)와 같은 칸이다
        }

        void setHeap( CharT* pData, size_t count, size_t capacity ) noexcept
        {
            _heap._pData           = pData;
            _heap._size            = count;
            _heap._capacityAndFlag = capacity | kHeapFlag;
            pData[count]           = CharT();
        }

        static void copyChars( CharT* pDest, const CharT* pSource, size_t count )
        {
            if ( count > 0 )
                Traits::copy( pDest, pSource, count );
        }

        static CharT* allocate( size_t capacity )
        {
            Allocator alloc{};
            return std::allocator_traits<Allocator>::allocate( alloc, capacity + 1 );
        }

        static void deallocate( CharT* pData, size_t capacity ) noexcept
        {
            Allocator alloc{};
            std::allocator_traits<Allocator>::deallocate( alloc, pData, capacity + 1 );
        }

        static constexpr size_t roundCapacity( size_t capacity ) noexcept { return capacity | kRoundMask; }

        /** @brief @p required 글자를 담게 키울 때의 용량 — 지금의 1.5 배와 큰 쪽입니다(한 글자씩 붙여도 전체가 O(n)이다). */
        size_t computeGrowth( size_t required ) const
        {
            if ( required > max_size() )
                throw std::bad_alloc();
            const size_t grown = capacity() + capacity() / 2;
            return roundCapacity( required > grown ? required : grown );
        }

        void release() noexcept
        {
            if ( isHeap() )
                deallocate( _heap._pData, capacity() );
        }

        /** @brief @p other 의 칸을 통째로 가져오고 빈 글로 둡니다. 이 객체의 버퍼는 미리 놓았거나 없다. */
        void take( StringStorage& other ) noexcept
        {
            std::memcpy( _arrSmall, other._arrSmall, sizeof( Heap ) );
            other.setEmpty();
        }

        void reallocate( size_t newCapacity )
        {
            CharT* const pNew    = allocate( newCapacity );
            const size_t oldSize = size();
            copyChars( pNew, data(), oldSize );
            release();
            setHeap( pNew, oldSize, newCapacity );
        }

    private:
        union
        {
            Heap  _heap;
            CharT _arrSmall[kSmallCount]; ///< 마지막 칸은 남은 칸 수(꽉 차면 널 문자)
        };
    };
} // namespace sw::internal
