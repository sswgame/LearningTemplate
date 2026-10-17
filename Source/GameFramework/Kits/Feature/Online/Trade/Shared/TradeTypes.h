/**
 * @file TradeTypes.h
 * @brief 거래 키트의 와이어 타입입니다. 클라이언트와 서버가 공유하며 항목, 상태, 닫힌 이유, 결과, 한쪽, 스냅샷을 정의합니다.
 * @details 플레이어 간 거래는 양쪽 제시, 잠금, 양쪽 확정 순서로 진행하며, 정산은 에스크로 없이 원장 이동 하나로 처리한다(분개 키는 거래 id). 사용자가 정한 방식이다.
 *          제시를 바꾸면 양쪽의 잠금과 확정이 풀리고 제시 버전이 올라간다. 확정은 "내가 본 (내 버전, 상대 버전)" 을 싣는다. 바꿔치기 사기를 막으려는 것이며 WoW 와 언리얼 예제의 거래창과 같은 규칙이다.
 *          와이어에는 고정 배치형(B)으로, 저장소의 거래 레코드에는 자기 기술형(A, `TradeProtocol::encodeSession`)으로 실린다. 배치는 아래 `PROPERTY` 선언 순서와 키가 정한다.
 *          자산 id 상한(48)은 `LedgerConstant::kMaxAssetIDSize` 와 같은 값이다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
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
    REFLECT()
    struct TradeLeg
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 48 )
        string _assetID{};
        PROPERTY()
        int64 _amount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 거래 상태입니다. 와이어와 저장에 쓰는 값입니다. */
    ENUM()
    enum class TradeState : uint8
    {
        Invited = 0, ///< 신청했다. 상대가 받으면 Open 이 된다
        Open,        ///< 제시, 잠금, 확정을 진행 중이다
        Settled,     ///< 정산했다. 분개 하나로 처리한다
        Failed,      ///< 정산할 때 잔액이 모자라거나 상한을 넘었다. 아무것도 움직이지 않았다
        Cancelled    ///< 취소, 거절, 떠남, 시한, 재시작으로 닫혔다. 아무것도 움직이지 않았다
    };

    /** @brief 거래가 닫힌 이유입니다. 와이어와 저장에 쓰는 값입니다. */
    ENUM()
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
    ENUM()
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
        Invalid,
        UpdateRequired ///< 온라인 스키마 해시가 다르다(`kVersionMismatch`). 클라이언트만 만들고 서버는 보내지 않는다. 다시 접속하지 않으므로 재시도하지 않는다
    };
} // namespace sw

namespace sw
{
    /** @brief 거래의 한쪽입니다. */
    REFLECT()
    struct TradeSide
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 8 )
        vector<TradeLeg> _listLeg{}; ///< 상한은 `TradeConstant::kMaxLegsPerSide` 와 같다
        PROPERTY()
        AccountID _accountID{ kInvalidAccountID };
        PROPERTY()
        uint32 _offerRevision{ 0 };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bLocked{ SW_FALSE };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bConfirmed{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 거래 하나의 스냅샷입니다. 두 당사자에게 알리는 값입니다. */
    REFLECT()
    struct SW_KIT_API TradeSnapshot
    {
        REFLECT_BODY();
        PROPERTY()
        TradeSide _arrSide[2]{}; ///< 0번이 신청한 쪽이다
        PROPERTY()
        uint64 _tradeID{ 0 };
        PROPERTY()
        int64 _createdMs{ 0 };
        PROPERTY()
        int64 _updatedMs{ 0 };
        PROPERTY()
        TradeState _state{ TradeState::Invited };
        PROPERTY()
        TradeCloseReason _closeReason{ TradeCloseReason::None };

        /** @brief 계정의 쪽 번호(0 또는 1)를 반환합니다. 당사자가 아니면 -1 입니다. */
        int32 findSideIndex( AccountID accountID ) const;
        bool  isClosed() const { return _state == TradeState::Settled || _state == TradeState::Failed || _state == TradeState::Cancelled; }
        bool  isBothConfirmed() const { return _arrSide[0]._bConfirmed == SW_TRUE && _arrSide[1]._bConfirmed == SW_TRUE; }
    };
} // namespace sw
