/**
 * @file GUID.h
 * @brief 128비트 고유 값(`GUID`)과 그 글 표기입니다.
 * @details 에셋 정체성(`AssetGUID`)이 이 값을 든다. 언리얼 `FGuid` 자리다.
 *          - 글 표기는 하나다. 소문자 16진 32 자(하이픈 없음, 유니티 `.meta` 의 guid 표기). 대문자 · 하이픈은 읽지 않는다. 같은 값을 두 철자로 적으면 글로 대조하는 린트 · 색인이 갈린다.
 *          - 모두 0 은 "없음" 이다(`isValid`). 기본 생성이 그 값이다.
 *          - windows.h 의 전역 `GUID`(COM 의 `struct _GUID`)와 이름이 같다. sw 이름공간 안에서 Windows 형식을 쓰는 곳은 `::GUID` 로 적는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class GUID
     * @brief 128비트 값(앞 64 비트 `_high` · 뒤 64 비트 `_low`)입니다. 글 표기는 `_high` 를 먼저 쓴다.
     */
    class SW_API GUID
    {
    public:
        /** @brief 글 표기의 길이(소문자 16진 32 자)입니다. */
        static constexpr size_t kTextLength = 32;

        constexpr GUID() = default;
        constexpr GUID( uint64 high, uint64 low )
            : _high{ high }
            , _low{ low }
        {
        }

        /** @brief 운영체제 난수 128 비트로 새 값을 만듭니다(`SecureRandom`). 모두 0 은 나오지 않는다. */
        [[nodiscard]] static GUID create();
        /** @brief 글 표기 @p text 를 읽습니다. 소문자 16진 32 자가 아니면 없음입니다(대소문자 · 하이픈을 고쳐 읽지 않는다). 모두 0 도 읽는다. 거절은 쓰는 쪽 몫이다. */
        [[nodiscard]] static optional<GUID> tryParse( string_view text );

        /** @brief 글 표기(소문자 16진 32 자)입니다. */
        [[nodiscard]] string toString() const;
        /** @brief 모두 0 이 아니면 true 입니다. */
        [[nodiscard]] bool isValid() const { return _high != 0 || _low != 0; }
        /** @brief 해시 키(`DefaultHash`)입니다. 무작위 값이라 두 반쪽을 섞기만 한다. */
        [[nodiscard]] uint64 computeHash() const { return _high ^ _low; }

        [[nodiscard]] bool operator==( const GUID& other ) const { return _high == other._high && _low == other._low; }
        [[nodiscard]] bool operator!=( const GUID& other ) const { return ( *this == other ) == false; }
        /** @brief 정렬 순서(앞 64 비트 먼저)입니다. 글 표기의 사전순과 같다. */
        [[nodiscard]] bool operator<( const GUID& other ) const { return _high != other._high ? _high < other._high : _low < other._low; }

    private:
        uint64 _high{ 0 };
        uint64 _low{ 0 };
    };
} // namespace sw
