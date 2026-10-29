/**
 * @file ServiceStoreFactory.cpp
 * @brief 서버 설정의 저장소 항목(`ServerStoreEntry`) 하나로 서비스 저장소를 만듭니다. 저장소 항목은 드라이버 이름, 접속 문자열, 비밀로 이루어집니다.
 * @details "memory" 는 기반 계층의 메모리 구현이며 테스트와 개발에 쓴다. 같은 프로세스의 저장소끼리 데이터를 나눠 쓴다.
 *          그 밖의 이름은 `SQLServiceStore` 로 만들며 드라이버 레지스트리에서 드라이버를 찾는다.
 *          이 빌드 타깃에 없는 드라이버이면 분명한 오류를 남기고 기동을 멈추며, 다른 드라이버로 대신하지 않는다.
 *          서버 조립은 `OnlineStoreDriverBinding` 으로만 닿고 이름으로 부르는 곳이 없어 헤더를 두지 않는다.
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Memory/LifetimeScope.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/TimePoint.h"

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

        /**
         * @brief 저장소를 만들고 초기화합니다. 실패하면 nullptr 과 실패 이유를 반환합니다.
         * @param secret 비밀번호(`ServerSecret::read` 로 읽은 것)입니다. 쓰고 나면 호출한 쪽이 비웁니다.
         * @param nowMs 마이그레이션 기록에 남길 벽시계 시각입니다.
         */
        unique_ptr<IServiceStore> createServiceStore( string_view driverName, string_view connection, string_view secret, int32 workerCount, UnixMilliseconds nowMs, string& outError )
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
    } // namespace
} // namespace sw

namespace sw
{
    // 모듈이 로드되면 서버 조립의 저장소 만들기를 이 팩토리에 묶고, 언로드되면 푼다. PostgreSQL 드라이버는 드라이버 `.cpp` 의 등록 객체가 올린다.
    const OnlineStoreDriverBinding s_serviceStoreDriverBinding{ &createServiceStore, nullptr };
} // namespace sw
