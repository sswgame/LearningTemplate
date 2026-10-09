#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Protocol/EconomyProtocol.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    const utf8* toString( EconomyResult result )
    {
        static constexpr const utf8* kArrName[] = { "Ok",
                                                    "NotSignedIn",
                                                    "InvalidRequest",
                                                    "UnknownOffer",
                                                    "NotOnSale",
                                                    "NotPurchasable",
                                                    "LimitReached",
                                                    "InsufficientFunds",
                                                    "CapExceeded",
                                                    "UnknownProduct",
                                                    "ReceiptInvalid",
                                                    "ReceiptRefunded",
                                                    "ReceiptPending",
                                                    "AlreadyRedeemed",
                                                    "Busy",
                                                    "Unavailable" };
        static_assert( SW_COUNT_OF( kArrName ) == static_cast<size_t>( EconomyResult::Count ), "EconomyResult names must match the enum" );
        const size_t index = static_cast<size_t>( result );
        return index < SW_COUNT_OF( kArrName ) ? kArrName[index] : "Unknown";
    }

    void EconomyProtocol::writePurchaseRequest( BitWriter& outWriter, const EconomyPurchaseRequest& request )
    {
        ServiceKeyUtil::writeString( outWriter, request._offerId );
        outWriter.writeVarInt( request._count );
    }

    bool EconomyProtocol::readPurchaseRequest( BitReader& reader, EconomyPurchaseRequest& outRequest )
    {
        if ( ServiceKeyUtil::readString( reader, kMaxIdSize, outRequest._offerId ) == false )
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
        return ServiceKeyUtil::readString( reader, kMaxIdSize, outRequest._storeName ) && ServiceKeyUtil::readString( reader, kMaxPayloadSize, outRequest._payload );
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
            ServiceKeyUtil::writeString( outWriter, balance._assetId );
            outWriter.writeVarInt( balance._amount );
        }
    }

    bool EconomyProtocol::readBalances( BitReader& reader, vector<LedgerBalance>& outListBalance )
    {
        const uint64 count = reader.readVarUint();
        if ( count > static_cast<uint64>( kMaxBalanceCount ) )
            return false;
        outListBalance.resize( static_cast<size_t>( count ) );
        for ( LedgerBalance& balance : outListBalance )
        {
            if ( ServiceKeyUtil::readString( reader, LedgerConstant::kMaxAssetIdSize, balance._assetId ) == false )
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
        ServiceKeyUtil::writeString( outWriter, reply._productId );
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
        const uint64 result = reader.readVarUint();
        if ( result >= static_cast<uint64>( EconomyResult::Count ) )
            return false;
        outReply._result    = static_cast<EconomyResult>( result );
        outReply._bReplayed = reader.readBool() ? SW_TRUE : SW_FALSE;
        if ( readBalances( reader, outReply._listBalance ) == false )
            return false;
        const bool bTextOk = ServiceKeyUtil::readString( reader, kMaxIdSize, outReply._productId ) &&
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
        switch ( errorCode )
        {
            case OnlineError::kOk:
                return EconomyResult::Ok;
            case OnlineError::kUnauthenticated:
                return EconomyResult::NotSignedIn;
            case OnlineError::kInvalidRequest:
                return EconomyResult::InvalidRequest;
            case OnlineError::kConflict:
                return EconomyResult::Busy;
            default:
                return EconomyResult::Unavailable;
        }
    }
} // namespace sw
