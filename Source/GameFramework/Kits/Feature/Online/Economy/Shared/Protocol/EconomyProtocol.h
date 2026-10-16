/**
 * @file EconomyProtocol.h
 * @brief 경제 서비스의 와이어 형식입니다. 메서드 번호(영역 `OnlineMethodRange::kEconomy`), 결과, 요청과 응답 형식을 정의하며 클라이언트와 서버가 함께 씁니다.
 * @details 응답 본문은 `EconomyResult` 뒤에 잔액과 내역이 이어지는 형식이다. 업무 결과는 본문에 담으며 거래와 계정 키트도 같다.
 *          공통 오류(`OnlineError`)만 오류 코드로 오며 로그인 없음, 손상된 본문, 시한 초과가 해당한다. 클라이언트는 `fromErrorCode` 로 이를 결과에 맞춘다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 경제 서비스 메서드 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)이고, 이름의 snake_case 가 지표 라벨입니다. 새 메서드는 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class EconomyMethodName : uint8
    {
        GetWallet,
        GetHistory,
        Purchase,
        RedeemReceipt,
        Count,
    };
} // namespace sw

namespace sw
{
    /** @brief 경제 서비스 메서드 번호입니다. 와이어 값이므로 바꾸면 `EconomyProtocol::kVersion` 을 올려야 합니다. */
    struct EconomyMethod : OnlineMethodTable<EconomyMethodName, OnlineMethodRange::kEconomy>
    {
        static constexpr uint16 kGetWallet     = toMethod( EconomyMethodName::GetWallet );
        static constexpr uint16 kGetHistory    = toMethod( EconomyMethodName::GetHistory );
        static constexpr uint16 kPurchase      = toMethod( EconomyMethodName::Purchase ); ///< 멱등 키가 필수이다
        static constexpr uint16 kRedeemReceipt = toMethod( EconomyMethodName::RedeemReceipt );
    };
} // namespace sw

namespace sw
{
    /** @brief 경제 요청의 결과입니다. 와이어 값이므로 순서를 바꾸지 않고 새 값은 끝에만 추가합니다. */
    ENUM( Count = Count )
    enum class EconomyResult : uint8
    {
        Ok = 0,
        NotSignedIn,
        InvalidRequest,
        UnknownOffer,
        NotOnSale,      ///< 판매 기간이 아니다
        NotPurchasable, ///< 결제 상품이라 화폐로 살 수 없다
        LimitReached,   ///< 계정당 구매 한도에 도달했다
        InsufficientFunds,
        CapExceeded,     ///< 받으면 잔액 상한을 넘는다
        UnknownProduct,  ///< 영수증의 상품이 카탈로그에 없다
        ReceiptInvalid,  ///< 서명, 형식, 상품이 맞지 않거나 모르는 스토어이거나 허락하지 않은 샌드박스이다
        ReceiptRefunded, ///< 환불되었거나 취소된 거래이다
        ReceiptPending,  ///< 스토어에 닿지 못했다. 같은 영수증으로 다시 시도한다
        AlreadyRedeemed, ///< 다른 계정이 이미 받은 영수증이다
        Busy,            ///< 버전 충돌이 상한 횟수를 넘었다. 같은 멱등 키로 다시 시도한다
        Unavailable,     ///< 저장소나 전송을 쓸 수 없다. 적용됐는지 알 수 없으므로 같은 멱등 키로 다시 시도한다
        UpdateRequired,  ///< 온라인 스키마 해시가 다르다(`kVersionMismatch`). 클라이언트만 만들고 서버는 보내지 않는다. 다시 접속하지 않으므로 재시도하지 않는다
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 구매 요청입니다. */
    struct EconomyPurchaseRequest
    {
        string _offerID{};
        int32  _count{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 영수증 지급 요청입니다. */
    struct EconomyRedeemRequest
    {
        string _storeName{};
        string _payload{}; ///< 스토어가 준 영수증이다. 서명된 JWS, 구매 토큰, 주문 id 가 해당하며 그 뜻은 제공자가 안다
    };
} // namespace sw

namespace sw
{
    /** @brief 내역 요청입니다. */
    struct EconomyHistoryRequest
    {
        string _cursor{}; ///< 앞 응답의 `_nextCursor` 이다. 처음에는 빈 문자열이다
        int32  _maxCount{ 20 };
    };
} // namespace sw

namespace sw
{
    /** @brief 내역 한 줄입니다. 이 계정 쪽의 자산별 순변화이며 `_amount` 가 음수이면 나간 것입니다. */
    struct EconomyHistoryEntry
    {
        vector<LedgerBalance> _listChange{};
        string                _reason{};
        string                _memo{};
        int64                 _timeMs{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 응답입니다. 메서드마다 쓰는 필드만 채웁니다. 실패하면 `_result` 만 의미가 있습니다. */
    struct EconomyReply
    {
        vector<LedgerBalance>       _listBalance{}; ///< GetWallet 은 전부(스냅샷)를, Purchase 와 Redeem 은 바뀐 것만 담는다
        vector<EconomyHistoryEntry> _listHistory{};
        string                      _productID{}; ///< Redeem 의 영수증에 든 스토어 상품이다
        string                      _nextCursor{};
        EconomyResult               _result{ EconomyResult::Ok };
        uint8                       _bReplayed{ SW_FALSE }; ///< 같은 멱등 키나 같은 영수증의 지난 결과이다
    };
} // namespace sw

namespace sw
{
    /** @brief 형식을 읽고 씁니다. 읽을 때 상한을 넘거나 모자라면 false 를 반환합니다. */
    struct SW_KIT_API EconomyProtocol
    {
        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다
        static constexpr uint32          kVersion          = 1;
        static constexpr int32           kMaxPayloadSize   = 16 * 1024; ///< 영수증의 상한이다. Apple JWS 는 수 KB 이다
        static constexpr int32           kMaxIDSize        = 64;
        static constexpr int32           kMaxCursorSize    = 512;
        static constexpr int32           kMaxHistoryCount  = 100;
        static constexpr int32           kMaxPurchaseCount = 1000;

        static void               writePurchaseRequest( BitWriter& outWriter, const EconomyPurchaseRequest& request );
        [[nodiscard]] static bool readPurchaseRequest( BitReader& reader, EconomyPurchaseRequest& outRequest );
        static void               writeRedeemRequest( BitWriter& outWriter, const EconomyRedeemRequest& request );
        [[nodiscard]] static bool readRedeemRequest( BitReader& reader, EconomyRedeemRequest& outRequest );
        static void               writeHistoryRequest( BitWriter& outWriter, const EconomyHistoryRequest& request );
        [[nodiscard]] static bool readHistoryRequest( BitReader& reader, EconomyHistoryRequest& outRequest );
        /** @brief 응답 본문을 읽고 씁니다. 결과 뒤에 필드가 이어집니다. */
        static void               writeReply( BitWriter& outWriter, const EconomyReply& reply );
        [[nodiscard]] static bool readReply( BitReader& reader, EconomyReply& outReply );
        static void               writeBalances( BitWriter& outWriter, const vector<LedgerBalance>& listBalance );
        [[nodiscard]] static bool readBalances( BitReader& reader, vector<LedgerBalance>& outListBalance );

        /** @brief 공통 오류 코드(`OnlineError`)를 결과로 바꿉니다. 로그인 없음, 손상된 본문, 충돌은 각각 대응하는 결과로, 나머지(전송, 저장소, 시한)는 Unavailable 로 바꿉니다. */
        static EconomyResult fromErrorCode( uint16 errorCode );
    };
} // namespace sw
