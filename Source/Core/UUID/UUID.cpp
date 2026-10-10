#include "pch.h"

#include "Core/UUID/UUID.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include <random>

namespace sw
{
    namespace
    {
        struct UUIDInternal
        {
            static int32 hexNibble( utf8 c )
            {
                if ( c >= '0' && c <= '9' )
                    return c - '0';
                if ( c >= 'a' && c <= 'f' )
                    return 10 + ( c - 'a' );
                if ( c >= 'A' && c <= 'F' )
                    return 10 + ( c - 'A' );
                return -1;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UUID UUID::generate()
    {
        // 난수는 운영체제에서 곧바로 받는다(std::random_device — Windows 는 RtlGenRandom, 리눅스는 getrandom). 32 비트 씨앗 하나로
        // 시작한 mt19937_64 에서 뽑으면 서로 다른 실행이 같은 씨앗을 뽑을 때 **같은 GUID 열**이 다시 나온다(씨앗이 2^32 가지뿐이라 편집기를 수만
        // 번 열면 일어난다). 에셋 GUID 는 여러 사람의 작업이 저장소에서 합쳐지므로 겹치면 참조가 엉뚱한 에셋으로 풀린다. 언리얼의
        // FGuid::NewGuid 도 OS 의 GUID 를 쓴다. random_device 는 스레드 안전이 약속되지 않아 스레드마다 둔다.
        thread_local std::random_device t_randomDevice;

        UUID uuid{};
        for ( uint32 wordIndex = 0; wordIndex < 4; ++wordIndex )
        {
            const uint32 randomWord = static_cast<uint32>( t_randomDevice() );
            Memory::copy( uuid._arrBytes + wordIndex * 4, &randomWord, sizeof( randomWord ) );
        }

        // 버전 4
        uuid._arrBytes[6] = static_cast<uint8>( ( uuid._arrBytes[6] & 0x0f ) | 0x40 );
        // RFC 4122 변형(variant) 비트
        uuid._arrBytes[8] = static_cast<uint8>( ( uuid._arrBytes[8] & 0x3f ) | 0x80 );
        return uuid;
    }

    bool UUID::tryParse( string_view text, UUID& outUUID )
    {
        if ( text.size() != 36 )
            return false;
        if ( text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-' )
            return false;

        UUID   uuid{};
        uint32 byteIndex{ 0 };
        for ( size_t charIndex = 0; charIndex < text.size(); ++charIndex )
        {
            if ( text[charIndex] == '-' )
                continue;
            if ( charIndex + 1 >= text.size() )
                return false;
            const int32 hi = UUIDInternal::hexNibble( text[charIndex] );
            const int32 lo = UUIDInternal::hexNibble( text[charIndex + 1] );
            if ( hi < 0 || lo < 0 || byteIndex >= 16 )
                return false;
            uuid._arrBytes[byteIndex++] = static_cast<uint8>( ( hi << 4 ) | lo );
            ++charIndex;
        }
        if ( byteIndex != 16 )
            return false;
        outUUID = uuid;
        return true;
    }

    string UUID::toString() const
    {
        static constexpr utf8 kArrHex[] = "0123456789abcdef";
        string                out;
        out.resize( 36 );
        uint32 outIndex{ 0 };
        for ( uint32 byteIndex = 0; byteIndex < 16; ++byteIndex )
        {
            if ( byteIndex == 4 || byteIndex == 6 || byteIndex == 8 || byteIndex == 10 )
                out[outIndex++] = '-';
            out[outIndex++] = kArrHex[( _arrBytes[byteIndex] >> 4 ) & 0x0f];
            out[outIndex++] = kArrHex[_arrBytes[byteIndex] & 0x0f];
        }
        return out;
    }

    bool UUID::isNull() const
    {
        for ( uint8 byteVal : _arrBytes )
        {
            if ( byteVal != 0 )
                return false;
        }
        return true;
    }

    bool UUID::operator==( const UUID& other ) const { return Memory::compare( _arrBytes, other._arrBytes, sizeof( _arrBytes ) ) == 0; }

    bool UUID::operator<( const UUID& other ) const
    {
        return Memory::compare( _arrBytes, other._arrBytes, sizeof( _arrBytes ) ) < 0;
    }
} // namespace sw
