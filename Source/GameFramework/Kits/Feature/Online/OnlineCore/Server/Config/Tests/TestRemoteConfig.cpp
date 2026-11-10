#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Observability/MetricRegistry.h"

#include "GameFramework/Runtime/Online/Audit/Server/ServiceAuditLog.h"
#include "GameFramework/Runtime/Online/Config/Server/RemoteConfig.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

// RemoteConfigTest — 원격 설정: 다시 읽기 뒤 값 · 해시, 출시 비율(0 · 10000 · 5000)과 계정마다 늘 같은 쪽, 클라이언트 묶음에 숨은 키 없음 · 잘린 묶음은 바꾸지 않음,
// 낡은 판의 바꾸기는 Conflict(감사 줄 포함), A/B 실험(변형 비중 규칙, 계정마다 같은 변형, 클라이언트도 같은 변형, 변형별 지표), 키 규칙 · 틀린 바꾸기 거절(nogpu).

using namespace sw;

namespace
{
    ServiceAuditEntry makeAudit( int64 timeMs )
    {
        ServiceAuditEntry audit;
        audit._actor   = "gm.0000000000000001";
        audit._action  = "config.set";
        audit._subject = "config";
        audit._timeMs  = timeMs;
        return audit;
    }

    RemoteConfigValue makeInteger( int64 value, bool bClientVisible )
    {
        RemoteConfigValue configValue;
        configValue._type           = RemoteConfigValueType::Integer;
        configValue._integer        = value;
        configValue._bClientVisible = bClientVisible ? SW_TRUE : SW_FALSE;
        return configValue;
    }

    RemoteConfigValue makeFlag( int32 rolloutBasisPoints )
    {
        RemoteConfigValue configValue;
        configValue._type               = RemoteConfigValueType::Flag;
        configValue._integer            = 1;
        configValue._rolloutBasisPoints = rolloutBasisPoints;
        configValue._bClientVisible     = SW_TRUE;
        return configValue;
    }

    int32 countEnabled( const RemoteConfig& config, string_view flag )
    {
        int32 enabledCount = 0;
        for ( uint64 accountID = 1; accountID <= 1000; ++accountID )
        {
            if ( config.isFeatureEnabled( flag, accountID ) )
                ++enabledCount;
        }
        return enabledCount;
    }
} // namespace

SW_TEST_CASE( RemoteConfigTest, ReloadReadsValuesAndTheSnapshotHash )
{
    MemoryServiceDatabase database;
    MemoryServiceStore    front{ &database };
    RemoteConfig          writer;
    const uint64          emptyHash = writer.getSnapshotHash();
    writer.submitSet( front, "account.minimum_build.windows", makeInteger( 42, true ), makeAudit( 1 ) );
    RemoteConfigValue text;
    text._type = RemoteConfigValueType::Text;
    text._text = "maintenance at 10:00";
    writer.submitSet( front, "notice.banner", text, makeAudit( 2 ) );
    (void)front.pollCompletions();
    SW_EXPECT_TRUE( writer.getLastSetResult() == ServiceStoreResult::Ok );
    SW_EXPECT_NOT_EQUAL( emptyHash, writer.getSnapshotHash() );
    SW_EXPECT_EQUAL( 2, database.countRecords( ServiceAuditLog::getTable() ) );

    RemoteConfig reader; // 다른 서버 — 다시 읽어야 안다
    reader.requestReload( front );
    SW_EXPECT_EQUAL( 1, reader.getPendingWorkCount() );
    (void)front.pollCompletions();
    SW_EXPECT_EQUAL( 0, reader.getPendingWorkCount() );
    int64 minimumBuild = 0;
    SW_EXPECT_TRUE( reader.findInteger( "account.minimum_build.windows", minimumBuild ) );
    SW_EXPECT_EQUAL( int64( 42 ), minimumBuild );
    string banner;
    SW_EXPECT_TRUE( reader.findText( "notice.banner", banner ) );
    SW_EXPECT_EQUAL( banner, string( "maintenance at 10:00" ) );
    SW_EXPECT_FALSE( reader.findInteger( "notice.banner", minimumBuild ) ); // 종류가 다르면 없는 것
    SW_EXPECT_EQUAL( writer.getSnapshotHash(), reader.getSnapshotHash() );
    front.shutdown();
}

