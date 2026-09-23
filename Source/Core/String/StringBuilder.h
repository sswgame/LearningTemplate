/**
 * @file StringBuilder.h
 * @brief 고정 크기 스택 버퍼로 시작해 용량을 넘으면 힙으로 늘어나는 문자열 빌더(sw::StringBuilder)입니다.
 *
 * [구조와 최적화]
 * 1. 스택 · 힙 혼합 저장: 템플릿 인자 `Capacity` 크기의 스택 배열(`_arrStaticBuffer`)에서 할당 없이 시작합니다.
 * 2. 용량 증가(`ensureCapacity`): 스택 용량을 넘을 때만 힙 버퍼(`_pDynamicBuffer`)를 두 배씩 늘려 할당하고 포인터를 바꿉니다.
 * 3. 제자리 포맷(`appendFormat`): 타입 안전 포맷터로 스택 · 힙 버퍼에 바로 포맷해 임시 std::string 을 만들지 않습니다.
 * 4. 이동: 힙 버퍼를 가지고 있으면 복사 없이 포인터만 넘깁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    template <uint32 Capacity = 256>
    /**
     * @class StringBuilder
     * @brief 처음에는 고정 용량 스택 버퍼로 할당 없이 동작하고, 필요하면 힙 버퍼로 자동으로 늘어나는 문자열 빌더입니다.
     */
    class StringBuilder
    {
    public:
        /**
         * @brief 스택 버퍼(_arrStaticBuffer)를 처음 버퍼로 두고 널 문자로 끝냅니다.
         * @note `_arrStaticBuffer` 는 **일부러 값 초기화하지 않습니다.** 아래에서 `[0] = '\0'` 으로 널 종료를 세우고, 이후 모든
         *       쓰기가 `_length` 와 종료 문자를 함께 유지하므로 `Capacity` 바이트를 0 으로 채우는 것은 낭비입니다. 직렬화는
         *       스칼라 값 하나마다 StringBuilder<8192> 를 만들기 때문에, 값 하나를 쓸 때마다 8KB memset 을 하고 있었습니다.
         */
        StringBuilder() noexcept
            : _pDynamicBuffer{ nullptr }
            , _pBuffer{ _arrStaticBuffer }
            , _capacity{ Capacity }
            , _length{ 0 }
        {
            _arrStaticBuffer[0] = '\0';
        }

        /**
         * @brief 힙 버퍼를 할당했으면 해제합니다.
         */
        ~StringBuilder()
        {
            if ( _pDynamicBuffer != nullptr )
            {
                Memory::free( _pDynamicBuffer );
                _pDynamicBuffer = nullptr;
            }
        }

        /** @brief 복사를 금지합니다. */
        StringBuilder( const StringBuilder& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        StringBuilder& operator=( const StringBuilder& ) = delete;

        /**
         * @brief 힙 버퍼를 가지고 있으면 포인터를 넘겨받고, 스택 버퍼면 내용을 복사합니다.
         */
        StringBuilder( StringBuilder&& other ) noexcept
            : _pDynamicBuffer{ other._pDynamicBuffer }
            , _pBuffer{ _arrStaticBuffer }
            , _capacity{ other._capacity }
            , _length{ other._length }

        {
            if ( other._pDynamicBuffer != nullptr )
            {
                _pBuffer                  = other._pDynamicBuffer;
                other._pDynamicBuffer     = nullptr;
                other._pBuffer            = other._arrStaticBuffer;
                other._capacity           = Capacity;
                other._length             = 0;
                other._arrStaticBuffer[0] = '\0';
            }
            else
            {
                Memory::copy( _arrStaticBuffer, other._arrStaticBuffer, other._length + 1 );
                _pBuffer                  = _arrStaticBuffer;
                other._length             = 0;
                other._arrStaticBuffer[0] = '\0';
            }
        }

        /**
         * @brief 가지고 있던 힙 버퍼를 정리한 뒤, 상대의 버퍼를 넘겨받습니다.
         */
        StringBuilder& operator=( StringBuilder&& other ) noexcept
        {
            if ( this != &other )
            {
                if ( _pDynamicBuffer != nullptr )
                    Memory::free( _pDynamicBuffer );

                _pDynamicBuffer = other._pDynamicBuffer;
                _capacity       = other._capacity;
                _length         = other._length;

                if ( other._pDynamicBuffer != nullptr )
                {
                    _pBuffer                  = other._pDynamicBuffer;
                    other._pDynamicBuffer     = nullptr;
                    other._pBuffer            = other._arrStaticBuffer;
                    other._capacity           = Capacity;
                    other._length             = 0;
                    other._arrStaticBuffer[0] = '\0';
                }
                else
                {
                    Memory::copy( _arrStaticBuffer, other._arrStaticBuffer, other._length + 1 );
                    _pBuffer                  = _arrStaticBuffer;
                    other._length             = 0;
                    other._arrStaticBuffer[0] = '\0';
                }
            }
            return *this;
        }

        /**
         * @brief 힙 버퍼를 할당하거나 용량을 늘립니다(두 배씩 늘립니다).
         * @return 요청한 만큼 담을 수 있으면 true. **false 면 버퍼는 손대지 않은 그대로입니다.**
         * @details 예전에는 `Memory::allocate` 의 결과를 확인하지 않고 곧바로 `Memory::copy` 의 목적지로 넘겼습니다. 할당이
         *          실패하면(nullptr) nullptr 에 복사하고, 이어서 `_pBuffer` 가 nullptr 인 채로 `_capacity` 만 커져서 **그 뒤의 모든
         *          append 가 nullptr 에 씁니다.** 실패하면 아무것도 바꾸지 않는 쪽이 맞습니다. 지금까지 쌓은 내용이 그대로 남고,
         *          호출하는 쪽은 잘린 문자열을 보게 됩니다.
         *
         *          여기서 로그를 남기지 않는 이유는 **로거 자신이 이 클래스를 쓰기 때문입니다**(`formatString.h` 가 stderr 로 직접
         *          출력하는 것과 같은 이유입니다).
         */
        SW_NOINLINE bool ensureCapacity( uint32 additionalSize )
        {
            const uint32 requiredCapacity = _length + additionalSize + 1;
            if ( requiredCapacity <= _capacity )
                return true;

            uint32 newCapacity = MathUtil::max( _capacity * 2, 32u );
            while ( newCapacity < requiredCapacity )
            {
                newCapacity *= 2;
            }

            utf8* pNewBuffer = static_cast<utf8*>( Memory::allocate( newCapacity * sizeof( utf8 ) ) );
            if ( pNewBuffer == nullptr )
                return false;

            Memory::copy( pNewBuffer, _pBuffer, _length + 1 );

            if ( _pDynamicBuffer != nullptr )
                Memory::free( _pDynamicBuffer );

            _pDynamicBuffer = pNewBuffer;
            _pBuffer        = _pDynamicBuffer;
            _capacity       = newCapacity;
            return true;
        }

        /** @brief string_view 문자열을 뒤에 이어 붙입니다. */
        SW_INLINE StringBuilder& append( string_view sv )
        {
            if ( sv.empty() )
                return *this;

            const uint32 svSize = static_cast<uint32>( sv.size() );
            if ( ensureCapacity( svSize ) == false )
                return *this;

            Memory::copy( _pBuffer + _length, sv.data(), svSize );
            _length += svSize;
            _pBuffer[_length] = '\0';
            return *this;
        }

        /** @brief 널 종료 C 문자열을 뒤에 이어 붙입니다. */
        SW_INLINE StringBuilder& append( const utf8* pStr )
        {
            if ( pStr == nullptr )
                return *this;

            const uint32 strLen = StringUtil::strlen( pStr );
            return append( pStr, strLen );
        }

        /** @brief 길이가 정해진 C 문자열을 뒤에 이어 붙입니다(strlen 비용을 아낍니다). */
        SW_INLINE StringBuilder& append( const utf8* pStr, const uint32 strLen )
        {
            if ( pStr == nullptr || strLen == 0 )
                return *this;

            if ( ensureCapacity( strLen ) == false )
                return *this;

            Memory::copy( _pBuffer + _length, pStr, strLen );
            _length += strLen;
            _pBuffer[_length] = '\0';
            return *this;
        }

        /** @brief 문자 하나를 뒤에 붙입니다. */
        SW_INLINE StringBuilder& append( const utf8 c )
        {
            if ( ensureCapacity( 1 ) == false )
                return *this;

            _pBuffer[_length++] = c;
            _pBuffer[_length]   = '\0';
            return *this;
        }

        /**
         * @brief 32비트 정수를 버퍼에 할당 없이 포맷해 이어 붙입니다.
         */
        SW_INLINE StringBuilder& append( const int32 val )
        {
            if ( ensureCapacity( constant::kMaxBuffer16 ) == false )
                return *this;

            _length += StringUtil::formatNumber( _pBuffer + _length, _capacity - _length, val );
            return *this;
        }

        /**
         * @brief 32비트 부호 없는 정수를 버퍼에 할당 없이 포맷해 이어 붙입니다.
         */
        SW_INLINE StringBuilder& append( const uint32 val )
        {
            if ( ensureCapacity( constant::kMaxBuffer16 ) == false )
                return *this;

            _length += StringUtil::formatNumber( _pBuffer + _length, _capacity - _length, val );
            return *this;
        }

        /**
         * @brief 64비트 정수를 버퍼에 할당 없이 포맷해 이어 붙입니다.
         */
        SW_INLINE StringBuilder& append( const int64 val )
        {
            if ( ensureCapacity( constant::kMaxBuffer32 ) == false )
                return *this;

            _length += StringUtil::formatNumber( _pBuffer + _length, _capacity - _length, val );
            return *this;
        }

        /**
         * @brief 64비트 부호 없는 정수를 버퍼에 할당 없이 포맷해 이어 붙입니다.
         */
        SW_INLINE StringBuilder& append( const uint64 val )
        {
            if ( ensureCapacity( constant::kMaxBuffer32 ) == false )
                return *this;

            _length += StringUtil::formatNumber( _pBuffer + _length, _capacity - _length, val );
            return *this;
        }

        /** @brief 32비트 실수를 버퍼에 할당 없이 포맷해 이어 붙입니다. */
        SW_INLINE StringBuilder& append( const float32 val )
        {
            if ( ensureCapacity( constant::kMaxBuffer32 ) == false )
                return *this;

            _length += StringUtil::formatNumber( _pBuffer + _length, _capacity - _length, val );
            return *this;
        }

        /** @brief 64비트 실수를 버퍼에 할당 없이 포맷해 이어 붙입니다. */
        SW_INLINE StringBuilder& append( const float64 val )
        {
            if ( ensureCapacity( constant::kMaxBuffer64 ) == false )
                return *this;

            _length += StringUtil::formatNumber( _pBuffer + _length, _capacity - _length, val );
            return *this;
        }

        /** @brief 포맷 문자열과 가변 인자를 포맷해 이어 붙입니다. */
        template <typename... Args>
        SW_INLINE StringBuilder& appendFormat( string_view format, Args&&... args )
        {
            if ( format.empty() )
                return *this;

            uint32 available = _capacity - _length;
            if ( available < 2 )
            {
                if ( ensureCapacity( 256 ) == false )
                    return *this;

                available = _capacity - _length;
            }

            for ( ;; )
            {
                // 재시도 루프라서 **forward 하지 않는다.** std::forward 는 한 번만 쓰기로 한 약속인데, 여기서는 버퍼가 모자라면
                // 같은 인자 팩을 다시 넘긴다. 지금은 formatstring 이 인자를 읽기만 해서 문제가 없지만, 인자를 옮길 여지가 생기는
                // 순간 두 번째 시도가 빈 값을 출력한다.
                formatstring( _pBuffer + _length, available, format, args... );
                const uint32 written = StringUtil::strlen( _pBuffer + _length );

                // formatstring 이 available - 1 까지 꽉 채웠다면 버퍼가 모자랐을 수 있으므로, 늘린 뒤 다시 시도한다

                if ( written < available - 1 )
                {
                    _length += written;
                    return *this;
                }

                _pBuffer[_length] = '\0';

                // 늘리지 못하면 **여기서 끝낸다.** 예전에는 실패를 확인하지 않아 `available` 이 그대로였고, 같은 크기로 다시 포맷하고
                // 다시 늘리려는 루프가 끝없이 돌았다.
                if ( ensureCapacity( available ) == false )
                    return *this;

                available = _capacity - _length;
            }
        }

        /** @brief 널 종료 C 문자열 포인터를 반환합니다. */
        SW_INLINE const utf8* c_str() const noexcept { return _pBuffer; }

        /** @brief 현재 내용을 string_view 로 반환합니다. */
        SW_INLINE string_view view() const noexcept { return string_view( _pBuffer, _length ); }

        /** @brief 현재 문자열 길이를 반환합니다(널 문자 제외). */
        SW_INLINE uint32 size() const noexcept { return _length; }

        /** @brief 현재 버퍼 용량을 반환합니다. */
        SW_INLINE uint32 capacity() const noexcept { return _capacity; }

        /** @brief 내용을 비웁니다(버퍼 메모리는 그대로 둡니다). */
        SW_INLINE void clear() noexcept
        {
            _length     = 0;
            _pBuffer[0] = '\0';
        }

    private:
        utf8   _arrStaticBuffer[Capacity];
        utf8*  _pDynamicBuffer;
        utf8*  _pBuffer;
        uint32 _capacity;
        uint32 _length;
    };
} // namespace sw
