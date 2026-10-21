/**
 * @file IBalanceNotice.h
 * @brief 계정의 잔액이 클라이언트 요청 없이 바뀌었음을 그 계정에 알리는 인터페이스입니다. 경제 키트가 구현해 서버 조립 문맥(`OnlineServerKitContext::_pBalanceNotice`)에 내놓고,
 *        GM 키트(지급과 회수)가 자기 완료에서 부릅니다. 경제 키트 자신의 환불 회수도 같은 경로로 알립니다.
 * @details - 알림은 바뀐 잔액 값(절댓값)을 싣습니다. 클라이언트는 캐시를 그 값으로 덮으므로 알림이 늦거나 중복되어도 결과가 같습니다.
 *          - 계정이 이 서버에 붙어 있으면 바로 보내고, 다른 서버에 붙어 있으면 접속 상태를 찾아 그 서버로 중계합니다. 접속하지 않은 계정에는 보내지 않습니다.
 *            다음 로그인이 지갑 전체를 읽기 때문에 놓친 알림이 잔액을 틀리게 하지 않습니다.
 *          - 서비스 스레드 하나에서만 씁니다. 경제 키트가 시작하기 전이나 종료한 뒤의 호출은 무시됩니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"

namespace sw
{
    /**
     * @class IBalanceNotice
     * @brief 잔액이 바뀐 계정에 알림을 보내는 인터페이스입니다.
     */
    class IBalanceNotice
    {
    public:
        IBalanceNotice()          = default;
        virtual ~IBalanceNotice() = default;

        IBalanceNotice( const IBalanceNotice& )            = delete;
        IBalanceNotice& operator=( const IBalanceNotice& ) = delete;

        /** @brief @p accountID 의 바뀐 잔액 @p listBalance 를 알립니다. 목록이 비었거나 계정이 올바르지 않으면 보내지 않습니다. */
        virtual void notifyBalancesChanged( AccountID accountID, const vector<LedgerBalance>& listBalance ) = 0;
    };
} // namespace sw
