/**
 * @file ServiceStoreFactory.cpp
 * @brief 서버 설정의 저장소 항목(`ServerStoreEntry`) 하나로 서비스 저장소를 만듭니다. 저장소 항목은 드라이버 이름, 접속 문자열, 비밀로 이루어집니다.
 * @details "memory" 는 기반 계층의 메모리 구현이며 테스트와 개발에 쓴다. 같은 프로세스의 저장소끼리 데이터를 나눠 쓴다.
 *          그 밖의 이름은 `SQLServiceStore` 로 만들며 드라이버 레지스트리에서 드라이버를 찾는다.
 *          이 빌드 타깃에 없는 드라이버이면 분명한 오류를 남기고 기동을 멈추며, 다른 드라이버로 대신하지 않는다.
 *          서버 조립은 이 키트의 모듈 서브시스템(`IServiceStoreDriver` 로 공개)으로만 닿고 이름으로 부르는 곳이 없어 헤더를 두지 않는다.
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Time/TimePoint.h"

#include "Engine/Module/ModuleSubsystemRegistrar.h"

#include "GameFramework/Base/Online/Server/IOnlineStoreDriver.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Server/SQLServiceStore.h"

namespace sw
{
    /**
     * @class SQLServiceStoreDriver
     * @brief 저장 키트의 저장소 드라이버입니다 — 이 키트의 모듈 서브시스템(`SW_MODULE_SUBSYSTEM( Engine, … )`)이고 `IServiceStoreDriver` 로 공개된다.
     * @details "memory" 저장소의 데이터(같은 프로세스의 저장소끼리 나눠 쓴다)를 값으로 든다 — 예전에는 소유자 없는 엔진 수명 값(`ScopeOwned`)이었다. 저장소는 이 데이터를
     *          주소로 들므로 키트가 내려가기(자식 범위가 걷히기) 전에 소멸해야 한다(서버 조립이 그렇게 한다 — 저장소 코드도 이 키트에 있다).
     */
    class SQLServiceStoreDriver final : public ISubsystem, public IServiceStoreDriver
    {
    public:
        static constexpr const utf8* kSubsystemName = "SQLServiceStoreDriver";

        // ISubsystem — 세울 것이 없다(드라이버 레지스트리는 드라이버 `.cpp` 의 등록 객체가 올린다).
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /**
         * @brief 저장소를 만들고 초기화합니다. 실패하면 nullptr 과 실패 이유를 반환합니다.
         * @param secret 비밀번호(`ServerSecret::read` 로 읽은 것)입니다. 쓰고 나면 호출한 쪽이 비웁니다.
         * @param nowMs 마이그레이션 기록에 남길 벽시계 시각입니다.
         */
        unique_ptr<IServiceStore> createServiceStore( string_view driverName, string_view connection, string_view secret, int32 workerCount, UnixMilliseconds nowMs,
                                                      string& outError ) override
        {
            if ( driverName == kMemoryStoreDriverName )
                return make_unique<MemoryServiceStore>( &_memoryDatabase );
            SQLConnectionPoolConfig settings;
            settings._connection              = string( connection );
            settings._secret                  = string( secret );
            settings._workerCount             = workerCount;
            unique_ptr<SQLServiceStore> store = make_unique<SQLServiceStore>();
            if ( store->initialize( driverName, settings, SQLServiceStore::kMigrationFolder, nowMs, outError ) == false )
                return nullptr;
            return store;
        }

    private:
        MemoryServiceDatabase _memoryDatabase; ///< "memory" 저장소의 데이터 — 같은 프로세스의 저장소끼리 나눠 쓴다
    };

    // 키트가 실리면 엔진 범위 아래 키트의 자식 범위에 서고, 내려가면 함께 걷힌다. PostgreSQL 드라이버는 드라이버 `.cpp` 의 등록 객체가 올린다.
    SW_MODULE_SUBSYSTEM( Engine, SQLServiceStoreDriver, IServiceStoreDriver );
} // namespace sw
