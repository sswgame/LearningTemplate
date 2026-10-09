/**
 * @file FakeReceiptValidator.h
 * @brief 가짜 스토어 "fake" — 영수증 `fake|<상품 id>|<거래 id>|<ok|invalid|refunded|retry|sandbox>` 를 그대로 믿고 다음 `pollCompletions` 에 결과를 냅니다(결정적).
 * @details 개발 서버 · 시험 전용이다(`isDevelopmentOnly`) — 서명이 없으니 Shipping 서버의 경제 서비스는 이것이 든 등록부를 거절한다.
 */
#pragma once
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Server/Economy/Receipt/ReceiptValidator.h"

namespace sw
{
    /** @class FakeReceiptValidator @brief 가짜 영수증 검증 제공자입니다. */
    class SW_GF_API FakeReceiptValidator final : public IReceiptValidator
    {
    public:
        FakeReceiptValidator();

        const utf8* getStoreName() const override { return "fake"; }
        bool        isDevelopmentOnly() const override { return true; }
        void        submitValidation( const ReceiptValidationRequest& request ) override;
        void        pollCompletions( vector<ReceiptValidationResult>& outListResult ) override;
        int32       getPendingCount() const { return static_cast<int32>( _listPending.size() ); }

    private:
        vector<ReceiptValidationResult> _listPending;
    };
} // namespace sw
