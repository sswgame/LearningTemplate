#include "pch.h"

#include "GameFramework/Kits/Online/Trade/TradeProtocol.h"

#include "Core/Network/BitStream.h"

namespace sw
{
    void TradeReplyWire::writeReply( BitWriter& outWriter, TradeResult result, const TradeSnapshot& snapshot, const vector<TradeBalance>& listBalance )
    {
        outWriter.writeVarUint( static_cast<uint64>( result ) );
        TradeWire::writeSnapshot( outWriter, snapshot );
        outWriter.writeVarUint( listBalance.size() );
        for ( const TradeBalance& balance : listBalance )
        {
            outWriter.writeBlob( reinterpret_cast<const uint8*>( balance._assetId.data() ), static_cast<int32>( balance._assetId.size() ) );
            outWriter.writeVarInt( balance._amount );
        }
    }

    bool TradeReplyWire::readReply( BitReader& reader, TradeResult& outResult, TradeSnapshot& outSnapshot, vector<TradeBalance>& outListBalance )
    {
        outListBalance.clear();
        const uint64 result = reader.readVarUint();
        if ( result > static_cast<uint64>( TradeResult::Invalid ) )
            return false;
        outResult = static_cast<TradeResult>( result );
        if ( TradeWire::readSnapshot( reader, outSnapshot ) == false )
            return false;
        const uint64 balanceCount = reader.readVarUint();
        if ( balanceCount > static_cast<uint64>( TradeConstant::kMaxLegsPerSide * 2 ) )
            return false;
        for ( uint64 index = 0; index < balanceCount; ++index )
        {
            vector<uint8> assetBytes;
            if ( reader.readBlob( assetBytes, TradeConstant::kMaxAssetIdSize ) == false )
                return false;
            TradeBalance& balance = outListBalance.emplace_back();
            balance._assetId.assign( reinterpret_cast<const utf8*>( assetBytes.data() ), assetBytes.size() );
            balance._amount = reader.readVarInt();
        }
        return reader.hasOverflowed() == false;
    }
} // namespace sw
