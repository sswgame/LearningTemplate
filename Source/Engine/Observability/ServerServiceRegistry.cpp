#include "pch.h"

#include "Engine/Observability/ServerServiceRegistry.h"

namespace sw
{
    namespace
    {
        struct ServerServiceRegistryInternal
        {
            static constexpr const utf8* kListName = "Server service";

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
    void ServerServiceRegistry::registerFactory( ServerServiceFactory* pFactory )
    {
        if ( pFactory == nullptr || pFactory->_pCreate == nullptr )
            return;
        (void)ServerServiceRegistryInternal::getList().add( pFactory, ServerServiceRegistryInternal::kListName );
    }

    void ServerServiceRegistry::unregisterFactory( ServerServiceFactory* pFactory )
    {
        ServerServiceRegistryInternal::getList().remove( pFactory );
    }

    void ServerServiceRegistry::collectFactories( vector<ServerServiceFactory*>& outListFactory )
    {
        ServerServiceRegistryInternal::getList().collectAs( outListFactory );
    }

    int32 ServerServiceRegistry::getFactoryCount()
    {
        return ServerServiceRegistryInternal::getList().getCount();
    }
} // namespace sw
