#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Server/ServiceStoreFactory.h"

#include "Core/Memory/LifetimeScope.h"

#include "GameFramework/Base/Online/Server/OnlineStoreDriverTable.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Server/SQLServiceStore.h"

namespace sw
{
    namespace
    {
        /**
         * @brief "memory" 저장소의 데이터의 소유자입니다 — 엔진 수명 범위가 소유해 엔진 종료 때 사라진다(처음 만든 "memory" 저장소가 만든다).
         * @details 같은 프로세스의 저장소끼리 나눠 쓴다. 저장소는 이 데이터를 주소로 들므로 엔진이 내려가기 전에 소멸해야 한다(서버 조립이 그렇게 한다).
         */
        ScopeOwned<MemoryServiceDatabase> s_memoryDatabase;

        MemoryServiceDatabase& getMemoryDatabase()
        {
            return s_memoryDatabase.getRequired(); // 엔진 수명 범위가 닫힌 뒤의 접근은 결함이다 — "memory" 저장소를 쓸 엔진이 없다
        }
    } // namespace
} // namespace sw

namespace sw
{
    unique_ptr<IServiceStore> ServiceStoreFactory::createServiceStore( string_view driverName, string_view connection, string_view secret, int32 workerCount, UnixMilliseconds nowMs,
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
