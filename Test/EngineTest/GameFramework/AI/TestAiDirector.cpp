#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/AI/Director/AiDirector.h"
#include "GameFramework/Base/AI/Director/AiDirectorProfile.h"
#include "GameFramework/Base/AI/SpawnDirector.h"
#include "GameFramework/Base/World/GameFlags.h"
#include "GameFramework/Base/World/WorldClock.h"

#include "TestFramework/TestFramework.h"

// 페이싱 감독 — 데이터 검사, 긴장도 모델(충격 · 초당 · 바닥 · 식기 지연), 단계 넘기기(쌓기 → 절정 → 쉼 → 쌓기)와 순환 수,
// 단계별 스폰 예산(쉬는 동안 0) · 태그, 풀(단계 진입 · 주기 · 예산)의 쿨다운 · 상한 · 문맥 조건, 보상 밀도, 결정성, 추적 · 설명.

using namespace sw;

namespace
{
    struct AiDirectorTestInternal
    {
        static constexpr const utf8* kPacingXml = R"(
<AiDirector startPhase="BuildUp">
  <Intensity max="1" decayPerSecond="0.1" decayDelay="2">
    <Signal id="damage" kind="impulse" scale="0.1" combat="true"/>
    <Signal id="nearby" kind="rate" scale="0.05" max="0.2" combat="true"/>
    <Signal id="lowAmmo" kind="level" scale="0.5"/>
  </Intensity>
  <Phase id="BuildUp" spawnScale="1" spawnTags="Common">
    <Curve time="0" scale="1"/><Curve time="10" scale="2"/>
    <Exit to="Peak" intensityAbove="0.8" minTime="2"/>
    <Exit to="Peak" minTime="60"/>
  </Phase>
  <Phase id="Peak" spawnScale="2" spawnTags="Common,Special">
    <Exit to="Relax" minTime="5"/>
  </Phase>
  <Phase id="Relax" spawnScale="0" rewardScale="3">
    <Exit to="BuildUp" intensityBelow="0.2" calmFor="4" minTime="10"/>
  </Phase>
  <Pool id="horde" trigger="phaseEnter" phase="Peak">
    <Encounter id="swarm" weight="1" count="6"/>
    <Encounter id="tank" weight="100" count="1" scale="3" minCycle="1" maxCount="1"/>
  </Pool>
</AiDirector>
)";

        static constexpr const utf8* kSpawnXml = R"(
<SpawnTable budgetPerMinute="60" maxBudget="4" startBudget="0">
  <Entry id="grunt" cost="1" weight="1" max="50" tags="Common"/>
  <Entry id="runner" cost="1" weight="1" max="50" tags="Common"/>
  <Entry id="boss" cost="4" weight="1000" max="1" tags="Special"/>
</SpawnTable>
)";

        static constexpr const utf8* kCooldownXml = R"(
<AiDirector>
  <Phase id="Only"/>
  <Pool id="ambient" trigger="interval" interval="1" chance="1" cooldown="2">
    <Encounter id="bird" weight="1" cooldown="5"/>
    <Encounter id="wolf" weight="1" cooldown="5" maxCount="2"/>
  </Pool>
</AiDirector>
)";

        static constexpr const utf8* kContextXml = R"(
<AiDirector>
  <Calendar weathers="clear,storm"/>
  <Phase id="Calm"/>
  <Phase id="Hunt"/>
  <Pool id="road" trigger="interval" interval="1">
    <Encounter id="ambush" areas="Forest,Road" phases="Night" pacing="Hunt"/>
    <Encounter id="stranger" tags="Player.Mounted" notTags="Player.Wanted" weathers="clear"/>
    <Encounter id="bounty" flags="bounty>=2" minIntensity="0.5" maxIntensity="0.9"/>
    <Encounter id="late" minTime="30" minCycle="1"/>
  </Pool>
</AiDirector>
)";

        static constexpr const utf8* kRewardXml = R"(
<AiDirector startPhase="Fight">
  <Intensity><Signal id="lowAmmo" kind="level" scale="0"/></Intensity>
  <Phase id="Fight" rewardScale="1"/>
  <Phase id="Rest" rewardScale="3"/>
  <Pool id="supplies" kind="reward" trigger="budget" perMinute="60" maxBudget="3" need="lowAmmo" needScale="1">
    <Encounter id="ammo" cost="1"/>
    <Encounter id="crate" cost="9"/>
  </Pool>
