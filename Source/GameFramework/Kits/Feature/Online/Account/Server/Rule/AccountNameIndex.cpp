#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Rule/AccountNameIndex.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Rule/LoginStoreLogic.h"

namespace sw
{
    ServiceStoreResult AccountNameIndex::readIdentityByDisplayName( IServiceStoreConnection& connection, string_view displayName, AccountIdentity& outIdentity ) const
    {
        return LoginStoreLogic::readIdentityByDisplayName( connection, displayName, outIdentity );
    }

    ServiceStoreResult AccountNameIndex::readIdentity( IServiceStoreConnection& connection, AccountID accountID, AccountIdentity& outIdentity ) const
    {
        return LoginStoreLogic::readIdentity( connection, accountID, outIdentity );
    }

    AccountExternalLinks::AccountExternalLinks( ILoginCrypto* pCrypto )
        : _pCrypto{ pCrypto }
    {
    }

    ServiceStoreResult AccountExternalLinks::readIsLinked( IServiceStoreConnection& connection, AccountID accountID, string_view provider, string_view subject, bool& outbLinked ) const
    {
        return LoginStoreLogic::readIsLinked( connection, *_pCrypto, accountID, provider, subject, outbLinked );
    }
} // namespace sw
