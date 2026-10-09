#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/Server/SqlStore/ServiceStoreFactory.h"

#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Storage/Server/SqlStore/Driver/Postgres/PostgresDriver.h"
#include "GameFramework/Kits/Feature/Storage/Server/SqlStore/SqlServiceStore.h"
#include "GameFramework/Kits/Feature/Storage/SqlStore/Sql/SqlDriverRegistry.h"

namespace sw
{
    namespace
    {
        struct ServiceStoreFactoryInternal
        {
            /** @brief "memory" 저장소의 데이터 — 같은 프로세스의 앞들이 나눠 쓴다. */
            static MemoryServiceDatabase& getMemoryDatabase()
            {
                static MemoryServiceDatabase s_database;
                return s_database;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    unique_ptr<IServiceStore> ServiceStoreFactory::createServiceStore( string_view driverName, string_view connection, string_view secret, int32 workerCount, int64 nowMs,
                                                                       string& outError )
    {
        if ( driverName == kMemoryDriverName )
            return make_unique<MemoryServiceStore>( &ServiceStoreFactoryInternal::getMemoryDatabase() );
        // 이 모듈의 드라이버(PostgreSQL)를 처음 쓸 때 올린다 — 같은 객체를 두 번 올리면 등록부가 조용히 무시한다.
        SqlDriverRegistry::registerDriver( &PostgresDriver::getInstance() );
        SqlConnectionPoolSettings settings;
        settings._connection              = string( connection );
        settings._secret                  = string( secret );
        settings._workerCount             = workerCount;
        unique_ptr<SqlServiceStore> store = make_unique<SqlServiceStore>();
        if ( store->initialize( driverName, settings, SqlServiceStore::kMigrationFolder, nowMs, outError ) == false )
            return nullptr;
        return store;
    }

    void ServiceStoreFactory::shutdown() { SqlDriverRegistry::unregisterDriver( &PostgresDriver::getInstance() ); }
} // namespace sw
