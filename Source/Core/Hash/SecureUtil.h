/**
 * @file SecureUtil.h
 * @brief 비밀 값을 다루는 작은 도구입니다 — 길이에만 시간이 드는 비교(`equalsConstantTime`)와 키 있는 PRF(SipHash-2-4).
 * @details 토큰 · 다이제스트 · MAC 비교는 여기 하나를 쓴다. 번역 단위마다 사본을 두면 한 곳만 고쳐지고 나머지가 앞부분이 맞는 만큼 빨리 거절하는 비교로 남는다.
 *          SipHash 는 64 비트 출력의 키 있는 PRF 다(Aumasson · Bernstein 2012). 짧은 입력의 위조 방지(챌린지 값)에 쓰고, 긴 메시지 인증은 보안 제공자의 HMAC 을 쓴다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include <string_view>

namespace sw
{
    /** @brief SipHash 의 128 비트 키입니다. 비밀이므로 운영체제 난수로 채운다. */
    struct SipHashKey
    {
        uint64 _k0{ 0 };
        uint64 _k1{ 0 };
    };
} // namespace sw

namespace sw
{
    struct SecureUtil
    {
        /** @brief @p size 바이트를 비교합니다. 내용과 무관하게 같은 시간이 든다(어느 바이트에서 달라졌는지 시간으로 드러나지 않는다). */
        [[nodiscard]] static bool equalsConstantTime( const uint8* pLeft, const uint8* pRight, size_t size )
        {
            uint8 difference = 0;
            for ( size_t index = 0; index < size; ++index )
            {
                difference |= static_cast<uint8>( pLeft[index] ^ pRight[index] );
            }
            return difference == 0;
        }

        /** @brief 두 문자열을 비교합니다. 길이가 다르면 거짓이고, 시간은 짧은 쪽 길이에만 든다(길이 자체는 숨기지 않는다). */
        [[nodiscard]] static bool equalsConstantTime( std::string_view left, std::string_view right )
        {
            uint8        difference = left.size() == right.size() ? 0 : 1;
            const size_t common     = left.size() < right.size() ? left.size() : right.size();
            for ( size_t index = 0; index < common; ++index )
            {
                difference |= static_cast<uint8>( left[index] ^ right[index] );
            }
            return difference == 0;
        }

        /** @brief SipHash-2-4 입니다. 같은 키와 입력이면 어느 플랫폼에서도 같은 값이다(바이트는 리틀 엔디언으로 읽는다). */
        [[nodiscard]] static uint64 sipHash24( const SipHashKey& key, const uint8* pData, size_t size )
        {
            uint64       v0     = key._k0 ^ 0x736f6d6570736575ULL;
            uint64       v1     = key._k1 ^ 0x646f72616e646f6dULL;
            uint64       v2     = key._k0 ^ 0x6c7967656e657261ULL;
            uint64       v3     = key._k1 ^ 0x7465646279746573ULL;
            const size_t blocks = size / 8;
            for ( size_t block = 0; block < blocks; ++block )
            {
                const uint64 word = readLittle64( pData + block * 8, 8 );
                v3 ^= word;
                sipRound( v0, v1, v2, v3 );
                sipRound( v0, v1, v2, v3 );
                v0 ^= word;
            }
            const uint64 last = ( static_cast<uint64>( size ) << 56 ) | readLittle64( pData + blocks * 8, size - blocks * 8 );
            v3 ^= last;
            sipRound( v0, v1, v2, v3 );
            sipRound( v0, v1, v2, v3 );
            v0 ^= last;
            v2 ^= 0xff;
            for ( int32 round = 0; round < 4; ++round )
            {
                sipRound( v0, v1, v2, v3 );
            }
            return v0 ^ v1 ^ v2 ^ v3;
        }

    private:
        static uint64 rotateLeft( uint64 value, int32 bits ) { return ( value << bits ) | ( value >> ( 64 - bits ) ); }

        static void sipRound( uint64& v0, uint64& v1, uint64& v2, uint64& v3 )
        {
            v0 += v1;
            v1 = rotateLeft( v1, 13 );
            v1 ^= v0;
            v0 = rotateLeft( v0, 32 );
            v2 += v3;
            v3 = rotateLeft( v3, 16 );
            v3 ^= v2;
            v0 += v3;
            v3 = rotateLeft( v3, 21 );
            v3 ^= v0;
            v2 += v1;
            v1 = rotateLeft( v1, 17 );
            v1 ^= v2;
            v2 = rotateLeft( v2, 32 );
        }

        /** @brief @p count(0..8) 바이트를 리틀 엔디언 정수로 읽습니다. */
        static uint64 readLittle64( const uint8* pBytes, size_t count )
        {
            uint64 value = 0;
            for ( size_t index = 0; index < count; ++index )
            {
                value |= static_cast<uint64>( pBytes[index] ) << ( 8 * index );
            }
            return value;
        }
    };
} // namespace sw
