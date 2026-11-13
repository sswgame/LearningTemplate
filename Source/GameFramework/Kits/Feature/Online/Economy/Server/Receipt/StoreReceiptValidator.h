/**
 * @file StoreReceiptValidator.h
 * @brief HTTP 로 묻는 실제 스토어 검증 제공자의 공통 틀입니다. 스토어는 요청의 모양과 응답 파싱만 적고, 판단은 여기 한 벌만 둡니다.
 * @details 틀이 정하는 것:
 *          - **응답 분류**(`classifyResponse`): 전송 실패 · 429 · 5xx 는 Retry, 401 · 403 은 우리 자격 문제라 Retry(오류 로그는 스토어가 남긴다), 그 밖의 4xx 는 Invalid, 2xx 인데 JSON 이 아니면 Retry 다.
 *            "스토어가 없다고 답했다" 만 Invalid 이다 — 읽지 못한 답을 Invalid 로 하면 결제한 사람의 영수증이 버려진다.
 *          - **요청과의 대조**(`applyOrderFacts`): 물은 거래 · 상품 · 키와 답이 다르면 Invalid, 판정에 필요한 거래 id · 상품이 없으면 Invalid 이다. 스토어는 답이 말한 값을 옮겨 적기만 한다.
 *          - **조회 구간**(`ReceiptPollWindow`): 끝까지 읽은 조회만 구간을 옮긴다. 실패 · 읽지 못한 항목 · 잇지 못한 잘림은 그대로 둬 다음 조회가 다시 읽는다 — 구간을 지나치면 그 환불을 영영 놓친다.
 *            기준 시각은 서비스가 저장소(`econ_refund_cursor`)에 남긴다. 받기 전(`restoreRefundCursor`)에는 조회하지 않아 재시작이 구간을 비우지 않고, 스토어의 한계를 넘겨 멈춘 동안 놓친 구간은 경고한다.
 *          - **지급 알림**(`confirmDelivery`): 답이 "지급 뒤에 알려라" 고 하면(`_bNeedsDeliveryConfirm`) 결과에 키만 실어 내고, 알림은 서비스가 지급을 커밋한 뒤에 맡긴다. 2xx Confirmed, 그 밖의 4xx Refused, 닿지 못함 · 자격 거절 Retry 다.
 *          - 결과 · 환불 알림의 큐, 겹친 알림 거름, 종료 때 Retry 로 끝내기, 자격이 준비될 때까지 호출을 기다리게 하기.
 *          언리얼 OSS(`IOnlinePurchase`)는 플랫폼마다 영수증 파싱만 다르고 공통 상태(`EPurchaseTransactionState`)를 내며, 유니티 IAP 도 `IPurchaseReceipt` 하나로 모은다(기억에 의존). 둘 다 판단을 플랫폼 코드에
 *          흩어 두는데, 이 틀은 한 걸음 더 가 분류와 대조까지 위로 올린다 — 스토어가 늘 때 같은 결함을 다시 쓰지 못하게 하려는 것이다. 새 스토어는 이 클래스를 상속해 순수 가상 다섯을 적는다.
 *          시계를 읽지 않는다 — 시각은 서버 루프의 `now` 다. 서비스 스레드 하나에서 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptValidator.h"
#include "GameFramework/Runtime/Online/HTTP/HTTPTypes.h"

namespace sw
{
    class HTTPClient;
    class JSONValue;
} // namespace sw

namespace sw
{
    /** @brief 스토어 응답의 분류입니다. */
    enum class ReceiptResponseKind : uint8
    {
        Unreachable = 0,    ///< 전송 실패 · 429 · 5xx — 나중에 다시 묻는다
        CredentialRejected, ///< 401 · 403 — 우리 자격이 거절됐다. 영수증 탓이 아니다
        Refused,            ///< 그 밖의 2xx 아닌 답 — 스토어가 모른다고 했다
        Answered            ///< 2xx — 본문을 읽어 판단한다
    };
} // namespace sw

namespace sw
{
    /** @brief 영수증을 읽어 알게 된, 스토어에 무엇을 물었는가입니다. 스토어마다 아는 칸이 다르고 모르는 칸은 비워 둡니다. */
    struct ReceiptAsk
    {
        string _transactionID{}; ///< 거래 id 를 물었다(스팀 주문 번호, 애플 거래 id). 답의 거래 id 가 이것과 같아야 한다
        string _productID{};     ///< 상품을 정해 물었다(구글). 답이 상품을 말하면 이것과 같아야 하고, 말하지 않으면 이 값을 쓴다
        string _askedKey{};      ///< 거래 id 가 아닌 물음의 키(구글 구매 토큰). 답이 되풀이하면 같아야 한다
    };

