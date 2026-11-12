#include "pch.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처가 엔진 서비스(`AssetPaths`)를 직접 쓴다

#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Character/AI/Director/AIDirector.h"
#include "GameFramework/Runtime/Character/AI/Director/AIDirectorProfile.h"

using namespace sw;

namespace
{
    /** @brief 쌓기 → 절정 → 쉼을 도는 프로필과 감독, 받은 이벤트입니다. */
    struct DirectorRig
    {
        static constexpr const utf8* kProfileXML = R"(<AIDirector startPhase="BuildUp">
  <Intensity max="1" decayPerSecond="0.5" decayDelay="1">
    <Signal id="hit" kind="Impulse" scale="0.1" max="0.5" combat="true"/>
    <Signal id="near" kind="Rate" scale="0.1" max="0.2"/>
    <Signal id="low" kind="Level" scale="0.3"/>
  </Intensity>
  <Phase id="BuildUp" spawnScale="1">
    <Exit to="Peak" intensityAbove="0.8"/>
    <Exit to="Peak" minTime="90"/>
  </Phase>
  <Phase id="Peak" spawnScale="2"><Exit to="Relax" minTime="5"/></Phase>
  <Phase id="Relax" spawnScale="0" rewardScale="2"><Exit to="BuildUp" intensityBelow="0.25" calmFor="2" minTime="3"/></Phase>
  <Pool id="horde" trigger="PhaseEnter" phase="Peak" picks="2">
    <Encounter id="swarm" count="6" scale="1.5"/>
  </Pool>
  <Pool id="ambient" trigger="Interval" interval="10" pacing="Relax">
    <Encounter id="stranger" cooldown="100"/>
  </Pool>
  <Pool id="supplies" kind="Reward" trigger="Budget" perMinute="60" maxBudget="4" need="low" needScale="1" pacing="Relax">
    <Encounter id="ammo" cost="3"/>
  </Pool>
</AIDirector>)";

        AIDirectorProfile       _profile;
        AIDirector              _director;
        vector<AIDirectorEvent> _listEvent;

        explicit DirectorRig( uint32 seed = 7 )
        {
            SW_REQUIRE_TRUE( _profile.loadFromXMLText( kProfileXML, "DirectorTest" ) );
            _director.initialize( &_profile, nullptr, seed );
        }

        void run( float32 seconds, float32 step = 0.1f )
        {
            const int32 frameCount = static_cast<int32>( seconds / step + 0.5f );
            for ( int32 frame = 0; frame < frameCount; ++frame )
            {
                _director.update( step );
            }
            _director.pollEvents( _listEvent );
        }

        int32 count( AIDirectorEventKind kind, const utf8* pID ) const
        {
            const hashed_string id( pID );
            int32               total = 0;
            for ( const AIDirectorEvent& event : _listEvent )
            {
                total += event._kind == kind && event._id == id ? 1 : 0;
            }
            return total;
        }

        void spike() { (void)_director.getBuiltinIntensityModel().addSignal( hashed_string( "hit" ), 100.0f ); }

        static vector<uint8> save( const AIDirector& director )
        {
            Archive archive;
            director.writeState( archive );
            return vector<uint8>( archive.getData(), archive.getData() + archive.getSize() );
        }
    };

    /** @brief 단일 풀 프로필을 읽는 도우미입니다. */
    struct DirectorProfileTestUtil
    {
        static string makePoolProfile( string_view poolXML )
        {
            return string( "<AIDirector startPhase=\"A\"><Phase id=\"A\"><Exit to=\"B\" minTime=\"1000\"/></Phase><Phase id=\"B\"/>" ) + string( poolXML ) + "</AIDirector>";
        }
    };
} // namespace

// ---- 긴장도 모델 ----

