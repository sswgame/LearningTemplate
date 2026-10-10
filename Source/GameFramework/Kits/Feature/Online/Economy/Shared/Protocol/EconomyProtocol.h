/**
 * @file EconomyProtocol.h
 * @brief 경제 서비스의 와이어 — 메서드 번호(영역 `OnlineMethodRange::kEconomy`) · 결과 · 요청/응답 형식입니다(클라이언트 · 서버가 같이 쓴다).
 * @details 응답 몸 = `EconomyResult` + 잔액 · 내역(업무 결과는 몸에 — 거래 · 계정 키트와 같다). 공통 오류(`OnlineError` — 로그인 없음 · 깨진 몸 · 시한)만 오류 코드로 오고,
 *          클라이언트는 `fromErrorCode` 로 결과에 맞춘다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 경제 서비스 메서드입니다. 와이어 값 — 바꾸면 `EconomyProtocol::kVersion` 을 올린다. */
    struct EconomyMethod
    {
        static constexpr uint16 kGetWallet     = OnlineMethodRange::kEconomy + 0x01;
        static constexpr uint16 kGetHistory    = OnlineMethodRange::kEconomy + 0x02;
        static constexpr uint16 kPurchase      = OnlineMethodRange::kEconomy + 0x03; ///< 멱등 키 필수
        static constexpr uint16 kRedeemReceipt = OnlineMethodRange::kEconomy + 0x04;
        static constexpr int32  kCount         = 4;

        static_assert( OnlineMethodRange::isInRange( kRedeemReceipt, OnlineMethodRange::kEconomy ) && OnlineMethodRange::isMethod( kRedeemReceipt ) );

        /** @brief 0 부터의 번호(지표 · 표)입니다. 모르는 메서드면 −1 입니다. */
        static constexpr int32 toIndex( uint16 method )
        {
            return kGetWallet <= method && method < kGetWallet + kCount ? static_cast<int32>( method - kGetWallet ) : -1;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 경제 요청의 결과입니다. 와이어 값이라 순서를 바꾸지 않는다(끝에만 더한다). */
    enum class EconomyResult : uint8
    {
        Ok = 0,
        NotSignedIn,
        InvalidRequest,
        UnknownOffer,
        NotOnSale,      ///< 판매 기간 밖
        NotPurchasable, ///< 결제 상품(화폐로 살 수 없다)
        LimitReached,   ///< 계정당 구매 한도
        InsufficientFunds,
        CapExceeded,     ///< 받으면 잔액 상한을 넘는다
        UnknownProduct,  ///< 영수증의 상품이 카탈로그에 없다
        ReceiptInvalid,  ///< 서명 · 형식 · 상품이 맞지 않는다 · 모르는 스토어 · 허락하지 않은 샌드박스
        ReceiptRefunded, ///< 환불 · 취소된 거래
        ReceiptPending,  ///< 스토어에 닿지 못했다 — 같은 영수증으로 다시
        AlreadyRedeemed, ///< 다른 계정이 이미 받은 영수증
        Busy,            ///< 판 충돌이 상한을 넘었다 — 같은 멱등 키로 다시
        Unavailable,     ///< 저장소 · 전송 — 적용 여부 모름, 같은 멱등 키로 다시
        Count
    };

    SW_GF_API const utf8* toString( EconomyResult result );
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
        string _payload{}; ///< 스토어가 준 영수증(서명된 JWS · 구매 토큰 · 주문 id — 제공자가 뜻을 안다)
    };
} // namespace sw

namespace sw
{
    /** @brief 내역 요청입니다. */
    struct EconomyHistoryRequest
    {
        string _cursor{}; ///< 앞 응답의 `_nextCursor`(처음은 빈 글)
        int32  _maxCount{ 20 };
    };
} // namespace sw

namespace sw
{
    /** @brief 내역 한 줄 — 이 계정 쪽의 자산별 순변화입니다(`_amount` 가 음수면 나감). */
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
    /** @brief 모든 메서드의 응답(메서드마다 쓰는 칸만 찬다)입니다. 실패면 `_result` 만 뜻이 있다. */
    struct EconomyReply
    {
        vector<LedgerBalance>       _listBalance{}; ///< GetWallet: 전부(스냅숏), Purchase · Redeem: 바뀐 것만
        vector<EconomyHistoryEntry> _listHistory{};
        string                      _productID{}; ///< Redeem — 영수증의 스토어 상품
        string                      _nextCursor{};
        EconomyResult               _result{ EconomyResult::Ok };
        uint8                       _bReplayed{ SW_FALSE }; ///< 같은 멱등 키 · 같은 영수증의 지난 결과
    };
} // namespace sw

namespace sw
{
    /** @brief 형식 쓰기 · 읽기입니다. 읽기는 상한을 넘거나 모자라면 false 입니다. */
    struct SW_GF_API EconomyProtocol
    {
        static constexpr uint32 kVersion          = 1;
        static constexpr int32  kMaxPayloadSize   = 16 * 1024; ///< 영수증(Apple JWS 는 수 KB)
        static constexpr int32  kMaxIDSize        = 64;
        static constexpr int32  kMaxCursorSize    = 512;
        static constexpr int32  kMaxHistoryCount  = 100;
        static constexpr int32  kMaxBalanceCount  = 1024;
        static constexpr int32  kMaxPurchaseCount = 1000;

        static void               writePurchaseRequest( BitWriter& outWriter, const EconomyPurchaseRequest& request );
        [[nodiscard]] static bool readPurchaseRequest( BitReader& reader, EconomyPurchaseRequest& outRequest );
        static void               writeRedeemRequest( BitWriter& outWriter, const EconomyRedeemRequest& request );
        [[nodiscard]] static bool readRedeemRequest( BitReader& reader, EconomyRedeemRequest& outRequest );
        static void               writeHistoryRequest( BitWriter& outWriter, const EconomyHistoryRequest& request );
        [[nodiscard]] static bool readHistoryRequest( BitReader& reader, EconomyHistoryRequest& outRequest );
        /** @brief 응답 몸입니다(결과 + 칸). */
        static void               writeReply( BitWriter& outWriter, const EconomyReply& reply );
        [[nodiscard]] static bool readReply( BitReader& reader, EconomyReply& outReply );
        static void               writeBalances( BitWriter& outWriter, const vector<LedgerBalance>& listBalance );
        [[nodiscard]] static bool readBalances( BitReader& reader, vector<LedgerBalance>& outListBalance );

        /** @brief 공통 오류 코드(`OnlineError`) → 결과입니다 — 로그인 없음 · 깨진 몸 · 충돌은 그 결과, 나머지(전송 · 저장소 · 시한)는 Unavailable 입니다. */
        static EconomyResult fromErrorCode( uint16 errorCode );
    };
} // namespace sw
