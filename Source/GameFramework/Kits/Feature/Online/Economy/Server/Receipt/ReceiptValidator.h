/**
 * @file ReceiptValidator.h
 * @brief 결제 영수증 검증 계약 — 스토어(Apple · Google · Steam · 가짜)마다 제공자 하나, 비동기(맡김 → 서비스 스레드가 거둔다)입니다.
 * @details 제공자는 스토어 서버와의 통신을 **자기 스레드**에서 한다(HTTP · 서명 검증) — 서비스 스레드는 `pollCompletions` 로 결과만 받는다.
 *          검증이 끝나면 서비스가 원장 지급을 하고, 분개 키는 거래 id(`rcpt.<스토어>/…`)라 같은 영수증은 어느 계정으로든 한 번만 지급된다.
 *          제품 이름은 `Receipt/Provider/<제품>/` 폴더 안에만 쓴다. PlayFab `RedeemAppleAppStoreInventoryItems` 의 검증 + 지급과 같은 자리다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 검증 결과의 상태입니다. */
    enum class ReceiptStatus : uint8
    {
        Valid = 0,
        Invalid,  ///< 서명 · 형식 · 앱 id · 상품이 맞지 않는다
        Refunded, ///< 환불 · 취소된 거래
        Retry,    ///< 스토어에 닿지 못했다 — 같은 영수증으로 나중에
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 검증 요청입니다. */
    struct ReceiptValidationRequest
    {
        string _storeName{};
        string _payload{};
        uint64 _accountId{ 0 };
        uint64 _ticket{ 0 }; ///< 등록부가 매기는 번호 — 결과가 같은 번호로 돌아온다
    };
} // namespace sw

namespace sw
{
    /** @brief 검증 결과입니다. */
    struct ReceiptValidationResult
    {
        string        _storeName{};
        string        _transactionId{}; ///< 스토어의 거래 id(원장 분개 키가 된다) — 96 바이트 이하
        string        _productId{};
        string        _failureText{}; ///< 로그용(영어)
        uint64        _ticket{ 0 };
        uint64        _accountId{ 0 };
        int64         _purchaseTimeMs{ 0 };
        ReceiptStatus _status{ ReceiptStatus::Invalid };
        uint8         _bSandbox{ SW_FALSE }; ///< 시험 결제(샌드박스 · 테스트 카드)
    };
} // namespace sw

namespace sw
{
    /** @class IReceiptValidator @brief 스토어 하나의 검증 제공자입니다. 서비스 스레드가 `submitValidation` · `pollCompletions` 를 부른다. */
    class SW_GF_API IReceiptValidator
    {
    public:
        IReceiptValidator()          = default;
        virtual ~IReceiptValidator() = default;

        IReceiptValidator( const IReceiptValidator& )            = delete;
        IReceiptValidator& operator=( const IReceiptValidator& ) = delete;

        /** @brief 스토어 이름(`[0-9a-z_]`, 27 자 이하 — 분개 범위 `rcpt.<이름>` 에 든다)입니다. */
        virtual const utf8* getStoreName() const = 0;
        /** @brief 개발 · 시험 전용 제공자인가입니다(서명 없음 — Shipping 서비스가 거절한다). */
        virtual bool isDevelopmentOnly() const { return false; }
        /** @brief 검증을 맡깁니다. 결과는 정확히 한 번 `pollCompletions` 로 나온다(내릴 때 남은 것은 Retry 로). */
        virtual void submitValidation( const ReceiptValidationRequest& request ) = 0;
        /** @brief 끝난 결과를 @p outListResult 뒤에 붙입니다. */
        virtual void pollCompletions( vector<ReceiptValidationResult>& outListResult ) = 0;
        virtual void shutdown() {}
    };
} // namespace sw

namespace sw
{
    /** @class ReceiptValidatorRegistry @brief 스토어 이름 → 제공자(빌려 쓴다)입니다. 서비스 하나가 하나 든다. */
    class SW_GF_API ReceiptValidatorRegistry
    {
    public:
        ReceiptValidatorRegistry();

        /** @brief 같은 스토어 이름 둘 · 규칙 밖 이름은 오류 로그와 함께 false 입니다. */
        [[nodiscard]] bool registerValidator( IReceiptValidator* pValidator );
        IReceiptValidator* findValidator( string_view storeName ) const;
        /** @brief 개발 전용 제공자가 하나라도 있는가입니다. */
        bool hasDevelopmentValidator() const;
        /** @brief 맡기고 번호를 돌려줍니다. 모르는 스토어면 0 이고 맡기지 않는다. */
        uint64 submitValidation( string_view storeName, string_view payload, uint64 accountId );
        void   pollCompletions( vector<ReceiptValidationResult>& outListResult );
        void   shutdown();

    private:
        vector<IReceiptValidator*> _listValidator;
        uint64                     _nextTicket;
    };
} // namespace sw