</AiDirector>
)";

        static int32 countKind( const vector<AiDirectorEvent>& listEvent, AiDirectorEventKind kind, const utf8* pId = nullptr )
        {
            int32 count = 0;
            for ( const AiDirectorEvent& event : listEvent )
            {
                const bool bIdMatches = pId == nullptr || event._id == hashed_string( pId );
                count += event._kind == kind && bIdMatches ? 1 : 0;
            }
            return count;
        }

        static const AiDirectorEvent* findLast( const vector<AiDirectorEvent>& listEvent, AiDirectorEventKind kind )
        {
            const AiDirectorEvent* pFound = nullptr;
            for ( const AiDirectorEvent& event : listEvent )
                pFound = event._kind == kind ? &event : pFound;
            return pFound;
        }

        /** @brief 같은 대본(시각마다 같은 신호)을 돌려 사건과 상태 해시를 받습니다 — 결정성 시험이 씨앗만 바꿔 부른다. */
        static uint64 runScript( uint32 seed, vector<AiDirectorEvent>& outListEvent )
        {
            AiDirectorProfile profile;
            SpawnTable        table;
            if ( profile.loadFromXmlText( kPacingXml, "Pacing" ) == false || table.loadFromXmlText( kSpawnXml, "Spawn" ) == false )
                return 0;
            AiDirector director;
            director.initialize( &profile, &table, seed );
            outListEvent.clear();
            for ( int32 step = 0; step < 1200; ++step )
            {
                // 30 초마다 맞고 처음 6 초는 둘러싸인다. 죽은 스폰은 3 초 뒤 돌려준다.
                if ( step % 300 == 0 )
                    (void)director.getBuiltinIntensityModel().addSignal( hashed_string( "damage" ), 9.0f );
                (void)director.getBuiltinIntensityModel().setSignal( hashed_string( "nearby" ), step % 300 < 60 ? 3.0f : 0.0f );
                director.update( 0.1f );
                director.drainEvents( outListEvent );
                for ( const AiDirectorEvent& event : outListEvent )
                {
                    const bool bExpired = event._kind == AiDirectorEventKind::Spawned && director.getTime() - event._time >= 3.0f && event._spawnId != 0;
                    if ( bExpired )
                        (void)director.notifyDespawned( event._spawnId );
                }
            }
            return director.computeStateHash();
        }
    };
} // namespace

/**
 * @brief [AiDirectorTest] 프로필 — 모르는 원소 · 속성 · 종류 · 단계 · 신호는 로드 오류이고, 맞는 프로필은 단계 · 풀 · 신호를 다 읽는다
 */
SW_TEST_CASE( AiDirectorTest, ProfileRejectsUnknownNames )
{
    using Internal = AiDirectorTestInternal;
    AiDirectorProfile profile;
    SW_ASSERT_TRUE( profile.loadFromXmlText( Internal::kPacingXml, "Pacing" ) );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( profile.getPhases().size() ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( profile.getPools().size() ) );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( profile.getIntensity()._listSignal.size() ) );
    SW_EXPECT_EQUAL( 0, profile.getStartPhaseIndex() );
    SW_EXPECT_EQUAL( 1, profile.getPhases()[0]._listExit[0]._toIndex );
    SW_EXPECT_NEAR_EQUAL( 1.5f, profile.getPhases()[0]._spawnCurve.evaluate( 5.0f ), 1.0e-6f );
    vector<hashed_string> listId;
    profile.collectEncounterIds( listId );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listId.size() ) );

    struct BadCase
    {
        const utf8* _pXml;
        const utf8* _pMessage;
    };
    const BadCase arrBad[] = {
        {                                                                "<AiDirector><Phase id=\"A\" speed=\"2\"/></AiDirector>",     "unknown attribute 'speed'"},
        {                                                            "<AiDirector><Phase id=\"A\"/><Wave id=\"x\"/></AiDirector>",        "unknown element <Wave>"},
        {                                                     "<AiDirector><Phase id=\"A\"><Exit to=\"B\"/></Phase></AiDirector>", "exits to an unknown phase 'B'"},
        {               "<AiDirector><Phase id=\"A\"/><Pool id=\"p\" trigger=\"often\"><Encounter id=\"e\"/></Pool></AiDirector>",       "unknown trigger 'often'"},
        {                    "<AiDirector><Phase id=\"A\"/><Pool id=\"p\" pacing=\"Z\"><Encounter id=\"e\"/></Pool></AiDirector>",      "unknown pacing phase 'Z'"},
        {"<AiDirector><Phase id=\"A\"/><Pool id=\"p\" trigger=\"budget\" need=\"ammo\"><Encounter id=\"e\"/></Pool></AiDirector>",         "unknown signal 'ammo'"},
        {                    "<AiDirector><Intensity><Signal id=\"s\" kind=\"pulse\"/></Intensity><Phase id=\"A\"/></AiDirector>",          "unknown kind 'pulse'"},
        {                                                           "<AiDirector startPhase=\"Z\"><Phase id=\"A\"/></AiDirector>",               "start phase 'Z'"},
        {                                                                                                         "<AiDirector/>",                    "no <Phase>"},
    };
    for ( const BadCase& bad : arrBad )
    {
        test::ScopedLogCollector logs;
        AiDirectorProfile        broken;
        {
            SW_TEST_DEFENSIVE_SCOPE( "broken director profile" );
            SW_EXPECT_FALSE( broken.loadFromXmlText( bad._pXml, "Broken" ) );
        }
        SW_EXPECT_TRUE_MSG( logs.countContaining( bad._pMessage ) >= 1, ( string( bad._pMessage ) + " not reported:" + logs.joined() ).c_str() );
        SW_EXPECT_TRUE( broken.getPhases().empty() );
    }
}

/**
 * @brief [AiDirectorTest] 긴장도 모델 — 충격은 상한까지 한 번에, 초당 신호는 상한으로 잘려 쌓이고, 싸움 뒤 지연이 지나야 식으며, 바닥 신호 아래로는 내려가지 않는다
 */