    /** @brief 스토어 답이 말한 사실입니다. 파싱이 옮겨 적기만 하고 판단은 틀이 합니다. */
    struct ReceiptOrderFacts
    {
        string        _transactionID{};
        string        _productID{};
        string        _answeredKey{};  ///< 답이 되풀이한 물음의 키(없으면 비어 있다)
        string        _storeBuyerID{}; ///< 답이 말한 구매자(없으면 비어 있다)
        string        _failureText{};  ///< `_status` 가 Valid 가 아닐 때의 까닭(영어)
        int64         _purchaseTimeMs{ 0 };
        int64         _deliveryDeadlineMs{ 0 }; ///< `_bNeedsDeliveryConfirm` 일 때 스토어가 결제를 되돌리는 시각
        ReceiptStatus _status{ ReceiptStatus::Invalid };
        uint8         _bSandbox{ SW_FALSE };
        uint8         _bNeedsDeliveryConfirm{ SW_FALSE }; ///< Valid 면 지급한 뒤 스토어에 알려야 한다(구글의 구매 확인). 지급 전에 알리면 지급이 실패했을 때 환불도 재시도도 없다
    };

    /** @brief 환불 조회 응답 한 쪽에서 읽은 것입니다. 알림 자체는 `addRefund` 로 넘깁니다. */
    struct ReceiptRefundPage
    {
        string _nextPageToken{};         ///< 다음 쪽의 토큰(없으면 이 쪽이 끝이다)
        int64  _cursorMs{ 0 };           ///< 시각으로 잇는 스토어의 다음 시작 시각(`_bTruncated` 일 때)
        uint8  _bIncomplete{ SW_FALSE }; ///< 읽지 못한 것이 있다 — 구간을 옮기지 않는다
        uint8  _bTruncated{ SW_FALSE };  ///< 한 번에 읽는 상한까지 왔는데 다음 쪽 토큰이 없다
    };

    /** @brief 틀이 보내는 요청 하나입니다. */
    struct ReceiptCall
    {
        enum class Kind : uint8
        {
            Validate = 0,    ///< 영수증 확인
            ConfirmDelivery, ///< 지급을 마쳤다는 알림(`IReceiptValidator::confirmDelivery`)
            RefundPage       ///< 환불 조회의 한 쪽
        };

        ReceiptValidationRequest _request{};
        ReceiptAsk               _ask{};
        string                   _recordKey{}; ///< ConfirmDelivery — 결과에 그대로 돌려주는 저장소 키
        string                   _pageToken{}; ///< RefundPage — 첫 쪽이면 비어 있다
        int32                    _page{ 0 };   ///< RefundPage — 몇 번째 쪽인가
        Kind                     _kind{ Kind::Validate };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ReceiptPollWindow
     * @brief 환불 조회의 구간과 주기입니다. 끝까지 읽은 조회만 시작점을 옮깁니다.
     * @details 저장소에 남은 기준이 없으면 첫 조회의 시각이 기준이 된다(첫 조회들이 실패해도 시작점이 뒤로 밀리지 않는다). 시작점은 `기준 - 겹침` 이고 스토어가 받는 가장 오랜 시각(`maxAgeMs`, 0 이면 제한 없음)으로 자른다.
     *          잘린 만큼은 스토어에 물을 길이 없는 구간이다(`getLostMs`).
     */
    class SW_KIT_SERVER_API ReceiptPollWindow
    {
    public:
        ReceiptPollWindow( int64 intervalMs, int64 lookbackMs, int64 maxAgeMs );