SW_TEST_CASE( AIDirectorIntensityTest, ImpulseCapsAndDecaysAfterTheDelay )
{
    DirectorRig               rig;
    AIDirectorIntensityModel& model = rig._director.getBuiltinIntensityModel();
    SW_EXPECT_FALSE( model.addSignal( hashed_string( "nothing" ), 1.0f ) );
    SW_EXPECT_FALSE( model.addSignal( hashed_string( "near" ), 1.0f ) ); // Rate 는 setSignal 이다
    SW_EXPECT_FALSE( model.setSignal( hashed_string( "hit" ), 1.0f ) );  // Impulse 는 addSignal 이다

    SW_REQUIRE_TRUE( model.addSignal( hashed_string( "hit" ), 2.0f ) );
    SW_EXPECT_NEAR( 0.2f, model.getIntensity(), 1.0e-4f ); // 2 x scale 0.1
    SW_REQUIRE_TRUE( model.addSignal( hashed_string( "hit" ), 100.0f ) );
    SW_EXPECT_NEAR( 0.7f, model.getIntensity(), 1.0e-4f ); // 한 번에 최대 0.5 만 더한다
    SW_EXPECT_NEAR( 0.0f, model.getCalmSeconds(), 1.0e-4f );

    model.update( 1.0f ); // 지연 안이라 식지 않는다
    SW_EXPECT_NEAR( 0.7f, model.getIntensity(), 1.0e-4f );
    model.update( 0.5f ); // 지연을 넘긴 뒤 0.5 초만 식는다
    SW_EXPECT_NEAR( 0.45f, model.getIntensity(), 1.0e-4f );
    model.update( 10.0f );
    SW_EXPECT_NEAR( 0.0f, model.getIntensity(), 1.0e-4f );
}

SW_TEST_CASE( AIDirectorIntensityTest, RateAccumulatesAndLevelIsAFloor )
{
    DirectorRig               rig;
    AIDirectorIntensityModel& model = rig._director.getBuiltinIntensityModel();
    SW_REQUIRE_TRUE( model.addSignal( hashed_string( "hit" ), 1.0f ) ); // 싸움 신호가 평온 시계를 되돌려 식기 지연 안에 든다
    SW_REQUIRE_TRUE( model.setSignal( hashed_string( "near" ), 100.0f ) );
    model.update( 0.5f ); // 초당 최대 0.2
    SW_EXPECT_NEAR( 0.2f, model.getStress(), 1.0e-4f );
    SW_REQUIRE_TRUE( model.setSignal( hashed_string( "near" ), 0.0f ) );

    SW_REQUIRE_TRUE( model.setSignal( hashed_string( "low" ), 3.0f ) );
    SW_EXPECT_NEAR( 0.9f, model.getIntensity(), 1.0e-4f ); // 바닥이 스트레스보다 크다
    SW_EXPECT_NEAR( 3.0f, model.getSignal( hashed_string( "low" ) ), 1.0e-4f );
    SW_EXPECT_NEAR( 0.0f, model.getSignal( hashed_string( "nothing" ) ), 0.0f );

    model.reset();
    SW_EXPECT_NEAR( 0.0f, model.getStress(), 0.0f );
    SW_EXPECT_NEAR( 0.0f, model.getSignal( hashed_string( "low" ) ), 0.0f );
}

// ---- 단계 ----

SW_TEST_CASE( AIDirectorPhaseTest, ExitsFollowIntensityTimeAndCalmAndCountCycles )
{
    DirectorRig rig;
    SW_EXPECT_TRUE( rig._director.getPhase() == hashed_string( "BuildUp" ) );
    rig.run( 1.0f );
    SW_EXPECT_TRUE( rig._director.getPhase() == hashed_string( "BuildUp" ) ); // 긴장도가 낮고 시간도 안 찼다

    rig.spike();
    rig.spike();
    rig.run( 0.1f );
    SW_EXPECT_TRUE( rig._director.getPhase() == hashed_string( "Peak" ) );
    SW_EXPECT_NEAR( 0.0f, rig._director.getPhaseTime(), 0.11f );
    SW_EXPECT_EQUAL( 0, rig._director.getCycle() );

    rig.run( 5.1f );
    SW_EXPECT_TRUE( rig._director.getPhase() == hashed_string( "Relax" ) );

    rig.run( 2.5f );
    rig.spike(); // 시간이 차도 싸움 뒤 2 초가 지나기 전에는 쉼에서 나가지 못한다
    rig.run( 1.0f );
    SW_EXPECT_TRUE( rig._director.getPhase() == hashed_string( "Relax" ) );
    rig.run( 3.0f );
    SW_EXPECT_TRUE( rig._director.getPhase() == hashed_string( "BuildUp" ) );
    SW_EXPECT_EQUAL( 1, rig._director.getCycle() ); // 시작 단계로 돌아왔다
}

