#include "pch.h"

#include "GameFramework/Kits/Online/Server/Account/AccountNameIndex.h"

#include "GameFramework/Kits/Online/Server/Account/LoginStoreLogic.h"

namespace sw
{
    ServiceStoreResult AccountNameIndex::readIdentityByDisplayName( IServiceStoreConnection& connection, string_view displayName, AccountIdentity& outIdentity ) const
    {
        return LoginStoreLogic::readIdentityByDisplayName( connection, displayName, outIdentity );
    }

    ServiceStoreResult AccountNameIndex::readIdentity( IServiceStoreConnection& connection, AccountId accountId, AccountIdentity& outIdentity ) const
    {
        return LoginStoreLogic::readIdentity( connection, accountId, outIdentity );
    }
} // namespace sw