SW_TEST_CASE( AiDirectorTest, IntensityModelBuildsDecaysAndFloors )
{
    AiDirectorProfile profile;
    SW_ASSERT_TRUE( profile.loadFromXmlText( AiDirectorTestInternal::kPacingXml, "Pacing" ) );
    AiDirectorIntensityModel model;
    model.initialize( &profile.getIntensity() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, model.getIntensity(), 1.0e-6f );
    SW_EXPECT_TRUE( model.getCalmSeconds() > 1000.0f );

    SW_EXPECT_TRUE( model.addSignal( hashed_string( "damage" ), 3.0f ) );
    SW_EXPECT_NEAR_EQUAL( 0.3f, model.getIntensity(), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, model.getCalmSeconds(), 1.0e-6f );
    SW_EXPECT_FALSE( model.addSignal( hashed_string( "nearby" ), 1.0f ) ); // 초당 신호는 set 으로만
    SW_EXPECT_FALSE( model.setSignal( hashed_string( "damage" ), 1.0f ) );
    SW_EXPECT_FALSE( model.addSignal( hashed_string( "unknown" ), 1.0f ) );

    // 지연(2 초) 동안은 식지 않는다.
    model.update( 2.0f );
    SW_EXPECT_NEAR_EQUAL( 0.3f, model.getIntensity(), 1.0e-5f );
    // 그 뒤로 초당 0.1.
    model.update( 1.0f );
    SW_EXPECT_NEAR_EQUAL( 0.2f, model.getIntensity(), 1.0e-5f );

    // 초당 신호: 값 10 × 0.05 = 0.5 지만 상한 0.2/s. 싸움 신호라 식기 지연이 다시 시작된다.
    SW_EXPECT_TRUE( model.setSignal( hashed_string( "nearby" ), 10.0f ) );
    model.update( 1.0f );
    SW_EXPECT_NEAR_EQUAL( 0.4f, model.getIntensity(), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, model.getCalmSeconds(), 1.0e-6f );
    SW_EXPECT_TRUE( model.setSignal( hashed_string( "nearby" ), 0.0f ) );

    // 충격은 상한(1)에서 멈춘다.
    SW_EXPECT_TRUE( model.addSignal( hashed_string( "damage" ), 100.0f ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, model.getIntensity(), 1.0e-6f );

    // 바닥: 탄이 모자라면(값 1 × 0.5) 스트레스가 다 식어도 0.5 아래로 내려가지 않는다.
    SW_EXPECT_TRUE( model.setSignal( hashed_string( "lowAmmo" ), 1.0f ) );
    for ( int32 step = 0; step < 40; ++step )
        model.update( 1.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, model.getStress(), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, model.getIntensity(), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, model.getSignal( hashed_string( "lowAmmo" ) ), 1.0e-6f );
}

/**
 * @brief [AiDirectorTest] 단계 넘기기 — 긴장도가 차면 절정, 절정은 정한 시간만, 쉼은 식고 조용해진 뒤에 끝나고, 시작 단계로 돌아오면 순환 수가 오른다
 */
SW_TEST_CASE( AiDirectorTest, PhasesFollowIntensityThroughBuildUpPeakRelax )
{
    using Internal = AiDirectorTestInternal;
    AiDirectorProfile profile;
    SW_ASSERT_TRUE( profile.loadFromXmlText( Internal::kPacingXml, "Pacing" ) );
    AiDirector director;
    director.initialize( &profile, nullptr, 11u );
    vector<AiDirectorEvent> listEvent;
    director.drainEvents( listEvent );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listEvent.size() ) );
    SW_EXPECT_TRUE( listEvent[0]._kind == AiDirectorEventKind::PhaseChanged && listEvent[0]._id == hashed_string( "BuildUp" ) && listEvent[0]._detail == -1 );

    // 맞아서 긴장도 1 — 그래도 쌓기 단계의 최소 시간(2 초)은 채운다.
    SW_ASSERT_TRUE( director.getBuiltinIntensityModel().addSignal( hashed_string( "damage" ), 10.0f ) );
    director.update( 1.0f );
    SW_EXPECT_TRUE( director.getPhase() == hashed_string( "BuildUp" ) );
    director.update( 1.0f );
    SW_EXPECT_TRUE( director.getPhase() == hashed_string( "Peak" ) );
    listEvent.clear();
    director.drainEvents( listEvent );
    const AiDirectorEvent* pChange = Internal::findLast( listEvent, AiDirectorEventKind::PhaseChanged );
    SW_ASSERT_TRUE( pChange != nullptr );
    SW_EXPECT_TRUE( pChange->_source == hashed_string( "BuildUp" ) );
    SW_EXPECT_EQUAL( 0, pChange->_detail ); // 긴장도 길(0 번)
    // 절정에 들어서며 호드 풀이 한 번 고른다 — 탱크는 아직 첫 순환이라 막혀 무리가 나온다.
    SW_EXPECT_EQUAL( 1, Internal::countKind( listEvent, AiDirectorEventKind::Encounter, "swarm" ) );
    SW_EXPECT_EQUAL( 6, Internal::findLast( listEvent, AiDirectorEventKind::Encounter )->_count );

    // 절정은 5 초.
    for ( int32 step = 0; step < 9; ++step )
        director.update( 0.5f );
    SW_EXPECT_TRUE( director.getPhase() == hashed_string( "Peak" ) );
    director.update( 0.5f );
    SW_EXPECT_TRUE( director.getPhase() == hashed_string( "Relax" ) );
    SW_EXPECT_NEAR_EQUAL( 7.0f, director.getTime(), 1.0e-4f );

    // 쉼: 맞은 지 2 초 뒤부터 초당 0.1 씩 식는다 — 0.2 아래는 10 초, 쉼의 최소 시간 10 초는 17 초. 그 전에는 돌아가지 않는다.
    while ( director.getTime() < 16.9f )
    {
        director.update( 0.1f );
        SW_EXPECT_TRUE( director.getPhase() == hashed_string( "Relax" ) );
    }
    for ( int32 step = 0; step < 3 && director.getPhase() == hashed_string( "Relax" ); ++step )
        director.update( 0.1f );
    SW_EXPECT_TRUE( director.getPhase() == hashed_string( "BuildUp" ) );
    SW_EXPECT_EQUAL( 1, director.getCycle() );
    SW_EXPECT_NEAR_EQUAL( 17.0f, director.getTime(), 0.15f );

    // 두 번째 절정 — 순환 1 이라 탱크(가중치 100)가 열린다. 한 판에 한 번(maxCount 1)이라 세 번째는 다시 무리.
    listEvent.clear();
    for ( int32 peak = 0; peak < 2; ++peak )
    {
        SW_ASSERT_TRUE( director.getBuiltinIntensityModel().addSignal( hashed_string( "damage" ), 10.0f ) );
        for ( int32 step = 0; step < 400 && director.getPhase() != hashed_string( "Relax" ); ++step )
            director.update( 0.1f );
        for ( int32 step = 0; step < 400 && director.getPhase() != hashed_string( "BuildUp" ); ++step )
            director.update( 0.1f );
    }
    director.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, Internal::countKind( listEvent, AiDirectorEventKind::Encounter, "tank" ) );
    SW_EXPECT_EQUAL( 1, Internal::countKind( listEvent, AiDirectorEventKind::Encounter, "swarm" ) );
    SW_EXPECT_EQUAL( 3, director.getCycle() );

    // 시간 길: 아무 일이 없어도 쌓기는 60 초에 절정으로 간다.
    AiDirector quiet;
    quiet.initialize( &profile, nullptr, 11u );
    for ( int32 step = 0; step < 599; ++step )
        quiet.update( 0.1f );
    SW_EXPECT_TRUE( quiet.getPhase() == hashed_string( "BuildUp" ) );
    quiet.update( 0.2f );
    SW_EXPECT_TRUE( quiet.getPhase() == hashed_string( "Peak" ) );
    // 강제 단계 바꾸기.
    SW_EXPECT_TRUE( quiet.forcePhase( hashed_string( "Relax" ) ) );
    SW_EXPECT_FALSE( quiet.forcePhase( hashed_string( "Nope" ) ) );
    SW_EXPECT_TRUE( quiet.getPhase() == hashed_string( "Relax" ) );
}

