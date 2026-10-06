/**
 * @file LedgerTypes.h
 * @brief 원장 타입 — 보유자(계정 · 맡김 · 발행 · 소각) · 이동 다리 · 잔액 · 요청 · 결과 · 분개, 분개 키 만들기와 글자 규칙입니다.
 * @details 원장은 자산의 뜻을 모른다. 자산 id 는 `[0-9a-z_.-]` 48 자 이하이고 관례로 화폐는 `cur.<이름>`, 아이템은 `item.<이름>` 이다(카탈로그가 뜻을 준다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 원장 보유자의 종류입니다. 와이어 · 저장 값이라 순서를 바꾸지 않는다. */
    enum class LedgerHolderKind : uint8
    {
        Account = 0, ///< 플레이어 계정 — 잔액이 음수가 되지 않는다(환불 회수 `_bAllowDebt` 만 예외 — 빚)
        Escrow,      ///< 맡김(경매 · 우편 첨부) — 음수가 되지 않는다. 도메인 + 토큰으로 이름 짓는다
        Mint,        ///< 발행 — 잔액 레코드가 없다(분개의 합이 발행량)
        Sink,        ///< 소각 — 잔액 레코드가 없다
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 원장 보유자 하나입니다. 키는 `acct/<16 진 16>` · `esc/<도메인>/<토큰>` · `sys/mint` · `sys/sink` 입니다. */
    struct SW_GF_API LedgerHolder
    {
        string           _escrowDomain{}; ///< Escrow 만 — `[0-9a-z_]`, 16 자 이하("mail" · "auction")
        string           _escrowToken{};  ///< Escrow 만 — `[0-9a-z_.-]`, 64 자 이하
        uint64           _accountId{ 0 }; ///< Account 만
        LedgerHolderKind _kind{ LedgerHolderKind::Mint };

        static LedgerHolder makeAccount( uint64 accountId );
        static LedgerHolder makeEscrow( string_view domain, string_view token );
        static LedgerHolder makeMint();
        static LedgerHolder makeSink();

        bool isSystem() const { return _kind == LedgerHolderKind::Mint || _kind == LedgerHolderKind::Sink; }
        /** @brief 계정 id 0 · 규칙 밖 도메인/토큰 · 모르는 종류면 false 입니다. */
        bool   isValid() const;
        string makeKey() const;
        bool   operator==( const LedgerHolder& other ) const;
        bool   operator!=( const LedgerHolder& other ) const { return ( *this == other ) == false; }
    };
} // namespace sw

namespace sw
{
    /** @brief 이동 한 다리 — @p _from 에서 @p _to 로 @p _assetId 를 @p _amount(1 이상)만큼입니다. */
    struct LedgerPosting
    {
        LedgerHolder _from{};
        LedgerHolder _to{};
        string       _assetId{};
        int64        _amount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 잔액 하나입니다. 계정 잔액은 환불 회수로 음수(빚)일 수 있다. */
    struct LedgerBalance
    {
        string _assetId{};
        int64  _amount{ 0 };
        uint64 _version{ 0 }; ///< 레코드 판 — 0 이면 레코드가 없다(잔액 0)

        bool isDebt() const { return _amount < 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 이동을 요청한 쪽입니다(감사 · 내역 표시). 저장 값이라 순서를 바꾸지 않는다. */
    enum class LedgerActorKind : uint8
    {
        System = 0, ///< 서버 로직(보상 · 만료 · 예약 작업)
        Player,     ///< 플레이어 요청(구매 · 거래 · 수령)
        Admin,      ///< GM 도구
        Count
    };

    /** @brief 원장 호출의 결과입니다. */
    enum class LedgerResult : uint8
    {
        Ok = 0,            ///< 적용했다 — 또는 같은 분개 키로 이미 적용돼 있었다(`_bReplayed`)
        InsufficientFunds, ///< 계정 · 맡김 잔액이 모자란다 · 빚이 있는 재화를 쓰려 했다(`_failedPostingIndex`)
        CapExceeded,       ///< 계정 잔액 상한을 넘는다(`_failedPostingIndex`)
        JournalKeyReused,  ///< 같은 분개 키에 다른 내용 — 다시 쓴 멱등 키이거나 이미 쓰인 영수증
        Conflict,          ///< 판 충돌이 재시도 상한을 넘었다 — 부르는 쪽이 같은 키로 다시
        Invalid,           ///< 규칙 위반(다리 수 · 금액 · 같은 보유자끼리 · 시스템끼리 · 글자 규칙 · 쓰기 상한)
        Unavailable        ///< 저장소 — 적용됐는지 모른다(같은 키로 다시 하면 가려진다)
    };

    SW_GF_API const utf8* toString( LedgerResult result );
} // namespace sw

namespace sw
{
    /** @brief 자산마다 계정 잔액 상한을 알려 주는 쪽입니다(화폐 카탈로그가 구현). */
    class SW_GF_API ILedgerPolicy
    {
    public:
        ILedgerPolicy()          = default;
        virtual ~ILedgerPolicy() = default;

        ILedgerPolicy( const ILedgerPolicy& )            = default;
        ILedgerPolicy& operator=( const ILedgerPolicy& ) = default;