SW_TEST_CASE( AIDirectorPhaseTest, SecondExitIsATimeLimit )
{
    DirectorRig rig;
    rig.run( 89.0f, 1.0f );
    SW_EXPECT_TRUE( rig._director.getPhase() == hashed_string( "BuildUp" ) );
    rig.run( 2.0f, 1.0f );
    SW_EXPECT_TRUE( rig._director.getPhase() == hashed_string( "Peak" ) );
}

SW_TEST_CASE( AIDirectorPhaseTest, ForcePhaseAndNegativeTimeAndRestart )
{
    DirectorRig rig;
    SW_EXPECT_FALSE( rig._director.forcePhase( hashed_string( "Nothing" ) ) );
    SW_REQUIRE_TRUE( rig._director.forcePhase( hashed_string( "Relax" ) ) );
    SW_EXPECT_TRUE( rig._director.getPhase() == hashed_string( "Relax" ) );

    const float32 time = rig._director.getTime();
    rig._director.update( -1.0f );
    SW_EXPECT_NEAR( time, rig._director.getTime(), 0.0f );

    rig.run( 1.0f );
    rig._director.restart();
    SW_EXPECT_TRUE( rig._director.getPhase() == hashed_string( "BuildUp" ) );
    SW_EXPECT_NEAR( 0.0f, rig._director.getTime(), 0.0f );
    SW_EXPECT_EQUAL( 0, rig._director.getCycle() );
}

SW_TEST_CASE( AIDirectorPhaseTest, NoProfileDoesNothing )
{
    AIDirector director;
    director.update( 1.0f );
    SW_EXPECT_EQUAL( -1, director.getPhaseIndex() );
    SW_EXPECT_TRUE( director.getPhase().empty() );
    SW_EXPECT_FALSE( director.forcePhase( hashed_string( "A" ) ) );
    string text;
    director.explain( text );
    SW_EXPECT_TRUE( text.find( "not initialized" ) != string::npos );
}

// ---- 풀 ----

SW_TEST_CASE( AIDirectorPoolTest, PhaseEnterPicksOnEntryWithDataValues )
{
    DirectorRig rig;
    SW_REQUIRE_TRUE( rig._director.forcePhase( hashed_string( "Peak" ) ) );
    rig.run( 0.1f );
    SW_EXPECT_EQUAL( 2, rig.count( AIDirectorEventKind::Encounter, "swarm" ) ); // picks = 2
    for ( const AIDirectorEvent& event : rig._listEvent )
    {
        if ( event._kind != AIDirectorEventKind::Encounter )
            continue;
        SW_EXPECT_TRUE( event._source == hashed_string( "horde" ) );
        SW_EXPECT_EQUAL( 6, event._count );
        SW_EXPECT_NEAR( 1.5f, event._scale, 0.0f );
    }
}