SW_TEST_CASE( RemoteConfigTest, RolloutIsDeterministicPerAccount )
{
    MemoryServiceDatabase database;
    MemoryServiceStore    front{ &database };
    RemoteConfig          config;
    config.submitSet( front, "feature.none", makeFlag( 0 ), makeAudit( 1 ) );
    config.submitSet( front, "feature.all", makeFlag( 10000 ), makeAudit( 2 ) );
    config.submitSet( front, "feature.half", makeFlag( 5000 ), makeAudit( 3 ) );
    (void)front.pollCompletions();
    SW_EXPECT_EQUAL( 0, countEnabled( config, "feature.none" ) );
    SW_EXPECT_EQUAL( 1000, countEnabled( config, "feature.all" ) );
    const int32 halfCount = countEnabled( config, "feature.half" );
    SW_EXPECT_TRUE( 450 <= halfCount && halfCount <= 550 );
    for ( uint64 accountID = 1; accountID <= 50; ++accountID )
    {
        SW_EXPECT_EQUAL( config.isFeatureEnabled( "feature.half", accountID ), RemoteConfig::computeRolloutBucket( "feature.half", accountID ) < 5000 );
    }
    SW_EXPECT_TRUE( config.isFeatureEnabled( "feature.missing", 7, true ) ); // 없는 플래그는 기본값
    SW_EXPECT_FALSE( config.isFeatureEnabled( "feature.missing", 7, false ) );
    front.shutdown();
}

SW_TEST_CASE( RemoteConfigTest, ExperimentAssignsStableVariantsAndCountsThem )
{
    vector<RemoteConfigVariant> listVariant;
    SW_EXPECT_TRUE( RemoteConfig::parseVariants( "control:5000;cheap_gems:5000", listVariant ) );
    SW_EXPECT_EQUAL( listVariant.size(), size_t( 2 ) );
    SW_EXPECT_FALSE( RemoteConfig::parseVariants( "control:5000;b:4000", listVariant ) ); // 합이 10000 이 아니다
    SW_EXPECT_FALSE( RemoteConfig::parseVariants( "control:5000;control:5000", listVariant ) );
    SW_EXPECT_FALSE( RemoteConfig::parseVariants( "Control:10000", listVariant ) );
    SW_EXPECT_FALSE( RemoteConfig::parseVariants( "a:0;b:10000", listVariant ) );
    SW_EXPECT_FALSE( RemoteConfig::parseVariants( "", listVariant ) );
    SW_EXPECT_FALSE( RemoteConfig::parseVariants( "a:x;b:10000", listVariant ) ); // 비중이 수가 아니다

    MemoryServiceDatabase database;
    MemoryServiceStore    front{ &database };
    RemoteConfig          server;
    RemoteConfigValue     experiment;
    experiment._type           = RemoteConfigValueType::Experiment;
    experiment._text           = "control:5000;cheap_gems:5000";
    experiment._bClientVisible = SW_TRUE;
    server.submitSet( front, "exp.gem_price", experiment, makeAudit( 1 ) );
    RemoteConfigValue broken = experiment;
    broken._text             = "control:9999";
    SW_EXPECT_LOG( "RemoteConfig.ChangeRejected" ); // 틀린 실험 값은 쓰기 전에 거절된다
    server.submitSet( front, "exp.broken", broken, makeAudit( 2 ) );
    (void)front.pollCompletions();
    string variant;
    SW_EXPECT_FALSE( server.findVariant( "exp.broken", 1, variant ) );
    SW_EXPECT_FALSE( server.findVariant( "exp.missing", 1, variant ) );
    int32 cheapCount = 0;
    for ( uint64 accountID = 1; accountID <= 1000; ++accountID )
    {
        SW_REQUIRE_TRUE( server.findVariant( "exp.gem_price", accountID, variant ) );
        cheapCount += variant == "cheap_gems" ? 1 : 0;
    }
    SW_EXPECT_TRUE( 450 <= cheapCount && cheapCount <= 550 );

    BitWriter snapshot;
    server.writeClientSnapshot( snapshot );
    const vector<uint8> snapshotBytes = snapshot.releaseBytes();
    RemoteConfig        client;
    BitReader           reader( snapshotBytes.data(), static_cast<int32>( snapshotBytes.size() ) );
    SW_REQUIRE_TRUE( client.readClientSnapshot( reader ) );
    string clientVariant;
    SW_REQUIRE_TRUE( server.findVariant( "exp.gem_price", 77, variant ) );
    SW_REQUIRE_TRUE( client.findVariant( "exp.gem_price", 77, clientVariant ) );
    SW_EXPECT_EQUAL( variant, clientVariant ); // 클라이언트도 같은 변형이다

    MetricRegistry                registry;
    RemoteConfigExperimentMetrics metrics{ &registry };
    SW_EXPECT_TRUE( metrics.recordExposure( server, "exp.gem_price", 77 ) );
    SW_EXPECT_TRUE( metrics.recordEvent( server, "exp.gem_price", 77, "purchase" ) );
    SW_EXPECT_FALSE( metrics.recordEvent( server, "exp.gem_price", 77, "Bad Event" ) );
    SW_EXPECT_FALSE( metrics.recordExposure( server, "exp.missing", 77 ) );
    string text;
    registry.writePrometheusText( text );
    SW_EXPECT_TRUE( text.find( "remote_config_experiment_total" ) != string::npos );
    SW_EXPECT_TRUE( text.find( string( "variant=\"" ) + variant + "\"" ) != string::npos );
    front.shutdown();
}