        /** @brief @p assetId 의 계정 잔액 상한입니다(0 이하 = 상한 없음 — 그래도 `kMaxBalance`). 저장소 스레드에서 불린다 — 읽기만 한다. */
        virtual int64 getBalanceCap( string_view assetId ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 원장 상한입니다. */
    struct LedgerConstant
    {
        static constexpr int32 kMaxPostingCount   = 16;                 ///< 한 이동의 다리 수
        static constexpr int64 kMaxAmount         = 1000000000000000ll; ///< 다리 하나 · 잔액 하나(10^15 — 다리 16 개 합이 int64 안)
        static constexpr int64 kMaxBalance        = kMaxAmount;
        static constexpr int32 kMaxAssetIdSize    = 48;
        static constexpr int32 kMaxReasonSize     = 32;
        static constexpr int32 kMaxMemoSize       = 256;
        static constexpr int32 kMaxJournalKeySize = 200;
        static constexpr int32 kMaxTokenBytes     = 96; ///< `makeFromToken` 이 16 진으로 펴는 토큰 바이트
        static constexpr int32 kMaxRetryCount     = 8;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 이동 요청 하나 — 다리 여럿이 모두 적용되거나 아무것도 적용되지 않습니다.
     * @details `_journalKey` 가 멱등 키다. 같은 키로 다시 오면 다시 적용하지 않고 처음 결과를 돌려준다(다리 · 사유가 다르면 `JournalKeyReused`).
     *          `_pPolicy` 는 빌려 쓴다 — 일(work)이 끝날 때까지 살아야 한다.
     *          `_bAllowDebt` 는 환불 회수(이미 써 버린 재화를 거둠)만 켠다 — 보낸 계정 잔액이 음수(빚)가 되어도 적용한다. 빚이 있는 동안 그 재화를
     *          쓰는 이동은 모두 `InsufficientFunds` 이고(잔액이 더 내려가므로), 받는 이동이 빚을 먼저 갚는다. 맡김은 빚을 지지 않는다.
     */
    struct LedgerTransferRequest
    {
        vector<LedgerPosting> _listPosting{};
        string                _journalKey{};
        string                _reason{}; ///< 사유 코드 `[0-9a-z_.]`("shop.buy" · "trade.settle" · "mail.claim" · "admin.grant" · "refund.revoke")
        string                _memo{};   ///< 자유 글(GM 메모 · 티켓 번호), UTF-8
        const ILedgerPolicy*  _pPolicy{ nullptr };
        int64                 _timeMs{ 0 }; ///< 서버 벽시계(유닉스 밀리초)
        uint64                _actorId{ 0 };
        LedgerActorKind       _actorKind{ LedgerActorKind::System };
        uint8                 _bAllowDebt{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 이동 결과 — 성공이면 관여한 계정 · 맡김 보유자의 이동 뒤 잔액입니다. */
    struct LedgerTransferOutcome
    {
        struct HolderBalance
        {
            LedgerHolder  _holder{};
            LedgerBalance _balance{};
        };

        vector<HolderBalance> _listHolderBalance{};
        string                _journalKey{};
        int64                 _timeMs{ 0 };
        int32                 _failedPostingIndex{ -1 };
        LedgerResult          _result{ LedgerResult::Invalid };
        uint8                 _bReplayed{ SW_FALSE };

        bool isApplied() const { return _result == LedgerResult::Ok; }
    };
} // namespace sw

namespace sw
{
    /** @brief 분개 한 건입니다. */
    struct LedgerJournalEntry
    {
        vector<LedgerPosting> _listPosting{};
        string                _journalKey{};
        string                _reason{};
        string                _memo{};
        int64                 _timeMs{ 0 };
        uint64                _actorId{ 0 };
        uint64                _contentHash{ 0 }; ///< 다리 · 사유 · 빚 허용의 해시 — 같은 키 다른 내용 판정
        LedgerActorKind       _actorKind{ LedgerActorKind::System };
    };
} // namespace sw

namespace sw
{
    /** @brief 분개 키 만들기 — `<범위>/<꼬리>`. 범위는 `[0-9a-z_.]` 32 자 이하입니다. */
    struct SW_GF_API LedgerJournalKey
    {
        /** @brief 클라이언트 멱등 키(128 비트)로 — `<범위>/<16 진 32>`. */
        static string makeFromIdempotency( string_view scope, uint64 keyHigh, uint64 keyLow );
        /** @brief 바깥 토큰(영수증 거래 id · 우편 키 · 거래 id)으로 — `<범위>/<토큰 바이트의 16 진>`. 범위가 틀리거나 토큰이 비거나 `kMaxTokenBytes` 를 넘으면 false 입니다. */
        [[nodiscard]] static bool makeFromToken( string_view scope, string_view token, string& outKey );
        static string             makeAccountScope( uint64 accountId );    ///< `acct.<16 진 16>`
        static string             makeAdminScope( uint64 adminAccountId ); ///< `gm.<16 진 16>`
    };
} // namespace sw

namespace sw
{
    /** @brief 원장 글자 규칙 · 보유자 직렬화 — 원장 · 우편 · 키트가 같이 씁니다. */
    struct SW_GF_API LedgerUtil
    {
        static bool isValidAssetId( string_view assetId );       ///< `[0-9a-z_.-]`, 1..48
        static bool isValidReasonCode( string_view reason );     ///< `[0-9a-z_.]`, 1..32
        static bool isValidEscrowDomain( string_view domain );   ///< `[0-9a-z_]`, 1..16
        static bool isValidEscrowToken( string_view token );     ///< `[0-9a-z_.-]`, 1..64
        static bool isValidJournalKey( string_view journalKey ); ///< 저장소 키 규칙, 1..200, `/` 하나 이상

        static void               writeHolder( BitWriter& outWriter, const LedgerHolder& holder );
        [[nodiscard]] static bool readHolder( BitReader& reader, LedgerHolder& outHolder );
        static void               writePosting( BitWriter& outWriter, const LedgerPosting& posting );
        [[nodiscard]] static bool readPosting( BitReader& reader, LedgerPosting& outPosting );
    };
} // namespace sw
