#include "pch.h"

#include "Engine/Observability/ServerServiceRegistry.h"

#include "Core/Concurrency/atomic.h"

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

            /** @brief 묶인 서버 문맥입니다. 서버 기동과 종료(게임 스레드)에서 바꾸고 모듈이 읽으므로 원자 포인터로 둡니다. */
            static atomic<const ServerServiceContext*>& getHostContext()
            {
                static atomic<const ServerServiceContext*> s_pContext{ nullptr };
                return s_pContext;
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

    void ServerServiceRegistry::bindHostContext( const ServerServiceContext* pContext )
    {
        ServerServiceRegistryInternal::getHostContext().store( pContext, std::memory_order_release );
    }

    const ServerServiceContext* ServerServiceRegistry::findHostContext()
    {
        return ServerServiceRegistryInternal::getHostContext().load( std::memory_order_acquire );
    }
} // namespace sw
