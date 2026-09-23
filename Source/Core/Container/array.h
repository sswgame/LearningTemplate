/**
 * @file array.h
 * @brief std::array 래퍼입니다. 디버그 빌드에서는 RaceDetectContext 로 동시 접근을 잡아냅니다.
 *
 * [쓰면 안 되는 곳: 여러 스레드가 **일부러** 동시에 만지는 버퍼]
 * 이 래퍼의 탐지기는 한 번에 한 스레드만 만지는 컨테이너를 전제로 합니다. lock-free 자료구조처럼 여러 스레드가 같은
 * 버퍼를 동시에 만지도록 설계된 곳에 쓰면 그 접근이 레이스로 보고되고, 보고는 Fatal 이라 **프로세스가 죽습니다.**
 * 그런 곳에는 `std::array` 를 씁니다. `ConcurrentQueue` · `LockFreeQueue` · `RenderThread` 의 링 버퍼가 `std::array` 인
 * 이유입니다.
 *
 * 드물게 터진다는 점이 오히려 더 나쁩니다. 가드가 걸리는 구간은 `operator[]` · `at()` 호출 그 자체뿐이고(참조를 반환한
 * 뒤의 원소 접근은 가드 밖입니다), 두 스레드가 정확히 그 몇 개의 명령 안에서 겹쳐야 보고됩니다. 실제로 재 봤습니다
 * (2026-09-19). `ConcurrentQueue` 의 버퍼를 `sw::array` 로 바꾸고 10만 건 MPMC 스트레스를 8번 돌렸는데 한 번도 보고되지
 * 않았습니다. 안전하다는 뜻이 아니라, **언젠가 다른 사람의 기계에서 한 번 죽는다**는 뜻입니다. "테스트가 초록이니
 * 괜찮다" 로 판단하지 마십시오.
 *
 * @note 같은 스레드가 겹쳐 들어가는 오탐(가드를 잡은 채 가드 메서드를 부르는 경우)은 2026-09-19 에 탐지기가 주인 스레드를
 *       기억하도록 고쳐서 해결했습니다. 그 수정은 이 규칙과 관계가 없습니다. 여기서 말하는 것은 정말로 여러 스레드가
 *       동시에 들어오는 경우입니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/DataRaceDetector.h"

namespace sw
{
#if defined( SW_ENABLE_STL_CONTAINER )
    template <typename T, size_t N>
    using array = std::array<T, N>;
#else
    /** @brief std::array 에 디버그 레이스 탐지를 더한 것입니다. API 는 STL 과 같습니다. */
    template <typename T, size_t N>
    class array
    {
        SW_RACE_CTX_MEMBER

    public:
        using value_type             = T;
        using size_type              = size_t;
        using difference_type        = ptrdiff_t;
        using reference              = value_type&;
        using const_reference        = const value_type&;
        using pointer                = value_type*;
        using const_pointer          = const value_type*;
        using iterator               = value_type*;
        using const_iterator         = const value_type*;
        using reverse_iterator       = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        // 실제 저장소인 C 배열
        T _elems[N > 0 ? N : 1]{};

        // ------------------------------------------------------------------------------
        // 1) 생성 · 대입
        // ------------------------------------------------------------------------------
        constexpr array() = default;

        template <typename... Args, typename = std::enable_if_t<( sizeof...( Args ) > 0 && sizeof...( Args ) <= N && ( std::is_convertible_v<Args, T> && ... ) )>>
        constexpr array( Args&&... args )
            : _elems{ static_cast<T>( std::forward<Args>( args ) )... } {}

        array( std::initializer_list<T> list )
        {
            size_t i{ 0 };
            for ( const auto& item : list )
            {
                if ( i < N )
                    _elems[i++] = item;
            }
        }

        constexpr array( const array& other )                = default;
        constexpr array& operator=( const array& other )     = default;
        constexpr array( array&& other ) noexcept            = default;
        constexpr array& operator=( array&& other ) noexcept = default;

        // ------------------------------------------------------------------------------
        // 2) 원소 접근
        // ------------------------------------------------------------------------------
        [[nodiscard]] reference at( size_type pos )
        {
            SW_SCOPED_RACE_WRITE();
            if ( pos >= N )
                throw std::out_of_range( "array::at: index out of range" );
            return _elems[pos];
        }

        [[nodiscard]] const_reference at( size_type pos ) const
        {
            SW_SCOPED_RACE_READ();
            if ( pos >= N )
                throw std::out_of_range( "array::at: index out of range" );
            return _elems[pos];
        }

        [[nodiscard]] constexpr reference operator[]( size_type pos ) noexcept
        {
            return _elems[pos];
        }

        [[nodiscard]] constexpr const_reference operator[]( size_type pos ) const noexcept
        {
            return _elems[pos];
        }

        [[nodiscard]] constexpr reference front() noexcept
        {
            return _elems[0];
        }

        [[nodiscard]] constexpr const_reference front() const noexcept
        {
            return _elems[0];
        }

        [[nodiscard]] constexpr reference back() noexcept
        {
            return _elems[N > 0 ? N - 1 : 0];
        }

        [[nodiscard]] constexpr const_reference back() const noexcept
        {
            return _elems[N > 0 ? N - 1 : 0];
        }

        [[nodiscard]] constexpr pointer data() noexcept
        {
            return _elems;
        }

        [[nodiscard]] constexpr const_pointer data() const noexcept
        {
            return _elems;
        }

        // ------------------------------------------------------------------------------
        // 3) 반복자
        // ------------------------------------------------------------------------------
        [[nodiscard]] constexpr iterator begin() noexcept
        {
            return _elems;
        }

        [[nodiscard]] constexpr const_iterator begin() const noexcept
        {
            return _elems;
        }

        [[nodiscard]] constexpr const_iterator cbegin() const noexcept
        {
            return _elems;
        }

        [[nodiscard]] constexpr iterator end() noexcept
        {
            return _elems + N;
        }

        [[nodiscard]] constexpr const_iterator end() const noexcept
        {
            return _elems + N;
        }

        [[nodiscard]] constexpr const_iterator cend() const noexcept
        {
            return _elems + N;
        }

        [[nodiscard]] reverse_iterator rbegin() noexcept { return reverse_iterator( end() ); }

        [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator( end() ); }

        [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator( cend() ); }

        [[nodiscard]] reverse_iterator rend() noexcept { return reverse_iterator( begin() ); }

        [[nodiscard]] const_reverse_iterator rend() const noexcept { return const_reverse_iterator( begin() ); }

        [[nodiscard]] const_reverse_iterator crend() const noexcept { return const_reverse_iterator( cbegin() ); }

        // ------------------------------------------------------------------------------
        // 4) 용량
        // ------------------------------------------------------------------------------
        [[nodiscard]] constexpr bool empty() const noexcept { return N == 0; }

        [[nodiscard]] constexpr size_type size() const noexcept { return N; }

        [[nodiscard]] constexpr size_type max_size() const noexcept { return N; }

        // ------------------------------------------------------------------------------
        // 5) 연산
        // ------------------------------------------------------------------------------
        void fill( const T& value )
        {
            SW_SCOPED_RACE_WRITE();
            for ( size_type index = 0; index < N; ++index )
            {
                _elems[index] = value;
            }
        }

        void swap( array& other ) noexcept( std::is_nothrow_swappable_v<T> )
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            for ( size_type index = 0; index < N; ++index )
            {
                using std::swap;
                swap( _elems[index], other._elems[index] );
            }
        }
    };

    // ------------------------------------------------------------------------------
    // 6) 비교 연산자
    // ------------------------------------------------------------------------------
    template <typename T, size_t N>
    [[nodiscard]] bool operator==( const array<T, N>& lhs, const array<T, N>& rhs )
    {
        for ( size_t elementIndex = 0; elementIndex < N; ++elementIndex )
        {
            if ( ( lhs[elementIndex] == rhs[elementIndex] ) == false )
                return false;
        }
        return true;
    }

    template <typename T, size_t N>
    [[nodiscard]] bool operator!=( const array<T, N>& lhs, const array<T, N>& rhs ) { return ( lhs == rhs ) == false; }

    template <typename T, size_t N>
    [[nodiscard]] bool operator<( const array<T, N>& lhs, const array<T, N>& rhs )
    {
        for ( size_t elementIndex = 0; elementIndex < N; ++elementIndex )
        {
            if ( lhs[elementIndex] < rhs[elementIndex] )
                return true;
            if ( rhs[elementIndex] < lhs[elementIndex] )
                return false;
        }
        return false;
    }

    template <typename T, size_t N>
    [[nodiscard]] bool operator<=( const array<T, N>& lhs, const array<T, N>& rhs ) { return !( rhs < lhs ); }

    template <typename T, size_t N>
    [[nodiscard]] bool operator>( const array<T, N>& lhs, const array<T, N>& rhs ) { return rhs < lhs; }

    template <typename T, size_t N>
    [[nodiscard]] bool operator>=( const array<T, N>& lhs, const array<T, N>& rhs ) { return !( lhs < rhs ); }

    template <size_t I, typename T, size_t N>
    [[nodiscard]] T& get( array<T, N>& a ) noexcept
    {
        static_assert( I < N, "array index out of bounds" );
        return a[I];
    }

    template <size_t I, typename T, size_t N>
    [[nodiscard]] const T& get( const array<T, N>& a ) noexcept
    {
        static_assert( I < N, "array index out of bounds" );
        return a[I];
    }

    template <size_t I, typename T, size_t N>
    [[nodiscard]] T&& get( array<T, N>&& a ) noexcept
    {
        static_assert( I < N, "array index out of bounds" );
        return std::move( a[I] );
    }

    template <size_t I, typename T, size_t N>
    [[nodiscard]] const T&& get( const array<T, N>&& a ) noexcept
    {
        static_assert( I < N, "array index out of bounds" );
        return std::move( a[I] );
    }
#endif
} // namespace sw

// ------------------------------------------------------------------------------
// 구조적 바인딩용 std::tuple_size · std::tuple_element 특수화
//
// 커스텀 array 일 때만 필요하다. `SW_ENABLE_STL_CONTAINER` 가 켜지면 `sw::array` 는 `std::array` 의 별칭이라,
// 아래 특수화는 표준 라이브러리가 이미 제공하는 것을 다시 정의하게 된다. `pair.h` 도 같은 이유로 같은 모양이다.
// 둘 다 예전에는 그 옵션을 켜면 컴파일되지 않았다.
// ------------------------------------------------------------------------------
#if !defined( SW_ENABLE_STL_CONTAINER )
namespace std
{
    template <typename T, size_t N>
    struct tuple_size<sw::array<T, N>> : std::integral_constant<size_t, N>
    {
    };

    template <size_t I, typename T, size_t N>
    struct tuple_element<I, sw::array<T, N>>
    {
        using type = T;
    };
} // namespace std
#endif
