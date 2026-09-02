/**
 * @file Uuid.h
 * @brief 128비트 UUID v4 생성 및 문자열 변환.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) Uuid — 16바이트 RFC 4122 v4. generate / tryParse / toString
    // ------------------------------------------------------------------------------
    /**
     * @struct Uuid
     * @brief 16바이트로 저장하는 RFC 4122 UUID입니다.
     */
    struct SW_API Uuid
    {
        uint8 _arrBytes[16]{};

        /** @brief 난수 UUID v4를 채운 값을 만듭니다. */
        static Uuid generate();

        /** @brief 하이픈 UUID 문자열을 파싱합니다. 실패 시 false입니다. */
        static bool tryParse( string_view text, Uuid& outUuid );

        /** @brief 하이픈이 있는 소문자 16진수 정규 문자열입니다. */
        string toString() const;

        /** @brief 16바이트가 모두 0(nil UUID)이면 true입니다. */
        bool isNull() const;
        /** @brief 16바이트가 모두 같으면 true입니다. */
        bool operator==( const Uuid& other ) const;
        /** @brief 한 바이트라도 다르면 true입니다. */
        bool operator!=( const Uuid& other ) const { return ( *this == other ) == false; }
        /** @brief 바이트 사전순으로 작으면 true입니다. */
        bool operator<( const Uuid& other ) const;
    };
} // namespace sw

namespace std
{
    /** @brief Uuid 를 unordered_map 키로 쓸 때 바이트를 섞은 해시입니다. */
    template <>
    struct hash<sw::Uuid>
    {
        /** @brief 16바이트를 곱셈 해시로 접습니다. */
        size_t operator()( const sw::Uuid& uuid ) const noexcept
        {
            size_t hashValue{ 0 };
            for ( uint8 byteVal : uuid._arrBytes )
            {
                hashValue = ( hashValue * 131u ) + static_cast<size_t>( byteVal );
            }
            return hashValue;
        }
    };
} // namespace std
