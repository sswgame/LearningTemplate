#include "pch.h"

#include "GameFramework/Runtime/Online/Service/OnlineClientRegistry.h"

namespace sw
{
    namespace
    {
        struct OnlineClientRegistryInternal
        {
            /** @brief 등록 목록입니다(함수 정적 — 정적 초기화 순서와 상관없이 처음 쓸 때 선다). */
            static SW_DEFINE_SERVICE_FACTORY_LIST( getList, "Online client" )
        };
    } // namespace
} // namespace sw

namespace sw
{
    void OnlineClientRegistry::registerFactory( OnlineClientFactory* pFactory )
    {
        if ( pFactory == nullptr || pFactory->_pCreate == nullptr )
            return;
        (void)ServiceFactoryNode::addTo( OnlineClientRegistryInternal::getList(), pFactory );
    }

    void OnlineClientRegistry::unregisterFactory( OnlineClientFactory* pFactory )
    {
        (void)OnlineClientRegistryInternal::getList().remove( pFactory ); // 올라 있지 않은 것을 빼는 것은 할 일이 없다
    }

    void OnlineClientRegistry::collectFactories( vector<OnlineClientFactory*>& outListFactory )
    {
        ServiceFactoryNode::collectAs( OnlineClientRegistryInternal::getList(), outListFactory );
    }
} // namespace sw
