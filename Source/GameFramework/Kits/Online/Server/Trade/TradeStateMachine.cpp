#include "pch.h"

#include "GameFramework/Kits/Online/Server/Trade/TradeStateMachine.h"

#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"

namespace sw
{
    bool DefaultTradePolicy::isTradable( string_view assetId ) const { return assetId.substr( 0, 5 ) == "item." || assetId.substr( 0, 4 ) == "cur."; }

    TradeResult TradeStateMachine::validateOffer( const vector<TradeLeg>& listLeg, const ITradePolicy& policy )
    {
        if ( static_cast<int32>( listLeg.size() ) > policy.getMaxLegsPerSide() || static_cast<int32>( listLeg.size() ) > TradeConstant::kMaxLegsPerSide )
            return TradeResult::TooManyLegs;
        for ( size_t legIndex = 0; legIndex < listLeg.size(); ++legIndex )
        {
            const TradeLeg& leg       = listLeg[legIndex];
            const bool      bAmountOk = 1 <= leg._amount && leg._amount <= LedgerConstant::kMaxAmount;
            if ( LedgerUtil::isValidAssetId( leg._assetId ) == false || bAmountOk == false )
                return TradeResult::Invalid;
            if ( policy.isTradable( leg._assetId ) == false )
                return TradeResult::NotTradable;
            for ( size_t otherIndex = 0; otherIndex < legIndex; ++otherIndex )
            {
                if ( listLeg[otherIndex]._assetId == leg._assetId )
                    return TradeResult::Invalid; // 같은 자산은 한 다리로
            }
        }
        return TradeResult::Ok;
    }

    void TradeStateMachine::close( TradeSnapshot& inoutTrade, TradeState state, TradeCloseReason reason, int64 nowMs )
    {
        if ( inoutTrade.isClosed() )
            return;
        inoutTrade._state       = state;
        inoutTrade._closeReason = reason;
        inoutTrade._updatedMs   = nowMs;
    }

    TradeResult TradeStateMachine::apply( TradeSnapshot& inoutTrade, const TradeCommand& command, const ITradePolicy& policy, int64 nowMs )
    {
        const int32 sideIndex = inoutTrade.findSideIndex( command._actorId );
        if ( sideIndex < 0 )
            return TradeResult::NotParty;
        if ( inoutTrade.isClosed() )
            return TradeResult::WrongState;
        TradeSide& own  = inoutTrade._arrSide[sideIndex];
        TradeSide& peer = inoutTrade._arrSide[1 - sideIndex];

        if ( command._kind == TradeCommandKind::Cancel )
        {
            close( inoutTrade, TradeState::Cancelled, command._reason, nowMs );
            return TradeResult::Ok;
        }
        if ( inoutTrade._state == TradeState::Invited )
        {
            const bool bInvitee = sideIndex == 1;
            if ( bInvitee == false )
                return TradeResult::WrongState;
            if ( command._kind == TradeCommandKind::Accept )
            {
                inoutTrade._state     = TradeState::Open;
                inoutTrade._updatedMs = nowMs;
                return TradeResult::Ok;
            }
            if ( command._kind == TradeCommandKind::Decline )
            {
                close( inoutTrade, TradeState::Cancelled, TradeCloseReason::Declined, nowMs );
                return TradeResult::Ok;
            }
            return TradeResult::WrongState;
        }

        // Open
        switch ( command._kind )
        {
            case TradeCommandKind::SetOffer:
            {
                const TradeResult validated = validateOffer( command._listLeg, policy );
                if ( validated != TradeResult::Ok )
                    return validated;
                own._listLeg = command._listLeg;
                ++own._offerRevision;
                for ( TradeSide& side : inoutTrade._arrSide )
                {
                    side._bLocked    = SW_FALSE; // 바뀐 제시는 양쪽이 다시 봐야 한다
                    side._bConfirmed = SW_FALSE;
                }
                inoutTrade._updatedMs = nowMs;
                return TradeResult::Ok;
            }
            case TradeCommandKind::Lock:
            {
                own._bLocked          = SW_TRUE;
                inoutTrade._updatedMs = nowMs;
                return TradeResult::Ok;
            }
            case TradeCommandKind::Confirm:
            {
                const bool bBothLocked = own._bLocked == SW_TRUE && peer._bLocked == SW_TRUE;
                if ( bBothLocked == false )
                    return TradeResult::WrongState;
                if ( command._seenOwnRevision != own._offerRevision || command._seenPeerRevision != peer._offerRevision )
                    return TradeResult::StaleOffer;
                own._bConfirmed       = SW_TRUE; // 이미 확정이면 그대로 Ok(멱등 — 재시도)
                inoutTrade._updatedMs = nowMs;
                return TradeResult::Ok;
            }
            case TradeCommandKind::Accept:
            case TradeCommandKind::Decline:
            case TradeCommandKind::Cancel:
            {
                return TradeResult::WrongState;
            }
        }
        return TradeResult::WrongState;
    }
} // namespace sw
