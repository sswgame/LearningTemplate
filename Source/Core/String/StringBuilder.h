/**
 * @file StringBuilder.h
 * @brief 고정 크기 스택 버퍼(Small String Optimization)로 시작해 용량 초과 시 힙으로 확장하는 고성능 문자열 빌더(sw::StringBuilder)
 *
 * [주요 아키텍처 및 최적화 기법]:
 * 1. Hybrid Stack/Heap Storage: 템플릿 인자 `Capacity` 크기의 스택 정적 배열(`_staticBuffer`)에서 0-Alloc으로 시작.
 * 2. Dynamic Capacity Growth (`ensureCapacity`): 스택 용량을 초과할 때만 힙(`_dynamicBuffer`)을 2배씩 동적 할당하여 포인터 전환.
 * 3. In-Place Formatting (`appendFormat`): 타입 세이프 포맷터를 통해 스택/힙 버퍼에 직결 포맷팅하여 임시 std::string 생성을 배제.
 * 4. Move Semantics: 힙 버퍼 소유 시 0-Copy 포인터 이전 지원.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

namespace sw
{
    template <uint32 Capacity = 256>
    /**
     * @class StringBuilder
     * @brief 초기 고정 용량 스택 버퍼로 할당 없이 동작하며, 필요 시 힙 버퍼로 자동 확장하는 문자열 빌더
     */
    class StringBuilder
    {
    public:
        /**
         * @brief 스택 버퍼(_arrStaticBuffer)를 초기 버퍼로 지정하고 널 종단 문자로 초기화합니다.
         */
        StringBuilder() noexcept
            : _arrStaticBuffer{}
            , _pDynamicBuffer{ nullptr }
            , _pBuffer{ _arrStaticBuffer }
            , _capacity{ Capacity }
            , _length{ 0 }
        {
            _arrStaticBuffer[0] = '\0';
        }

        /**
         * @brief 소멸자: 힙 동적 버퍼가 할당되어 있다면 안전하게 해제합니다.
         */
        ~StringBuilder()
        {
            if ( _pDynamicBuffer != nullptr )
            {
                Memory::freeMemory( _pDynamicBuffer );
                _pDynamicBuffer = nullptr;
            }
        }

        /** @brief 복사 생성 금지 */
        StringBuilder( const StringBuilder& ) = delete;
        /** @brief 복사 대입 금지 */
        StringBuilder& operator=( const StringBuilder& ) = delete;

        /**
         * @brief 이동 생성자: 힙 버퍼가 있는 경우 포인터를 이전하고, 스택 버퍼인 경우 스택 버퍼를 복사합니다.
         */
        StringBuilder( StringBuilder&& other ) noexcept
            : _arrStaticBuffer{}
            , _pDynamicBuffer{ other._pDynamicBuffer }
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
         * @brief 이동 대입 연산자: 기존 힙 버퍼를 정리하고 우측 객체의 버퍼 상태를 소유권 이전합니다.
         */
        StringBuilder& operator=( StringBuilder&& other ) noexcept
        {
            if ( this != &other )
            {
                if ( _pDynamicBuffer != nullptr )
                {
                    Memory::freeMemory( _pDynamicBuffer );
                }

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
         * @brief 동적 버퍼 할당 및 용량 확장 (2배 지수 확장 전략)
         */
        SW_NOINLINE void ensureCapacity( uint32 additionalSize )
        {
            const uint32 requiredCapacity = _length + additionalSize + 1;
            if ( requiredCapacity <= _capacity )
                return;

            uint32 newCapacity = MathUtil::max( _capacity * 2, 32u );
            while ( newCapacity < requiredCapacity )
            {
                newCapacity *= 2;
            }

            utf8* pNewBuffer = static_cast<utf8*>( Memory::allocMemory( newCapacity * sizeof( utf8 ) ) );
            Memory::copy( pNewBuffer, _pBuffer, _length + 1 );

            if ( _pDynamicBuffer != nullptr )
                Memory::freeMemory( _pDynamicBuffer );

            _pDynamicBuffer = pNewBuffer;
            _pBuffer        = _pDynamicBuffer;
            _capacity       = newCapacity;
        }

        /** @brief string_view 문자열을 뒤에 이어 붙입니다. */
        SW_INLINE StringBuilder& append( string_view sv )
        {
            if ( sv.empty() )
                return *this;

            const uint32 svSize = static_cast<uint32>( sv.size() );
            ensureCapacity( svSize );
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

        /** @brief 길이 지정 C 문자열을 뒤에 이어 붙입니다 (strlen 오버헤드 생략). */
        SW_INLINE StringBuilder& append( const utf8* pStr, const uint32 strLen )
        {
            if ( pStr == nullptr || strLen == 0 )
                return *this;

            ensureCapacity( strLen );
            Memory::copy( _pBuffer + _length, pStr, strLen );
            _length += strLen;
            _pBuffer[_length] = '\0';
            return *this;
        }

        /** @brief 단일 문자를 뒤에 이어 붙입니다. */
        SW_INLINE StringBuilder& append( const utf8 c )
        {
            ensureCapacity( 1 );
            _pBuffer[_length++] = c;
            _pBuffer[_length]   = '\0';
            return *this;
        }

        /**
         * @brief 32비트 정수를 내부 버퍼에 0-Alloc으로 고속 포맷팅하여 이어 붙입니다.
         */
        SW_INLINE StringBuilder& append( const int32 val )
        {
            ensureCapacity( constant::kMaxBuffer16 );
            _length += StringUtil::formatNumber( _pBuffer + _length, _capacity - _length, val );
            return *this;
        }

        /**
         * @brief 32비트 부호없는 정수를 내부 버퍼에 0-Alloc으로 고속 포맷팅하여 이어 붙입니다.
         */
        SW_INLINE StringBuilder& append( const uint32 val )
        {
            ensureCapacity( constant::kMaxBuffer16 );
            _length += StringUtil::formatNumber( _pBuffer + _length, _capacity - _length, val );
            return *this;
        }

        /**
         * @brief 64비트 정수를 내부 버퍼에 0-Alloc으로 고속 포맷팅하여 이어 붙입니다.
         */
        SW_INLINE StringBuilder& append( const int64 val )
        {
            ensureCapacity( constant::kMaxBuffer32 );
            _length += StringUtil::formatNumber( _pBuffer + _length, _capacity - _length, val );
            return *this;
        }

        /**
         * @brief 64비트 부호없는 정수를 내부 버퍼에 0-Alloc으로 고속 포맷팅하여 이어 붙입니다.
         */
        SW_INLINE StringBuilder& append( const uint64 val )
        {
            ensureCapacity( constant::kMaxBuffer32 );
            _length += StringUtil::formatNumber( _pBuffer + _length, _capacity - _length, val );
            return *this;
        }

        /** @brief 32비트 실수를 내부 버퍼에 0-Alloc으로 고속 포맷팅하여 이어 붙입니다. */
        SW_INLINE StringBuilder& append( const float32 val )
        {
            ensureCapacity( constant::kMaxBuffer32 );
            _length += StringUtil::formatNumber( _pBuffer + _length, _capacity - _length, val );
            return *this;
        }

        /** @brief 64비트 실수를 내부 버퍼에 0-Alloc으로 고속 포맷팅하여 이어 붙입니다. */
        SW_INLINE StringBuilder& append( const float64 val )
        {
            ensureCapacity( constant::kMaxBuffer64 );
            _length += StringUtil::formatNumber( _pBuffer + _length, _capacity - _length, val );
            return *this;
        }

        /** @brief 포맷 문자열과 가변 인자를 이어 붙입니다. */
        template <typename... Args>
        SW_INLINE StringBuilder& appendFormat( string_view format, Args&&... args )
        {
            if ( format.empty() )
                return *this;

            uint32 available = _capacity - _length;
            if ( available < 2 )
            {
                ensureCapacity( 256 );
                available = _capacity - _length;
            }

            for ( ;; )
            {
                formatstring( _pBuffer + _length, available, format, std::forward<Args>( args )... );
                const uint32 written = StringUtil::strlen( _pBuffer + _length );

                // formatstring이 available-1까지 꽉 채워졌다면 버퍼가 부족했을 수 있으므로 확장 후 재시도

                if ( written < available - 1 )
                {
                    _length += written;
                    return *this;
                }

                _pBuffer[_length] = '\0';
                ensureCapacity( available );
                available = _capacity - _length;
            }
        }

        /** @brief 널 종료 C 문자열 포인터를 반환합니다. */
        SW_INLINE const utf8* c_str() const noexcept { return _pBuffer; }

        /** @brief 현재 내용을 가벼운 string_view로 반환합니다. */
        SW_INLINE string_view view() const noexcept { return string_view( _pBuffer, _length ); }

        /** @brief 현재 문자열 길이를 반환합니다 (널 제외). */
        SW_INLINE uint32 size() const noexcept { return _length; }

        /** @brief 현재 버퍼 용량을 반환합니다. */
        SW_INLINE uint32 capacity() const noexcept { return _capacity; }

        /** @brief 내용을 비웁니다 (버퍼 메모리는 유지). */
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