        /** @brief 저장소에 남은 기준 시각을 넣습니다. 0 이면 기준이 없는 것입니다. */
        void restore( int64 lastReadMs ) { _lastMs = lastReadMs; }
        /** @brief 지금 기준 시각입니다. 끝까지 읽은 조회가 옮깁니다. */
        int64 getLastReadMs() const { return _lastMs; }
        /** @brief 이번 조회의 시작점이 스토어 한계에 잘려 묻지 못하는 구간의 길이입니다. 잘리지 않았거나 잘린 조회를 잇는 중이면 0 입니다. */
        int64 getLostMs() const;
        /** @brief 주기가 됐고 진행 중인 조회가 없으면 새 조회를 시작하고 true 입니다. */
        [[nodiscard]] bool tryBegin( int64 nowMs );
        /** @brief 이번 조회의 시작 시각입니다. */
        int64 getFromMs() const;
        /** @brief 이번 조회의 끝 시각입니다. */
        int64 getToMs() const { return _startMs; }
        bool  isInFlight() const { return _bInFlight == SW_TRUE; }
        /** @brief 끝까지 읽었습니다. 다음 조회의 기준을 이번 조회로 옮깁니다. */
        void finishRead();
        /** @brief 읽지 못했습니다. 다음 조회가 같은 시작점에서 다시 읽습니다. */
        void finishFailed();
        /** @brief 끝까지 갔지만 읽지 못한 항목이 있었습니다. 다음 조회가 구간의 처음(잘린 조회를 잇는 중이었어도)부터 다시 읽습니다. */
        void finishIncomplete();
        /** @brief 상한에서 잘렸습니다. 다음 `tryBegin` 을 바로 받아 @p cursorMs 부터 잇습니다. 이번 조회의 끝 시각은 그대로입니다. */
        void continueFrom( int64 cursorMs );

    private:
        int64 _intervalMs;
        int64 _lookbackMs;
        int64 _maxAgeMs;
        int64 _nextMs;   ///< 다음 조회 시각
        int64 _lastMs;   ///< 시작점의 기준(지난 성공한 조회의 시작 시각, 없으면 첫 조회의 시각)
        int64 _startMs;  ///< 지금 조회의 시작 시각
        int64 _cursorMs; ///< 잘린 조회를 잇는 시각(0 이면 잇는 중이 아니다)
        uint8 _bInFlight : 1;
    };
} // namespace sw

namespace sw
{
    /**
     * @class StoreReceiptValidator
     * @brief 실제 스토어 제공자의 공통 틀입니다. 파일 머리말 참고.
     */
    class SW_KIT_SERVER_API StoreReceiptValidator : public IReceiptValidator
    {
    public:
        static constexpr int32 kMaxRefundPages = 200; ///< 한 조회가 따라가는 쪽 수의 상한. 토큰이 맴돌아도 끝난다 — 넘으면 구간을 옮기지 않고 경고한다

        /** @brief 응답을 분류합니다. */
        static ReceiptResponseKind classifyResponse( const HTTPClientResponse& response );
        /** @brief 답의 사실을 물은 것과 대조해 결과를 채웁니다. @p inoutResult 의 표 · 계정 · 물은 값은 이미 있습니다. */
        static void applyOrderFacts( const ReceiptAsk& ask, const ReceiptOrderFacts& facts, ReceiptValidationResult& inoutResult );

        const ReceiptBuyerRule& getBuyerRule() const final { return _buyerRule; }
        void                    submitValidation( const ReceiptValidationRequest& request, UnixMilliseconds now ) final;
        /** @brief HTTP 클라이언트를 돌리고, 주기가 되면 환불을 조회하고, 끝난 응답을 처리합니다. */
        void               update( UnixMilliseconds now ) final;
        void               pollCompletions( vector<ReceiptValidationResult>& outListResult ) final;
        void               pollRefunds( vector<ReceiptRefundNotification>& outListNotice ) final;
        void               removeRefund( string_view transactionID ) final;
        bool               usesRefundCursor() const final { return true; }
        void               restoreRefundCursor( int64 lastReadMs ) final;
        [[nodiscard]] bool takeRefundCursor( int64& outLastReadMs ) final;
        void               confirmDelivery( const ReceiptDeliveryTask& task, UnixMilliseconds now ) final;
        void               pollDeliveryResults( vector<ReceiptDeliveryResult>& outListServiceResult ) final;
        void               shutdown() final;

    protected:
        /** @brief 자격의 상태입니다. */
        enum class AuthState : uint8
        {
            Ready = 0, ///< 지금 보낼 수 있다
            Waiting,   ///< 자격을 받는 중이다 — 호출을 기다리게 하고 `flushWaiting` 을 기다린다
            Failed     ///< 자격을 만들지 못했다
        };

        /** @brief @p pHTTPClient 는 빌려 쓰며 이 제공자만 응답을 회수합니다. */
        StoreReceiptValidator( ReceiptBuyerRule buyerRule, ReceiptPollWindow refundWindow, HTTPClient* pHTTPClient );
        ~StoreReceiptValidator() override;

