#include "pch.h"

#include "GameFramework/Base/Online/Ledger/LedgerTransferWork.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"

namespace sw
{
    LedgerTransferWork::LedgerTransferWork( const LedgerTransferRequest& request, CompleteDelegate onComplete )
        : _request{ request }
        , _outcome{}
        , _onComplete{ onComplete }
    {
    }

    void LedgerTransferWork::run( IServiceStoreConnection& connection ) { (void)Ledger::executeTransfer( connection, _request, _outcome ); }

    void LedgerTransferWork::complete()
    {
        if ( _onComplete.isBound() )
            _onComplete( _outcome );
    }
} // namespace sw
