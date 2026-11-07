#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/API/EconomyClient.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/API/EconomyMirror.h"
#include "GameFramework/Runtime/Online/Service/OnlineClientRegistry.h"

namespace sw
{
    EconomyClient::EconomyClient()
        : _listBalance{}
        , _revision{ 0 }
    {
    }

    void EconomyClient::onPush( EconomyMethod::BalanceChanged, const EconomyBalanceChanged& body )
    {
        applyBalances( body._listBalance, false );
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

    void EconomyClient::onCallSucceeded( EconomyMethod::GetWallet, const OnlineNoBody& request, const EconomyReply& reply )
    {
        (void)request;
        applyBalances( reply._listBalance, true ); // 지갑 응답은 전체 스냅샷이다
    }

    void EconomyClient::onCallSucceeded( EconomyMethod::Purchase, const EconomyPurchaseRequest& request, const EconomyReply& reply )
    {
        (void)request;
        if ( reply._listBalance.empty() == false )
            applyBalances( reply._listBalance, false );
    }

    void EconomyClient::onCallSucceeded( EconomyMethod::RedeemReceipt, const EconomyRedeemRequest& request, const EconomyReply& reply )
    {
        (void)request;
        if ( reply._listBalance.empty() == false )
            applyBalances( reply._listBalance, false );
    }

    void EconomyClient::onCallSucceeded( EconomyMethod::WithdrawPurchase, const EconomyWithdrawRequest& request, const EconomyReply& reply )
    {
        (void)request;
        if ( reply._listBalance.empty() == false )
            applyBalances( reply._listBalance, false );
    }
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_CLIENT( EconomyClient );
} // namespace sw
