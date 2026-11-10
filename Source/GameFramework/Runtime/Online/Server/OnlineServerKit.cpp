#include "pch.h"

#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"

#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"

namespace sw
{
    namespace
    {
        struct OnlineServerKitInternal
        {
            /** @brief 등록 목록입니다(함수 정적 — 정적 초기화 순서와 상관없이 처음 쓸 때 선다). */
            static SW_DEFINE_SERVICE_FACTORY_LIST( getList, "Online server kit" )
        };
    } // namespace
} // namespace sw

namespace sw
{
    expected<void> IOnlineServerKit::registerServerService( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, IOnlineServerService* pService )
    {
        if ( context._pHost->registerServerService( pService ) )
            return {};
        return unexpected( Error{ ErrorKind::AlreadyExists, "the method range of the kit is already taken", entry._name } );
    }

    void OnlineServerKitRegistry::registerFactory( OnlineServerKitFactory* pFactory )
    {
        if ( pFactory == nullptr || pFactory->_pCreate == nullptr )
            return;
        (void)ServiceFactoryNode::addTo( OnlineServerKitInternal::getList(), pFactory );
    }

    void OnlineServerKitRegistry::unregisterFactory( OnlineServerKitFactory* pFactory )
    {
        (void)OnlineServerKitInternal::getList().remove( pFactory ); // 올라 있지 않은 것을 빼는 것은 할 일이 없다
    }

    void OnlineServerKitRegistry::collectFactories( vector<OnlineServerKitFactory*>& outListFactory )
    {
        ServiceFactoryNode::collectAs( OnlineServerKitInternal::getList(), outListFactory );
    }

    OnlineServerKitFactory* OnlineServerKitRegistry::findFactory( string_view name )
    {
        vector<OnlineServerKitFactory*> listFactory;
        collectFactories( listFactory );
        for ( OnlineServerKitFactory* pFactory : listFactory )
        {
            if ( string_view( pFactory->_pName ) == name )
                return pFactory;
        }
        return nullptr;
    }
} // namespace sw
