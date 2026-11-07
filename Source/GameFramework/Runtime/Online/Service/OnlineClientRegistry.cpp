#include "pch.h"

#include "GameFramework/Runtime/Online/Service/OnlineClientRegistry.h"

namespace sw
{
    namespace
    {
        struct OnlineClientRegistryInternal
        {
            static constexpr const utf8* kListName = "Online client";

            /** @brief 정적 초기화 순서와 상관없이 처음 쓸 때 만들어지도록 함수 안의 정적 객체로 둡니다. */
            static ServiceFactoryList& getList()
            {
                static ServiceFactoryList s_list{ kListName };
                return s_list;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void OnlineClientRegistry::registerFactory( OnlineClientFactory* pFactory )
    {
        if ( pFactory == nullptr || pFactory->_pCreate == nullptr )
            return;
        (void)OnlineClientRegistryInternal::getList().add( pFactory );
    }

    void OnlineClientRegistry::unregisterFactory( OnlineClientFactory* pFactory )
    {
        OnlineClientRegistryInternal::getList().remove( pFactory );
    }

    void OnlineClientRegistry::collectFactories( vector<OnlineClientFactory*>& outListFactory )
    {
        OnlineClientRegistryInternal::getList().collectAs( outListFactory );
    }
} // namespace sw