/**
 * @brief [AiDirectorTest] 스폰 예산 — 쌓기는 단계 곡선 배율로, 절정은 두 배로 쌓고 특수 태그를 열며, 쉬는 동안은 쌓지도 내지도 않는다
 */
SW_TEST_CASE( AiDirectorTest, SpawnBudgetFollowsThePhase )
{
    using Internal = AiDirectorTestInternal;
    AiDirectorProfile profile;
    SpawnTable        table;
    SW_ASSERT_TRUE( profile.loadFromXmlText( Internal::kPacingXml, "Pacing" ) );
    SW_ASSERT_TRUE( table.loadFromXmlText( Internal::kSpawnXml, "Spawn" ) );
    AiDirector director;
    director.initialize( &profile, &table, 5u );
    vector<AiDirectorEvent> listEvent;

    // 쌓기 0..10 초: 배율이 1 → 2 로 오른다 — 초당 1 × 평균 1.5 = 15 개. 보스는 Special 태그라 쌓기에선 나오지 않는다.
    for ( int32 step = 0; step < 100; ++step )
        director.update( 0.1f );
    director.drainEvents( listEvent );
    const int32 buildUpSpawns = Internal::countKind( listEvent, AiDirectorEventKind::Spawned );
    SW_EXPECT_TRUE_MSG( 14 <= buildUpSpawns && buildUpSpawns <= 15, std::to_string( buildUpSpawns ).c_str() );
    SW_EXPECT_EQUAL( 0, Internal::countKind( listEvent, AiDirectorEventKind::Spawned, "boss" ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f, director.getSpawnDirector().getBudgetScale(), 1.0e-4f );

    // 절정 5 초: 배율 2, 보스(가중치 1000, 비용 4)가 열린다.
    SW_ASSERT_TRUE( director.forcePhase( hashed_string( "Peak" ) ) );
    listEvent.clear();
    for ( int32 step = 0; step < 49; ++step )
        director.update( 0.1f );
    director.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, Internal::countKind( listEvent, AiDirectorEventKind::Spawned, "boss" ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f, director.getSpawnDirector().getBudgetScale(), 1.0e-6f );

    // 쉼(최소 10 초): 하나도 내지 않고 남은 예산도 그대로다.
    director.update( 0.2f );
    SW_ASSERT_TRUE( director.getPhase() == hashed_string( "Relax" ) );
    const float32 budgetAtRest = director.getSpawnDirector().getBudget();
    listEvent.clear();
    for ( int32 step = 0; step < 95; ++step )
        director.update( 0.1f );
    SW_EXPECT_TRUE( director.getPhase() == hashed_string( "Relax" ) );
    director.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 0, Internal::countKind( listEvent, AiDirectorEventKind::Spawned ) );
    SW_EXPECT_NEAR_EQUAL( budgetAtRest, director.getSpawnDirector().getBudget(), 1.0e-6f );

    // 죽은 개체는 감독을 지나 스폰 감독으로 돌아가고 Despawned 사건이 남는다.
    AiDirector fresh;
    fresh.initialize( &profile, &table, 5u );
    for ( int32 step = 0; step < 20; ++step )
        fresh.update( 0.1f );
    listEvent.clear();
    fresh.drainEvents( listEvent );
    const AiDirectorEvent* pSpawned = Internal::findLast( listEvent, AiDirectorEventKind::Spawned );
    SW_ASSERT_TRUE( pSpawned != nullptr );
    const int32 aliveBefore = fresh.getSpawnDirector().getTotalAliveCount();
    SW_EXPECT_TRUE( fresh.notifyDespawned( pSpawned->_spawnId ) );
    SW_EXPECT_FALSE( fresh.notifyDespawned( pSpawned->_spawnId ) );
    SW_EXPECT_EQUAL( aliveBefore - 1, fresh.getSpawnDirector().getTotalAliveCount() );
    listEvent.clear();
    fresh.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, Internal::countKind( listEvent, AiDirectorEventKind::Despawned ) );
}