SW_TEST_CASE( RemoteConfigTest, ClientSnapshotCarriesOnlyVisibleKeys )
{
    MemoryServiceDatabase database;
    MemoryServiceStore    front{ &database };
    RemoteConfig          server;
    server.submitSet( front, "client.max_party", makeInteger( 4, true ), makeAudit( 1 ) );
    server.submitSet( front, "server.secret_rate", makeInteger( 99, false ), makeAudit( 2 ) );
    (void)front.pollCompletions();
    SW_REQUIRE_EQUAL( 2, server.getValueCount() );

    BitWriter writer;
    server.writeClientSnapshot( writer );
    const vector<uint8> bytes = writer.releaseBytes();
    BitReader           reader( bytes.data(), static_cast<int32>( bytes.size() ) );
    RemoteConfig        client;
    SW_REQUIRE_TRUE( client.readClientSnapshot( reader ) );
    int64 value = 0;
    SW_EXPECT_TRUE( client.findInteger( "client.max_party", value ) );
    SW_EXPECT_FALSE( client.findInteger( "server.secret_rate", value ) );
    SW_EXPECT_EQUAL( 1, client.getValueCount() );
    SW_EXPECT_EQUAL( server.getSnapshotHash(), client.getSnapshotHash() ); // 해시는 보이는 묶음의 것

    // 잘린 묶음은 거절하고 앞 값을 그대로 둔다
    BitReader truncated( bytes.data(), static_cast<int32>( bytes.size() ) - 1 );
    SW_EXPECT_FALSE( client.readClientSnapshot( truncated ) );
    SW_EXPECT_EQUAL( 1, client.getValueCount() );
    RemoteConfig broken;
    BitReader    truncatedAgain( bytes.data(), static_cast<int32>( bytes.size() ) - 1 );
    SW_EXPECT_FALSE( broken.readClientSnapshot( truncatedAgain ) );
    SW_EXPECT_EQUAL( 0, broken.getValueCount() );
    front.shutdown();
}

