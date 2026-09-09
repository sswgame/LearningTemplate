/**
 * @file TagID.h
 * @brief intern 된 문자열 태그 식별자 (FNV-1a 해시 + 문자열 역조회).
 *
 * @details 태그는 "parent.child" 점 계층을 가지며 비교는 해시 하나로 끝난다. 리터럴은 컴파일
 *          타임(`""_tag`), 런타임 문자열은 `TagID::request` 로 만든다.
 *
 * @note 이 타입이 **Core 에 있는 이유**: 예전에는 `Engine/Object/Component/TagSystem.h` 안에
 *       있었다. 쓰는 것이 Core 기능뿐인데도 Object 폴더에 놓여 있어서, 직렬화기가 TagID 의
 *       기본 핸들러를 등록하려고 Object 를 include 해야 했다. 그 한 줄이 Reflection·
 *       Serialization·Config 를 Engine 코어 강결합 묶음에 묶어 두는 고리였다(엣지 하나를 끊자
 *       묶음이 10 → 8 로 줄었다). 리플렉션이 필요한 태그 집합·질의는 `Engine` 쪽
 *       `Object/Component/TagSystem.h` 에 남아 있다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) TagID — intern된 문자열 + 해시, 점 계층 (parent.child)
    // ------------------------------------------------------------------------------
    struct SW_API TagID
    {
        uint64      _id{ 0 };
        const utf8* _pString{ nullptr }; ///< 태그 문자열 (리터럴 또는 intern된 문자열)

        /** @brief 빈 태그입니다. */
        constexpr TagID() = default;
        /** @brief ID와 문자열로 태그를 만듭니다. */
        constexpr TagID( uint64 id, const utf8* pStr = nullptr )
            : _id{ id }
            , _pString{ pStr } {}

        /** @brief ID가 같은지 비교합니다. */
        constexpr bool operator==( const TagID& other ) const { return _id == other._id; }
        /** @brief ID가 다른지 비교합니다. */
        constexpr bool operator!=( const TagID& other ) const { return _id != other._id; }
        /** @brief ID 오름차순으로 비교합니다. */
        constexpr bool operator<( const TagID& other ) const { return _id < other._id; }

        /** @brief 유효한 태그인지 반환합니다. */
        constexpr bool isValid() const { return _id != 0; }

        /** @brief 태그의 문자열을 반환합니다 (레지스트리 역조회 포함). */
        const utf8* getString() const;

        /** @brief parentTag가 자신과 같거나 조상 체인에 있으면 true */
        bool isSubtagOf( const TagID& parentTag ) const
        {
            if ( _id == parentTag._id )
                return true;

            const utf8* pSource = getString();
            const utf8* pParent = parentTag.getString();
            if ( pSource == nullptr || pParent == nullptr )
                return false;

            while ( *pParent != '\0' )
            {
                if ( *pSource != *pParent )
                    return false;
                ++pSource;
                ++pParent;
            }
            return *pSource == '.';
        }

        /** @brief 런타임에 태그를 만들거나 가져옵니다 (문자열 intern). */
        static TagID request( string_view str );
    };

    // ------------------------------------------------------------------------------
    // 2) intern — 런타임은 TagID::request, 리터럴은 ""_tag
    // ------------------------------------------------------------------------------
    /** @brief 리터럴에서 컴파일 타임 태그를 만듭니다. */
    constexpr TagID operator""_tag( const utf8* pStr, size_t len )
    {
        uint64 hash = StringUtil::kOffset64;

        for ( size_t charIndex = 0; charIndex < len; ++charIndex )
        {
            hash = ( hash ^ static_cast<uint64>( pStr[charIndex] ) ) * StringUtil::kPrime64;
        }

        return TagID( hash, pStr );
    }
} // namespace sw
