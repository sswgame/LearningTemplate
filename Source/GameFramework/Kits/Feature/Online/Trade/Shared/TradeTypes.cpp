#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Trade/Shared/TradeTypes.h"

#include "Core/Network/BitStream.h"

namespace sw
{
    const utf8* toString( TradeResult result )
    {
        switch ( result )
        {
            case TradeResult::Ok:
                return "Ok";
            case TradeResult::NotFound:
                return "NotFound";
            case TradeResult::NotParty:
                return "NotParty";
            case TradeResult::AlreadyTrading:
                return "AlreadyTrading";
            case TradeResult::PeerOffline:
                return "PeerOffline";
            case TradeResult::PeerBusy:
                return "PeerBusy";
            case TradeResult::WrongState:
                return "WrongState";
            case TradeResult::StaleOffer:
                return "StaleOffer";
            case TradeResult::NotTradable:
                return "NotTradable";
            case TradeResult::TooManyLegs:
                return "TooManyLegs";
            case TradeResult::InsufficientFunds:
                return "InsufficientFunds";
            case TradeResult::CapExceeded:
                return "CapExceeded";
            case TradeResult::Unavailable:
                return "Unavailable";
            case TradeResult::Invalid:
                return "Invalid";
        }
        return "Unknown";
    }

    int32 TradeSnapshot::findSideIndex( AccountId accountId ) const
    {
        if ( accountId == kInvalidAccountId )
            return -1;
        if ( _arrSide[0]._accountId == accountId )
            return 0;
        if ( _arrSide[1]._accountId == accountId )
            return 1;
        return -1;
    }

    void TradeWire::writeLegs( BitWriter& outWriter, const vector<TradeLeg>& listLeg )
    {
        outWriter.writeVarUint( listLeg.size() );
        for ( const TradeLeg& leg : listLeg )
        {
            outWriter.writeBlob( reinterpret_cast<const uint8*>( leg._assetId.data() ), static_cast<int32>( leg._assetId.size() ) );
            outWriter.writeVarInt( leg._amount );
        }
    }

    bool TradeWire::readLegs( BitReader& reader, vector<TradeLeg>& outListLeg )
    {
        outListLeg.clear();
        const uint64 legCount = reader.readVarUint();
        if ( legCount > static_cast<uint64>( TradeConstant::kMaxLegsPerSide * 2 ) )
            return false;
        for ( uint64 index = 0; index < legCount; ++index )
        {
            vector<uint8> assetBytes;
            if ( reader.readBlob( assetBytes, TradeConstant::kMaxAssetIdSize ) == false )
                return false;
            TradeLeg& leg = outListLeg.emplace_back();
            leg._assetId.assign( reinterpret_cast<const utf8*>( assetBytes.data() ), assetBytes.size() );
            leg._amount = reader.readVarInt();
        }
        return reader.hasOverflowed() == false;
    }

    void TradeWire::writeSnapshot( BitWriter& outWriter, const TradeSnapshot& snapshot )
    {
        outWriter.writeVarUint( snapshot._tradeId );
        outWriter.writeVarInt( snapshot._createdMs );
        outWriter.writeVarInt( snapshot._updatedMs );
        outWriter.writeVarUint( static_cast<uint64>( snapshot._state ) );
        outWriter.writeVarUint( static_cast<uint64>( snapshot._closeReason ) );
        for ( const TradeSide& side : snapshot._arrSide )
        {
            outWriter.writeVarUint( side._accountId );
            outWriter.writeVarUint( side._offerRevision );
            outWriter.writeBool( side._bLocked == SW_TRUE );
            outWriter.writeBool( side._bConfirmed == SW_TRUE );
            writeLegs( outWriter, side._listLeg );
        }
    }

    bool TradeWire::readSnapshot( BitReader& reader, TradeSnapshot& outSnapshot )
    {
        outSnapshot              = TradeSnapshot{};
        outSnapshot._tradeId     = reader.readVarUint();
        outSnapshot._createdMs   = reader.readVarInt();
        outSnapshot._updatedMs   = reader.readVarInt();
        const uint64 state       = reader.readVarUint();
        const uint64 closeReason = reader.readVarUint();
        if ( state > static_cast<uint64>( TradeState::Cancelled ) || closeReason > static_cast<uint64>( TradeCloseReason::CapExceeded ) )
            return false;
        outSnapshot._state       = static_cast<TradeState>( state );
        outSnapshot._closeReason = static_cast<TradeCloseReason>( closeReason );
        for ( TradeSide& side : outSnapshot._arrSide )
        {
            side._accountId     = reader.readVarUint();
            side._offerRevision = static_cast<uint32>( reader.readVarUint() );
            side._bLocked       = reader.readBool() ? SW_TRUE : SW_FALSE;
            side._bConfirmed    = reader.readBool() ? SW_TRUE : SW_FALSE;
            if ( readLegs( reader, side._listLeg ) == false || static_cast<int32>( side._listLeg.size() ) > TradeConstant::kMaxLegsPerSide )
                return false;
        }
        return reader.hasOverflowed() == false;
    }
} // namespace sw
