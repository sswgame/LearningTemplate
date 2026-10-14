#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Shared/API/EconomyClient.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Kits/Feature/Online/Economy/Shared/API/EconomyMirror.h"

namespace sw
{
    EconomyClient::EconomyClient()
        : _listBalance{}
        , _revision{ 0 }
    {
    }

    uint64 EconomyClient::requestWallet( const ReplyDelegate& onReply )
    {
        return sendCall( EconomyMethod::kGetWallet, BitWriter{}, NetIdempotencyKey{}, onReply );
    }

    uint64 EconomyClient::requestHistory( const EconomyHistoryRequest& request, const ReplyDelegate& onReply )
    {
        BitWriter body;
        EconomyProtocol::writeHistoryRequest( body, request );
        return sendCall( EconomyMethod::kGetHistory, body, NetIdempotencyKey{}, onReply );
    }

    uint64 EconomyClient::requestPurchase( const EconomyPurchaseRequest& request, const NetIdempotencyKey& key, const ReplyDelegate& onReply )
    {
        BitWriter body;
        EconomyProtocol::writePurchaseRequest( body, request );
        return sendCall( EconomyMethod::kPurchase, body, key.isValid() ? key : NetIdempotencyKey::makeRandom(), onReply );
    }

    uint64 EconomyClient::requestRedeem( const EconomyRedeemRequest& request, const NetIdempotencyKey& key, const ReplyDelegate& onReply )
    {
        BitWriter body;
        EconomyProtocol::writeRedeemRequest( body, request );
        return sendCall( EconomyMethod::kRedeemReceipt, body, key.isValid() ? key : NetIdempotencyKey::makeRandom(), onReply );
    }

    void EconomyClient::applyBalances( const vector<LedgerBalance>& listBalance, bool bSnapshot )
    {
        if ( bSnapshot )
        {
            _listBalance = listBalance;
        }
        else
        {
            for ( const LedgerBalance& balance : listBalance )
            {
                LedgerBalance* pCached = nullptr;
                for ( LedgerBalance& cached : _listBalance )
                {
                    if ( cached._assetID == balance._assetID )
                        pCached = &cached;
                }
                if ( pCached != nullptr )
                    pCached->_amount = balance._amount;
                else
                    _listBalance.push_back( balance );
            }
        }
        ++_revision;
    }

    void EconomyClient::applyLedgerBalances( Wallet& inoutWallet ) const
    {
        EconomyMirror::applyToWallet( _listBalance, true, inoutWallet );
    }

    int64 EconomyClient::getBalance( string_view assetID ) const
    {
        for ( const LedgerBalance& balance : _listBalance )
        {
            if ( balance._assetID == assetID )
                return balance._amount;
        }
        return 0;
    }

    void EconomyClient::onCallSucceeded( const Call& call, const EconomyReply& reply )
    {
        const bool bSnapshot = call._method == EconomyMethod::kGetWallet;
        if ( bSnapshot || reply._listBalance.empty() == false )
            applyBalances( reply._listBalance, bSnapshot );
    }
} // namespace sw
