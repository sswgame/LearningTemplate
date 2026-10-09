#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Rule/AccountNameIndex.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Rule/LoginStoreLogic.h"

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
