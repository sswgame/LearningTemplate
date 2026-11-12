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

    expected<void> IOnlineServerKit::addTTLRule( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, ServiceTTLRule rule )
    {
        if ( rule._table.empty() || rule.isValid() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the retention rule needs a table, a positive expire-after and a time reader or a TTL index", entry._name } );
        for ( const ServiceTTLRule& other : context._listTTLRule )
        {
            if ( other._table == rule._table && other._keyPrefix == rule._keyPrefix )
            {
                Error error{ ErrorKind::AlreadyExists, "the retention rule of the table is already registered", entry._name };
                error.appendDetail( " (" ).appendDetail( rule._table.viewOrEmpty() ).appendDetail( ")" );
                return unexpected( error );
            }
        }
        context._listTTLRule.push_back( std::move( rule ) );
        return {};
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
