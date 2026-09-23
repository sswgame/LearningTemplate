/**
 * @file TagID.h
 * @brief intern 된 문자열 태그 식별자입니다(FNV-1a 해시 + 문자열 역조회).
 *
 * @details 태그는 "parent.child" 처럼 점으로 나뉜 계층을 가지며, 비교는 해시 하나로 끝납니다. 리터럴은 컴파일 타임에
 *          (`""_tag`), 런타임 문자열은 `TagID::request` 로 만듭니다.
 *
 * @note 이 타입이 **Core 에 있는 이유**: 예전에는 `Engine/Object/Component/TagSystem.h` 안에 있었습니다. 쓰는 것이 Core
 *       기능뿐인데도 Object 폴더에 있어서, 직렬화기가 TagID 의 기본 핸들러를 등록하려면 Object 를 include 해야 했습니다.
 *       그 한 줄이 Reflection · Serialization · Config 를 Engine 코어의 강결합 묶음에 묶어 두는 고리였습니다(그 연결 하나를
 *       끊자 묶음이 10개에서 8개로 줄었습니다). 리플렉션이 필요한 태그 집합과 질의는 `Engine` 쪽
 *       `Object/Component/TagSystem.h` 에 남아 있습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) TagID — intern 된 문자열 + 해시, 점 계층(parent.child)
    // ------------------------------------------------------------------------------
    struct SW_API TagID
    {
        uint64      _id{ 0 };
        const utf8* _pString{ nullptr }; ///< 태그 문자열(리터럴 또는 intern 된 문자열)

        /** @brief 빈 태그입니다. */
        constexpr TagID() = default;
        /** @brief ID 와 문자열로 태그를 만듭니다. */
        constexpr TagID( uint64 id, const utf8* pStr = nullptr )
            : _id{ id }
            , _pString{ pStr } {}

        /** @brief ID 가 같은지 비교합니다. */
        constexpr bool operator==( const TagID& other ) const { return _id == other._id; }
        /** @brief ID 가 다른지 비교합니다. */
        constexpr bool operator!=( const TagID& other ) const { return _id != other._id; }
        /** @brief ID 오름차순으로 비교합니다. */
        constexpr bool operator<( const TagID& other ) const { return _id < other._id; }

        /** @brief 유효한 태그인지 반환합니다. */
        constexpr bool isValid() const { return _id != 0; }

        /**
         * @brief 태그 문자열에서 ID 를 구합니다. **리터럴 · 런타임 · 조회가 모두 이 함수를 씁니다.**
         * @details 예전에는 이 해시를 세 곳이 각자 계산했고 규칙이 서로 달랐습니다. `""_tag` 와 `request` 는 손으로 펼친 FNV-1a
         *          였고(대소문자 구별 + `char` 부호 확장), 에디터 Hierarchy 의 `tag:` 필터는 `computeHash64` 를 기본 인자로 불러
         *          **대소문자를 무시**했습니다. 저장소의 태그는 모두 대문자로 시작하므로(`Collider` · `Sprite` · `UI` ·
         *          `Faction.Player` …) 그 필터는 **하나도 찾지 못했습니다.** 소문자 태그만 우연히 맞았을 것입니다.
         *
         *          대소문자를 무시하는 쪽으로 맞춥니다. `request` 는 문자열을 `hashed_string` 으로 intern 하는데, 그 intern 이 이미
         *          대소문자를 무시하므로(`Player` 와 `player` 는 한 항목입니다) ID 만 대소문자를 구별하면 **같은 문자열을 가리키는
         *          두 태그가 서로 다른 ID** 를 갖는 모순이 생깁니다. 태그는 ID 가 아니라 **문자열로 직렬화**되므로(SerializeContext 가
         *          `TagID::request( text )` 로 다시 읽습니다) 규칙을 바꿔도 저장된 씬은 그대로입니다.
         */
        static constexpr uint64 computeId( const utf8* pStr, size_t length )
        {
            return StringUtil::computeHash64( pStr, length, true );
        }

        /** @brief 태그의 문자열을 반환합니다(레지스트리 역조회 포함). */
        const utf8* getString() const;

        /** @brief parentTag 가 자신과 같거나 조상 체인에 있으면 true 입니다. */
        bool isSubtagOf( const TagID& parentTag ) const
        {
            if ( _id == parentTag._id )
                return true;

            const utf8* pSource = getString();
            const utf8* pParent = parentTag.getString();
            if ( pSource == nullptr || pParent == nullptr )
                return false;

            // ID 가 대소문자를 무시하므로 문자열 비교도 대소문자를 무시해야 한다. 한쪽만 구별하면 "같은 태그인데 조상이 아니다"
            // 라는 답이 나온다.
            while ( *pParent != '\0' )
            {
                if ( StringUtil::toLowerChar( *pSource ) != StringUtil::toLowerChar( *pParent ) )
                    return false;
                ++pSource;
                ++pParent;
            }
            return *pSource == '.';
        }

        /** @brief 런타임에 태그를 만들거나 가져옵니다(문자열 intern). */
        static TagID request( string_view str );
    };

    // ------------------------------------------------------------------------------
    // 2) intern — 런타임은 TagID::request, 리터럴은 ""_tag
    // ------------------------------------------------------------------------------
    /** @brief 리터럴로 컴파일 타임 태그를 만듭니다. */
    constexpr TagID operator""_tag( const utf8* pStr, size_t len )
    {
        return TagID( TagID::computeId( pStr, len ), pStr );
    }
} // namespace sw
