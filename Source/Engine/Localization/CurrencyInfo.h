/**
 * @file CurrencyInfo.h
 * @brief 통화(ISO 4217) 한 줄 — 코드 · 기호 · 소수 자리입니다. 줄은 문화권 표(`*.cultures.xml` 의 `<currencies>`)의 데이터이고, `CultureInfo::formatCurrency` 와 `{n, number, currency}` 가 읽습니다.
 * @details 표에 없는 통화는 찾기가 nullptr 이고, 호출자가 코드를 기호 자리에 쓰고 소수 2 자리로 다룹니다.
 *          기호는 문화권마다 달리 쓰이지만(미국 밖의 `US$`) 이 표는 한 가지 철자만 가집니다 — 문화권별 기호가 필요해지면 문화권 데이터로 덮는 칸을 더합니다(CLDR 의 문화권별 currencies).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 통화 한 줄입니다. */
    struct CurrencyInfo
    {
        string _code{};               ///< ISO 4217 코드(`KRW`)
        string _symbol{};             ///< 화면에 쓰는 기호(`₩`)
        uint8  _minorDigitCount{ 2 }; ///< 소수 자리 수(KRW · JPY 0, USD · EUR 2). 표의 `digits` 는 필수다
    };
} // namespace sw