SW_TEST_CASE( RemoteConfigTest, StaleSetConflictsAndOnlyTheWinnerIsWritten )
{
    MemoryServiceDatabase database;
    MemoryServiceStore    frontA{ &database };
    MemoryServiceStore    frontB{ &database };
    RemoteConfig          configA;
    RemoteConfig          configB;
    configA.requestReload( frontA );
    configB.requestReload( frontB );
    (void)frontA.pollCompletions();
    (void)frontB.pollCompletions();

    configA.submitSet( frontA, "feature.trade_enabled", makeFlag( 10000 ), makeAudit( 1 ) );
    (void)frontA.pollCompletions();
    SW_EXPECT_TRUE( configA.getLastSetResult() == ServiceStoreResult::Ok );
    configB.submitSet( frontB, "feature.trade_enabled", makeFlag( 0 ), makeAudit( 2 ) ); // B 는 그새 바뀐 것을 모른다
    (void)frontB.pollCompletions();
    SW_EXPECT_TRUE( configB.getLastSetResult() == ServiceStoreResult::Conflict );
    SW_EXPECT_EQUAL( 1, database.countRecords( ServiceAuditLog::getTable() ) ); // 진 쪽은 감사 줄도 없다

    configB.requestReload( frontB ); // 다시 읽으면(서버는 주기로 다시 읽는다) 이긴 값이 보이고, 다시 바꿀 수 있다
    (void)frontB.pollCompletions();
    SW_EXPECT_TRUE( configB.isFeatureEnabled( "feature.trade_enabled", 7 ) );
    configB.submitSet( frontB, "feature.trade_enabled", makeFlag( 0 ), makeAudit( 3 ) );
    (void)frontB.pollCompletions();
    SW_EXPECT_TRUE( configB.getLastSetResult() == ServiceStoreResult::Ok );
    SW_EXPECT_FALSE( configB.isFeatureEnabled( "feature.trade_enabled", 7 ) );
    frontA.shutdown();
    frontB.shutdown();
}

/** @brief [RemoteConfigTest] 키는 `[0-9a-z_.]` 1 자 이상이다. 틀린 키 · 범위 밖 출시 비율 · 상한을 넘는 글은 쓰기 전에 거절되고 아무것도 바뀌지 않는다 */
SW_TEST_CASE( RemoteConfigTest, InvalidKeysAndValuesAreRejectedBeforeTheWrite )
{
    SW_EXPECT_TRUE( RemoteConfig::isValidKey( "feature.trade_enabled" ) );
    SW_EXPECT_TRUE( RemoteConfig::isValidKey( "a" ) );
    SW_EXPECT_FALSE( RemoteConfig::isValidKey( "" ) );
    SW_EXPECT_FALSE( RemoteConfig::isValidKey( "Feature.Trade" ) );
    SW_EXPECT_FALSE( RemoteConfig::isValidKey( "feature trade" ) );
    SW_EXPECT_FALSE( RemoteConfig::isValidKey( "feature-trade" ) );

    MemoryServiceDatabase database;
    MemoryServiceStore    front{ &database };
    RemoteConfig          config;
    RemoteConfigValue     tooLong;
    tooLong._type = RemoteConfigValueType::Text;
    tooLong._text.assign( static_cast<size_t>( RemoteConfigValue::kMaxTextSize ) + 1, 'x' );
    SW_EXPECT_LOG( "RemoteConfig.ChangeRejected" );
    config.submitSet( front, "Bad.Key", makeInteger( 1, true ), makeAudit( 1 ) );
    config.submitSet( front, "feature.over", makeFlag( RemoteConfigValue::kFullRolloutPoints + 1 ), makeAudit( 2 ) );
    config.submitSet( front, "notice.long", tooLong, makeAudit( 3 ) );
    (void)front.pollCompletions();
    SW_EXPECT_EQUAL( 0, config.getValueCount() );
    SW_EXPECT_EQUAL( 0, database.countRecords( RemoteConfig::getTable() ) );
    SW_EXPECT_EQUAL( 0, database.countRecords( ServiceAuditLog::getTable() ) );
    front.shutdown();
}
