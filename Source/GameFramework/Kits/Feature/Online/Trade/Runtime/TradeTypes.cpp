#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Trade/Runtime/TradeTypes.h"

#include "Core/Foundation/Constants.h"

namespace sw
{
    optional<int32> TradeSnapshot::findSideIndex( AccountID accountID ) const
    {
        if ( accountID == kInvalidAccountID )
            return nullopt;
        if ( _arrSide[0]._accountID == accountID )
            return 0;
        if ( _arrSide[1]._accountID == accountID )
            return 1;
        return nullopt;
    }
} // namespace sw