/**
 * @brief [AiDirectorTest] 스폰 감독의 예산 배율 — 배율만큼 빨리 쌓이고, 0 이면 모아 둔 예산이 있어도 내지 않는다(쉬는 단계의 약속)
 */
SW_TEST_CASE( AiDirectorTest, SpawnDirectorBudgetScalePausesAndScales )
{
    SpawnTable table;
    SW_ASSERT_TRUE( table.loadFromXmlText( R"(<SpawnTable budgetPerMinute="60" maxBudget="10" startBudget="3"><Entry id="grunt" cost="1"/></SpawnTable>)", "Scale" ) );
    SpawnDirector director;
    director.initialize( &table, 1u );
    director.setBudgetScale( 0.0f );
    SW_EXPECT_EQUAL( 0, director.update( 1.0f ) );
    SW_EXPECT_NEAR_EQUAL( 3.0f, director.getBudget(), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, director.getTime(), 1.0e-6f );
    director.setBudgetScale( 2.0f );
    SW_EXPECT_EQUAL( 5, director.update( 1.0f ) ); // 모은 3 + 초당 1 × 2
    SW_EXPECT_NEAR_EQUAL( 0.0f, director.getBudget(), 1.0e-5f );
}

/**
 * @brief [AiDirectorTest] 쿨다운 · 상한 — 풀 쿨다운(2 초) · 항목 쿨다운(5 초) · 한 판 상한(늑대 2 번)을 어느 씨앗에서도 지킨다
 */
SW_TEST_CASE( AiDirectorTest, CooldownsAndCapsHoldForEverySeed )
{
    using Internal = AiDirectorTestInternal;
    AiDirectorProfile profile;
    SW_ASSERT_TRUE( profile.loadFromXmlText( Internal::kCooldownXml, "Cooldown" ) );
    for ( uint32 seed = 1; seed <= 20; ++seed )
    {
        AiDirector director;
        director.initialize( &profile, nullptr, seed );
        vector<AiDirectorEvent> listEvent;
        for ( int32 step = 0; step < 600; ++step )
            director.update( 0.1f );
        director.drainEvents( listEvent );
        float32 lastPool = -100.0f;
        float32 lastBird = -100.0f;
        float32 lastWolf = -100.0f;
        int32   picks    = 0;
        for ( const AiDirectorEvent& event : listEvent )
        {
            if ( event._kind != AiDirectorEventKind::Encounter )
                continue;
            ++picks;
            SW_EXPECT_TRUE( event._time - lastPool >= 2.0f - 1.0e-3f );
            lastPool        = event._time;
            float32& lastId = event._id == hashed_string( "bird" ) ? lastBird : lastWolf;
            SW_EXPECT_TRUE( event._time - lastId >= 5.0f - 1.0e-3f );
            lastId = event._time;
        }
        SW_EXPECT_TRUE( Internal::countKind( listEvent, AiDirectorEventKind::Encounter, "wolf" ) <= 2 );
        // 60 초 — 새는 5 초마다 열리니 열 번쯤은 고른다(쿨다운이 고르기를 막기만 하고 굶기지 않는다).
        SW_EXPECT_TRUE_MSG( picks >= 12, std::to_string( picks ).c_str() );
    }
}

/**
 * @brief [AiDirectorTest] 문맥 조건 — 지역 태그 · 하루의 때(WorldClock) · 페이싱 단계 · 플레이어 태그 · 날씨 · 플래그 식 · 긴장도 범위 · 시간 · 순환이 각각 막고, 설명이 그 까닭을 적는다
 */
