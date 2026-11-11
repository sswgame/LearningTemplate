/**
 * @file EconomyProtocol.h
 * @brief 경제 서비스의 와이어 형식입니다. 메서드 번호(영역 `OnlineMethodRange::kEconomy`), 결과, 요청과 응답 형식을 정의하며 클라이언트와 서버가 함께 씁니다.
 * @details 요청과 응답은 `REFLECT` 구조체 하나씩이고 본문은 고정 배치형(B)이다. 바이트 배치는 아래 `PROPERTY` 선언 순서와 키가 정하며 손 코덱과 손 버전 상수는 없다.
 *          응답은 메서드마다 따로이다(`EconomyWalletReply` · `EconomyHistoryReply` · `EconomyPurchaseReply` · `EconomyRedeemReply` · `EconomyWithdrawReply`). 업무 결과는 응답의 `_result` 이다.
 *          공통 범주(`OnlineError`)는 오류 코드로만 오며 로그인 없음, 손상된 본문, 충돌, 일시 장애, 시한 초과가 해당한다. 업무 결과(`EconomyResult`)는 도메인 값만이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Service/OnlineLimit.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolRegistry.h"

namespace sw
{
    /** @brief 경제 서비스 메서드 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)이고, 이름의 snake_case 가 지표 라벨입니다. 새 메서드는 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class EconomyMethodName : uint8
    {
        GetWallet,
        GetHistory,
        Purchase,
        RedeemReceipt,
        WithdrawPurchase,
        Count,
    };

    /** @brief 경제 알림 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)입니다. 새 알림은 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class EconomyPushName : uint8
    {
        BalanceChanged,
        Count,
    };
} // namespace sw

namespace sw
{
    /** @brief 경제 요청의 결과입니다. 와이어 값이므로 순서를 바꾸지 않고 새 값은 끝에만 추가합니다. */
    ENUM( Count = Count )
    enum class EconomyResult : uint8
    {
        Ok = 0,
        UnknownOffer,
        NotOnSale,      ///< 판매 기간이 아니다
        NotPurchasable, ///< 결제 상품이라 화폐로 살 수 없다
        LimitReached,   ///< 계정당 구매 한도에 도달했다
        InsufficientFunds,
        CapExceeded,       ///< 받으면 잔액 상한을 넘는다
        UnknownProduct,    ///< 영수증의 상품이 카탈로그에 없다
        ReceiptInvalid,    ///< 서명, 형식, 상품이 맞지 않거나 모르는 스토어이거나 허락하지 않은 샌드박스이다
        ReceiptRefunded,   ///< 환불되었거나 취소된 거래이다
        ReceiptPending,    ///< 스토어에 닿지 못했다. 같은 영수증으로 다시 시도한다
        AlreadyRedeemed,   ///< 다른 계정이 이미 받은 영수증이다
        AlreadyRefunded,   ///< 이미 환불되었거나 청약철회한 거래이다
        WithdrawalExpired, ///< 청약철회 기한(구매 뒤 7일)이 지났다
        NotPurchaser,      ///< 다른 계정이 받은 거래이거나 받은 적이 없는 거래이다. 영수증의 구매자가 이 계정이 아닐 때도 쓴다
        NothingToWithdraw, ///< 지급한 재화를 모두 써서 돌려받을 것이 없다
        BuyerTokenMissing, ///< 스토어 답에 구매 때 찍은 계정 토큰이 없다 — 클라이언트의 구매 요청이 `ReceiptBuyerToken` 을 싣지 않았다(`StorePurchaseFlow`)
        Count
    };

    /**
     * @brief 경제 결과입니다. 업무 결과(`EconomyResult`) 또는 공통 범주(`OnlineError`)입니다.
     * @details 규칙 밖 값 · 다른 구매에 쓴 멱등 키는 `kInvalidRequest`, 버전 충돌이 상한을 넘으면 `kBusy`, 저장소 · 전송 장애는 `kUnavailable`(적용됐는지 모르므로 같은 키로 다시 한다),
     *          로그인 없음은 `kUnauthenticated` 이다. 서버 로직과 같은 프로세스의 호출(`EconomyService::submitCall`)이 이 형식을 쓴다.
     */
    using EconomyServiceResult = OnlineServiceResult<EconomyResult>;
} // namespace sw

namespace sw
{
    /** @brief 구매 요청입니다. */
    REFLECT()
    struct EconomyPurchaseRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = EconomyProtocol::kMaxOfferIDSize )
        string _offerID{};
        PROPERTY( Min = 1, Max = 1000 )
        int32 _count{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 영수증 지급 요청입니다. */
    REFLECT()
    struct EconomyRedeemRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = EconomyProtocol::kMaxStoreNameSize )
        string _storeName{};
        PROPERTY( MaxCount = EconomyProtocol::kMaxReceiptPayloadSize )
        string _payload{}; ///< 스토어가 준 영수증이다. 서명된 JWS, 구매 토큰, 주문 id 가 해당하며 그 뜻은 제공자가 안다. Apple JWS 는 수 KB 이다
    };
} // namespace sw

