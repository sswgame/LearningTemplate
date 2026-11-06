/**
 * @file ReceiptValidator.h
 * @brief 결제 영수증 검증 계약입니다. 스토어(Apple, Google, Steam, 가짜)마다 제공자가 하나씩 있으며 비동기로 동작합니다. 서비스 스레드가 맡기고 나중에 회수합니다.
 * @details 제공자는 스토어 서버와의 통신(HTTP, 서명 검증)을 **자기 스레드**에서 한다. 서비스 스레드는 `pollCompletions` 로 결과만 받는다.
 *          검증이 끝나면 서비스가 원장에 지급한다. 분개 키는 거래 id(`rcpt.<스토어>/…`)이므로 같은 영수증은 어느 계정으로든 한 번만 지급된다.
 *          환불과 결제 취소 알림(Apple 서버 알림 v2, Google RTDN, Steam 환불 보고)은 제공자가 받아 `pollRefunds` 로 넘긴다. 서비스가 지급한 재화를 빚을 허용해 회수한다.
 *          제품 이름은 `Receipt/Provider/<제품>/` 폴더 안에만 쓴다. PlayFab 의 `RedeemAppleAppStoreInventoryItems` 가 하는 검증과 지급에 해당한다.
 *          실제 스토어의 공통 판단(응답 분류 · 요청 대조 · 조회 구간)은 `StoreReceiptValidator` 가 한 벌 가진다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

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

    /**
     * @brief 영수증의 구매자를 지급받을 계정과 어떻게 잇는가입니다. 제공자마다 명시적으로 정한다(`IReceiptValidator::getBuyerRule`) — 기본값으로 지나가지 않게 순수 가상이다.
     * @details 묶지 않으면 남의 주문 번호를 먼저 낸 계정이 지급받는다(분개 키가 거래 id 라 "한 번만" 은 지켜도 "누구에게" 는 지키지 않는다). 대조는 `EconomyStoreLogic::redeemReceipt` 한 곳이 한다.
     *          언리얼 OSS 의 `IOnlinePurchase` 와 유니티 IAP 는 검증을 백엔드에 맡기고, 스토어의 답에 신원을 싣는 칸(앱 계정 토큰 · 난독화 계정 id)은 게임 서버가 대조하도록 둔다 — 이 자리가 그 대조다.
     */
    enum class ReceiptBuyerBinding : uint8
    {
        None = 0,     ///< 스토어 답에서 계정으로 이을 신원이 없다 — 먼저 낸 계정이 받는다. 가짜 스토어와, 구매 때 토큰을 찍지 않는 클라이언트를 받는 설정의 명시적 선택이다
        LinkedLogin,  ///< 스토어 답의 신원이 그 계정에 연동된 외부 로그인 주체와 같아야 한다(스팀의 `steamid`). 계정 키트의 `IAccountExternalLinks` 가 필요하다
        AccountToken, ///< 스토어 답이 구매 때 찍은 계정 토큰(`ReceiptBuyerToken`)을 되풀이해야 한다(구글의 난독화 계정 id, 애플의 `appAccountToken`). 없으면 거절한다
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 제공자 하나의 구매자 규칙입니다. */
    struct ReceiptBuyerRule
    {
        string              _loginProvider{}; ///< `LinkedLogin` — 잇는 외부 로그인 제공자 이름(계정 키트의 저장 키)
        ReceiptBuyerBinding _binding{ ReceiptBuyerBinding::None };
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
        string           _storeName{};
        string           _transactionID{}; ///< 스토어의 거래 id 이다. 원장 분개 키가 되며 96바이트 이하이다
        string           _productID{};
        string           _failureText{};  ///< 로그용 문자열이며 영어로 쓴다
        string           _storeBuyerID{}; ///< 스토어 답이 말한 구매자이다(스토어 계정 id 또는 구매 때 찍은 토큰). 말하지 않았으면 비어 있다
        ReceiptBuyerRule _buyerRule{};    ///< 이 값을 지급 계정과 어떻게 대조하는가
        uint64           _ticket{ 0 };
        uint64           _accountID{ 0 };
        int64            _purchaseTimeMs{ 0 };
        ReceiptStatus    _status{ ReceiptStatus::Invalid };
        uint8            _bSandbox{ SW_FALSE }; ///< 테스트 결제이다. 샌드박스나 테스트 카드가 해당한다
    };
} // namespace sw