SW_TEST_CASE( AIDirectorPoolTest, IntervalPoolRunsOnlyInItsPacingAndRespectsCooldown )
{
    DirectorRig rig;
    rig.run( 30.0f, 1.0f ); // 쌓기 단계에는 돌지 않는다
    SW_EXPECT_EQUAL( 0, rig.count( AIDirectorEventKind::Encounter, "stranger" ) );

    SW_REQUIRE_TRUE( rig._director.forcePhase( hashed_string( "Relax" ) ) );
    (void)rig._director.getBuiltinIntensityModel().setSignal( hashed_string( "low" ), 3.0f ); // 긴장도를 잡아 쉼에 머문다
    rig.run( 11.0f, 1.0f );
    SW_EXPECT_EQUAL( 1, rig.count( AIDirectorEventKind::Encounter, "stranger" ) );
    rig.run( 30.0f, 1.0f ); // 항목 쿨다운 100 초 동안 다시 나오지 않는다
    SW_EXPECT_EQUAL( 1, rig.count( AIDirectorEventKind::Encounter, "stranger" ) );

    const int32  poolIndex = rig._director.findPoolIndex( hashed_string( "ambient" ) ).value();
    const uint32 mask      = rig._director.computeBlockMask( poolIndex, 0 );
    SW_EXPECT_TRUE( ( mask & AIDirectorBlock::kCooldown ) != 0 );
    SW_EXPECT_FALSE( rig._director.findPoolIndex( hashed_string( "nothing" ) ).has_value() );
}

SW_TEST_CASE( AIDirectorPoolTest, BudgetPoolPaysCostAndScalesWithPhaseAndNeed )
{
    DirectorRig rig;
    SW_REQUIRE_TRUE( rig._director.forcePhase( hashed_string( "Relax" ) ) );
    AIDirectorIntensityModel& model     = rig._director.getBuiltinIntensityModel();
    const int32               poolIndex = rig._director.findPoolIndex( hashed_string( "supplies" ) ).value();

    (void)model.setSignal( hashed_string( "low" ), 2.0f ); // 필요 2 → 배율 1 + 1 x 2 = 3, 단계 보상 배율 2, 초당 1 예산
    rig._director.update( 0.1f );
    SW_EXPECT_NEAR( 0.6f, rig._director.getPoolBudget( poolIndex ), 1.0e-3f );

    rig.run( 0.5f, 0.1f ); // 3.6 에 닿기 전 3 을 넘는 순간 낸다
    SW_EXPECT_EQUAL( 1, rig.count( AIDirectorEventKind::Reward, "ammo" ) );
    SW_EXPECT_TRUE( rig._director.getPoolBudget( poolIndex ) < 3.0f );
    for ( const AIDirectorEvent& event : rig._listEvent )
    {
        if ( event._kind == AIDirectorEventKind::Reward )
            SW_EXPECT_NEAR( 3.0f, event._cost, 0.0f );
    }
    SW_EXPECT_NEAR( 0.0f, rig._director.getPoolBudget( 99 ), 0.0f );
}

SW_TEST_CASE( AIDirectorPoolTest, BlockMaskNamesEveryReason )
{
    const string      xml = DirectorProfileTestUtil::makePoolProfile( R"(<Pool id="p" trigger="Budget" maxBudget="1" pacing="A">
    <Encounter id="free"/>
    <Encounter id="weightless" weight="0"/>
    <Encounter id="onlyB" pacing="B"/>
    <Encounter id="capped" maxCount="0"/>
    <Encounter id="late" minCycle="1"/>
    <Encounter id="wait" minTime="50"/>
    <Encounter id="tense" minIntensity="0.5"/>
    <Encounter id="forest" areas="Forest"/>
    <Encounter id="expensive" cost="9"/>
  </Pool>)" );
    AIDirectorProfile profile;
    SW_REQUIRE_TRUE( profile.loadFromXMLText( xml, "DirectorTest" ) );
    AIDirector director;
    director.initialize( &profile, nullptr, 1 );

    SW_EXPECT_EQUAL( 0u, director.computeBlockMask( 0, 0 ) );
    SW_EXPECT_EQUAL( AIDirectorBlock::kWeight, director.computeBlockMask( 0, 1 ) );
    SW_EXPECT_EQUAL( AIDirectorBlock::kPacing, director.computeBlockMask( 0, 2 ) );
    SW_EXPECT_EQUAL( AIDirectorBlock::kMaxCount, director.computeBlockMask( 0, 3 ) );
    SW_EXPECT_EQUAL( AIDirectorBlock::kCycle, director.computeBlockMask( 0, 4 ) );
    SW_EXPECT_EQUAL( AIDirectorBlock::kTime, director.computeBlockMask( 0, 5 ) );
    SW_EXPECT_EQUAL( AIDirectorBlock::kIntensity, director.computeBlockMask( 0, 6 ) );
    SW_EXPECT_EQUAL( AIDirectorBlock::kArea, director.computeBlockMask( 0, 7 ) );
    SW_EXPECT_EQUAL( AIDirectorBlock::kCost, director.computeBlockMask( 0, 8 ) );
    SW_EXPECT_EQUAL( AIDirectorBlock::kWeight, director.computeBlockMask( 9, 0 ) ); // 없는 풀
    SW_EXPECT_EQUAL( AIDirectorBlock::kWeight, director.computeBlockMask( 0, 99 ) );

    AIDirectorContext context;
    context._listAreaTag.push_back( hashed_string( "Forest" ) );
    director.setContext( context );
    SW_EXPECT_EQUAL( 0u, director.computeBlockMask( 0, 7 ) );

    string text;
    director.explain( text );
    SW_EXPECT_TRUE( text.find( "phase 'A'" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "[minTime 1000.00]" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "- weightless" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "+ free" ) != string::npos );
}

