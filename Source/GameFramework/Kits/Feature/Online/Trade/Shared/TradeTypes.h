/**
 * @file TradeTypes.h
 * @brief 거래 키트의 와이어 타입(클라이언트 · 서버 공유) — 다리 · 상태 · 닫힌 까닭 · 결과 · 한쪽 · 스냅숏과 코덱입니다.
 * @details 플레이어 간 거래는 양쪽 제시 → 잠금 → 양쪽 확정이고, 정산은 맡김 없이 원장 이동 하나(분개 키 = 거래 id)다(사용자 결정).
 *          제시를 바꾸면 양쪽 잠금 · 확정이 풀리고 판이 오른다 — 확정은 "내가 본 (내 판, 상대 판)" 을 싣는다(바꿔치기 사기 막기, WoW · 언리얼 예제 거래창과 같은 규칙).
 *          값은 와이어 형식이다 — 바꾸면 `TradeProtocol::kVersion` 을 올린다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 거래 상한입니다. */
    struct TradeConstant
    {
        static constexpr int32 kMaxLegsPerSide  = 8; ///< 원장 이동 다리 16 = 양쪽 8
        static constexpr int64 kInviteTimeoutMs = 30 * 1000;
        static constexpr int64 kIdleTimeoutMs   = 5 * 60 * 1000;
        static constexpr int32 kMaxAssetIdSize  = 48;
    };
} // namespace sw

namespace sw
{
    /** @brief 다리 하나 — 자산 id(원장 규칙 — `item.*` · `cur.*`) · 수량(1 이상)입니다. */
    struct TradeLeg
    {
        string _assetId{};
        int64  _amount{ 0 };
    };

    /** @brief 거래 상태입니다. 와이어 · 저장 값이다. */
    enum class TradeState : uint8
    {
        Invited = 0, ///< 신청했다 — 상대가 받으면 Open
        Open,        ///< 제시 · 잠금 · 확정 중
        Settled,     ///< 정산했다(분개 하나)
        Failed,      ///< 정산 때 모자랐다 · 상한 — 아무것도 안 움직였다
        Cancelled    ///< 취소 · 거절 · 떠남 · 시한 · 재시작 — 아무것도 안 움직였다
    };

    /** @brief 거래가 닫힌 까닭입니다. 와이어 · 저장 값이다. */
    enum class TradeCloseReason : uint8
    {
        None = 0,
        Declined,
        CancelledByParty,
        PartyLeft,
        Timeout,
        ServerRestart,
        Sanctioned,
        InsufficientFunds,
        CapExceeded
    };

    /** @brief 거래 호출의 결과입니다. 와이어 값이다. */
    enum class TradeResult : uint8
    {
        Ok = 0,
        NotFound,
        NotParty,
        AlreadyTrading, ///< 신청한 쪽이 이미 다른 거래 중
        PeerOffline,
        PeerBusy,    ///< 상대가 이미 다른 거래 중
        WrongState,  ///< 그 상태에서 할 수 없는 명령
        StaleOffer,  ///< 확정이 본 판이 지금 판과 다르다(그새 제시가 바뀌었다)
        NotTradable, ///< 거래할 수 없는 자산
        TooManyLegs,
        InsufficientFunds,
        CapExceeded,
        Unavailable, ///< 저장소 — 다시(같은 멱등 키로)
        Invalid
    };

    SW_GF_API const utf8* toString( TradeResult result );
} // namespace sw

namespace sw
{
    /** @brief 거래의 한쪽입니다. */
    struct TradeSide
    {
        vector<TradeLeg> _listLeg{};
        AccountId        _accountId{ kInvalidAccountId };
        uint32           _offerRevision{ 0 };
        uint8            _bLocked{ SW_FALSE };
        uint8            _bConfirmed{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 거래 하나의 모습입니다(두 당사자에게 알리는 것). */
    struct SW_GF_API TradeSnapshot
    {
        TradeSide        _arrSide[2]{}; ///< 0 = 신청한 쪽
        uint64           _tradeId{ 0 };
        int64            _createdMs{ 0 };
        int64            _updatedMs{ 0 };
        TradeState       _state{ TradeState::Invited };
        TradeCloseReason _closeReason{ TradeCloseReason::None };

        /** @brief 계정의 쪽 번호(0 · 1)입니다. 당사자가 아니면 −1. */
        int32 findSideIndex( AccountId accountId ) const;
        bool  isClosed() const { return _state == TradeState::Settled || _state == TradeState::Failed || _state == TradeState::Cancelled; }
        bool  isBothConfirmed() const { return _arrSide[0]._bConfirmed == SW_TRUE && _arrSide[1]._bConfirmed == SW_TRUE; }
    };
} // namespace sw

namespace sw
{
    /** @brief 거래 와이어 코덱입니다. 읽기는 형식 · 상한이 틀리면 false 입니다. */
    struct SW_GF_API TradeWire
    {
        static void               writeLegs( BitWriter& outWriter, const vector<TradeLeg>& listLeg );
        [[nodiscard]] static bool readLegs( BitReader& reader, vector<TradeLeg>& outListLeg );
        static void               writeSnapshot( BitWriter& outWriter, const TradeSnapshot& snapshot );
        [[nodiscard]] static bool readSnapshot( BitReader& reader, TradeSnapshot& outSnapshot );
    };
} // namespace sw
