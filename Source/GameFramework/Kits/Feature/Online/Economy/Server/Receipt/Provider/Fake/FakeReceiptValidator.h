/**
 * @file FakeReceiptValidator.h
 * @brief 가짜 스토어 "fake" 입니다. 영수증 `fake|<상품 id>|<거래 id>|<ok|invalid|refunded|retry|sandbox>` 를 그대로 믿고 다음 `pollCompletions` 에서 결과를 냅니다. 결과는 결정적입니다.
 * @details 개발 서버와 테스트 전용이다(`isDevelopmentOnly`). 서명이 없으므로 Shipping 서버의 경제 서비스는 이것을 가진 레지스트리를 거절한다.
 */
#pragma once
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptValidator.h"

namespace sw
{
    /** @class FakeReceiptValidator @brief 가짜 영수증 검증 제공자입니다. */
    class SW_KIT_SERVER_API FakeReceiptValidator final : public IReceiptValidator
    {
    public:
        FakeReceiptValidator();

        const utf8* getStoreName() const override { return "fake"; }
        bool        isDevelopmentOnly() const override { return true; }
        void        submitValidation( const ReceiptValidationRequest& request, UnixMilliseconds now ) override;
        /** @brief 스토어 통신이 없어 할 일이 없습니다. 결과는 맡길 때 이미 정해진다. */
        void update( UnixMilliseconds now ) override { (void)now; }
        void pollCompletions( vector<ReceiptValidationResult>& outListResult ) override;
        void pollRefunds( vector<ReceiptRefundNotice>& outListNotice ) override;
        /** @brief 다음 `pollRefunds` 에 환불 알림 하나를 냅니다(테스트와 개발 서버). */
        void  pushRefund( string_view transactionID, int64 refundedMs );
        int32 getPendingCount() const { return static_cast<int32>( _listPending.size() ); }

    private:
        vector<ReceiptValidationResult> _listPending;
        vector<ReceiptRefundNotice>     _listRefund;
    };
} // namespace sw
