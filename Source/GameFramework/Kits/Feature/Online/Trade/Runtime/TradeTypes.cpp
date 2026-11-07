#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Trade/Runtime/TradeTypes.h"

#include "Core/Foundation/Constants.h"

namespace sw
{
    int32 TradeSnapshot::findSideIndex( AccountID accountID ) const
    {
        if ( accountID == kInvalidAccountID )
            return constant::invalid_index::kInt32;
        if ( _arrSide[0]._accountID == accountID )
            return 0;
        if ( _arrSide[1]._accountID == accountID )
            return 1;
        return constant::invalid_index::kInt32;
    }
} // namespace sw
