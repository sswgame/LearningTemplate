#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Trade/Shared/TradeTypes.h"

namespace sw
{
    int32 TradeSnapshot::findSideIndex( AccountID accountID ) const
    {
        if ( accountID == kInvalidAccountID )
            return -1;
        if ( _arrSide[0]._accountID == accountID )
            return 0;
        if ( _arrSide[1]._accountID == accountID )
            return 1;
        return -1;
    }
} // namespace sw
