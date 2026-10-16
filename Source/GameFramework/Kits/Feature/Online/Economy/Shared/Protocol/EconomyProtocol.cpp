#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Shared/Protocol/EconomyProtocol.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Service/OnlineLimit.h"
#include "GameFramework/Base/Online/Service/OnlineReplyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    void EconomyProtocol::writePurchaseRequest( BitWriter& outWriter, const EconomyPurchaseRequest& request )
    {
        ServiceKeyUtil::writeString( outWriter, request._offerID );
        outWriter.writeVarInt( request._count );
    }

    bool EconomyProtocol::readPurchaseRequest( BitReader& reader, EconomyPurchaseRequest& outRequest )
    {
        if ( ServiceKeyUtil::readString( reader, kMaxIDSize, outRequest._offerID ) == false )
            return false;
        const int64 count   = reader.readVarInt();
        outRequest._count   = static_cast<int32>( count );
        const bool bCountOk = 1 <= count && count <= kMaxPurchaseCount;
        return reader.hasOverflowed() == false && bCountOk;
    }

    void EconomyProtocol::writeRedeemRequest( BitWriter& outWriter, const EconomyRedeemRequest& request )
    {
        ServiceKeyUtil::writeString( outWriter, request._storeName );
        ServiceKeyUtil::writeString( outWriter, request._payload );
    }

    bool EconomyProtocol::readRedeemRequest( BitReader& reader, EconomyRedeemRequest& outRequest )
    {
        return ServiceKeyUtil::readString( reader, kMaxIDSize, outRequest._storeName ) && ServiceKeyUtil::readString( reader, kMaxPayloadSize, outRequest._payload );
    }

    void EconomyProtocol::writeHistoryRequest( BitWriter& outWriter, const EconomyHistoryRequest& request )
    {
        ServiceKeyUtil::writeString( outWriter, request._cursor );
        outWriter.writeVarInt( request._maxCount );
    }

    bool EconomyProtocol::readHistoryRequest( BitReader& reader, EconomyHistoryRequest& outRequest )
    {
        if ( ServiceKeyUtil::readString( reader, kMaxCursorSize, outRequest._cursor ) == false )
            return false;
        const int64 maxCount = reader.readVarInt();
        outRequest._maxCount = static_cast<int32>( maxCount );
        const bool bCountOk  = 1 <= maxCount && maxCount <= kMaxHistoryCount;
        return reader.hasOverflowed() == false && bCountOk;
    }

    void EconomyProtocol::writeBalances( BitWriter& outWriter, const vector<LedgerBalance>& listBalance )
    {
        outWriter.writeVarUint( listBalance.size() );
        for ( const LedgerBalance& balance : listBalance )
        {
            ServiceKeyUtil::writeString( outWriter, balance._assetID );
            outWriter.writeVarInt( balance._amount );
        }
    }

    bool EconomyProtocol::readBalances( BitReader& reader, vector<LedgerBalance>& outListBalance )
    {
        const uint64 count = reader.readVarUint();
        if ( count > static_cast<uint64>( OnlineLimit::kMaxBalanceCount ) )
            return false;
        outListBalance.resize( static_cast<size_t>( count ) );
        for ( LedgerBalance& balance : outListBalance )
        {
            if ( ServiceKeyUtil::readString( reader, LedgerConstant::kMaxAssetIDSize, balance._assetID ) == false )
                return false;
            balance._amount = reader.readVarInt();
        }
        return reader.hasOverflowed() == false;
    }

    void EconomyProtocol::writeReply( BitWriter& outWriter, const EconomyReply& reply )
    {
        outWriter.writeVarUint( static_cast<uint64>( reply._result ) );
        outWriter.writeBool( reply._bReplayed == SW_TRUE );
        writeBalances( outWriter, reply._listBalance );
        ServiceKeyUtil::writeString( outWriter, reply._productID );
        ServiceKeyUtil::writeString( outWriter, reply._nextCursor );
        outWriter.writeVarUint( reply._listHistory.size() );
        for ( const EconomyHistoryEntry& entry : reply._listHistory )
        {
            ServiceKeyUtil::writeString( outWriter, entry._reason );
            ServiceKeyUtil::writeString( outWriter, entry._memo );
            outWriter.writeVarInt( entry._timeMs );
            writeBalances( outWriter, entry._listChange );
        }
    }

    bool EconomyProtocol::readReply( BitReader& reader, EconomyReply& outReply )
    {
        if ( OnlineReplyUtil::readEnum( reader, EconomyResult::Count, outReply._result ) == false )
            return false;
        outReply._bReplayed = reader.readBool() ? SW_TRUE : SW_FALSE;
        if ( readBalances( reader, outReply._listBalance ) == false )
            return false;
        const bool bTextOk = ServiceKeyUtil::readString( reader, kMaxIDSize, outReply._productID ) &&
                             ServiceKeyUtil::readString( reader, kMaxCursorSize, outReply._nextCursor );
        if ( bTextOk == false )
            return false;
        const uint64 historyCount = reader.readVarUint();
        if ( historyCount > static_cast<uint64>( kMaxHistoryCount ) )
            return false;
        outReply._listHistory.resize( static_cast<size_t>( historyCount ) );
        for ( EconomyHistoryEntry& entry : outReply._listHistory )
        {
            const bool bEntryTextOk = ServiceKeyUtil::readString( reader, LedgerConstant::kMaxReasonSize, entry._reason ) &&
                                      ServiceKeyUtil::readString( reader, LedgerConstant::kMaxMemoSize, entry._memo );
            if ( bEntryTextOk == false )
                return false;
            entry._timeMs = reader.readVarInt();
            if ( readBalances( reader, entry._listChange ) == false )
                return false;
        }
        return reader.hasOverflowed() == false;
    }

    EconomyResult EconomyProtocol::fromErrorCode( uint16 errorCode )
    {
        static constexpr OnlineErrorResult<EconomyResult> kArrErrorResult[] = {
            {             OnlineError::kOk,             EconomyResult::Ok},
            {OnlineError::kUnauthenticated,    EconomyResult::NotSignedIn},
            { OnlineError::kInvalidRequest, EconomyResult::InvalidRequest},
            {       OnlineError::kConflict,           EconomyResult::Busy},
        };
        return OnlineError::toResult( errorCode, kArrErrorResult, EconomyResult::Unavailable );
    }
} // namespace sw

namespace sw
{
    const OnlineProtocolEntry EconomyProtocol::kOnlineEntry{ "Economy", OnlineMethodRange::kEconomy, EconomyProtocol::kVersion };
} // namespace sw
