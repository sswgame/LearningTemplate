/**
 * @file Expected.h
 * @brief 값 또는 실패 이유를 돌려주는 한 벌의 결과 형식 `sw::expected<T, E>` 와 "없음이 정상" 인 조회 별칭 `sw::optional<T>` 입니다.
 * @details C++17 이라 `std::expected`(C++23)를 못 쓴다 — 인터페이스(has_value · operator bool · value · error · value_or · operator* ·
 *          operator-> · and_then · transform · or_else · `unexpected<E>`)는 그것과 같게 두었다. "없음" 도 실패 이유가 하나뿐인 경우라
 *          `optional<T>` 는 `expected<T, Absent>` 의 별칭이다(사용자 결정 2026-11-08 — 언리얼은 `TValueOrError` · `TOptional` 두 벌, 여기는 한 벌).
 *          자리는 Foundation(티어 0 — 언리얼 Core/Templates): 형식 · 매크로만 쓰고 모든 티어가 반환 형식으로 쓴다.
 *
 *          std 와 다른 점(모두 의도):
 *          - `expected<T, E>` 는 기본 생성자가 없다(언리얼 `TValueOrError` 와 같다 — "아직 아무것도 아닌 결과" 를 만들지 못하게). `optional<T>` 만
 *            기본 생성이 "없음" 이다(`TOptional` · `std::optional` 과 같다).
 *          - 예외를 쓰지 않는다: 틀린 쪽의 `value()` · `error()` 는 `bad_expected_access` 대신 프로세스를 멈춘다(`SW_LOW_LEVEL_FATAL`).
 *            `operator*` · `operator->` 는 Debug 단언만 한다(std 도 미정의 동작).
 *          - 형식 자체가 `[[nodiscard]]` 다 — 결과를 버리는 호출은 컴파일 경고(오류)다. 정말 버릴 때만 `(void)` 로 적는다.
 * @note 규칙(언제 expected · 언제 optional · outError 출력 인자 금지)은 `docs/agents/Errors.md` 다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 없음 · 실패 꼬리표
    // ------------------------------------------------------------------------------

    /** @brief "찾지 못했다 · 아직 없다" 처럼 이유가 하나뿐인 실패입니다. `optional<T>` 의 오류 형식입니다. */
    struct Absent
    {
        constexpr bool operator==( const Absent& ) const noexcept { return true; }
        constexpr bool operator!=( const Absent& ) const noexcept { return false; }
    };

    /** @brief `optional<T>` 를 "없음" 으로 만드는 꼬리표 형식입니다(`std::nullopt_t` 대응). */
    struct nullopt_t
    {
        explicit constexpr nullopt_t( int32 ) noexcept {}
    };
    /** @brief `return nullopt;` 로 "없음" 을 돌려줍니다. */
    inline constexpr nullopt_t nullopt{ 0 };

    /**
     * @class unexpected
     * @brief `expected` 를 실패 쪽으로 만드는 감싸개입니다(`std::unexpected` 대응). `return unexpected( Error{ … } );`
     */
    template <typename E>
    class unexpected
    {
        static_assert( std::is_object_v<E> && std::is_array_v<E> == false && std::is_const_v<E> == false, "unexpected<E> needs a plain object type" );

    public:
        template <typename G = E, std::enable_if_t<std::is_constructible_v<E, G&&> && std::is_same_v<std::decay_t<G>, unexpected> == false, int32> = 0>
        constexpr explicit unexpected( G&& error )
            : _error( std::forward<G>( error ) )
        {
        }

        constexpr const E& error() const& noexcept { return _error; }
        constexpr E&       error() & noexcept { return _error; }
        constexpr E&&      error() && noexcept { return std::move( _error ); }

    private:
        E _error;
    };

    template <typename E>
    unexpected( E ) -> unexpected<E>;

    // ------------------------------------------------------------------------------
    // 2) Error — 기본 오류 형식(종류 + 리터럴 문장 + 할당 없는 짧은 대상)
    // ------------------------------------------------------------------------------

    /** @brief 실패의 종류입니다. 부르는 쪽이 갈래를 나눌 때 문장 대신 이것을 봅니다. */
    enum class ErrorKind : uint8
    {
        InvalidArgument, ///< 입력이 형식 · 범위에 맞지 않는다
        NotFound,        ///< 가리킨 대상(이름 · 파일 · 키)이 없다
        AlreadyExists,   ///< 같은 이름 · 키가 이미 있다
        Cycle,           ///< 의존이 순환한다
        Unsupported,     ///< 이 구성 · 형식에서 하지 않는 일이다
        ExternalFailure, ///< 바깥 라이브러리 · OS 호출이 실패했다(대상에 그 쪽 오류 글)
    };

    /**
     * @class Error
     * @brief `expected<T>` 의 기본 오류입니다. 힙을 쓰지 않습니다.
     * @details - **문장**(`message`)은 문자열 리터럴 포인터다 — 로그 id 처럼 고정된 영어 한 마디("not a boolean").
     *          - **대상**(`detail`)은 무엇이 틀렸는지(키 · 값 · 이름)를 안에 복사해 둔다. `kDetailCapacity` 를 넘으면 자른다 —
     *            입력의 수명과 상관없이 오류만 들고 다녀도 되게(string_view 로 입력을 가리키면 입력이 먼저 사라질 수 있다).
     *          - 길이가 정해지지 않은 자료(목록 · 여러 줄 보고)를 그대로 넘겨야 하면 이 형식 대신 그 도메인의 오류 형식을 `E` 로 쓴다.
     *          언리얼은 오류 쪽에 `FText` · `FString` 을 담는다(할당). 여기는 실패 경로도 할당 없이 두고, 글로 만드는 일은 로그 자리에서 한다.
     */
    class Error
    {
    public:
        /** @brief 대상(detail)에 담는 최대 바이트 수입니다. 넘치면 자릅니다. 파일 경로(Windows 260 자) 하나와 바깥 라이브러리의 까닭 한 줄이 함께 들어가는 크기다 — 159 일 때 긴 경로의 끝(파일 이름)이 잘렸다. */
        static constexpr size_t kDetailCapacity = 383;

        /**
         * @param kind 실패의 종류
         * @param pMessage 고정 문장(문자열 리터럴 — 정적 수명이어야 한다)
         * @param detail 무엇이 틀렸는지(복사한다)
         */
        Error( ErrorKind kind, const utf8* pMessage, std::string_view detail = {} ) noexcept
            : _pMessage{ pMessage }
            , _kind{ kind }
            , _detailSize{ 0 }
            , _arrDetail{}
        {
            SW_ASSERT( pMessage != nullptr );
            appendDetail( detail );
        }

        /** @brief 대상 뒤에 @p text 를 덧붙입니다. 용량을 넘는 부분은 버립니다. */
        Error& appendDetail( std::string_view text ) noexcept
        {
            const size_t room  = kDetailCapacity - _detailSize;
            const size_t count = text.size() < room ? text.size() : room;
            if ( count > 0 )
                std::memcpy( _arrDetail + _detailSize, text.data(), count );
            _detailSize = static_cast<uint16>( _detailSize + count );
            return *this;
        }

        /**
         * @brief 대상 앞에 @p text 를 붙입니다(바깥 함수가 어느 원소 · 파일인지를 더할 때 — 문장은 안쪽 리터럴 그대로). 용량을 넘으면 뒤쪽(원래 대상의 끝)을 버립니다.
         * @details 언리얼은 바깥에서 `FText::Format` 으로 새 글을 짓는다(할당). 여기는 같은 고정 칸 안에서 옮긴다.
         */
        Error& prependDetail( std::string_view text ) noexcept
        {
            const size_t count = text.size() < kDetailCapacity ? text.size() : kDetailCapacity;
            const size_t kept  = _detailSize + count <= kDetailCapacity ? _detailSize : kDetailCapacity - count;
            if ( kept > 0 )
                std::memmove( _arrDetail + count, _arrDetail, kept );
            if ( count > 0 )
                std::memcpy( _arrDetail, text.data(), count );
            _detailSize = static_cast<uint16>( count + kept );
            return *this;
        }

        [[nodiscard]] ErrorKind        kind() const noexcept { return _kind; }
        [[nodiscard]] std::string_view message() const noexcept { return _pMessage; }
        [[nodiscard]] std::string_view detail() const noexcept { return std::string_view{ _arrDetail, _detailSize }; }

    private:
        static_assert( kDetailCapacity <= std::numeric_limits<uint16>::max(), "the detail size is stored in a uint16" );

        const utf8* _pMessage;                       ///< 고정 문장(리터럴)
        ErrorKind   _kind;                           ///< 실패의 종류
        uint16      _detailSize;                     ///< `_arrDetail` 에 쓴 바이트 수
        utf8        _arrDetail[kDetailCapacity + 1]; ///< 대상 사본(널 종료하지 않는다 — `detail()` 로 읽는다)
    };

    // ------------------------------------------------------------------------------
    // 3) expected<T, E>
    // ------------------------------------------------------------------------------

    template <typename T, typename E = Error>
    class expected;

    /** @brief "없음이 정상" 인 조회의 결과입니다(`TOptional` · `std::optional` 대응). 이유가 둘 이상이면 `expected<T, E>` 를 쓴다. */
    template <typename T>
    using optional = expected<T, Absent>;

    namespace internal
    {
        template <typename T>
        struct ExpectedTraits : std::false_type
        {
        };
        template <typename T, typename E>
        struct ExpectedTraits<expected<T, E>> : std::true_type
        {
        };

        template <typename T>
        struct UnexpectedTraits : std::false_type
        {
        };
        template <typename E>
        struct UnexpectedTraits<unexpected<E>> : std::true_type
        {
        };

        /** @brief 틀린 쪽을 읽었을 때 멈춥니다(std 는 `bad_expected_access` 를 던진다 — 엔진은 예외를 쓰지 않는다). */
        [[noreturn]] inline void failExpectedAccess( const utf8* pWhat )
        {
            SW_LOW_LEVEL_FATAL( "sw::expected: %s", pWhat );
            std::abort();
        }
    } // namespace internal

    /**
     * @class expected
     * @brief 값 `T` 또는 실패 이유 `E` 중 하나를 듭니다(`std::expected` · 언리얼 `TValueOrError` 대응).
     * @tparam T 값 형식(참조 · 배열 불가). 값이 없는 성공은 `expected<void, E>`
     * @tparam E 실패 형식. 기본은 `Error`, "없음" 하나뿐이면 `Absent`(`optional<T>`)
     */
    template <typename T, typename E>
    class [[nodiscard]] expected
    {
        static_assert( std::is_object_v<T> && std::is_array_v<T> == false && internal::UnexpectedTraits<std::remove_cv_t<T>>::value == false &&
                           std::is_same_v<std::remove_cv_t<T>, nullopt_t> == false,
                       "expected<T, E>: T must be a plain object type" );
        static_assert( std::is_object_v<E> && std::is_array_v<E> == false && std::is_const_v<E> == false, "expected<T, E>: E must be a plain object type" );

        template <typename U>
        static constexpr bool kIsValueArgument = std::is_constructible_v<T, U&&> && std::is_same_v<std::decay_t<U>, expected> == false &&
                                                 internal::UnexpectedTraits<std::decay_t<U>>::value == false &&
                                                 std::is_same_v<std::decay_t<U>, nullopt_t> == false;

    public:
        using value_type      = T;
        using error_type      = E;
        using unexpected_type = unexpected<E>;
        template <typename U>
        using rebind = expected<U, E>;

        /** @brief `optional<T>` 만: "없음" 으로 만듭니다. `expected<T, E>` 는 기본 생성자가 없다(파일 머리 참고). */
        template <typename G = E, std::enable_if_t<std::is_same_v<G, Absent>, int32> = 0>
        expected() noexcept
            : _bHasValue{ SW_FALSE }
        {
            ::new ( static_cast<void*>( &_storage._error ) ) E{};
        }

        /** @brief `optional<T>` 만: `return nullopt;` */
        template <typename G = E, std::enable_if_t<std::is_same_v<G, Absent>, int32> = 0>
        expected( nullopt_t ) noexcept
            : expected()
        {
        }

        /** @brief 값으로 만듭니다. `return value;` 가 되도록 암시 변환이다. */
        template <typename U = T, std::enable_if_t<kIsValueArgument<U> && std::is_convertible_v<U&&, T>, int32> = 0>
        expected( U&& value )
            : _bHasValue{ SW_TRUE }
        {
            ::new ( static_cast<void*>( &_storage._value ) ) T( std::forward<U>( value ) );
        }

        /** @brief 암시 변환이 안 되는 값은 명시적으로 만듭니다. */
        template <typename U = T, std::enable_if_t<kIsValueArgument<U> && std::is_convertible_v<U&&, T> == false, int32> = 0>
        explicit expected( U&& value )
            : _bHasValue{ SW_TRUE }
        {
            ::new ( static_cast<void*>( &_storage._value ) ) T( std::forward<U>( value ) );
        }

        template <typename G, std::enable_if_t<std::is_constructible_v<E, const G&>, int32> = 0>
        expected( const unexpected<G>& failure )
            : _bHasValue{ SW_FALSE }
        {
            ::new ( static_cast<void*>( &_storage._error ) ) E( failure.error() );
        }

        template <typename G, std::enable_if_t<std::is_constructible_v<E, G&&>, int32> = 0>
        expected( unexpected<G>&& failure )
            : _bHasValue{ SW_FALSE }
        {
            ::new ( static_cast<void*>( &_storage._error ) ) E( std::move( failure ).error() );
        }

        expected( const expected& other )
            : _bHasValue{ other._bHasValue }
        {
            if ( _bHasValue )
                ::new ( static_cast<void*>( &_storage._value ) ) T( other._storage._value );
            else
                ::new ( static_cast<void*>( &_storage._error ) ) E( other._storage._error );
        }

        expected( expected&& other ) noexcept( std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_constructible_v<E> )
            : _bHasValue{ other._bHasValue }
        {
            if ( _bHasValue )
                ::new ( static_cast<void*>( &_storage._value ) ) T( std::move( other._storage._value ) );
            else
                ::new ( static_cast<void*>( &_storage._error ) ) E( std::move( other._storage._error ) );
        }

        ~expected() { destroy(); }

        expected& operator=( const expected& other )
        {
            if ( this == &other )
                return *this;
            if ( _bHasValue && other._bHasValue )
                _storage._value = other._storage._value;
            else if ( _bHasValue == false && other._bHasValue == false )
                _storage._error = other._storage._error;
            else
            {
                destroy();
                constructFrom( other );
            }
            return *this;
        }

        expected& operator=( expected&& other ) noexcept( std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_constructible_v<E> &&
                                                          std::is_nothrow_move_assignable_v<T> && std::is_nothrow_move_assignable_v<E> )
        {
            if ( this == &other )
                return *this;
            if ( _bHasValue && other._bHasValue )
                _storage._value = std::move( other._storage._value );
            else if ( _bHasValue == false && other._bHasValue == false )
                _storage._error = std::move( other._storage._error );
            else
            {
                destroy();
                constructFrom( std::move( other ) );
            }
            return *this;
        }

        template <typename U = T, std::enable_if_t<kIsValueArgument<U> && std::is_assignable_v<T&, U&&>, int32> = 0>
        expected& operator=( U&& value )
        {
            if ( _bHasValue )
                _storage._value = std::forward<U>( value );
            else
                emplace( std::forward<U>( value ) );
            return *this;
        }

        template <typename G, std::enable_if_t<std::is_constructible_v<E, G&&>, int32> = 0>
        expected& operator=( unexpected<G>&& failure )
        {
            destroy();
            ::new ( static_cast<void*>( &_storage._error ) ) E( std::move( failure ).error() );
            _bHasValue = SW_FALSE;
            return *this;
        }

        /** @brief `optional<T>` 만: 비웁니다(`x = nullopt;`). */
        template <typename G = E, std::enable_if_t<std::is_same_v<G, Absent>, int32> = 0>
        expected& operator=( nullopt_t ) noexcept
        {
            reset();
            return *this;
        }

        /** @brief `optional<T>` 만: 비웁니다(`TOptional::Reset` · `std::optional::reset`). */
        template <typename G = E, std::enable_if_t<std::is_same_v<G, Absent>, int32> = 0>
        void reset() noexcept
        {
            destroy();
            ::new ( static_cast<void*>( &_storage._error ) ) E{};
            _bHasValue = SW_FALSE;
        }

        /** @brief 있던 것을 지우고 그 자리에 값을 만듭니다. */
        template <typename... Args>
        T& emplace( Args&&... args )
        {
            destroy();
            ::new ( static_cast<void*>( &_storage._value ) ) T( std::forward<Args>( args )... );
            _bHasValue = SW_TRUE;
            return _storage._value;
        }

        // --- 관찰 ---

        [[nodiscard]] bool has_value() const noexcept { return _bHasValue; }
        explicit           operator bool() const noexcept { return _bHasValue; }

        [[nodiscard]] const T* operator->() const noexcept
        {
            SW_ASSERT( _bHasValue );
            return &_storage._value;
        }
        [[nodiscard]] T* operator->() noexcept
        {
            SW_ASSERT( _bHasValue );
            return &_storage._value;
        }
        [[nodiscard]] const T& operator*() const& noexcept
        {
            SW_ASSERT( _bHasValue );
            return _storage._value;
        }
        [[nodiscard]] T& operator*() & noexcept
        {
            SW_ASSERT( _bHasValue );
            return _storage._value;
        }
        [[nodiscard]] T&& operator*() && noexcept
        {
            SW_ASSERT( _bHasValue );
            return std::move( _storage._value );
        }

        [[nodiscard]] const T& value() const&
        {
            requireValue();
            return _storage._value;
        }
        [[nodiscard]] T& value() &
        {
            requireValue();
            return _storage._value;
        }
        [[nodiscard]] T&& value() &&
        {
            requireValue();
            return std::move( _storage._value );
        }

        [[nodiscard]] const E& error() const&
        {
            requireError();
            return _storage._error;
        }
        [[nodiscard]] E& error() &
        {
            requireError();
            return _storage._error;
        }
        [[nodiscard]] E&& error() &&
        {
            requireError();
            return std::move( _storage._error );
        }

        /** @brief 값이 있으면 값, 없으면 @p fallback 입니다. 필수 값의 폴백으로 쓰지 않는다 — "없음이 정상" 인 조회에서 기본을 고를 때만. */
        template <typename U>
        [[nodiscard]] T value_or( U&& fallback ) const&
        {
            return _bHasValue ? _storage._value : static_cast<T>( std::forward<U>( fallback ) );
        }
        template <typename U>
        [[nodiscard]] T value_or( U&& fallback ) &&
        {
            return _bHasValue ? std::move( _storage._value ) : static_cast<T>( std::forward<U>( fallback ) );
        }

        // --- 잇기(C++23 단항 연산) ---

        /** @brief 값이 있으면 `f( value )`(같은 `E` 의 `expected` 를 돌려줘야 한다), 없으면 오류를 그대로 넘깁니다. */
        template <typename F>
        auto and_then( F&& f ) &
        {
            return chainValue( *this, std::forward<F>( f ) );
        }
        template <typename F>
        auto and_then( F&& f ) const&
        {
            return chainValue( *this, std::forward<F>( f ) );
        }
        template <typename F>
        auto and_then( F&& f ) &&
        {
            return chainValue( std::move( *this ), std::forward<F>( f ) );
        }

        /** @brief 값이 있으면 `f( value )` 를 값으로 담은 `expected<U, E>`, 없으면 오류를 그대로 넘깁니다. */
        template <typename F>
        auto transform( F&& f ) &
        {
            return transformImpl( *this, std::forward<F>( f ) );
        }
        template <typename F>
        auto transform( F&& f ) const&
        {
            return transformImpl( *this, std::forward<F>( f ) );
        }
        template <typename F>
        auto transform( F&& f ) &&
        {
            return transformImpl( std::move( *this ), std::forward<F>( f ) );
        }

        /** @brief 실패면 `f( error )`(같은 `T` 의 `expected` 를 돌려줘야 한다), 값이면 그대로 넘깁니다. */
        template <typename F>
        auto or_else( F&& f ) &
        {
            return recoverError( *this, std::forward<F>( f ) );
        }
        template <typename F>
        auto or_else( F&& f ) const&
        {
            return recoverError( *this, std::forward<F>( f ) );
        }
        template <typename F>
        auto or_else( F&& f ) &&
        {
            return recoverError( std::move( *this ), std::forward<F>( f ) );
        }

        // --- 비교 ---

        template <typename U, std::enable_if_t<internal::ExpectedTraits<std::decay_t<U>>::value == false && internal::UnexpectedTraits<std::decay_t<U>>::value == false &&
                                                   std::is_same_v<std::decay_t<U>, nullopt_t> == false,
                                               int32> = 0>
        friend bool operator==( const expected& self, const U& value )
        {
            return self._bHasValue && static_cast<bool>( self._storage._value == value );
        }
        template <typename U, std::enable_if_t<internal::ExpectedTraits<std::decay_t<U>>::value == false && internal::UnexpectedTraits<std::decay_t<U>>::value == false &&
                                                   std::is_same_v<std::decay_t<U>, nullopt_t> == false,
                                               int32> = 0>
        friend bool operator!=( const expected& self, const U& value )
        {
            return ( self == value ) == false;
        }
        friend bool operator==( const expected& self, const expected& other )
        {
            if ( self._bHasValue != other._bHasValue )
                return false;
            return self._bHasValue ? static_cast<bool>( self._storage._value == other._storage._value ) : static_cast<bool>( self._storage._error == other._storage._error );
        }
        friend bool operator!=( const expected& self, const expected& other ) { return ( self == other ) == false; }
        friend bool operator==( const expected& self, nullopt_t ) noexcept { return self._bHasValue == false; }
        friend bool operator!=( const expected& self, nullopt_t ) noexcept { return self._bHasValue; }

    private:
        template <typename Self, typename F>
        static auto chainValue( Self&& self, F&& f )
        {
            using Result = std::remove_cv_t<std::remove_reference_t<std::invoke_result_t<F, decltype( ( std::forward<Self>( self )._storage._value ) )>>>;
            static_assert( internal::ExpectedTraits<Result>::value, "and_then: the function must return an expected" );
            static_assert( std::is_same_v<typename Result::error_type, E>, "and_then: the function must return the same error type" );
            if ( self._bHasValue )
                return Result( std::invoke( std::forward<F>( f ), std::forward<Self>( self )._storage._value ) );
            return Result( unexpected<E>( std::forward<Self>( self )._storage._error ) );
        }

        template <typename Self, typename F>
        static auto transformImpl( Self&& self, F&& f )
        {
            using U      = std::remove_cv_t<std::remove_reference_t<std::invoke_result_t<F, decltype( ( std::forward<Self>( self )._storage._value ) )>>>;
            using Result = expected<U, E>;
            if ( self._bHasValue == false )
                return Result( unexpected<E>( std::forward<Self>( self )._storage._error ) );
            if constexpr ( std::is_void_v<U> )
            {
                std::invoke( std::forward<F>( f ), std::forward<Self>( self )._storage._value );
                return Result();
            }
            else
                return Result( std::invoke( std::forward<F>( f ), std::forward<Self>( self )._storage._value ) );
        }

        template <typename Self, typename F>
        static auto recoverError( Self&& self, F&& f )
        {
            using Result = std::remove_cv_t<std::remove_reference_t<std::invoke_result_t<F, decltype( ( std::forward<Self>( self )._storage._error ) )>>>;
            static_assert( internal::ExpectedTraits<Result>::value, "or_else: the function must return an expected" );
            static_assert( std::is_same_v<typename Result::value_type, T>, "or_else: the function must return the same value type" );
            if ( self._bHasValue )
                return Result( std::forward<Self>( self )._storage._value );
            return Result( std::invoke( std::forward<F>( f ), std::forward<Self>( self )._storage._error ) );
        }

        void requireValue() const
        {
            if ( _bHasValue == false )
                internal::failExpectedAccess( "value() on a result that holds an error" );
        }

        void requireError() const
        {
            if ( _bHasValue )
                internal::failExpectedAccess( "error() on a result that holds a value" );
        }

        void destroy() noexcept
        {
            if ( _bHasValue )
                _storage._value.~T();
            else
                _storage._error.~E();
        }

        template <typename Other>
        void constructFrom( Other&& other )
        {
            if ( other._bHasValue )
                ::new ( static_cast<void*>( &_storage._value ) ) T( std::forward<Other>( other )._storage._value );
            else
                ::new ( static_cast<void*>( &_storage._error ) ) E( std::forward<Other>( other )._storage._error );
            _bHasValue = other._bHasValue;
        }

    private:
        union Storage
        {
        public:
            T _value;
            E _error;

        public:
            Storage() noexcept {}
            ~Storage() {}
        };

        Storage _storage;       ///< 값 또는 오류(`_bHasValue` 가 고른다)
        uint8   _bHasValue : 1; ///< `_storage` 가 값이면 true
    };

    /**
     * @class expected<void, E>
     * @brief 값 없는 성공 또는 실패 이유입니다(`bool f( …, string& outError )` 의 자리).
     */
    template <typename E>
    class [[nodiscard]] expected<void, E>
    {
        static_assert( std::is_object_v<E> && std::is_array_v<E> == false && std::is_const_v<E> == false, "expected<void, E>: E must be a plain object type" );
        static_assert( std::is_same_v<E, Absent> == false, "expected<void, Absent> says nothing: use bool" );

    public:
        using value_type      = void;
        using error_type      = E;
        using unexpected_type = unexpected<E>;
        template <typename U>
        using rebind = expected<U, E>;

        /** @brief 성공입니다(`return {};`). */
        expected() noexcept
            : _bHasValue{ SW_TRUE }
        {
        }

        template <typename G, std::enable_if_t<std::is_constructible_v<E, const G&>, int32> = 0>
        expected( const unexpected<G>& failure )
            : _bHasValue{ SW_FALSE }
        {
            ::new ( static_cast<void*>( &_storage._error ) ) E( failure.error() );
        }

        template <typename G, std::enable_if_t<std::is_constructible_v<E, G&&>, int32> = 0>
        expected( unexpected<G>&& failure )
            : _bHasValue{ SW_FALSE }
        {
            ::new ( static_cast<void*>( &_storage._error ) ) E( std::move( failure ).error() );
        }

        expected( const expected& other )
            : _bHasValue{ other._bHasValue }
        {
            if ( _bHasValue == false )
                ::new ( static_cast<void*>( &_storage._error ) ) E( other._storage._error );
        }

        expected( expected&& other ) noexcept( std::is_nothrow_move_constructible_v<E> )
            : _bHasValue{ other._bHasValue }
        {
            if ( _bHasValue == false )
                ::new ( static_cast<void*>( &_storage._error ) ) E( std::move( other._storage._error ) );
        }

        ~expected() { destroy(); }

        expected& operator=( const expected& other )
        {
            if ( this != &other )
            {
                destroy();
                _bHasValue = other._bHasValue;
                if ( _bHasValue == false )
                    ::new ( static_cast<void*>( &_storage._error ) ) E( other._storage._error );
            }
            return *this;
        }

        expected& operator=( expected&& other ) noexcept( std::is_nothrow_move_constructible_v<E> )
        {
            if ( this != &other )
            {
                destroy();
                _bHasValue = other._bHasValue;
                if ( _bHasValue == false )
                    ::new ( static_cast<void*>( &_storage._error ) ) E( std::move( other._storage._error ) );
            }
            return *this;
        }

        /** @brief 성공으로 되돌립니다. */
        void emplace() noexcept
        {
            destroy();
            _bHasValue = SW_TRUE;
        }

        [[nodiscard]] bool has_value() const noexcept { return _bHasValue; }
        explicit           operator bool() const noexcept { return _bHasValue; }
        void               operator*() const noexcept { SW_ASSERT( _bHasValue ); }

        void value() const
        {
            if ( _bHasValue == false )
                internal::failExpectedAccess( "value() on a result that holds an error" );
        }

        [[nodiscard]] const E& error() const&
        {
            requireError();
            return _storage._error;
        }
        [[nodiscard]] E& error() &
        {
            requireError();
            return _storage._error;
        }
        [[nodiscard]] E&& error() &&
        {
            requireError();
            return std::move( _storage._error );
        }

        /** @brief 성공이면 `f()`(같은 `E` 의 `expected` 를 돌려줘야 한다), 실패면 오류를 넘깁니다. */
        template <typename F>
        auto and_then( F&& f ) const&
        {
            using Result = std::remove_cv_t<std::remove_reference_t<std::invoke_result_t<F>>>;
            static_assert( internal::ExpectedTraits<Result>::value, "and_then: the function must return an expected" );
            static_assert( std::is_same_v<typename Result::error_type, E>, "and_then: the function must return the same error type" );
            if ( _bHasValue )
                return Result( std::invoke( std::forward<F>( f ) ) );
            return Result( unexpected<E>( _storage._error ) );
        }

        /** @brief 성공이면 `f()` 를 값으로 담은 `expected<U, E>`, 실패면 오류를 넘깁니다. */
        template <typename F>
        auto transform( F&& f ) const&
        {
            using U      = std::remove_cv_t<std::remove_reference_t<std::invoke_result_t<F>>>;
            using Result = expected<U, E>;
            if ( _bHasValue == false )
                return Result( unexpected<E>( _storage._error ) );
            if constexpr ( std::is_void_v<U> )
            {
                std::invoke( std::forward<F>( f ) );
                return Result();
            }
            else
                return Result( std::invoke( std::forward<F>( f ) ) );
        }

        /** @brief 실패면 `f( error )`(`expected<void, G>` 를 돌려줘야 한다), 성공이면 성공을 넘깁니다. */
        template <typename F>
        auto or_else( F&& f ) const&
        {
            using Result = std::remove_cv_t<std::remove_reference_t<std::invoke_result_t<F, const E&>>>;
            static_assert( internal::ExpectedTraits<Result>::value && std::is_void_v<typename Result::value_type>, "or_else: the function must return an expected<void, G>" );
            if ( _bHasValue )
                return Result();
            return Result( std::invoke( std::forward<F>( f ), _storage._error ) );
        }

    private:
        void requireError() const
        {
            if ( _bHasValue )
                internal::failExpectedAccess( "error() on a result that holds a value" );
        }

        void destroy() noexcept
        {
            if ( _bHasValue == false )
                _storage._error.~E();
        }

    private:
        union Storage
        {
        public:
            E _error;

        public:
            Storage() noexcept {}
            ~Storage() {}
        };

        Storage _storage;       ///< 실패일 때만 오류가 산다
        uint8   _bHasValue : 1; ///< 성공이면 true
    };
} // namespace sw
