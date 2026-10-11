/**
 * @file ReceiptValidator.h
 * @brief 결제 영수증 검증 계약입니다. 스토어(Apple, Google, Steam, 가짜)마다 제공자가 하나씩 있으며 비동기로 동작합니다. 서비스 스레드가 맡기고 나중에 회수합니다.
 * @details 제공자는 스토어 서버와의 통신(HTTP, 서명 검증)을 **자기 스레드**에서 한다. 서비스 스레드는 `pollCompletions` 로 결과만 받는다.
 *          검증이 끝나면 서비스가 원장에 지급한다. 분개 키는 거래 id(`rcpt.<스토어>/…`)이므로 같은 영수증은 어느 계정으로든 한 번만 지급된다.
 *          제품 이름은 `Receipt/Provider/<제품>/` 폴더 안에만 쓴다. PlayFab 의 `RedeemAppleAppStoreInventoryItems` 가 하는 검증과 지급에 해당한다.
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
        Invalid,  ///< 서명, 형식, 앱 id, 상품이 맞지 않는다
        Refunded, ///< 환불되었거나 취소된 거래이다
        Retry,    ///< 스토어에 닿지 못했다. 같은 영수증으로 나중에 다시 시도한다
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
        uint64 _accountID{ 0 };
        uint64 _ticket{ 0 }; ///< 레지스트리가 매기는 번호이다. 결과가 같은 번호로 돌아온다
    };
} // namespace sw

namespace sw
{
    /** @brief 검증 결과입니다. */
    struct ReceiptValidationResult
    {
        string        _storeName{};
        string        _transactionID{}; ///< 스토어의 거래 id 이다. 원장 분개 키가 되며 96바이트 이하이다
        string        _productID{};
        string        _failureText{}; ///< 로그용 문자열이며 영어로 쓴다
        uint64        _ticket{ 0 };
        uint64        _accountID{ 0 };
        int64         _purchaseTimeMs{ 0 };
        ReceiptStatus _status{ ReceiptStatus::Invalid };
        uint8         _bSandbox{ SW_FALSE }; ///< 테스트 결제이다. 샌드박스나 테스트 카드가 해당한다
    };
} // namespace sw

namespace sw
{
    /** @class IReceiptValidator @brief 스토어 하나의 검증 제공자입니다. 서비스 스레드가 `submitValidation` 과 `pollCompletions` 를 부릅니다. */
    class SW_GF_API IReceiptValidator
    {
    public:
        IReceiptValidator()          = default;
        virtual ~IReceiptValidator() = default;

        IReceiptValidator( const IReceiptValidator& )            = delete;
        IReceiptValidator& operator=( const IReceiptValidator& ) = delete;

        /** @brief 스토어 이름을 반환합니다. `[0-9a-z_]` 형식이며 27자 이하이고 분개 범위 `rcpt.<이름>` 에 들어갑니다. */
        virtual const utf8* getStoreName() const = 0;
        /** @brief 개발과 테스트 전용 제공자이면 true 입니다. 서명이 없으므로 Shipping 서비스가 거절합니다. */
        virtual bool isDevelopmentOnly() const { return false; }
        /** @brief 검증을 맡깁니다. 결과는 정확히 한 번 `pollCompletions` 로 나오며, 종료할 때 남은 것은 Retry 로 나옵니다. */
        virtual void submitValidation( const ReceiptValidationRequest& request ) = 0;
        /** @brief 끝난 결과를 @p outListResult 뒤에 붙입니다. */
        virtual void pollCompletions( vector<ReceiptValidationResult>& outListResult ) = 0;
        virtual void shutdown() {}
    };
} // namespace sw

namespace sw
{
    /** @class ReceiptValidatorRegistry @brief 스토어 이름으로 제공자를 찾는 레지스트리입니다. 제공자는 빌려 쓰며 서비스 하나가 레지스트리 하나를 가집니다. */
    class SW_GF_API ReceiptValidatorRegistry
    {
    public:
        ReceiptValidatorRegistry();

        /** @brief 같은 스토어 이름이 둘이거나 규칙에 맞지 않는 이름이면 오류 로그를 남기고 false 를 반환합니다. */
        [[nodiscard]] bool registerValidator( IReceiptValidator* pValidator );
        IReceiptValidator* findValidator( string_view storeName ) const;
        /** @brief 개발 전용 제공자가 하나라도 있으면 true 입니다. */
        bool hasDevelopmentValidator() const;
        /** @brief 검증을 맡기고 번호를 반환합니다. 모르는 스토어이면 0을 반환하며 맡기지 않습니다. */
        uint64 submitValidation( string_view storeName, string_view payload, uint64 accountID );
        void   pollCompletions( vector<ReceiptValidationResult>& outListResult );
        void   shutdown();

    private:
        vector<IReceiptValidator*> _listValidator;
        uint64                     _nextTicket;
    };
} // namespace sw
