#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Server/ServiceStoreFactory.h"

#include "GameFramework/Base/Online/Server/OnlineStoreDriverTable.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Server/Driver/Postgres/PostgresDriver.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Server/SQLServiceStore.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/SQLDriverRegistry.h"

namespace sw
{
    namespace
    {
        /** @brief "memory" 저장소의 데이터입니다. 같은 프로세스의 저장소끼리 나눠 씁니다. */
        MemoryServiceDatabase& getMemoryDatabase()
        {
            static MemoryServiceDatabase s_database;
            return s_database;
        }
    } // namespace
} // namespace sw

namespace sw
{
    unique_ptr<IServiceStore> ServiceStoreFactory::createServiceStore( string_view driverName, string_view connection, string_view secret, int32 workerCount, int64 nowMs,
                                                                       string& outError )
    {
        if ( driverName == OnlineStoreDriverTable::kMemoryDriverName )
            return make_unique<MemoryServiceStore>( &getMemoryDatabase() );
        // 이 모듈의 드라이버(PostgreSQL)를 처음 쓸 때 등록한다. 같은 객체를 두 번 등록하면 레지스트리가 조용히 무시한다.
        SQLDriverRegistry::registerDriver( &PostgresDriver::getInstance() );
        SQLConnectionPoolConfig settings;
        settings._connection              = string( connection );
        settings._secret                  = string( secret );
        settings._workerCount             = workerCount;
        unique_ptr<SQLServiceStore> store = make_unique<SQLServiceStore>();
        if ( store->initialize( driverName, settings, SQLServiceStore::kMigrationFolder, nowMs, outError ) == false )
            return nullptr;
        return store;
    }

    void ServiceStoreFactory::shutdown()
    {
        SQLDriverRegistry::unregisterDriver( &PostgresDriver::getInstance() );
    }
} // namespace sw

namespace sw
{
    // 모듈이 로드되면 서버 조립의 저장소 만들기를 이 팩토리에 묶고, 언로드되면 풀고 PostgreSQL 드라이버를 레지스트리에서 내린다.
    const OnlineStoreDriverBinding s_serviceStoreDriverBinding{ &ServiceStoreFactory::createServiceStore, &ServiceStoreFactory::shutdown };
} // namespace sw