namespace sw
{
    /** @brief 청약철회 요청입니다. 지급 응답의 스토어와 거래 id 를 그대로 보냅니다. */
    REFLECT()
    struct EconomyWithdrawRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = EconomyProtocol::kMaxStoreNameSize )
        string _storeName{};
        PROPERTY( MaxCount = LedgerConstant::kMaxTokenBytes )
        string _transactionID{}; ///< 상한은 `LedgerConstant::kMaxTokenBytes` 와 같다
    };
} // namespace sw

namespace sw
{
    /** @brief 내역 요청입니다. */
    REFLECT()
    struct EconomyHistoryRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = EconomyProtocol::kMaxCursorSize )
        string _cursor{}; ///< 앞 응답의 `_nextCursor` 이다. 처음에는 빈 문자열이다
        PROPERTY( Min = 1, Max = EconomyProtocol::kMaxHistoryCount )
        int32 _maxCount{ 20 };
    };
} // namespace sw

namespace sw
{
    /** @brief 내역 한 줄입니다. 이 계정 쪽의 자산별 순변화이며 `_amount` 가 음수이면 나간 것입니다. */
    REFLECT()
    struct EconomyHistoryEntry
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = OnlineLimit::kMaxBalanceCount )
        vector<LedgerBalance> _listChange{}; ///< 상한은 `OnlineLimit::kMaxBalanceCount` 와 같다
        PROPERTY( MaxCount = LedgerConstant::kMaxReasonSize )
        string _reason{}; ///< 상한은 `LedgerConstant::kMaxReasonSize` 와 같다
        PROPERTY( MaxCount = LedgerConstant::kMaxMemoSize )
        string _memo{}; ///< 상한은 `LedgerConstant::kMaxMemoSize` 와 같다
        PROPERTY()
        int64 _timeMs{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 지갑(`GetWallet`)의 응답입니다. 이 계정의 잔액 전부(스냅샷)입니다. */
    REFLECT()
    struct EconomyWalletReply
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = OnlineLimit::kMaxBalanceCount )
        vector<LedgerBalance> _listBalance{}; ///< 상한은 `OnlineLimit::kMaxBalanceCount` 와 같다

        /** @brief 다시 시도할 업무 결과가 없다(스토어 대기는 지급에서만 난다 — `EconomyRedeemReply::isRetryable`). */
        [[nodiscard]] bool isRetryable() const { return false; }
    };

    /** @brief 내역(`GetHistory`)의 응답입니다. */
    REFLECT()
    struct EconomyHistoryReply
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = EconomyProtocol::kMaxHistoryCount )
        vector<EconomyHistoryEntry> _listHistory{};
        PROPERTY( MaxCount = EconomyProtocol::kMaxCursorSize )
        string _nextCursor{};

        /** @brief 다시 시도할 업무 결과가 없다(스토어 대기는 지급에서만 난다 — `EconomyRedeemReply::isRetryable`). */
        [[nodiscard]] bool isRetryable() const { return false; }
    };

    /** @brief 구매(`Purchase`)의 응답입니다. */
    REFLECT()
    struct EconomyPurchaseReply
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = OnlineLimit::kMaxBalanceCount )
        vector<LedgerBalance> _listBalance{}; ///< 바뀐 잔액만 담는다. 상한은 `OnlineLimit::kMaxBalanceCount` 와 같다
        PROPERTY()
        EconomyResult _result{ EconomyResult::Ok };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bReplayed{ SW_FALSE }; ///< 같은 멱등 키의 지난 결과이다

        /** @brief 다시 시도할 업무 결과가 없다(스토어 대기는 지급에서만 난다 — `EconomyRedeemReply::isRetryable`). */
        [[nodiscard]] bool isRetryable() const { return false; }
    };

    /** @brief 영수증 지급(`RedeemReceipt`)의 응답입니다. */
    REFLECT()
    struct EconomyRedeemReply
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = OnlineLimit::kMaxBalanceCount )
        vector<LedgerBalance> _listBalance{}; ///< 바뀐 잔액만 담는다. 상한은 `OnlineLimit::kMaxBalanceCount` 와 같다
        PROPERTY( MaxCount = EconomyProtocol::kMaxProductIDSize )
        string _productID{}; ///< 영수증에 포함된 스토어 상품이다
        PROPERTY()
        EconomyResult _result{ EconomyResult::Ok };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bReplayed{ SW_FALSE }; ///< 같은 영수증의 지난 결과이다

        /**
         * @brief 같은 멱등 키로 다시 시도할 업무 결과(스토어 대기)인지입니다. 서버는 이 답을 기억하지 않고(`OnlineServiceHost::respondReply`) 클라이언트 흐름은 거래를 마치지 않는다.
         * @details 충돌 · 일시 장애는 공통 범주(`kBusy` · `kUnavailable`)로 오고 보존은 그 범주가 정한다 — 이 판정은 공통 범주가 아닌 재시도 결과만 본다. 스토어 대기는 지급에서만 난다.
         */
        [[nodiscard]] bool isRetryable() const { return _result == EconomyResult::ReceiptPending; }
    };

    /** @brief 청약철회(`WithdrawPurchase`)의 응답입니다. */
    REFLECT()
    struct EconomyWithdrawReply
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = OnlineLimit::kMaxBalanceCount )
        vector<LedgerBalance> _listBalance{}; ///< 회수 뒤 바뀐 잔액이다. 상한은 `OnlineLimit::kMaxBalanceCount` 와 같다
        PROPERTY( MaxCount = LedgerConstant::kMaxPostingCount )
        vector<LedgerBalance> _listWithdrawn{}; ///< 회수한 자산과 양이다. 스토어 환불 금액은 이것으로 정한다
        PROPERTY()
        EconomyResult _result{ EconomyResult::Ok };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bReplayed{ SW_FALSE }; ///< 같은 거래의 지난 결과이다

        /** @brief 다시 시도할 업무 결과가 없다(스토어 대기는 지급에서만 난다 — `EconomyRedeemReply::isRetryable`). */
        [[nodiscard]] bool isRetryable() const { return false; }
    };
} // namespace sw

