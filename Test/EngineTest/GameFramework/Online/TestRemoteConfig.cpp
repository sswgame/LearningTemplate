#include "pch.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Config/RemoteConfig.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"

#include "TestFramework/TestFramework.h"

// 원격 설정 — 다시 읽기 뒤 값 · 해시, 출시 비율(0 · 10000 · 5000)과 계정마다 늘 같은 쪽, 클라이언트 묶음에 숨은 키 없음, 낡은 판의 바꾸기는 Conflict(감사 줄 · 버스 알림 포함).

using namespace sw;

namespace
{
    struct TestRemoteConfigInternal
    {
        static ServiceAuditEntry makeAudit( int64 timeMs )
        {
            ServiceAuditEntry audit;
            audit._actor   = "gm.0000000000000001";
            audit._action  = "config.set";
            audit._subject = "config";
            audit._timeMs  = timeMs;
            return audit;
        }

        static RemoteConfigValue makeInteger( int64 value, bool bClientVisible )
        {
            RemoteConfigValue configValue;
            configValue._type           = RemoteConfigValueType::Integer;
            configValue._integer        = value;
            configValue._bClientVisible = bClientVisible ? SW_TRUE : SW_FALSE;
            return configValue;
        }

        static RemoteConfigValue makeFlag( int32 rolloutBasisPoints )
        {
            RemoteConfigValue configValue;
            configValue._type               = RemoteConfigValueType::Flag;
            configValue._integer            = 1;
            configValue._rolloutBasisPoints = rolloutBasisPoints;
            configValue._bClientVisible     = SW_TRUE;
            return configValue;
        }

        static int32 countEnabled( const RemoteConfig& config, string_view flag )
        {
            int32 enabledCount = 0;
            for ( uint64 accountId = 1; accountId <= 1000; ++accountId )
            {
                if ( config.isFeatureEnabled( flag, accountId ) )
                    ++enabledCount;
            }
            return enabledCount;
        }
    };
} // namespace

SW_TEST_CASE( RemoteConfigTest, ReloadReadsValuesAndTheSnapshotHash )
{
    using Internal = TestRemoteConfigInternal;
    MemoryServiceDatabase database;
    MemoryServiceStore    front{ &database };
    RemoteConfig          writer;
    const uint64          emptyHash = writer.getSnapshotHash();
    writer.submitSet( front, nullptr, "account.minimum_build.windows", Internal::makeInteger( 42, true ), Internal::makeAudit( 1 ) );
    RemoteConfigValue text;
    text._type = RemoteConfigValueType::Text;
    text._text = "maintenance at 10:00";
    writer.submitSet( front, nullptr, "notice.banner", text, Internal::makeAudit( 2 ) );
    front.pollCompletions();
    SW_EXPECT_TRUE( writer.getLastSetResult() == ServiceStoreResult::Ok );
    SW_EXPECT_NOT_EQUAL( emptyHash, writer.getSnapshotHash() );
    SW_EXPECT_EQUAL( 2, database.countRecords( ServiceAuditLog::getTable() ) );

    RemoteConfig reader; // 다른 서버 — 다시 읽어야 안다
    reader.requestReload( front );
    SW_EXPECT_EQUAL( 1, reader.getPendingWorkCount() );
    front.pollCompletions();
    SW_EXPECT_EQUAL( 0, reader.getPendingWorkCount() );
    int64 minimumBuild = 0;
    SW_EXPECT_TRUE( reader.findInteger( "account.minimum_build.windows", minimumBuild ) );
    SW_EXPECT_EQUAL( int64( 42 ), minimumBuild );
    string banner;
    SW_EXPECT_TRUE( reader.findText( "notice.banner", banner ) );
    SW_EXPECT_TRUE( banner == "maintenance at 10:00" );
    SW_EXPECT_FALSE( reader.findInteger( "notice.banner", minimumBuild ) ); // 종류가 다르면 없는 것
    SW_EXPECT_EQUAL( writer.getSnapshotHash(), reader.getSnapshotHash() );
}

SW_TEST_CASE( RemoteConfigTest, RolloutIsDeterministicPerAccount )
{
    using Internal = TestRemoteConfigInternal;
    MemoryServiceDatabase database;
    MemoryServiceStore    front{ &database };
    RemoteConfig          config;
    config.submitSet( front, nullptr, "feature.none", Internal::makeFlag( 0 ), Internal::makeAudit( 1 ) );
    config.submitSet( front, nullptr, "feature.all", Internal::makeFlag( 10000 ), Internal::makeAudit( 2 ) );
    config.submitSet( front, nullptr, "feature.half", Internal::makeFlag( 5000 ), Internal::makeAudit( 3 ) );
    front.pollCompletions();
    SW_EXPECT_EQUAL( 0, Internal::countEnabled( config, "feature.none" ) );
    SW_EXPECT_EQUAL( 1000, Internal::countEnabled( config, "feature.all" ) );
    const int32 halfCount = Internal::countEnabled( config, "feature.half" );
    SW_EXPECT_TRUE( 450 <= halfCount && halfCount <= 550 );
    for ( uint64 accountId = 1; accountId <= 50; ++accountId )
        SW_EXPECT_EQUAL( config.isFeatureEnabled( "feature.half", accountId ), config.isFeatureEnabled( "feature.half", accountId ) );
    SW_EXPECT_TRUE( config.isFeatureEnabled( "feature.missing", 7, true ) ); // 없는 플래그는 기본값
    SW_EXPECT_FALSE( config.isFeatureEnabled( "feature.missing", 7, false ) );
}

