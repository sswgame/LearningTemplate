/**
 * @file TradeStateMachine.h
 * @brief 거래 상태 기계 — 순수 함수(저장 · 원장을 모른다). 거래 서비스의 저장 일이 레코드를 읽어 이것으로 새 레코드를 만들고 판 조건으로 쓴다.
 * @details 상태 표: Invited ─(상대 수락)→ Open ─(제시 바꿈: 그쪽 판 +1, 양쪽 잠금 · 확정 풀림)→ Open ─(잠금 둘 · 확정 둘)→ 정산(서비스).
 *          어느 때나 취소 · 거절 → Cancelled. 닫힌 거래 · 허용 안 된 칸은 `WrongState`. 같은 확정 두 번은 Ok(멱등).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Trade/TradeTypes.h"

namespace sw
{
    /**
     * @class ITradePolicy
     * @brief 무엇을 거래할 수 있나 — 게임이 정한다(귀속 아이템 · 이벤트 재화 막기). 저장소 스레드에서도 불린다(읽기만).
     */
    class SW_GF_API ITradePolicy
    {
    public:
        ITradePolicy()          = default;
        virtual ~ITradePolicy() = default;

        ITradePolicy( const ITradePolicy& )            = default;
        ITradePolicy& operator=( const ITradePolicy& ) = default;

        virtual bool  isTradable( string_view assetId ) const = 0;
        virtual int32 getMaxLegsPerSide() const               = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 기본 정책 — `item.` · `cur.` 접두만, 한쪽 8 다리. */
    class SW_GF_API DefaultTradePolicy final : public ITradePolicy
    {
    public:
        bool  isTradable( string_view assetId ) const override;
        int32 getMaxLegsPerSide() const override { return TradeConstant::kMaxLegsPerSide; }
    };
} // namespace sw

namespace sw
{
    /** @brief 거래 명령 종류입니다. */
    enum class TradeCommandKind : uint8
    {
        Accept = 0, ///< 초대받은 쪽
        Decline,    ///< 초대받은 쪽
        SetOffer,
        Lock,
        Confirm,
        Cancel
    };

    /** @brief 명령 하나입니다. */
    struct TradeCommand
    {
        vector<TradeLeg> _listLeg{}; ///< SetOffer
        AccountId        _actorId{ kInvalidAccountId };
        uint32           _seenOwnRevision{ 0 };                         ///< Confirm — 확정하는 쪽이 본 자기 제시 판
        uint32           _seenPeerRevision{ 0 };                        ///< Confirm — 본 상대 제시 판
        TradeCloseReason _reason{ TradeCloseReason::CancelledByParty }; ///< Cancel
        TradeCommandKind _kind{ TradeCommandKind::Cancel };
    };
} // namespace sw

namespace sw
{
    /** @brief 거래 상태 기계입니다. */
    struct SW_GF_API TradeStateMachine
    {
        /** @brief @p command 를 @p inoutTrade 에 적용합니다. Ok 가 아니면 @p inoutTrade 는 그대로입니다. @p nowMs 는 `_updatedMs` 에. */
        static TradeResult apply( TradeSnapshot& inoutTrade, const TradeCommand& command, const ITradePolicy& policy, int64 nowMs );
        /** @brief 제시가 규칙(다리 수 · 자산 id · 수량 · 거래 가능 · 같은 자산 두 번)을 지키는가입니다. */
        static TradeResult validateOffer( const vector<TradeLeg>& listLeg, const ITradePolicy& policy );
        /** @brief 닫습니다(취소 · 시한 · 떠남 · 재시작 — 아무것도 안 움직인다). 이미 닫혔으면 그대로. */
        static void close( TradeSnapshot& inoutTrade, TradeState state, TradeCloseReason reason, int64 nowMs );
    };
} // namespace sw