SW_TEST_CASE( AIDirectorPoolTest, MaxCountStopsAPoolForTheRun )
{
    const string      xml = DirectorProfileTestUtil::makePoolProfile( R"(<Pool id="p" trigger="Interval" interval="1"><Encounter id="once" maxCount="2"/></Pool>)" );
    AIDirectorProfile profile;
    SW_REQUIRE_TRUE( profile.loadFromXMLText( xml, "DirectorTest" ) );
    AIDirector director;
    director.initialize( &profile, nullptr, 1 );
    vector<AIDirectorEvent> listEvent;
    for ( int32 frame = 0; frame < 20; ++frame )
    {
        director.update( 1.0f );
    }
    director.pollEvents( listEvent );
    int32 picks = 0;
    for ( const AIDirectorEvent& event : listEvent )
    {
        picks += event._kind == AIDirectorEventKind::Encounter ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 2, picks );
}

// 긴 멈춤 한 번이 밀린 틱을 한꺼번에 쏟지 않는다.
SW_TEST_CASE( AIDirectorPoolTest, LongFrameIsCappedPerUpdate )
{
    const string      xml = DirectorProfileTestUtil::makePoolProfile( R"(<Pool id="p" trigger="Interval" interval="1"><Encounter id="tick"/></Pool>)" );
    AIDirectorProfile profile;
    SW_REQUIRE_TRUE( profile.loadFromXMLText( xml, "DirectorTest" ) );
    AIDirector director;
    director.initialize( &profile, nullptr, 1 );
    director.update( 500.0f );
    vector<AIDirectorEvent> listEvent;
    director.pollEvents( listEvent );
    int32 picks = 0;
    for ( const AIDirectorEvent& event : listEvent )
    {
        picks += event._kind == AIDirectorEventKind::Encounter ? 1 : 0;
    }
    SW_EXPECT_EQUAL( AIDirector::kMaxPicksPerUpdate, picks );
}

// ---- 결정성과 상태 ----

SW_TEST_CASE( AIDirectorStateTest, SameSeedSameInputsSameHash )
{
    DirectorRig first( 11 );
    DirectorRig second( 11 );
    first.spike();
    second.spike();
    first.run( 40.0f );
    second.run( 40.0f );
    SW_EXPECT_EQUAL( first._director.computeStateHash(), second._director.computeStateHash() );

    second.spike();
    second.run( 0.1f );
    SW_EXPECT_TRUE( first._director.computeStateHash() != second._director.computeStateHash() );
}