SW_TEST_CASE( RemoteConfigTest, ClientSnapshotCarriesOnlyVisibleKeys )
{
    using Internal = TestRemoteConfigInternal;
    MemoryServiceDatabase database;
    MemoryServiceStore    front{ &database };
    RemoteConfig          server;
    server.submitSet( front, nullptr, "client.max_party", Internal::makeInteger( 4, true ), Internal::makeAudit( 1 ) );
    server.submitSet( front, nullptr, "server.secret_rate", Internal::makeInteger( 99, false ), Internal::makeAudit( 2 ) );
    front.pollCompletions();
    SW_ASSERT_EQUAL( 2, server.getValueCount() );

    BitWriter writer;
    server.writeClientSnapshot( writer );
    const vector<uint8> bytes = writer.releaseBytes();
    BitReader           reader( bytes.data(), static_cast<int32>( bytes.size() ) );
    RemoteConfig        client;
    SW_ASSERT_TRUE( client.readClientSnapshot( reader ) );
    int64 value = 0;
    SW_EXPECT_TRUE( client.findInteger( "client.max_party", value ) );
    SW_EXPECT_FALSE( client.findInteger( "server.secret_rate", value ) );
    SW_EXPECT_EQUAL( 1, client.getValueCount() );
    SW_EXPECT_EQUAL( server.getSnapshotHash(), client.getSnapshotHash() ); // 해시는 보이는 묶음의 것

    BitReader    truncated( bytes.data(), static_cast<int32>( bytes.size() ) - 1 );
    RemoteConfig broken;
    SW_EXPECT_FALSE( broken.readClientSnapshot( truncated ) );
    SW_EXPECT_EQUAL( 0, broken.getValueCount() );
}

SW_TEST_CASE( RemoteConfigTest, StaleSetConflictsAndOnlyTheWinnerIsAnnounced )
{
    using Internal = TestRemoteConfigInternal;
    MemoryServiceDatabase database;
    MemoryServiceStore    frontA{ &database };
    MemoryServiceStore    frontB{ &database };
    LocalServerBusHub     hub;
    LocalServerBus        busA{ &hub, 1 };
    LocalServerBus        busB{ &hub, 2 };
    busB.subscribe( "config.changed" );
    RemoteConfig configA;
    RemoteConfig configB;
    configA.requestReload( frontA );
    configB.requestReload( frontB );
    frontA.pollCompletions();
    frontB.pollCompletions();

    configA.submitSet( frontA, &busA, "feature.trade_enabled", Internal::makeFlag( 10000 ), Internal::makeAudit( 1 ) );
    frontA.pollCompletions();
    SW_EXPECT_TRUE( configA.getLastSetResult() == ServiceStoreResult::Ok );
    configB.submitSet( frontB, &busB, "feature.trade_enabled", Internal::makeFlag( 0 ), Internal::makeAudit( 2 ) ); // B 는 그새 바뀐 것을 모른다
    frontB.pollCompletions();
    SW_EXPECT_TRUE( configB.getLastSetResult() == ServiceStoreResult::Conflict );
    SW_EXPECT_EQUAL( 1, database.countRecords( ServiceAuditLog::getTable() ) ); // 진 쪽은 감사 줄도 없다

    vector<ServerBusMessage> listMessage;
    SW_ASSERT_EQUAL( 1, busB.pollMessages( listMessage ) );
    const string key( reinterpret_cast<const utf8*>( listMessage[0]._bytes.data() ), listMessage[0]._bytes.size() );
    SW_EXPECT_TRUE( key == "feature.trade_enabled" );

    configB.requestReload( frontB ); // 알림을 받고 다시 읽으면 이긴 값이 보이고, 다시 바꿀 수 있다
    frontB.pollCompletions();
    SW_EXPECT_TRUE( configB.isFeatureEnabled( "feature.trade_enabled", 7 ) );
    configB.submitSet( frontB, &busB, "feature.trade_enabled", Internal::makeFlag( 0 ), Internal::makeAudit( 3 ) );
    frontB.pollCompletions();
    SW_EXPECT_TRUE( configB.getLastSetResult() == ServiceStoreResult::Ok );
    SW_EXPECT_FALSE( configB.isFeatureEnabled( "feature.trade_enabled", 7 ) );
}
