#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Server/ServiceStoreFactory.h"

#include "GameFramework/Base/Online/Server/OnlineStoreDriverTable.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Server/SQLServiceStore.h"

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
        SQLConnectionPoolConfig settings;
        settings._connection              = string( connection );
        settings._secret                  = string( secret );
        settings._workerCount             = workerCount;
        unique_ptr<SQLServiceStore> store = make_unique<SQLServiceStore>();
        if ( store->initialize( driverName, settings, SQLServiceStore::kMigrationFolder, nowMs, outError ) == false )
            return nullptr;
        return store;
    }
} // namespace sw

namespace sw
{
    // 모듈이 로드되면 서버 조립의 저장소 만들기를 이 팩토리에 묶고, 언로드되면 푼다. PostgreSQL 드라이버는 드라이버 `.cpp` 의 등록 객체가 올린다.
    const OnlineStoreDriverBinding s_serviceStoreDriverBinding{ &ServiceStoreFactory::createServiceStore, nullptr };
} // namespace sw