SW_TEST_CASE( AIDirectorStateTest, StateRoundTripsAndContinuesTheSameWay )
{
    DirectorRig rig;
    rig.spike();
    rig.spike();
    rig.run( 7.0f );
    const vector<uint8> listSaved = DirectorRig::save( rig._director );

    DirectorRig restored;
    Archive     reading( listSaved.data(), listSaved.size() );
    SW_REQUIRE_TRUE( restored._director.readState( reading ) );
    SW_EXPECT_EQUAL( rig._director.computeStateHash(), restored._director.computeStateHash() );
    SW_EXPECT_TRUE( restored._director.getPhase() == rig._director.getPhase() );

    rig.run( 20.0f );
    restored.run( 20.0f );
    SW_EXPECT_EQUAL( rig._director.computeStateHash(), restored._director.computeStateHash() );
}

SW_TEST_CASE( AIDirectorStateTest, BadBytesLeaveTheDirectorAlone )
{
    SW_EXPECT_LOG( "StateIO.HeaderTagMismatch" ); // 표가 다른 바이트를 거절하는 것이 이 테스트가 보려는 동작이다
    DirectorRig rig;
    rig.run( 1.0f );
    const vector<uint8> listSaved = DirectorRig::save( rig._director );

    DirectorRig target;
    target.run( 3.0f );
    const uint64 before = target._director.computeStateHash();

    Archive truncated( listSaved.data(), listSaved.size() - 1 );
    SW_EXPECT_FALSE( target._director.readState( truncated ) );

    vector<uint8> listBadTag = listSaved;
    listBadTag[0] ^= 0xff;
    Archive badTag( listBadTag.data(), listBadTag.size() );
    SW_EXPECT_FALSE( target._director.readState( badTag ) );
    SW_EXPECT_EQUAL( before, target._director.computeStateHash() );

    AIDirector empty; // 프로필 없는 감독은 읽지 않는다
    Archive    noProfile( listSaved.data(), listSaved.size() );
    SW_EXPECT_FALSE( empty.readState( noProfile ) );
}

// 결함: 시간 · 순환 · 예산 · 스트레스의 범위를 보지 않아 음수 순환이나 상한을 넘는 예산을 그대로 받았다.
SW_TEST_CASE( AIDirectorStateTest, OutOfRangeValuesAreRefused )
{
    DirectorRig rig;
    rig.run( 1.0f );
    Archive                  scratch;
    AIDirectorIntensityModel model;
    AIDirectorIntensityDef   def;
    def._max = 1.0f;
    model.initialize( &def );

    Archive stress; // 스트레스 2, 평온 0, 신호 0 개 — 상한 1 을 넘는다
    stress << 2.0f << 0.0f << uint32( 0 );
    Archive reading( stress.getData(), stress.getSize() );
    SW_EXPECT_FALSE( model.readState( reading ) );

    Archive calm; // 평온 시간이 음수다
    calm << 0.5f << -1.0f << uint32( 0 );
    Archive readingCalm( calm.getData(), calm.getSize() );
    SW_EXPECT_FALSE( model.readState( readingCalm ) );

    // 머리(태그 · 버전) 뒤: 단계 수 int32, 시간 float32, 단계 시간, 긴장도, 단계 번호, 순환 — 순환을 음수로 바꾼다.
    vector<uint8> listForged = DirectorRig::save( rig._director );
    Archive       header;
    rig._director.writeState( header );
    constexpr size_t kHeaderBytes = sizeof( uint32 ) * 2;
    constexpr size_t kCycleOffset = kHeaderBytes + sizeof( int32 ) + sizeof( float32 ) * 3 + sizeof( int32 );
    listForged[kCycleOffset + 3]  = 0xff;
    DirectorRig target;
    Archive     forged( listForged.data(), listForged.size() );
    SW_EXPECT_FALSE( target._director.readState( forged ) );
}

// ---- 프로필 ----