namespace sw
{
    /** @brief 스토어가 알린 환불이나 결제 취소 하나입니다. */
    struct ReceiptRefundNotice
    {
        string _storeName{};
        string _transactionID{}; ///< 지급 때의 거래 id 와 같다
        int64  _refundedMs{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IReceiptValidator
     * @brief 스토어 하나의 검증 제공자입니다. 서비스 스레드가 `submitValidation` · `update` · `pollCompletions` 를 부릅니다.
     * @details 제공자는 시계를 직접 읽지 않는다. 시각은 서버 루프의 `now`(유닉스 밀리초)를 인자로 받는다 — HTTP 시한과 환불 보고 조회 주기를
     *          같은 값으로 측정하고, 테스트는 가짜 시각을 넣어 조회 주기를 밀어 본다.
     */
    class SW_KIT_SERVER_API IReceiptValidator
    {
    public:
        IReceiptValidator()          = default;
        virtual ~IReceiptValidator() = default;

        IReceiptValidator( const IReceiptValidator& )            = delete;
        IReceiptValidator& operator=( const IReceiptValidator& ) = delete;

        /** @brief 스토어 이름을 반환합니다. `[0-9a-z_]` 형식이며 27자 이하이고 분개 범위 `rcpt.<이름>` 에 들어갑니다. */
        virtual const utf8* getStoreName() const = 0;
        /** @brief 개발과 테스트 전용 제공자이면 true 입니다. 서명이 없으므로 DevOnline 단위가 없는 구성(배포)의 서비스가 거절합니다(`EconomyDevOptions`). */
        virtual bool isDevelopmentOnly() const { return false; }
        /** @brief 이 제공자의 구매자 규칙입니다. 결과의 `_buyerRule` 과 같은 값이며, 서비스가 기동 때 필요한 포트가 있는지 확인합니다. */
        virtual const ReceiptBuyerRule& getBuyerRule() const = 0;
        /** @brief 검증을 맡깁니다(@p now 는 서버 루프의 시각). 결과는 정확히 한 번 `pollCompletions` 로 나오며, 종료할 때 남은 것은 Retry 로 나옵니다. */
        virtual void submitValidation( const ReceiptValidationRequest& request, UnixMilliseconds now ) = 0;
        /** @brief 서버 루프의 시각 @p now 로 스토어 통신을 진행합니다(응답 처리 · 시한 · 주기 조회). 결과는 `pollCompletions` · `pollRefunds` 가 꺼냅니다. */
        virtual void update( UnixMilliseconds now ) = 0;
        /** @brief 끝난 결과를 @p outListResult 뒤에 붙입니다. 진행은 하지 않습니다(`update`). */
        virtual void pollCompletions( vector<ReceiptValidationResult>& outListResult ) = 0;
        /** @brief 받은 환불 알림을 @p outListNotice 뒤에 붙입니다. 제공자마다 알림을 받는 길(웹훅, 구독, 보고 조회)이 다르므로 기본 구현을 두지 않습니다. */
        virtual void pollRefunds( vector<ReceiptRefundNotice>& outListNotice ) = 0;
        /** @brief 회수를 끝내지 못한 환불을 잊습니다 — 겹친 조회가 다시 알리게 한다. 다시 알릴 길이 없는 제공자는 할 일이 없습니다. */
        virtual void forgetRefund( string_view transactionID ) { (void)transactionID; }
        virtual void shutdown() {}
    };
} // namespace sw

namespace sw
{
    /** @class ReceiptValidatorRegistry @brief 스토어 이름으로 제공자를 찾는 레지스트리입니다. 제공자는 빌려 쓰며 서비스 하나가 레지스트리 하나를 가집니다. */
    class SW_KIT_SERVER_API ReceiptValidatorRegistry
    {
    public:
        ReceiptValidatorRegistry();

        /** @brief 같은 스토어 이름이 둘이거나 규칙에 맞지 않는 이름이면 오류 로그를 남기고 false 를 반환합니다. */
        [[nodiscard]] bool registerValidator( IReceiptValidator* pValidator );
        IReceiptValidator* findValidator( string_view storeName ) const;
        /** @brief 개발 전용 제공자가 하나라도 있으면 true 입니다. */
        bool hasDevelopmentValidator() const;
        /** @brief 외부 로그인 연동(`ReceiptBuyerBinding::LinkedLogin`)으로 구매자를 묶는 제공자가 하나라도 있으면 true 입니다. */
        bool requiresExternalLinks() const;
        /** @brief 검증을 맡기고 번호를 반환합니다. 모르는 스토어이면 0을 반환하며 맡기지 않습니다. */
        uint64 submitValidation( string_view storeName, string_view payload, uint64 accountID, UnixMilliseconds now );
        /** @brief 모든 제공자를 서버 루프의 시각 @p now 로 진행합니다. */
        void update( UnixMilliseconds now );
        void pollCompletions( vector<ReceiptValidationResult>& outListResult );
        void pollRefunds( vector<ReceiptRefundNotice>& outListNotice );
        /** @brief 회수를 끝내지 못한 환불을 그 제공자에서 잊습니다(겹친 조회가 다시 알린다). */
        void forgetRefund( const ReceiptRefundNotice& notice );
        void shutdown();

    private:
        vector<IReceiptValidator*> _listValidator;
        uint64                     _nextTicket;
    };
} // namespace sw
