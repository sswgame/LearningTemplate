#include "pch.h"

#include "Engine/Server/ServerServiceRegistry.h"

namespace sw
{
    namespace
    {
        struct ServerServiceRegistryInternal
        {
            /** @brief 등록 목록입니다(함수 정적 — 정적 초기화 순서와 상관없이 처음 쓸 때 선다). */
            static SW_DEFINE_FACTORY_REGISTRATION_LIST( getList, "Server service" )
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ServerServiceRegistry::registerFactory( ServerServiceRegistration* pFactory )
    {
        if ( pFactory == nullptr || pFactory->_pCreate == nullptr )
            return false;
        return FactoryRegistration::addTo( ServerServiceRegistryInternal::getList(), pFactory );
    }

    void ServerServiceRegistry::unregisterFactory( ServerServiceRegistration* pFactory )
    {
        (void)ServerServiceRegistryInternal::getList().remove( pFactory ); // 올라 있지 않은 것을 빼는 것은 할 일이 없다
    }

    void ServerServiceRegistry::collectFactories( vector<ServerServiceRegistration*>& outListFactory )
    {
        FactoryRegistration::collectAs( ServerServiceRegistryInternal::getList(), outListFactory );
    }
} // namespace sw
