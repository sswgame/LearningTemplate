#include "pch.h"

#include "Engine/Observability/ServiceFactoryList.h"

namespace sw
{
    SW_LOG_CALLER( "ServiceFactoryList" );

    namespace
    {
        struct ServiceFactoryListInternal
        {
            static bool isOrderedBefore( const ServiceFactoryNode* pLeft, const ServiceFactoryNode* pRight )
            {
                if ( pLeft->_order != pRight->_order )
                    return pLeft->_order < pRight->_order;
                return string_view( pLeft->_pName ) < string_view( pRight->_pName );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ServiceFactoryList::ServiceFactoryList()
        : _mutex{}
        , _pHead{ nullptr }
        , _count{ 0 }
    {
    }

    bool ServiceFactoryList::add( ServiceFactoryNode* pNode, const utf8* pListName )
    {
        if ( pNode == nullptr || pNode->_pName == nullptr )
            return false;
        std::scoped_lock<mutex> lock{ _mutex };
        for ( const ServiceFactoryNode* pExisting = _pHead; pExisting != nullptr; pExisting = pExisting->_pNext )
        {
            if ( pExisting == pNode )
                return true;
            if ( string_view( pExisting->_pName ) == string_view( pNode->_pName ) )
            {
                SW_LOG_ERROR( "%# '%#' is registered twice - the second registration is ignored", pListName, pNode->_pName );
                return false;
            }
        }
        pNode->_pNext = _pHead;
        _pHead        = pNode;
        ++_count;
        return true;
    }

    void ServiceFactoryList::remove( ServiceFactoryNode* pNode )
    {
        if ( pNode == nullptr )
            return;
        std::scoped_lock<mutex> lock{ _mutex };
        ServiceFactoryNode**    ppLink = &_pHead;
        while ( *ppLink != nullptr )
        {
            if ( *ppLink == pNode )
            {
                *ppLink       = pNode->_pNext;
                pNode->_pNext = nullptr;
                --_count;
                return;
            }
            ppLink = &( *ppLink )->_pNext;
        }
    }

    void ServiceFactoryList::collect( vector<ServiceFactoryNode*>& outListNode ) const
    {
        outListNode.clear();
        {
            std::scoped_lock<mutex> lock{ _mutex };
            for ( ServiceFactoryNode* pNode = _pHead; pNode != nullptr; pNode = pNode->_pNext )
            {
                outListNode.push_back( pNode );
            }
        }
        std::sort( outListNode.begin(), outListNode.end(), &ServiceFactoryListInternal::isOrderedBefore );
    }

    int32 ServiceFactoryList::getCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _count;
    }
} // namespace sw