SW_TEST_CASE( AiDirectorTest, ContextConditionsGateEncounters )
{
    using Internal = AiDirectorTestInternal;
    AiDirectorProfile profile;
    SW_ASSERT_TRUE( profile.loadFromXmlText( Internal::kContextXml, "Context" ) );
    AiDirector director;
    director.initialize( &profile, nullptr, 3u );
    const int32 pool = director.findPoolIndex( hashed_string( "road" ) );
    SW_ASSERT_EQUAL( 0, pool );

    // 아무 문맥 없음: 모두 막힌다.
    SW_EXPECT_EQUAL( AiDirectorBlock::kArea | AiDirectorBlock::kCondition | AiDirectorBlock::kPacing, director.computeBlockMask( pool, 0 ) );
    SW_EXPECT_EQUAL( AiDirectorBlock::kCondition, director.computeBlockMask( pool, 1 ) );
    SW_EXPECT_EQUAL( AiDirectorBlock::kCondition | AiDirectorBlock::kIntensity, director.computeBlockMask( pool, 2 ) );
    SW_EXPECT_EQUAL( AiDirectorBlock::kTime | AiDirectorBlock::kCycle, director.computeBlockMask( pool, 3 ) );

    // 숲 · 밤(시계) · 사냥 단계 → 매복이 열린다.
    WorldClock         clock;
    WorldClockSettings settings;
    settings._startHour = 23.0f;
    clock.initialize( settings );
    AiDirectorContext context;
    context.fillFromClock( clock );
    context._listAreaTag = { hashed_string( "Forest" ) };
    TagContainer playerTags;
    playerTags.addTag( TagID::request( "Player.Mounted" ) );
    GameFlags flags;
    flags.setFlag( hashed_string( "bounty" ), 3 );
    context._condition._pNpcTags = &playerTags;
    context._condition._pFlags   = &flags;
    context._condition._weather  = hashed_string( "clear" );
    director.setContext( context );
    SW_EXPECT_EQUAL( AiDirectorBlock::kPacing, director.computeBlockMask( pool, 0 ) );
    SW_ASSERT_TRUE( director.forcePhase( hashed_string( "Hunt" ) ) );
    SW_EXPECT_EQUAL( 0u, director.computeBlockMask( pool, 0 ) );
    // 낮이면 다시 막힌다.
    context._condition._phase = DayPhase::Day;
    director.setContext( context );
    SW_EXPECT_EQUAL( AiDirectorBlock::kCondition, director.computeBlockMask( pool, 0 ) );

    // 말을 탔고 수배되지 않았고 맑다 → 낯선 이. 수배 태그가 붙거나 폭풍이면 막힌다.
    SW_EXPECT_EQUAL( 0u, director.computeBlockMask( pool, 1 ) );
    playerTags.addTag( TagID::request( "Player.Wanted" ) );
    SW_EXPECT_EQUAL( AiDirectorBlock::kCondition, director.computeBlockMask( pool, 1 ) );
    playerTags.removeTag( TagID::request( "Player.Wanted" ) );
    context._condition._weather = hashed_string( "storm" );
    director.setContext( context );
    SW_EXPECT_EQUAL( AiDirectorBlock::kCondition, director.computeBlockMask( pool, 1 ) );

    // 현상금 2 이상이지만 긴장도 0.5..0.9 밖이다.
    SW_EXPECT_EQUAL( AiDirectorBlock::kIntensity, director.computeBlockMask( pool, 2 ) );
    flags.setFlag( hashed_string( "bounty" ), 1 );
    SW_EXPECT_EQUAL( AiDirectorBlock::kIntensity | AiDirectorBlock::kCondition, director.computeBlockMask( pool, 2 ) );

    string explanation;
    director.explain( explanation );
    SW_EXPECT_TRUE_MSG( explanation.find( "- ambush" ) != string::npos && explanation.find( "[condition]" ) != string::npos, explanation.c_str() );
    SW_EXPECT_TRUE_MSG( explanation.find( "- late" ) != string::npos && explanation.find( "[cycle]" ) != string::npos, explanation.c_str() );
    SW_EXPECT_TRUE_MSG( explanation.find( "phase 'Hunt'" ) != string::npos, explanation.c_str() );
}

/**
 * @brief [AiDirectorTest] 보상 밀도 — 예산 풀은 필요 신호와 단계의 보상 배율만큼 빨리 쌓이고, 예산 상한보다 비싼 항목은 고르지 않는다
 */
SW_TEST_CASE( AiDirectorTest, RewardDensityFollowsNeedAndPhase )
{
    using Internal = AiDirectorTestInternal;
    AiDirectorProfile profile;
    SW_ASSERT_TRUE( profile.loadFromXmlText( Internal::kRewardXml, "Reward" ) );
    const auto countRewards = [&]( const utf8* pPhase, float32 need ) -> int32
    {
        AiDirector director;
        director.initialize( &profile, nullptr, 9u );
        SW_EXPECT_TRUE( director.forcePhase( hashed_string( pPhase ) ) );
        SW_EXPECT_TRUE( director.getBuiltinIntensityModel().setSignal( hashed_string( "lowAmmo" ), need ) );
        for ( int32 step = 0; step < 100; ++step )
            director.update( 0.1f );
        vector<AiDirectorEvent> listEvent;
        director.drainEvents( listEvent );
        SW_EXPECT_EQUAL( 0, Internal::countKind( listEvent, AiDirectorEventKind::Reward, "crate" ) );
        SW_EXPECT_EQUAL( 0, Internal::countKind( listEvent, AiDirectorEventKind::Encounter ) );
        return Internal::countKind( listEvent, AiDirectorEventKind::Reward, "ammo" );
    };
    // 초당 1 × 10 초 = 10, 필요 1 이면 두 배, 쉼(보상 배율 3)이면 세 배.
    const int32 base = countRewards( "Fight", 0.0f );
    const int32 need = countRewards( "Fight", 1.0f );
    const int32 rest = countRewards( "Rest", 0.0f );
    SW_EXPECT_TRUE_MSG( 9 <= base && base <= 10, std::to_string( base ).c_str() );
    SW_EXPECT_TRUE_MSG( 19 <= need && need <= 20, std::to_string( need ).c_str() );
    SW_EXPECT_TRUE_MSG( 29 <= rest && rest <= 30, std::to_string( rest ).c_str() );
}

