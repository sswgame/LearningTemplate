/**
 * @file TradeKitConfig.h
 * @brief 거래 키트의 서버 설정입니다(`_listKit[]._settings`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"

namespace sw
{
    /** @brief 거래 키트의 서버 설정입니다. */
    REFLECT()
    struct TradeKitConfig
    {
        REFLECT_BODY();
        /** @brief 거래할 수 있는 자산 id 접두어입니다(예: `cur.gold`, `item.`). 하나는 있어야 합니다. */
        PROPERTY( Required )
        vector<string> _listTradablePrefix{};
        /** @brief 한쪽이 내놓을 수 있는 자산 줄 수입니다. */
        PROPERTY( Min = 1 )
        int32 _maxLegsPerSide{ 8 };
        /** @brief 닫힌 거래 레코드의 TTL 입니다. TTL 쓸기가 닫은 뒤 이 기간이 지난 거래를 지웁니다(분쟁 조회 기간). */
        PROPERTY( Required )
        ServiceTTLConfig _closedSessionTTL{};
    };
} // namespace sw