SW_TEST_CASE( AIDirectorProfileTest, RefusesBrokenData )
{
    SW_EXPECT_LOG( "AIDirectorProfile.PhaseDeclaredTwice" );
    SW_EXPECT_LOG( "AIDirectorProfile.PoolNoEncounter" );
    SW_EXPECT_LOG( "AIDirectorProfile.PoolTriggeredPhaseEnter" );
    SW_EXPECT_LOG( "AIDirectorProfile.PoolWaitsUnknownPhase" );
    SW_EXPECT_LOG( "AIDirectorProfile.PoolListsTwice" );
    {
        AIDirectorProfile profile;
        SW_EXPECT_FALSE( profile.loadFromXMLText( R"(<AIDirector startPhase="A"><Phase id="A"/><Phase id="A"/></AIDirector>)", "DirectorTest" ) );
    }
    {
        AIDirectorProfile profile;
        SW_EXPECT_FALSE( profile.loadFromXMLText( DirectorProfileTestUtil::makePoolProfile( R"(<Pool id="p" trigger="Interval"/>)" ), "DirectorTest" ) );
    }
    {
        AIDirectorProfile profile;
        SW_EXPECT_FALSE( profile.loadFromXMLText( DirectorProfileTestUtil::makePoolProfile( R"(<Pool id="p" trigger="PhaseEnter"><Encounter id="e"/></Pool>)" ), "DirectorTest" ) );
    }
    {
        AIDirectorProfile profile;
        SW_EXPECT_FALSE( profile.loadFromXMLText( DirectorProfileTestUtil::makePoolProfile( R"(<Pool id="p" trigger="PhaseEnter" phase="Z"><Encounter id="e"/></Pool>)" ), "DirectorTest" ) );
    }
    {
        AIDirectorProfile profile;
        SW_EXPECT_FALSE( profile.loadFromXMLText( DirectorProfileTestUtil::makePoolProfile( R"(<Pool id="p" trigger="Interval"><Encounter id="e"/><Encounter id="e"/></Pool>)" ), "DirectorTest" ) );
    }
}

SW_TEST_CASE( AIDirectorProfileTest, CollectsEveryEncounterIDOnce )
{
    DirectorRig           rig;
    vector<hashed_string> listID;
    rig._profile.collectEncounterIDs( listID );
    SW_EXPECT_EQUAL( size_t( 3 ), listID.size() );
    SW_EXPECT_EQUAL( 1, rig._profile.findPhaseIndex( hashed_string( "Peak" ) ).value() );
    SW_EXPECT_FALSE( rig._profile.findPhaseIndex( hashed_string( "Nothing" ) ).has_value() );
    SW_EXPECT_EQUAL( 2, rig._profile.findSignalIndex( hashed_string( "low" ) ).value() );
}

/**
 * @brief [AIDirectorProfileTest] 같은 경로는 GameFramework 모듈 캐시의 같은 객체이고, 다시 읽으면 쥔 쪽(감독)은 옛 프로필을 지키며 다시 받은 쪽은 새 프로필을 받는지 검증
 * @details 감독이 국면 · 풀 번호로 읽으므로 떼어 낸다(`Detach`) — 한 판은 받은 객체로 끝까지 돈다.
 */
