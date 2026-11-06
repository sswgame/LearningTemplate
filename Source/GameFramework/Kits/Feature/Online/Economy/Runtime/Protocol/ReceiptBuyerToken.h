/**
 * @file ReceiptBuyerToken.h
 * @brief 구매 때 스토어에 찍어 두는 계정 토큰입니다. 스토어가 서명한 답에 되풀이되므로 서버가 "이 결제를 시작한 계정" 을 압니다.
 * @details 클라이언트는 구글의 `BillingFlowParams.setObfuscatedAccountId` 와 애플의 `Product.PurchaseOption.appAccountToken` 에 이 값을 넣고, 서버는 지급할 계정으로 같은 값을 만들어 답과 비교한다(`ReceiptBuyerBinding::AccountToken`).
 *          한 벌로 두 스토어를 받으려고 UUID 모양이다 — 애플은 UUID 만 받고 구글은 64 자 이하 글이면 받는다.
 *          비밀이 없어도 된다: 막으려는 것은 남의 영수증을 내는 일이고, 찍는 것은 결제하는 사람 자신이다. 계정 id 는 난수이고 개인정보가 아니다(`LoginStoreLogic::makeNonZeroID`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/String/StringBuilder.h"

#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    /** @brief 계정 토큰을 만듭니다. */
    struct ReceiptBuyerToken
    {
        /** @brief @p accountID 의 토큰(`xxxxxxxx-xxxx-4000-8000-00000000xxxx`, 소문자 16 진)입니다. 계정 id 의 16 자리가 모두 들어가 계정마다 다릅니다. */
        static string make( uint64 accountID )
        {
            const string      hex  = ServiceKeyUtil::makeHex64( accountID );
            const string_view text = hex;
            return formatToString( "%#-%#-4000-8000-00000000%#", text.substr( 0, 8 ), text.substr( 8, 4 ), text.substr( 12, 4 ) );
        }
    };
} // namespace sw