/**
 * @brief [AiDirectorTest] 결정성 — 같은 씨앗 · 같은 대본이면 사건 하나하나와 상태 해시가 같고, 씨앗이 다르면 갈린다
 */
SW_TEST_CASE( AiDirectorTest, SameSeedRepeatsDifferentSeedDiffers )
{
    using Internal = AiDirectorTestInternal;
    vector<AiDirectorEvent> listFirst;
    vector<AiDirectorEvent> listSecond;
    vector<AiDirectorEvent> listOther;
    const uint64            hashFirst  = Internal::runScript( 21u, listFirst );
    const uint64            hashSecond = Internal::runScript( 21u, listSecond );
    const uint64            hashOther  = Internal::runScript( 2121u, listOther );
    SW_ASSERT_TRUE( hashFirst != 0 );
    SW_EXPECT_EQUAL( hashFirst, hashSecond );
    SW_EXPECT_TRUE( hashFirst != hashOther );
    SW_ASSERT_EQUAL( listFirst.size(), listSecond.size() );
    for ( size_t index = 0; index < listFirst.size(); ++index )
    {
        SW_EXPECT_TRUE( listFirst[index]._kind == listSecond[index]._kind && listFirst[index]._id == listSecond[index]._id );
        SW_EXPECT_EQUAL( listFirst[index]._time, listSecond[index]._time );
    }
    // 대본이 실제로 단계 셋을 모두 돌았는가 — 아니면 위 비교가 아무것도 보지 않은 것이다.
    SW_EXPECT_TRUE( Internal::countKind( listFirst, AiDirectorEventKind::PhaseChanged, "Relax" ) >= 2 );
    SW_EXPECT_TRUE( Internal::countKind( listFirst, AiDirectorEventKind::Spawned ) >= 20 );
    SW_EXPECT_TRUE( Internal::countKind( listFirst, AiDirectorEventKind::Despawned ) >= 10 );
}

/**
 * @brief [AiDirectorTest] 추적 — 최근 사건을 시간 순으로 적고(고리가 넘쳐도 순서를 지킨다), 단계 줄에는 나간 길과 순환이 든다
 */
SW_TEST_CASE( AiDirectorTest, TraceKeepsRecentEventsInOrder )
{
    using Internal = AiDirectorTestInternal;
    vector<AiDirectorEvent> listEvent;
    AiDirectorProfile       profile;
    SpawnTable              table;
    SW_ASSERT_TRUE( profile.loadFromXmlText( Internal::kPacingXml, "Pacing" ) );
    SW_ASSERT_TRUE( table.loadFromXmlText( Internal::kSpawnXml, "Spawn" ) );
    AiDirector director;
    director.initialize( &profile, &table, 1u );
    vector<AiDirectorEvent> listStep;
    for ( int32 step = 0; step < 6000; ++step )
    {
        if ( step % 300 == 0 )
            (void)director.getBuiltinIntensityModel().addSignal( hashed_string( "damage" ), 9.0f );
        director.update( 0.1f );
        // 선 개체는 바로 쓰러진다 — 동시 상한에 막히지 않고 사건이 고리를 넘치도록 쌓인다.
        listStep.clear();
        director.drainEvents( listStep );
        for ( const AiDirectorEvent& event : listStep )
        {
            if ( event._kind == AiDirectorEventKind::Spawned )
                (void)director.notifyDespawned( event._spawnId );
        }
        listEvent.insert( listEvent.end(), listStep.begin(), listStep.end() );
    }
    director.drainEvents( listEvent );
    SW_ASSERT_TRUE( static_cast<int32>( listEvent.size() ) > AiDirector::kMaxTraceEvent );
    string trace;
    director.dumpTrace( trace );
    int32   lineCount = 0;
    size_t  lineStart = 0;
    float32 lastTime  = -1.0f;
    while ( lineStart < trace.size() )
    {
        const size_t lineEnd = trace.find( '\n', lineStart );
        const string line    = trace.substr( lineStart, lineEnd - lineStart );
        float32      time    = 0.0f;
        SW_ASSERT_TRUE( line.size() > 2 && line[0] == '[' );
        SW_ASSERT_TRUE( StringUtil::parseFloat( line.substr( 1, line.find( 's' ) - 1 ), time ) );
        SW_EXPECT_TRUE( time >= lastTime );
        lastTime  = time;
        lineStart = lineEnd + 1;
        ++lineCount;
    }
    SW_EXPECT_EQUAL( AiDirector::kMaxTraceEvent, lineCount );
    SW_EXPECT_NEAR_EQUAL( listEvent.back()._time, lastTime, 0.01f );
    SW_EXPECT_TRUE_MSG( trace.find( "PhaseChanged 'Relax' from 'Peak' exit 0" ) != string::npos, trace.c_str() );
}

/**
 * @brief [AiDirectorTest] 상태를 쓰고 같은 프로필의 새 감독에 읽으면 같은 자리에서 이어 간다 — 이후 사건 · 상태 해시가 원본과 같다. 모양이 다른 프로필은 거절한다
 * @details 핫 리로드 · 세이브가 감독을 처음부터 돌리면 웨이브가 1 로 돌아간다.
 */