SW_TEST_CASE( AIDirectorProfileTest, SharedCacheGivesOneObjectAndReloadDetaches )
{
    const test::ScopedScratchDomain domain;
    const string                    id = test::ScopedScratchDomain::makeID( "arena.director.xml" ).str();
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "arena.director.xml" ), DirectorRig::kProfileXML ) );

    const shared_ptr<const AIDirectorProfile> pHeld  = SharedAssetCache<AIDirectorProfile>::acquire( id );
    const shared_ptr<const AIDirectorProfile> pAgain = SharedAssetCache<AIDirectorProfile>::acquire( id );
    SW_REQUIRE_TRUE( pHeld != nullptr );
    SW_EXPECT_TRUE( pHeld.get() == pAgain.get() );
    const optional<int32> peak = pHeld->findPhaseIndex( hashed_string( "Peak" ) );
    SW_REQUIRE_TRUE( peak.has_value() );
    SW_EXPECT_NEAR( 2.0f, pHeld->getPhases()[static_cast<size_t>( *peak )]._spawnScale, 0.0001f );

    string       changed = DirectorRig::kProfileXML;
    const size_t at      = changed.find( "<Phase id=\"Peak\" spawnScale=\"2\">" );
    SW_REQUIRE_TRUE( at != string::npos );
    changed.replace( at, string_view( "<Phase id=\"Peak\" spawnScale=\"2\">" ).size(), "<Phase id=\"Peak\" spawnScale=\"3\">" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "arena.director.xml" ), changed ) );
    SW_REQUIRE_TRUE( SharedAssetCache<AIDirectorProfile>::reloadShared( id ) );
    SW_EXPECT_NEAR( 2.0f, pHeld->getPhases()[static_cast<size_t>( *peak )]._spawnScale, 0.0001f ); // 쥔 쪽은 옛 객체 그대로(Detach)

    const shared_ptr<const AIDirectorProfile> pFresh = SharedAssetCache<AIDirectorProfile>::acquire( id );
    SW_REQUIRE_TRUE( pFresh != nullptr );
    SW_EXPECT_TRUE( pFresh.get() != pHeld.get() );
    SW_EXPECT_NEAR( 3.0f, pFresh->getPhases()[static_cast<size_t>( *peak )]._spawnScale, 0.0001f );
}

/**
 * @brief [AIDirectorStateTest] 판을 이어 가며 프로필을 바꾸면 국면 · 시계가 그대로 이어지고, 구조(풀 수)가 바뀐 프로필은 거절하고 옛 프로필과 상태로 되돌리는지 검증
 * @details 핫 리로드가 떼어 낸 새 객체를 슈터 게임 모드가 판 도중에 받는 길이다(`AIDirector::rebindProfile`).
 */
SW_TEST_CASE( AIDirectorStateTest, RebindProfileContinuesTheRun )
{
    DirectorRig rig;
    rig.run( 20.0f );
    const hashed_string phaseBefore = rig._director.getPhase();
    const float32       timeBefore  = rig._director.getTime();

    string       tuned = DirectorRig::kProfileXML;
    const size_t at    = tuned.find( "<Phase id=\"Peak\" spawnScale=\"2\">" );
    SW_REQUIRE_TRUE( at != string::npos );
    tuned.replace( at, string_view( "<Phase id=\"Peak\" spawnScale=\"2\">" ).size(), "<Phase id=\"Peak\" spawnScale=\"3\">" );
    AIDirectorProfile tunedProfile;
    SW_REQUIRE_TRUE( tunedProfile.loadFromXMLText( tuned, "DirectorTest" ) );
    SW_REQUIRE_TRUE( rig._director.rebindProfile( &tunedProfile ) );
    SW_EXPECT_TRUE( rig._director.getProfile() == &tunedProfile );
    SW_EXPECT_TRUE( rig._director.getPhase() == phaseBefore );
    SW_EXPECT_NEAR( timeBefore, rig._director.getTime(), 0.0001f );

    // 풀이 하나 더 있는 프로필은 번호를 이을 수 없다 — 거절하고 쥔 프로필과 상태로 되돌린다.
    string       reshaped = DirectorRig::kProfileXML;
    const size_t endAt    = reshaped.find( "</AIDirector>" );
    SW_REQUIRE_TRUE( endAt != string::npos );
    reshaped.insert( endAt, "<Pool id=\"extra\" trigger=\"Interval\" interval=\"10\"><Encounter id=\"lurker\"/></Pool>\n" );
    AIDirectorProfile reshapedProfile;
    SW_REQUIRE_TRUE( reshapedProfile.loadFromXMLText( reshaped, "DirectorTest" ) );
    const uint64 hashBefore = rig._director.computeStateHash();
    SW_EXPECT_FALSE( rig._director.rebindProfile( &reshapedProfile ) );
    SW_EXPECT_TRUE( rig._director.getProfile() == &tunedProfile );
    SW_EXPECT_EQUAL( hashBefore, rig._director.computeStateHash() );
}
