#include "pch.h"

#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"

#include "Core/Common/HashUtil.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"

#include <cstdio>
#include <cstdlib>

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
    OnlineProtocolEntry::OnlineProtocolEntry( const utf8* pName, uint16 methodRange, SchemaHashFunction pComputeMessageHash )
        : ServiceFactoryNode{}
        , _pComputeMessageHash{ pComputeMessageHash }
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
        if ( OnlineProtocolRegistryInternal::getList().add( pEntry, OnlineProtocolRegistryInternal::kListName ) )
            return;
        // 오르지 못한 항목은 해시에 빠지므로 이 빌드의 해시가 키트 목록과 어긋납니다. 고칠 곳은 항목 선언이므로 모든 구성에서 멈춥니다.
        // 모듈 로드(정적 초기화) 중이라 로거가 아직 없을 수 있어 표준 오류에도 직접 씁니다.
        const utf8* pName = pEntry->_pName != nullptr ? pEntry->_pName : "(no name)";
        std::fprintf( stderr, "Online protocol entry '%s' could not be registered (missing or duplicate name)\n", pName );
        std::fflush( stderr );
        SW_LOG_ERROR( "Online protocol entry '%#' could not be registered (missing or duplicate name)", pName );
        std::abort();
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
            hash                              = HashUtil::combine( hash, pEntry->_pComputeMessageHash() );
        }
        return hash;
    }
} // namespace sw
