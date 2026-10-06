#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/Time/MonotonicClock.h"

#include "EngineTest/GameFramework/Online/EphemeralStoreContract.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/Kits/Storage/Server/CacheStore/CacheStoreFactory.h"

#include <thread>

// RESP 휘발성 저장 — 실제 Valkey(리눅스 · WSL) · Garnet(Windows)에 계약 여덟. 서버가 있어야 돈다(SW_TEST_RESP_URL = 공장 끝점 글).
// 로컬: `docker run -p 6379:6379 valkey/valkey:8` 뒤 `set SW_TEST_RESP_URL=127.0.0.1:6379?prefix=t:`, Windows Garnet 은 `garnet-server --port 6380`.
// 같은 시험이 두 서버에서 같게 돌아 명령 부분집합(GF_Server_CacheStore README)을 지킨다.

using namespace sw;

SW_TEST_REQUIRES_ENVIRONMENT( EphemeralStoreRespTest, "SW_TEST_RESP_URL", "needs a Valkey or Garnet server" );

namespace
{
    struct RespServerFixture
    {
        unique_ptr<IEphemeralStore> _store;
        unique_ptr<IEphemeralStore> _secondStore;
        string                      _runPrefix;

        RespServerFixture()
            : _store{}
            , _secondStore{}
            , _runPrefix{}
        {
            const string endpoint = test::getEnvironmentValue( "SW_TEST_RESP_URL" );
            const string secret   = test::getEnvironmentValue( "SW_TEST_RESP_SECRET" ); // `user:password` 또는 비밀번호(없으면 AUTH 없음)
            string       error;
            _store = CacheStoreFactory::createEphemeralStore( CacheStoreFactory::kRespDriverName, endpoint, secret, error );
            SW_EXPECT_TRUE_MSG( _store != nullptr, error.c_str() );
            _secondStore = CacheStoreFactory::createEphemeralStore( CacheStoreFactory::kRespDriverName, endpoint, secret, error );
            SW_EXPECT_TRUE_MSG( _secondStore != nullptr, error.c_str() );
            // 실행마다 다른 키 — 앞선 실행이 남긴 키 · 순위와 섞이지 않는다.
            StringBuilder<constant::kMaxBuffer64> prefix;
            prefix.appendFormat( "run%#:", static_cast<uint32>( MonotonicClock::nowNanoseconds() ) );
            _runPrefix = string( prefix.view() );
        }

        IEphemeralStore& getStore() { return *_store; }
        IEphemeralStore& getSecondStore() { return *_secondStore; }
        void             advanceTimeMs( int64 deltaMs ) { std::this_thread::sleep_for( std::chrono::milliseconds( deltaMs ) ); }

        string makeKey( const utf8* pName )
        {
            string key = _runPrefix;
            key += pName;
            return key;
        }
    };
} // namespace

SW_EPHEMERAL_STORE_CONTRACT_SUITE( EphemeralStoreRespTest, RespServerFixture )

// 계약 앞의 전제 — 환경의 끝점 글을 공장이 읽는다(못 읽으면 계약 케이스가 모두 앞 없이 진다).
SW_TEST_CASE( EphemeralStoreRespTest, EndpointFromTheEnvironmentParses )
{
    CacheEndpoint endpoint;
    string        error;
    SW_EXPECT_TRUE_MSG( CacheStoreFactory::parseEndpoint( test::getEnvironmentValue( "SW_TEST_RESP_URL" ), endpoint, error ), error.c_str() );
}
