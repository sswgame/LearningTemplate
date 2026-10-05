#include "pch.h"

#include "EngineTest/GameFramework/Online/EphemeralStoreContract.h"

#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"

// 메모리 휘발성 저장 — 계약 여덟(만료 · 조건 쓰기 · 임대 · 고정 창 카운터 · 순위 · 발행/구독 · 상한 · 답 순서) + 내린 뒤 맡긴 요청.

using namespace sw;

namespace
{
    struct MemoryEphemeralFixture
    {
        MemoryEphemeralDatabase _database;
        MemoryEphemeralStore    _store;
        MemoryEphemeralStore    _secondStore;

        MemoryEphemeralFixture()
            : _database{}
            , _store{ &_database }
            , _secondStore{ &_database }
        {
            _database.setManualTimeMs( 1000000 );
        }

        IEphemeralStore& getStore() { return _store; }
        IEphemeralStore& getSecondStore() { return _secondStore; }
        void             advanceTimeMs( int64 deltaMs ) { _database.advanceTimeMs( deltaMs ); }

        string makeKey( const utf8* pName )
        {
            string key{ "contract:" };
            key += pName;
            return key;
        }
    };
} // namespace

SW_EPHEMERAL_STORE_CONTRACT_SUITE( EphemeralStoreMemoryTest, MemoryEphemeralFixture )

SW_TEST_CASE( EphemeralStoreMemoryTest, ShutdownAnswersUnavailableAndDropsSubscriptions )
{
    MemoryEphemeralDatabase database;
    MemoryEphemeralStore    publisher{ &database };
    MemoryEphemeralStore    subscriber{ &database };
    subscriber.subscribe( "bus:x" );
    subscriber.shutdown();
    EphemeralReply reply;
    SW_ASSERT_TRUE( test::EphemeralStoreContract::executeRequest( subscriber, EphemeralRequest::makeGet( "k" ), reply ) );
    SW_EXPECT_TRUE( reply._result == EphemeralResult::Unavailable );
    SW_ASSERT_TRUE( test::EphemeralStoreContract::executeRequest( publisher, EphemeralRequest::makePublish( "bus:x", vector<uint8>{ 1 } ), reply ) );
    SW_EXPECT_EQUAL( int64( 0 ), reply._integer ); // 내린 앞의 구독은 풀렸다
}