namespace sw
{
    /** @brief 잔액 변경 알림의 본문입니다. 바뀐 자산의 잔액 절댓값을 싣습니다. 서버 간 push 중계에도 실립니다. */
    REFLECT()
    struct SW_KIT_API EconomyBalanceChanged
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1; ///< push 중계(`OnlinePushRelay`)의 의미 버전
        PROPERTY( MaxCount = OnlineLimit::kMaxBalanceCount )
        vector<LedgerBalance> _listBalance{}; ///< 상한은 `OnlineLimit::kMaxBalanceCount` 와 같다
    };
} // namespace sw

namespace sw
{
    /** @brief Economy 메서드 표입니다. 번호는 열거자 순서에서 나오고, 요청 · 응답의 짝과 호출 규칙은 서술자가 정합니다. */
    struct EconomyMethod : OnlineMethodTable<EconomyMethodName, OnlineMethodRange::kEconomy, EconomyPushName>
    {
        using GetWallet        = Method<MethodName::GetWallet, OnlineNoBody, EconomyWalletReply>;
        using GetHistory       = Method<MethodName::GetHistory, EconomyHistoryRequest, EconomyHistoryReply>;
        using Purchase         = Method<MethodName::Purchase, EconomyPurchaseRequest, EconomyPurchaseReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using RedeemReceipt    = Method<MethodName::RedeemReceipt, EconomyRedeemRequest, EconomyRedeemReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using WithdrawPurchase = Method<MethodName::WithdrawPurchase, EconomyWithdrawRequest, EconomyWithdrawReply, OnlineMethodFlag::RequiresIdempotencyKey>; ///< 유상 구매 청약철회이다. 기한 안에 쓰지 않은 만큼 회수한다. 분개 키가 거래 id 라 다시 보내도 한 번만 적용된다
        using BalanceChanged   = Push<PushName::BalanceChanged, EconomyBalanceChanged>;                                                                        ///< 이 키트의 요청 밖에서 잔액이 바뀌었다(우편 수령, 거래 정산, GM 지급 · 회수, 환불 회수). 클라이언트는 캐시를 그 값으로 덮는다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct EconomyProtocol
     * @brief 경제 키트의 메시지 타입 목록과 등록 항목입니다. 본문은 고정 배치형(B)이며 `MessageTypes::write` · `read` 로만 쓰고 읽습니다.
     * @details 범위 · 길이 상한은 메시지 타입의 `PROPERTY` 키가 원본입니다. 범위 밖 값은 쓰기가 실패하고, 읽을 때는 `OutOfRange` 로 거절됩니다.
     */
    struct SW_KIT_API EconomyProtocol
    {
        using Table        = OnlineProtocolTable<EconomyMethod, OnlineMethodList<EconomyMethod::GetWallet, EconomyMethod::GetHistory, EconomyMethod::Purchase, EconomyMethod::RedeemReceipt, EconomyMethod::WithdrawPurchase>, OnlinePushList<EconomyMethod::BalanceChanged>>;
        using MessageTypes = Table::MessageTypes;
        using ResultName   = EconomyResult;

        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다
        static constexpr int32           kMaxOfferIDSize        = 64;
        static constexpr int32           kMaxStoreNameSize      = 64;
        static constexpr int32           kMaxProductIDSize      = 64;
        static constexpr int32           kMaxReceiptPayloadSize = 16384; ///< 영수증 한 장의 크기이다. Apple JWS 는 수 KB 이다
        static constexpr int32           kMaxCursorSize         = 512;
        static constexpr int32           kMaxHistoryCount       = 100; ///< 내역 응답 한 번의 항목 수이며 요청의 `_maxCount` 상한(`Max`)이기도 하다
    };
} // namespace sw
