/**
 * @file TradeStateMachine.h
 * @brief 거래 상태 기계입니다. 순수 함수이며 저장소와 원장은 모릅니다. 거래 서비스의 저장 작업이 레코드를 읽어 이 상태 기계로 새 레코드를 만들고 버전 조건으로 씁니다.
 * @details 상태 전이는 다음과 같다. Invited 에서 상대가 수락하면 Open 이 된다. Open 에서 제시를 바꾸면 그쪽의 제시 버전이 1 올라가고 양쪽의 잠금과 확정이 풀린다. 양쪽이 잠그고 양쪽이 확정하면 정산한다(서비스가 처리).
 *          취소와 거절은 언제든 Cancelled 로 이어진다. 닫힌 거래나 허용되지 않는 상태에서의 명령은 `WrongState` 이다. 같은 확정을 두 번 하면 Ok 이며 멱등이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Trade/Runtime/TradeTypes.h"

namespace sw
{
    /**
     * @class ITradePolicy
     * @brief 무엇을 거래할 수 있는지 정하는 정책입니다. 귀속 아이템이나 이벤트 재화를 막는 일은 게임이 정합니다. 저장소 스레드에서도 불리므로 읽기만 해야 합니다.
     */
    class ITradePolicy
    {
    public:
        ITradePolicy()          = default;
        virtual ~ITradePolicy() = default;

        ITradePolicy( const ITradePolicy& )            = default;
        ITradePolicy& operator=( const ITradePolicy& ) = default;

        virtual bool  isTradable( string_view assetID ) const = 0;
        virtual int32 getMaxLegsPerSide() const               = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 기본 정책입니다. `item.` 과 `cur.` 접두어만 허용하고 한쪽에 8항목까지 둡니다. */
    class SW_KIT_SERVER_API DefaultTradePolicy final : public ITradePolicy
    {
    public:
        bool  isTradable( string_view assetID ) const override;
        int32 getMaxLegsPerSide() const override { return TradeConstant::kMaxLegsPerSide; }
    };
} // namespace sw

namespace sw
{
    /** @brief 거래 명령 종류입니다. */
    enum class TradeCommandKind : uint8
    {
        Accept = 0, ///< 초대받은 쪽이 낸다
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
        AccountID        _actorID{ kInvalidAccountID };
        uint32           _seenOwnRevision{ 0 };                         ///< Confirm 에서 확정하는 쪽이 본 자기 제시의 버전이다
        uint32           _seenPeerRevision{ 0 };                        ///< Confirm 에서 본 상대 제시의 버전이다
        TradeCloseReason _reason{ TradeCloseReason::CancelledByParty }; ///< Cancel
        TradeCommandKind _kind{ TradeCommandKind::Cancel };
    };
} // namespace sw

namespace sw
{
    /** @brief 거래 상태 기계입니다. */
    struct SW_KIT_SERVER_API TradeStateMachine
    {
        /** @brief @p command 를 @p inoutTrade 에 적용합니다. Ok 가 아니면 @p inoutTrade 는 그대로입니다. @p nowMs 는 `_updatedMs` 에 기록합니다. */
        static TradeResult apply( TradeSnapshot& inoutTrade, const TradeCommand& command, const ITradePolicy& policy, int64 nowMs );
        /** @brief 제시가 규칙을 지키면 true 입니다. 규칙은 항목 수, 자산 id, 수량, 거래 가능 여부, 같은 자산의 중복 금지입니다. */
        static TradeResult validateOffer( const vector<TradeLeg>& listLeg, const ITradePolicy& policy );
        /** @brief 거래를 닫습니다. 취소, 시한, 떠남, 재시작이 해당하며 아무것도 움직이지 않습니다. 이미 닫혔으면 그대로 둡니다. */
        static void close( TradeSnapshot& inoutTrade, TradeState state, TradeCloseReason reason, int64 nowMs );
    };
} // namespace sw
