#include "pch.h"

#include "Core/UUID/UUID.h"

#include "Core/Foundation/HexDigitUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/SecureRandom.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    UUID UUID::generate()
    {
        // 난수는 운영체제에서 곧바로 받는다(`SecureRandom` — Windows 는 BCryptGenRandom, 리눅스는 getrandom). 32 비트 씨앗 하나로
        // 시작한 mt19937_64 에서 뽑으면 서로 다른 실행이 같은 씨앗을 뽑을 때 **같은 GUID 열**이 다시 나온다(씨앗이 2^32 가지뿐이라 편집기를 수만
        // 번 열면 일어난다). 에셋 GUID 는 여러 사람의 작업이 저장소에서 합쳐지므로 겹치면 참조가 엉뚱한 에셋으로 풀린다. 언리얼의
        // FGuid::NewGuid 도 OS 의 GUID 를 쓴다.
        UUID uuid{};
        SecureRandom::fill( uuid._arrBytes, sizeof( uuid._arrBytes ) );

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
        const bool bMisplacedDashes = text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-';
        if ( bMisplacedDashes )
            return false;

        UUID   uuid{};
        uint32 byteIndex{ 0 };
        for ( size_t charIndex = 0; charIndex < text.size(); ++charIndex )
        {
            if ( text[charIndex] == '-' )
                continue;
            if ( charIndex + 1 >= text.size() )
                return false;
            if ( byteIndex >= 16 || HexDigitUtil::tryParseBytes( text.substr( charIndex, 2 ), &uuid._arrBytes[byteIndex], 1 ) == false )
                return false;
            ++byteIndex;
            ++charIndex;
        }
        if ( byteIndex != 16 )
            return false;
        outUUID = uuid;
        return true;
    }

    string UUID::toString() const
    {
        string out;
        out.resize( 36 );
        uint32 outIndex{ 0 };
        for ( uint32 byteIndex = 0; byteIndex < 16; ++byteIndex )
        {
            const bool bDashPosition = byteIndex == 4 || byteIndex == 6 || byteIndex == 8 || byteIndex == 10;
            if ( bDashPosition )
                out[outIndex++] = '-';
            HexDigitUtil::writeDigits( &out[outIndex], _arrBytes[byteIndex], 2, HexCase::Lower );
            outIndex += 2;
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

    bool UUID::operator==( const UUID& other ) const
    {
        return Memory::compare( _arrBytes, other._arrBytes, sizeof( _arrBytes ) ) == 0;
    }

    bool UUID::operator<( const UUID& other ) const
    {
        return Memory::compare( _arrBytes, other._arrBytes, sizeof( _arrBytes ) ) < 0;
    }
} // namespace sw
