#include "pch.h"

#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"

#include "Core/Hash/HashUtil.h"

#include "Engine/Reflection/EnumName.h"

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
    OnlineProtocolEntry::OnlineProtocolEntry( const utf8* pName, uint16 methodRange, SchemaHashFunction pComputeMethodHash, SchemaHashFunction pComputeMessageHash )
        : ServiceFactoryNode{}
        , _pComputeMethodHash{ pComputeMethodHash }
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

    uint64 OnlineProtocolEntry::computeNameOrderHash( const vector<string>& listMethodName, const vector<string>& listPushName )
    {
        // 이름 사이와 두 목록 사이에 구분자를 두어 ("ab", "c") 와 ("a", "bc"), 메서드 끝과 알림 처음이 섞이지 않게 한다
        string canonical;
        for ( const string& name : listMethodName )
        {
            canonical += name;
            canonical += ',';
        }
        canonical += ';';
        for ( const string& name : listPushName )
        {
            canonical += name;
            canonical += ',';
        }
        return FixedLayout::computeSchemaHash( FixedLayout::kVersion, canonical, {} );
    }

    vector<string> OnlineProtocolEntry::collectEnumNames( const hashed_string& enumFqn, const int32 count )
    {
        vector<string> listName;
        if ( count == 0 )
            return listName; // 이름이 없는 빈 표(`OnlineNoPushName`)는 리플렉션을 찾지 않는다
        // 해시는 핸드셰이크 때(타입 등록이 끝난 뒤) 계산하므로 레지스트리와 로거가 있다
        for ( int32 index = 0; index < count; ++index )
        {
            const utf8* pName = findEnumName( enumFqn, index );
            if ( pName == nullptr )
                SW_FATAL( "Online method table '%#' has no reflected name for value %# - add ENUM( Count = Count ) to it", enumFqn.c_str(), index );
            listName.push_back( pName );
        }
        return listName;
    }

    void OnlineProtocolRegistry::registerEntry( OnlineProtocolEntry* pEntry )
    {
        if ( OnlineProtocolRegistryInternal::getList().add( pEntry, OnlineProtocolRegistryInternal::kListName ) )
            return;
        // 오르지 못한 항목은 해시에 빠지므로 이 빌드의 해시가 키트 목록과 어긋납니다. 고칠 곳은 항목 선언이므로 모든 구성에서 멈춥니다.
        // 모듈 로드(정적 초기화) 중이라 로거가 아직 없을 수 있다 — `SW_FATAL` 은 표준 오류에 먼저 쓴다.
        const utf8* pName = pEntry->_pName != nullptr ? pEntry->_pName : "(no name)";
        SW_FATAL( "Online protocol entry '%#' could not be registered (missing or duplicate name)", pName );
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
            hash                              = HashUtil::combine( hash, pEntry->_pComputeMethodHash() );
            hash                              = HashUtil::combine( hash, pEntry->_pComputeMessageHash() );
        }
        return hash;
    }
} // namespace sw
