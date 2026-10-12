#include "pch.h"

#include "GameFramework/Base/Online/Server/OnlineStoreDriverTable.h"

#include "Core/Concurrency/atomic.h"

namespace sw
{
    namespace
    {
        /** @brief 묶인 함수입니다. 모듈 로드와 언로드(로더 스레드)에서 쓰고 서버 조립(서비스 스레드)에서 읽으므로 원자 포인터로 둡니다. */
        atomic<OnlineStoreDriverTable::CreateServiceStoreFunction>   s_pCreateServiceStore{ nullptr };
        atomic<OnlineStoreDriverTable::CreateEphemeralStoreFunction> s_pCreateEphemeralStore{ nullptr };
    } // namespace
} // namespace sw

namespace sw
{
    void OnlineStoreDriverTable::bindServiceStoreFactory( CreateServiceStoreFunction pCreate )
    {
        s_pCreateServiceStore.store( pCreate, std::memory_order_release );
    }

    void OnlineStoreDriverTable::bindEphemeralStoreFactory( CreateEphemeralStoreFunction pCreate )
    {
        s_pCreateEphemeralStore.store( pCreate, std::memory_order_release );
    }

    OnlineStoreDriverTable::CreateServiceStoreFunction OnlineStoreDriverTable::findServiceStoreFactory()
    {
        return s_pCreateServiceStore.load( std::memory_order_acquire );
    }

    OnlineStoreDriverTable::CreateEphemeralStoreFunction OnlineStoreDriverTable::findEphemeralStoreFactory()
    {
        return s_pCreateEphemeralStore.load( std::memory_order_acquire );
    }
} // namespace sw
