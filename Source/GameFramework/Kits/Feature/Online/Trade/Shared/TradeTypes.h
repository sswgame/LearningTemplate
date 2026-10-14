/**
 * @file TradeTypes.h
 * @brief 거래 키트의 와이어 타입입니다. 클라이언트와 서버가 공유하며 항목, 상태, 닫힌 이유, 결과, 한쪽, 스냅샷, 코덱을 정의합니다.
 * @details 플레이어 간 거래는 양쪽 제시, 잠금, 양쪽 확정 순서로 진행하며, 정산은 에스크로 없이 원장 이동 하나로 처리한다(분개 키는 거래 id). 사용자가 정한 방식이다.
 *          제시를 바꾸면 양쪽의 잠금과 확정이 풀리고 제시 버전이 올라간다. 확정은 "내가 본 (내 버전, 상대 버전)" 을 싣는다. 바꿔치기 사기를 막으려는 것이며 WoW 와 언리얼 예제의 거래창과 같은 규칙이다.
 *          값은 와이어 형식이므로 바꾸면 `TradeProtocol::kVersion` 을 올려야 한다. 자산 id 상한은 `LedgerConstant::kMaxAssetIDSize` 를 따르므로 그 값을 바꿀 때도 버전을 올린다.
 */
#pragma once
#include "Core/Common/Defines.h"
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

    /** @brief 거래 상한 값들입니다. */
    struct TradeConstant
    {
        static constexpr int32 kMaxLegsPerSide  = 8; ///< 원장 이동 항목은 16개이며 양쪽이 8개씩이다
        static constexpr int64 kInviteTimeoutMs = 30 * constant::kMillisecondsPerSecond;
        static constexpr int64 kIdleTimeoutMs   = 5 * 60 * constant::kMillisecondsPerSecond;
    };
} // namespace sw

namespace sw
{
    /** @brief 거래 항목 하나입니다. 자산 id(원장 규칙에 따라 `item.*` 이나 `cur.*`)와 수량(1 이상)을 가집니다. */
    struct TradeLeg
    {
        string _assetID{};
        int64  _amount{ 0 };
    };

    /** @brief 거래 상태입니다. 와이어와 저장에 쓰는 값입니다. */
    enum class TradeState : uint8
    {
        Invited = 0, ///< 신청했다. 상대가 받으면 Open 이 된다
        Open,        ///< 제시, 잠금, 확정을 진행 중이다
        Settled,     ///< 정산했다. 분개 하나로 처리한다
        Failed,      ///< 정산할 때 잔액이 모자라거나 상한을 넘었다. 아무것도 움직이지 않았다
        Cancelled    ///< 취소, 거절, 떠남, 시한, 재시작으로 닫혔다. 아무것도 움직이지 않았다
    };

    /** @brief 거래가 닫힌 이유입니다. 와이어와 저장에 쓰는 값입니다. */
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

    /** @brief 거래 호출의 결과입니다. 와이어 값입니다. */
    enum class TradeResult : uint8
    {
        Ok = 0,
        NotFound,
        NotParty,
        AlreadyTrading, ///< 신청한 쪽이 이미 다른 거래 중이다
        PeerOffline,
        PeerBusy,    ///< 상대가 이미 다른 거래 중이다
        WrongState,  ///< 그 상태에서 할 수 없는 명령이다
        StaleOffer,  ///< 확정이 본 버전이 지금 버전과 다르다. 그사이 제시가 바뀌었다
        NotTradable, ///< 거래할 수 없는 자산이다
        TooManyLegs,
        InsufficientFunds,
        CapExceeded,
        Unavailable, ///< 저장소를 쓸 수 없다. 같은 멱등 키로 다시 시도한다
        Invalid
    };

    SW_KIT_API const utf8* toString( TradeResult result );
} // namespace sw

namespace sw
{
    /** @brief 거래의 한쪽입니다. */
    struct TradeSide
    {
        vector<TradeLeg> _listLeg{};
        AccountID        _accountID{ kInvalidAccountID };
        uint32           _offerRevision{ 0 };
        uint8            _bLocked{ SW_FALSE };
        uint8            _bConfirmed{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 거래 하나의 스냅샷입니다. 두 당사자에게 알리는 값입니다. */
    struct SW_KIT_API TradeSnapshot
    {
        TradeSide        _arrSide[2]{}; ///< 0번이 신청한 쪽이다
        uint64           _tradeID{ 0 };
        int64            _createdMs{ 0 };
        int64            _updatedMs{ 0 };
        TradeState       _state{ TradeState::Invited };
        TradeCloseReason _closeReason{ TradeCloseReason::None };

        /** @brief 계정의 쪽 번호(0 또는 1)를 반환합니다. 당사자가 아니면 -1 입니다. */
        int32 findSideIndex( AccountID accountID ) const;
        bool  isClosed() const { return _state == TradeState::Settled || _state == TradeState::Failed || _state == TradeState::Cancelled; }
        bool  isBothConfirmed() const { return _arrSide[0]._bConfirmed == SW_TRUE && _arrSide[1]._bConfirmed == SW_TRUE; }
    };
} // namespace sw

namespace sw
{
    /** @brief 거래 와이어 코덱입니다. 읽을 때 형식이나 상한이 틀리면 false 를 반환합니다. */
    struct SW_KIT_API TradeWire
    {
        static void               writeLegs( BitWriter& outWriter, const vector<TradeLeg>& listLeg );
        [[nodiscard]] static bool readLegs( BitReader& reader, vector<TradeLeg>& outListLeg );
        static void               writeSnapshot( BitWriter& outWriter, const TradeSnapshot& snapshot );
        [[nodiscard]] static bool readSnapshot( BitReader& reader, TradeSnapshot& outSnapshot );
    };
} // namespace sw
