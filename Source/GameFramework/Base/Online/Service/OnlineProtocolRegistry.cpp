#include "pch.h"

#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"

#include "Core/Common/HashUtil.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"

namespace sw
{
    namespace
    {
        struct OnlineProtocolRegistryInternal
        {
            static constexpr const utf8* kListName = "Online protocol";

            /** @brief 정적 초기화 순서와 상관없이 처음 쓸 때 만들어지도록 함수 안의 정적 객체로 둡니다. */
            static ServiceFactoryList& getList()
            {
                static ServiceFactoryList s_list{};
                return s_list;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    OnlineProtocolEntry::OnlineProtocolEntry( const utf8* pName, uint16 methodRange, uint32 version )
        : ServiceFactoryNode{}
        , _version{ version }
        , _methodRange{ methodRange }
    {
        _pName = pName;
        _order = static_cast<int32>( methodRange );
        OnlineProtocolRegistry::registerEntry( this );
    }

    OnlineProtocolEntry::~OnlineProtocolEntry()
    {
        OnlineProtocolRegistry::unregisterEntry( this );
    }

    void OnlineProtocolRegistry::registerEntry( OnlineProtocolEntry* pEntry )
    {
        (void)OnlineProtocolRegistryInternal::getList().add( pEntry, OnlineProtocolRegistryInternal::kListName ); // 같은 이름이면 목록이 오류 로그를 남기고 거절한다
    }

    void OnlineProtocolRegistry::unregisterEntry( OnlineProtocolEntry* pEntry )
    {
        OnlineProtocolRegistryInternal::getList().remove( pEntry );
    }

    uint64 OnlineProtocolRegistry::computeSchemaHash()
    {
        vector<ServiceFactoryNode*> listNode;
        OnlineProtocolRegistryInternal::getList().collect( listNode );
        uint64 hash = HashUtil::combine( 0, OnlineProtocolConstant::kBaseVersion );
        for ( const ServiceFactoryNode* pNode : listNode )
        {
            const OnlineProtocolEntry* pEntry = static_cast<const OnlineProtocolEntry*>( pNode );
            hash                              = HashUtil::combine( hash, pEntry->_methodRange );
            hash                              = HashUtil::combine( hash, pEntry->_version );
        }
        return hash;
    }
} // namespace sw
