#include "pch.h"

#include "GameFramework/Base/Online/Server/OnlineServerKit.h"

namespace sw
{
    namespace
    {
        struct OnlineServerKitInternal
        {
            static constexpr const utf8* kListName = "Online server kit";

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
    void OnlineServerKitRegistry::registerFactory( OnlineServerKitFactory* pFactory )
    {
        if ( pFactory == nullptr || pFactory->_pCreate == nullptr )
            return;
        (void)OnlineServerKitInternal::getList().add( pFactory, OnlineServerKitInternal::kListName );
    }

    void OnlineServerKitRegistry::unregisterFactory( OnlineServerKitFactory* pFactory )
    {
        OnlineServerKitInternal::getList().remove( pFactory );
    }

    void OnlineServerKitRegistry::collectFactories( vector<OnlineServerKitFactory*>& outListFactory )
    {
        OnlineServerKitInternal::getList().collectAs( outListFactory );
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
