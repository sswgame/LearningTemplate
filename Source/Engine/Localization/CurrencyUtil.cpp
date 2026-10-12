#include "pch.h"

#include "Engine/Localization/CurrencyUtil.h"

namespace sw
{
    namespace
    {
        struct CurrencyUtilInternal
        {
            static constexpr CurrencyInfo kArrCurrency[] = {
                {"USD",   "$", 2},
                {"EUR",   "€", 2},
                {"GBP",   "£", 2},
                {"JPY",   "¥", 0},
                {"KRW",   "₩", 0},
                {"CNY",   "¥", 2},
                {"TWD", "NT$", 2},
                {"PLN",  "zł", 2},
                {"RUB",   "₽", 2},
                {"SAR", "SAR", 2},
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    const CurrencyInfo* CurrencyUtil::findCurrency( string_view code )
    {
        for ( const CurrencyInfo& currency : CurrencyUtilInternal::kArrCurrency )
        {
            if ( code == currency._pCode )
                return &currency;
        }
        return nullptr;
    }
} // namespace sw