        /** @brief 영수증 글을 읽어 @p outAsk 를 채웁니다. 요청에 실을 수 없는 글이면 그 까닭(문장이 실패 글이다)입니다 — 스토어에 묻지 않고 Invalid 로 끝납니다. */
        [[nodiscard]] virtual expected<void> readPayload( string_view payload, ReceiptAsk& outAsk ) const = 0;
        /** @brief @p call 의 HTTP 요청을 만듭니다. */
        virtual HTTPClientRequest makeHTTPRequest( const ReceiptCall& call, UnixMilliseconds now ) = 0;
        /** @brief 영수증 확인 답을 사실로 옮겨 적습니다. @p root 는 JSON 이지만 모양은 보장하지 않습니다. */
        virtual void parseOrder( const JSONValue& root, const ReceiptCall& call, UnixMilliseconds now, ReceiptOrderFacts& outFacts ) = 0;
        /** @brief 환불 조회 답 한 쪽을 읽어 `addRefund` 로 알립니다. @p root 는 JSON 이지만 모양은 보장하지 않습니다. */
        virtual void parseRefundPage( const JSONValue& root, UnixMilliseconds now, ReceiptRefundPage& outPage ) = 0;
        /** @brief 401 · 403 을 받았습니다. 자격을 버리고 오류 로그를 남깁니다. */
        virtual void onCredentialRejected( const HTTPClientResponse& response ) = 0;

        /** @brief 호출 전에 자격을 준비합니다. 기본은 늘 준비됨입니다. */
        virtual AuthState prepareAuth( UnixMilliseconds now )
        {
            (void)now;
            return AuthState::Ready;
        }
        /** @brief 자격 요청의 응답이면 처리하고 true 입니다(끝나면 `flushWaiting`). */
        virtual bool consumeAuthResponse( const HTTPClientResponse& response, UnixMilliseconds now )
        {
            (void)response;
            (void)now;
            return false;
        }
        /** @brief 기다리던 호출을 보냅니다. @p bGranted 가 false 면 검증은 Retry 로, 환불 조회는 다음 주기로 넘깁니다. */
        void flushWaiting( bool bGranted, UnixMilliseconds now );
        /** @brief 환불 하나를 알립니다. 겹친 조회의 같은 거래는 한 번만 알립니다(비어 있으면 알리지 않습니다). */
        void                     addRefund( string_view transactionID, int64 refundedMs );
        const ReceiptPollWindow& getRefundWindow() const { return _refundWindow; }

    private:
        void                    dispatch( ReceiptCall call, UnixMilliseconds now );
        void                    send( ReceiptCall call, UnixMilliseconds now );
        void                    failCall( const ReceiptCall& call );
        ReceiptValidationResult makeResult( const ReceiptCall& call ) const;
        void                    completeRetry( const ReceiptCall& call, string failureText );
        void                    finishValidation( const ReceiptCall& call, const HTTPClientResponse& response, UnixMilliseconds now );
        void                    finishDelivery( const ReceiptCall& call, const HTTPClientResponse& response );
        void                    completeDelivery( const ReceiptCall& call, ReceiptDeliveryStatus status );
        void                    finishRefundPage( const ReceiptCall& call, const HTTPClientResponse& response, UnixMilliseconds now );

    private:
        ReceiptBuyerRule                   _buyerRule;
        ReceiptPollWindow                  _refundWindow;
        unordered_map<uint64, ReceiptCall> _mapRequestToCall;
        vector<ReceiptCall>                _listWaiting;
        vector<ReceiptValidationResult>    _listDone;
        vector<ReceiptRefundNotification>  _listRefund;
        vector<ReceiptDeliveryResult>      _listDeliveryDone;
        unordered_set<string>              _uniqueSetNotifiedRefund;
        HTTPClient*                        _pHTTPClient;
        uint8                              _bRefundIncomplete : 1; ///< 지금 조회의 어느 쪽에 읽지 못한 것이 있었다
        uint8                              _bStopped          : 1; ///< `shutdown` 뒤에는 새 요청을 보내지 않는다
        uint8                              _bCursorRestored   : 1; ///< 저장소의 기준 시각을 받았다. 그 전에는 조회하지 않는다
        uint8                              _bCursorMoved      : 1; ///< 끝까지 읽은 조회가 기준을 옮겼고 아직 꺼내 가지 않았다
    };
} // namespace sw
