#include "pch.h"

#include "Core/Registry/ServiceFactoryList.h"

namespace sw
{
    bool ServiceFactoryNode::addTo( List& list, ServiceFactoryNode* pNode )
    {
        const string_view        name   = pNode != nullptr && pNode->_pName != nullptr ? string_view{ pNode->_pName } : string_view{};
        const RegistrationResult result = list.addLogged( pNode, list.getListName(), name, pNode != nullptr ? pNode->_order : 0 );
        return result == RegistrationResult::Added || result == RegistrationResult::AlreadyPresent;
    }
} // namespace sw
