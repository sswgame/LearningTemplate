#include "pch.h"

#include "Engine/Observability/ServiceFactoryList.h"

#include "Core/String/RegistrationList.h"

namespace sw
{
    SW_LOG_CALLER( "ServiceFactoryList" );

    namespace
    {
        bool isOrderedBefore( const ServiceFactoryNode* pLeft, const ServiceFactoryNode* pRight )
        {
            if ( pLeft->_order != pRight->_order )
                return pLeft->_order < pRight->_order;
            return string_view( pLeft->_pName ) < string_view( pRight->_pName );
        }
    } // namespace
} // namespace sw

namespace sw
{
    ServiceFactoryList::ServiceFactoryList()
        : _mutex{}
        , _pHead{ nullptr }
        , _count{ 0 }
        , _pShadowHead{ nullptr }
    {
    }

    bool ServiceFactoryList::add( ServiceFactoryNode* pNode, const utf8* pListName )
    {
        if ( pNode == nullptr || pNode->_pName == nullptr )
            return false;
        ScopedLock<mutex>    lock{ _mutex };
        ServiceFactoryNode** ppLink = &_pHead;
        while ( *ppLink != nullptr )
        {
            ServiceFactoryNode* pExisting = *ppLink;
            if ( pExisting == pNode )
                return true;
            if ( string_view( pExisting->_pName ) == string_view( pNode->_pName ) )
            {
                if ( RegistrationReloadScope::isActive() == false )
                {
                    reportRegistrationRejected( RegistrationResult::DuplicateName, pListName, pNode->_pName );
                    return false;
                }
                // 교체 이미지의 등록이다. 옛 노드는 목록에서 빼 그림자 사슬로 옮긴다(옛 이미지가 내려가며 빼거나, 이 노드가 취소되면 복원한다).
                *ppLink           = pExisting->_pNext;
                pExisting->_pNext = _pShadowHead;
                _pShadowHead      = pExisting;
                --_count;
                continue;
            }
            ppLink = &pExisting->_pNext;
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
        ScopedLock<mutex>    lock{ _mutex };
        ServiceFactoryNode** ppLink = &_pHead;
        while ( *ppLink != nullptr )
        {
            if ( *ppLink == pNode )
            {
                *ppLink       = pNode->_pNext;
                pNode->_pNext = nullptr;
                --_count;
                restoreShadowOf( pNode->_pName );
                return;
            }
            ppLink = &( *ppLink )->_pNext;
        }

        // 올라 있지 않으면 대체당한 옛 노드일 수 있다 — 옛 이미지가 내려가는 것이므로 그림자만 버린다.
        ppLink = &_pShadowHead;
        while ( *ppLink != nullptr )
        {
            if ( *ppLink == pNode )
            {
                *ppLink       = pNode->_pNext;
                pNode->_pNext = nullptr;
                return;
            }
            ppLink = &( *ppLink )->_pNext;
        }
    }

    void ServiceFactoryList::restoreShadowOf( const utf8* pName )
    {
        ServiceFactoryNode** ppLink = &_pShadowHead;
        while ( *ppLink != nullptr )
        {
            ServiceFactoryNode* pShadow = *ppLink;
            if ( string_view( pShadow->_pName ) == string_view( pName ) )
            {
                *ppLink         = pShadow->_pNext;
                pShadow->_pNext = _pHead;
                _pHead          = pShadow;
                ++_count;
                return;
            }
            ppLink = &pShadow->_pNext;
        }
    }

    void ServiceFactoryList::collect( vector<ServiceFactoryNode*>& outListNode ) const
    {
        outListNode.clear();
        {
            ScopedLock<mutex> lock{ _mutex };
            for ( ServiceFactoryNode* pNode = _pHead; pNode != nullptr; pNode = pNode->_pNext )
            {
                outListNode.push_back( pNode );
            }
        }
        std::sort( outListNode.begin(), outListNode.end(), &isOrderedBefore );
    }

    int32 ServiceFactoryList::getCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _count;
    }
} // namespace sw
