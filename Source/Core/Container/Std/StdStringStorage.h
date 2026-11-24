/**
 * @file StdStringStorage.h
 * @brief sw::basic_string 의 std 저장 형식입니다(`SW_ENABLE_STL_CONTAINER`). std::basic_string 을 멤버로 듭니다. 쓰는 쪽은 `Core/Container/string.h` 를 include 합니다.
 * @details 짧은 글 길이 · 성장 정책은 표준 라이브러리의 것이다. 나머지 계약(범위 밖 위치 · 자기 글의 일부 넣기 · 옮긴 뒤 빈 글)은 `basic_string` 이 자체 저장 형식과 같게 낸다.
 */
#pragma once
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw::internal
{
    /** @brief 글자를 std::basic_string 에 맡깁니다. 레이스 · 범위 검사는 `basic_string` 이 합니다. */
    template <typename CharT, typename Traits, typename Allocator>
    class StringStorage
    {
    public:
        /**
         * @brief 표준 라이브러리의 문자열이 바이트로 옮겨도 되는 구현인지입니다.
         * @details MSVC STL(반복자 디버그 꺼짐)과 libc++ 는 자기 주소를 들지 않는다(libc++ 는 스스로 그렇게 표시한다 — ASan 은 짧은 글 버퍼에 독을 바르므로 뺀다).
         *          libstdc++ 는 짧은 글일 때 포인터가 자기 안의 버퍼를 가리키고, MSVC 반복자 디버그는 컨테이너 대리 객체가 이 객체의 주소를 든다.
         */
#if defined( _MSVC_STL_VERSION )
        static constexpr bool kBitwiseRelocatable = _ITERATOR_DEBUG_LEVEL == 0;
#elif defined( _LIBCPP_VERSION )
    #if __has_feature( address_sanitizer )
        static constexpr bool kBitwiseRelocatable = false;
    #else
        static constexpr bool kBitwiseRelocatable = true;
    #endif
#else
        static constexpr bool kBitwiseRelocatable = false;
#endif

        StringStorage() noexcept              = default;
        StringStorage( const StringStorage& ) = delete; // 복사는 `basic_string` 이 `initialize` 로 한다
        StringStorage( StringStorage&& other ) noexcept
            : _text{ std::move( other._text ) }
        {
            other._text.clear(); // 옮긴 뒤 빈 글(자체 저장 형식과 같은 계약)
        }
        ~StringStorage()                                 = default;
        StringStorage& operator=( const StringStorage& ) = delete;
        /** @brief @p other 의 글을 가져오고 @p other 를 빈 글로 둡니다. */
        StringStorage& operator=( StringStorage&& other ) noexcept
        {
            if ( this != &other )
            {
                _text = std::move( other._text );
                other._text.clear();
            }
            return *this;
        }

        CharT*       data() noexcept { return _text.data(); }
        const CharT* data() const noexcept { return _text.data(); }
        size_t       size() const noexcept { return _text.size(); }
        size_t       capacity() const noexcept { return _text.capacity(); }
        size_t       max_size() const noexcept { return _text.max_size(); }

        /** @brief 빈 글을 @p count 글자로 채웁니다(생성자가 쓴다). */
        void initialize( const CharT* pSource, size_t count )
        {
            if ( count > 0 )
                _text.assign( pSource, count );
        }

        void reserve( size_t newCapacity ) { _text.reserve( newCapacity ); }
        void shrink_to_fit() { _text.shrink_to_fit(); }
        /** @brief 글자 수를 @p count(지금 글자 수 이하)로 줄입니다. */
        void truncate( size_t count ) noexcept { _text.resize( count ); }
        void push_back( CharT ch ) { _text.push_back( ch ); }

        /** @brief [pos, pos + removeCount) 를 @p insertCount 칸의 자리로 바꾸고 그 자리를 돌려줍니다(글자는 부르는 쪽이 채운다). */
        CharT* openGap( size_t pos, size_t removeCount, size_t insertCount )
        {
            _text.replace( pos, removeCount, insertCount, CharT() );
            return _text.data() + pos;
        }

        void swap( StringStorage& other ) noexcept { _text.swap( other._text ); }

    private:
        std::basic_string<CharT, Traits, Allocator> _text;
    };
} // namespace sw::internal
