/**
 * @file CurrencyUtil.h
 * @brief 통화(ISO 4217) 코드의 기호와 소수 자리 표입니다. `CultureInfo::formatCurrency` 와 `{n, number, currency}` 가 읽습니다.
 * @details 표에 없는 통화는 `findCurrency` 가 nullptr 이고, 호출자가 코드를 기호 자리에 쓰고 소수 2 자리로 다룹니다.
 *          기호는 문화권마다 달리 쓰이지만(미국 밖의 `US$`) 이 표는 한 가지 철자만 가집니다 — 문화권별 기호가 필요해지면 문화권 데이터로 덮는 칸을 더합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    /** @brief 통화 한 줄입니다. */
    struct CurrencyInfo
    {
        const utf8* _pCode;           ///< ISO 4217 코드(`KRW`)
        const utf8* _pSymbol;         ///< 화면에 쓰는 기호(`₩`)
        uint8       _minorDigitCount; ///< 소수 자리 수(KRW · JPY 0, USD · EUR 2)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct CurrencyUtil
     * @brief 통화 표 조회입니다. 상태가 없습니다.
     */
    struct SW_API CurrencyUtil
    {
        /** @brief 코드의 통화입니다(대소문자 구분, 없으면 nullptr). */
        static const CurrencyInfo* findCurrency( string_view code );
    };
} // namespace sw