SW_TEST_CASE( AiDirectorTest, StateRoundTripContinuesFromTheSamePlace )
{
    using Internal = AiDirectorTestInternal;
    AiDirectorProfile profile;
    SpawnTable        table;
    SW_ASSERT_TRUE( profile.loadFromXmlText( Internal::kPacingXml, "Pacing" ) );
    SW_ASSERT_TRUE( table.loadFromXmlText( Internal::kSpawnXml, "Spawn" ) );

    /** @brief 대본 한 걸음 — 30 초마다 맞고, 처음 6 초는 둘러싸이고, 스폰은 3 초 뒤 돌려준다(산 목록은 게임의 몫이라 감독 밖에 든다). */
    struct Script
    {
        vector<uint32>  _listAliveId;
        vector<float32> _listAliveTime;

        void step( AiDirector& director, int32 stepIndex, vector<AiDirectorEvent>& outListEvent )
        {
            if ( stepIndex % 300 == 0 )
                (void)director.getBuiltinIntensityModel().addSignal( hashed_string( "damage" ), 9.0f );
            (void)director.getBuiltinIntensityModel().setSignal( hashed_string( "nearby" ), stepIndex % 300 < 60 ? 3.0f : 0.0f );
            director.update( 0.1f );
            const size_t first = outListEvent.size();
            director.drainEvents( outListEvent );
            for ( size_t index = first; index < outListEvent.size(); ++index )
            {
                if ( outListEvent[index]._kind == AiDirectorEventKind::Spawned && outListEvent[index]._spawnId != 0 )
                {
                    _listAliveId.push_back( outListEvent[index]._spawnId );
                    _listAliveTime.push_back( director.getTime() );
                }
            }
            for ( size_t index = 0; index < _listAliveId.size(); )
            {
                if ( director.getTime() - _listAliveTime[index] < 3.0f )
                {
                    ++index;
                    continue;
                }
                (void)director.notifyDespawned( _listAliveId[index] );
                _listAliveId.erase( _listAliveId.begin() + static_cast<ptrdiff_t>( index ) );
                _listAliveTime.erase( _listAliveTime.begin() + static_cast<ptrdiff_t>( index ) );
            }
        }
    };

    AiDirector              original;
    Script                  originalScript;
    vector<AiDirectorEvent> listOriginal;
    original.initialize( &profile, &table, 77u );
    // 한 번은 돌았고(순환 ≥ 1) 산 스폰이 있는 자리에서 저장해야 "이어 간다" 가 뜻이 있다 — 산 개체 목록이 실리지 않으면 예산 · 상한이 갈린다.
    int32 saveStep = 0;
    for ( ; saveStep < 2000; ++saveStep )
    {
        originalScript.step( original, saveStep, listOriginal );
        if ( saveStep >= 700 && original.getSpawnDirector().getTotalAliveCount() > 0 )
            break;
    }
    ++saveStep;
    SW_ASSERT_TRUE( original.getCycle() >= 1 );
    SW_ASSERT_TRUE( original.getSpawnDirector().getTotalAliveCount() > 0 );

    Archive written;
    original.writeState( written );
    AiDirector restored;
    restored.initialize( &profile, &table, 77u );
    Archive reader( written.getData(), written.getSize() );
    SW_ASSERT_TRUE( restored.readState( reader ) );
    SW_EXPECT_EQUAL( uint64( 0 ), reader.getRemainingBytes() );
    SW_EXPECT_EQUAL( original.computeStateHash(), restored.computeStateHash() );
    SW_EXPECT_EQUAL( original.getCycle(), restored.getCycle() );
    SW_EXPECT_TRUE( original.getPhase() == restored.getPhase() );

    Script                  restoredScript = originalScript; // 산 적 목록은 게임이 다시 세운 그대로
    vector<AiDirectorEvent> listAfterOriginal;
    vector<AiDirectorEvent> listAfterRestored;
    for ( int32 stepIndex = saveStep; stepIndex < saveStep + 700; ++stepIndex )
    {
        originalScript.step( original, stepIndex, listAfterOriginal );
        restoredScript.step( restored, stepIndex, listAfterRestored );
    }
    SW_EXPECT_EQUAL( original.computeStateHash(), restored.computeStateHash() );
    SW_ASSERT_EQUAL( listAfterOriginal.size(), listAfterRestored.size() );
    for ( size_t index = 0; index < listAfterOriginal.size(); ++index )
        SW_EXPECT_TRUE( listAfterOriginal[index]._kind == listAfterRestored[index]._kind && listAfterOriginal[index]._id == listAfterRestored[index]._id );
    SW_EXPECT_TRUE( Internal::countKind( listAfterOriginal, AiDirectorEventKind::Spawned ) > 0 );

    // 모양이 다른 프로필(단계 하나 · 풀 하나)은 거절하고 그대로다.
    AiDirectorProfile other;
    SW_ASSERT_TRUE( other.loadFromXmlText( Internal::kCooldownXml, "Cooldown" ) );
    AiDirector mismatched;
    mismatched.initialize( &other, nullptr, 77u );
    const uint64 hashBefore = mismatched.computeStateHash();
    Archive      mismatchReader( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( mismatched.readState( mismatchReader ) );
    SW_EXPECT_EQUAL( hashBefore, mismatched.computeStateHash() );
}
