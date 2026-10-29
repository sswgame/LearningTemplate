#include "pch.h"

#include "Engine/DedicatedServer/ServiceFactoryList.h"

namespace sw
{
    ServiceFactoryList::ServiceFactoryList( const utf8* pListName )
        : _pListName{ pListName }
        , _mutex{}
        , _list{ pListName, RegistrationOrder::ByOrderThenName, true }
    {
    }

    bool ServiceFactoryList::add( ServiceFactoryNode* pNode )
    {
        RegistrationResult result = RegistrationResult::NullItem;
        if ( pNode != nullptr )
        {
            ScopedLock<mutex> lock{ _mutex };
            result = _list.add( pNode, pNode->_pName != nullptr ? string_view{ pNode->_pName } : string_view{}, pNode->_order );
        }
        // 잠금은 잎이다 — 거절 로그는 놓은 뒤에 쓴다.
        reportRegistrationRejected( result, _pListName, pNode != nullptr && pNode->_pName != nullptr ? string_view{ pNode->_pName } : string_view{} );
        return result == RegistrationResult::Added || result == RegistrationResult::AlreadyPresent;
    }

    void ServiceFactoryList::remove( ServiceFactoryNode* pNode )
    {
        ScopedLock<mutex> lock{ _mutex };
        (void)_list.remove( pNode ); // 올라 있지 않은 것을 빼는 것은 할 일이 없다
    }

    void ServiceFactoryList::collect( vector<ServiceFactoryNode*>& outListNode ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        outListNode = _list.getItems();
    }

    int32 ServiceFactoryList::getCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return static_cast<int32>( _list.getCount() );
    }
} // namespace sw
