#include "pch.h"

#include "Core/Registry/FactoryRegistrationList.h"

namespace sw
{
    bool FactoryRegistration::addTo( List& list, FactoryRegistration* pNode )
    {
        const string_view        name   = pNode != nullptr && pNode->_pName != nullptr ? string_view{ pNode->_pName } : string_view{};
        const RegistrationResult result = list.addLogged( pNode, list.getListName(), name, pNode != nullptr ? pNode->_order : 0 );
        return result == RegistrationResult::Added || result == RegistrationResult::AlreadyPresent;
    }
} // namespace sw
