#include "pch.h"

#include "GameFramework/Kits/Storage/Server/SqlStore/ServiceStoreFactory.h"

#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Storage/Server/SqlStore/SqlServiceStore.h"

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
        SqlConnectionPoolSettings settings;
        settings._connection              = string( connection );
        settings._secret                  = string( secret );
        settings._workerCount             = workerCount;
        unique_ptr<SqlServiceStore> store = make_unique<SqlServiceStore>();
        if ( store->initialize( driverName, settings, SqlServiceStore::kMigrationFolder, nowMs, outError ) == false )
            return nullptr;
        return store;
    }

    void ServiceStoreFactory::shutdown() {}
} // namespace sw
