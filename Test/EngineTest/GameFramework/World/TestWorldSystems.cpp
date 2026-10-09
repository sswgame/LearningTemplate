#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/AI/SpawnDirector.h"
#include "GameFramework/Base/Input/TimingJudge.h"
#include "GameFramework/Base/Interaction/InteractionProgress.h"
#include "GameFramework/Base/World/AreaGraph.h"
#include "GameFramework/Base/World/GameFlags.h"

#include "TestFramework/TestFramework.h"

// 월드 공통 시스템 — 플래그 조건식(우선순위 · 괄호 · 잘못된 식), 방 그래프(잠긴 문 우회 · 일방통행 · 탐색률 · 막힌 경계), 진행형 상호작용(다인 · 끊김 · 퇴행 · 스킬 체크), 스폰 감독(예산 · 상한 · 최소 시각 · 재스폰 · 씨앗).

using namespace sw;

namespace
{
    constexpr const utf8* kAreaGraphXml = R"(
<AreaGraph>
  <Area id="hall" name="Hall" x="0" y="0" w="10" h="8" region="Floor1"/>
  <Area id="kitchen" x="10" y="0" w="6" h="8" region="Floor1"/>
  <Area id="attic" x="0" y="8" w="10" h="4" region="Floor1"/>
  <Area id="cellar" x="0" y="-6" w="8" h="6" region="Basement"/>
  <Area id="garden" x="16" y="0" w="12" h="12" region="Basement"/>
  <Area id="ledge" x="-6" y="4" w="4" h="2" region="Basement"/>
  <Area id="vault" x="16" y="-6" w="4" h="4" region="Basement"/>
  <Link from="hall" to="kitchen" kind="Passage"/>
  <Link from="hall" to="cellar" requires="hasKey_red" kind="Door"/>
  <Link from="kitchen" to="garden"/>
  <Link from="garden" to="cellar"/>
  <Link from="hall" to="attic" requires="hasDoubleJump || ladder>=2"/>
  <Link from="ledge" to="hall" oneWay="true"/>
  <Link from="garden" to="vault" requires="hasKey &amp;&amp;"/>
  <Link from="hall" to="nowhere"/>
</AreaGraph>
)";

    constexpr const utf8* kSkillCheckWindowXml = R"(
<TimingWindows>
  <Window grade="Great" early="0.05" late="0.05"/>
  <Window grade="Good" early="0.15" late="0.15"/>
</TimingWindows>
)";

    constexpr const utf8* kSpawnTableXml = R"(
<SpawnTable budgetPerMinute="60" maxBudget="3" startBudget="0">
  <Entry id="bug" cost="1" weight="1" max="2"/>
  <Entry id="giant" cost="3" weight="1" max="1" minTime="10" tags="Outdoor"/>
</SpawnTable>
)";

    constexpr const utf8* kSpawnMixXml = R"(
<SpawnTable budgetPerMinute="30" maxBudget="8" startBudget="2">
  <Entry id="a" cost="1" weight="3"/>
  <Entry id="b" cost="2" weight="2" max="3"/>
  <Entry id="c" cost="4" weight="1" max="1" minTime="20"/>
  <Curve time="600" scale="2"/>
  <Curve time="0" scale="0.5"/>
</SpawnTable>
)";

    /** @brief 스킬 체크 하나의 기록입니다(결정성 비교용). */
    struct SkillCheckRecord
    {
        float32 _startTime{ 0.0f };
        uint32  _actorId{ 0 };
        bool    _bSuccess{ false };
    };

    /** @brief 두 사람이 붙은 발전기를 돌리며 체크에 번갈아 응답합니다 — 맞힘, 늦게 누름, 응답 안 함. */
    void runSkillChecks( uint32 seed, const TimingJudge& judge, vector<SkillCheckRecord>& outListRecord, float32& outProgress, int32& outNoiseCount )
    {
        InteractionConfig config;
        config._duration           = 100.0f;
        config._maxParticipants    = 2;
        config._skillCheckInterval = 2.0f;
        config._skillCheckLeadTime = 1.0f;
        config._skillCheckPenalty  = 0.1f;
        config._skillCheckBonus    = 0.01f;
        config._gradeBonus.setValue( hashed_string( "Great" ), 0.1f );
        InteractionProgress generator;
        generator.initialize( config, &judge, seed );
        (void)generator.join( 7 );
        (void)generator.join( 9 );

        outListRecord.clear();
        outNoiseCount                         = 0;
        int32                    startedCount = 0;
        vector<InteractionEvent> listEvent;
        for ( int32 step = 0; step < 300; ++step )
        {
            generator.update( 0.1f );
            listEvent.clear();
            generator.drainEvents( listEvent );
            for ( const InteractionEvent& event : listEvent )
            {
                if ( event._kind == InteractionEvent::Kind::SkillCheckStarted )
                {
                    SkillCheckRecord record;
                    record._startTime = generator.getTime();
                    record._actorId   = event._actorId;
                    outListRecord.push_back( record );
                    const uint32  otherActor = event._actorId == 7 ? 9u : 7u;
                    const float32 before     = generator.getProgress();
                    SW_EXPECT_FALSE( generator.respondSkillCheck( otherActor, event._value ) ); // 남의 체크에는 응답할 수 없다
                    const int32 mode = startedCount % 3;
                    ++startedCount;
                    if ( mode == 0 )
                    {
                        SW_EXPECT_TRUE( generator.respondSkillCheck( event._actorId, event._value + 0.01f ) );
                        SW_EXPECT_NEAR_EQUAL( before + 0.1f, generator.getProgress(), 1.0e-5f ); // Great 보너스
                    }
                    else if ( mode == 1 )
                    {
                        SW_EXPECT_TRUE( generator.respondSkillCheck( event._actorId, event._value + 0.5f ) );
                        SW_EXPECT_NEAR_EQUAL( MathUtil::max( 0.0f, before - 0.1f ), generator.getProgress(), 1.0e-5f ); // 창 밖 = 실패
                    }
                    // mode 2 — 응답하지 않는다. 창이 닫히면 update 가 실패로 처리한다.
                }
                else if ( event._kind == InteractionEvent::Kind::SkillCheckSucceeded || event._kind == InteractionEvent::Kind::SkillCheckFailed )
                {
                    SW_ASSERT_TRUE( outListRecord.empty() == false );
                    outListRecord.back()._bSuccess = event._kind == InteractionEvent::Kind::SkillCheckSucceeded;
                    if ( event._bNoise == SW_TRUE )
                        ++outNoiseCount;
                }
            }
        }
        outProgress = generator.getProgress();
    }

    void runSpawnMix( uint32 seed, vector<SpawnEvent>& outListEvent )
    {
        SpawnTable table;
        SW_ASSERT_TRUE( table.loadFromXmlText( kSpawnMixXml, "SpawnMix" ) );
        SpawnDirector director;
        director.initialize( &table, seed );
        outListEvent.clear();
        uint32 despawnCursor = 0;
        for ( int32 step = 0; step < 240; ++step )
        {
            (void)director.update( 0.5f );
            director.drainEvents( outListEvent );
            // 몇 걸음마다 가장 오래된 것을 죽인다 — 상한이 풀려 다시 나온다.
            if ( step % 7 == 6 && director.notifyDespawned( ++despawnCursor ) == false )
                --despawnCursor;
        }
    }
} // namespace

SW_TEST_CASE( WorldSystemsTest, FlagConditionsFollowPrecedenceParenthesesAndRejectBadInput )
{
    GameFlags flags;
    flags.setFlag( hashed_string( "a" ) );
    flags.setFlag( hashed_string( "c" ), 1 );
    flags.setFlag( hashed_string( "count" ), 3 );
    flags.setFlag( hashed_string( "b" ), 0 ); // 0 은 없는 것
    SW_EXPECT_FALSE( flags.hasFlag( hashed_string( "b" ) ) );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( flags.getCount() ) );

    // && 가 || 보다 먼저 묶인다: a || (b && missing) = 참. 반대로 묶으면 (a || b) && missing = 거짓.
    SW_EXPECT_TRUE( flags.evaluate( "a || b && missing" ) );
    SW_EXPECT_FALSE( flags.evaluate( "(a || b) && missing" ) );
    SW_EXPECT_TRUE( flags.evaluate( "!b && a" ) );
    SW_EXPECT_FALSE( flags.evaluate( "!(a && c)" ) );
    SW_EXPECT_TRUE( flags.evaluate( "!!a" ) );
    SW_EXPECT_TRUE( flags.evaluate( "count>=3" ) );
    SW_EXPECT_FALSE( flags.evaluate( "count > 3" ) );
    SW_EXPECT_TRUE( flags.evaluate( "count==3 && a" ) );
    SW_EXPECT_FALSE( flags.evaluate( "count < c" ) );
    SW_EXPECT_TRUE( flags.evaluate( "count != c && -1 < b" ) );
    SW_EXPECT_TRUE( flags.evaluate( "  " ) ); // 조건 없음

    // 잘못된 식은 거짓 — 참이 될 수 있는 앞부분이 있어도.
    const utf8* const arrBadExpression[] = { "a &&", "(a", "a)", "a b", "count >=", "&& a", "a ||| c", "a & c", "99999999999", "!" };
    for ( const utf8* pExpression : arrBadExpression )
    {
        bool bResult = true;
        SW_EXPECT_FALSE( GameFlags::parseCondition( pExpression, flags, bResult ) );
        SW_EXPECT_FALSE( bResult );
        SW_EXPECT_FALSE( flags.evaluate( pExpression ) );
    }
    bool bResult = false;
    SW_EXPECT_TRUE( GameFlags::parseCondition( "((a))", flags, bResult ) );
    SW_EXPECT_TRUE( bResult );

    // 리비전은 값이 실제로 바뀔 때만 오른다.
    const uint32 revision = flags.getRevision();
    flags.setFlag( hashed_string( "count" ), 3 );
    SW_EXPECT_EQUAL( revision, flags.getRevision() );
    SW_EXPECT_EQUAL( 5, flags.addFlag( hashed_string( "count" ), 2 ) );
    SW_EXPECT_EQUAL( revision + 1, flags.getRevision() );
    SW_EXPECT_TRUE( flags.clearFlag( hashed_string( "c" ) ) );
    SW_EXPECT_FALSE( flags.clearFlag( hashed_string( "c" ) ) );

    // 세이브 목록은 이름 순, 되살리면 같은 값.
    flags.setFlag( hashed_string( "Zeta" ), -2 );
    flags.setFlag( hashed_string( "beta" ), 7 );
    vector<GameFlagEntry> listEntry;
    flags.fillEntries( listEntry );
    SW_ASSERT_TRUE( listEntry.size() == 4 );
    SW_EXPECT_TRUE( listEntry[0]._name == hashed_string( "a" ) );
    SW_EXPECT_TRUE( listEntry[1]._name == hashed_string( "beta" ) );
    SW_EXPECT_TRUE( listEntry[2]._name == hashed_string( "count" ) );
    SW_EXPECT_TRUE( listEntry[3]._name == hashed_string( "Zeta" ) );
    GameFlags restored;
    restored.restoreEntries( listEntry );
    SW_EXPECT_EQUAL( -2, restored.getFlag( hashed_string( "Zeta" ) ) );
    SW_EXPECT_EQUAL( 5, restored.getFlag( hashed_string( "count" ) ) );
    SW_EXPECT_TRUE( restored.evaluate( "beta >= 7 && Zeta < 0" ) );
}

SW_TEST_CASE( WorldSystemsTest, AreaGraphDetoursLockedDoorsAndReportsTheFrontier )
{
    AreaGraph graph;
    SW_ASSERT_TRUE( graph.loadFromXmlText( kAreaGraphXml, "AreaGraphTest" ) );
    SW_EXPECT_EQUAL( 7, static_cast<int32>( graph.getAreas().size() ) );
    SW_EXPECT_EQUAL( 7, static_cast<int32>( graph.getLinks().size() ) ); // 모르는 방으로 가는 연결은 빠진다
    const hashed_string hall( "hall" );
    const hashed_string cellar( "cellar" );

    // 잠긴 문을 돌아간다 — 열쇠가 생기면 바로 간다.
    GameFlags             flags;
    vector<hashed_string> listPath;
    SW_ASSERT_TRUE( graph.findPath( hall, cellar, flags, listPath ) );
    SW_ASSERT_TRUE( listPath.size() == 4 );
    SW_EXPECT_TRUE( listPath[1] == hashed_string( "kitchen" ) );
    SW_EXPECT_TRUE( listPath[2] == hashed_string( "garden" ) );
    SW_EXPECT_FALSE( graph.canTraverse( hall, cellar, flags ) );
    flags.setFlag( hashed_string( "hasKey_red" ) );
    SW_EXPECT_TRUE( graph.canTraverse( cellar, hall, flags ) ); // 양방향 문은 반대쪽에서도
    SW_ASSERT_TRUE( graph.findPath( hall, cellar, flags, listPath ) );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( listPath.size() ) );

    // 일방통행: 턱에서 홀로 떨어질 수는 있어도 올라갈 수는 없다.
    SW_EXPECT_TRUE( graph.canTraverse( hashed_string( "ledge" ), hall, flags ) );
    SW_EXPECT_FALSE( graph.canTraverse( hall, hashed_string( "ledge" ), flags ) );
    SW_EXPECT_FALSE( graph.findPath( hall, hashed_string( "ledge" ), flags, listPath ) );
    SW_EXPECT_TRUE( listPath.empty() );
    // 잘못된 조건의 문은 늘 잠긴다.
    flags.setFlag( hashed_string( "hasKey" ) );
    SW_EXPECT_FALSE( graph.canTraverse( hashed_string( "garden" ), hashed_string( "vault" ), flags ) );
    // 조건식의 비교도 문에 쓴다.
    flags.setFlag( hashed_string( "ladder" ), 1 );
    SW_EXPECT_FALSE( graph.canTraverse( hall, hashed_string( "attic" ), flags ) );
    flags.setFlag( hashed_string( "ladder" ), 2 );
    SW_EXPECT_TRUE( graph.canTraverse( hall, hashed_string( "attic" ), flags ) );

    // 방문 · 발견 — 잠긴 문 너머도 지도에 보이지만, 들어오기만 하는 일방통행의 출발 쪽은 보이지 않는다.
    SW_EXPECT_TRUE( graph.enterArea( hall ) );
    SW_EXPECT_FALSE( graph.enterArea( hall ) );
    SW_EXPECT_FALSE( graph.enterArea( hashed_string( "nowhere" ) ) );
    SW_EXPECT_TRUE( graph.isDiscovered( cellar ) );
    SW_EXPECT_TRUE( graph.isDiscovered( hashed_string( "attic" ) ) );
    SW_EXPECT_FALSE( graph.isDiscovered( hashed_string( "ledge" ) ) );
    SW_EXPECT_FALSE( graph.isVisited( cellar ) );
    (void)graph.enterArea( hashed_string( "kitchen" ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f / 7.0f, graph.computeExplorationRatio(), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 2.0f / 3.0f, graph.computeRegionRatio( hashed_string( "Floor1" ) ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, graph.computeRegionRatio( hashed_string( "Basement" ) ), 1.0e-6f );

    // 막힌 경계: 열쇠 · 사다리가 없으면 지하실 문과 다락 문. 열린 부엌 → 정원은 아니다.
    GameFlags               noFlags;
    vector<const AreaLink*> listFrontier;
    graph.collectLockedFrontier( noFlags, listFrontier );
    SW_ASSERT_TRUE( listFrontier.size() == 2 );
    SW_EXPECT_TRUE( listFrontier[0]->_to == cellar );
    SW_EXPECT_TRUE( listFrontier[1]->_to == hashed_string( "attic" ) );
    noFlags.setFlag( hashed_string( "hasKey_red" ) );
    graph.collectLockedFrontier( noFlags, listFrontier );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( listFrontier.size() ) );
    // 지하실을 돌아서 가 보면 그 문은 더는 경계가 아니다(양쪽 다 가 봤다).
    (void)graph.enterArea( hashed_string( "garden" ) );
    (void)graph.enterArea( cellar );
    graph.collectLockedFrontier( GameFlags{}, listFrontier );
    SW_ASSERT_TRUE( listFrontier.size() == 2 );
    SW_EXPECT_TRUE( listFrontier[0]->_to == hashed_string( "attic" ) );
    SW_EXPECT_TRUE( listFrontier[1]->_to == hashed_string( "vault" ) );

    vector<AreaRegionProgress> listRegion;
    graph.collectRegionProgress( listRegion );
    SW_ASSERT_TRUE( listRegion.size() == 2 );
    SW_EXPECT_TRUE( listRegion[1]._region == hashed_string( "Basement" ) );
    SW_EXPECT_EQUAL( 2, listRegion[1]._visitedCount );
    SW_EXPECT_EQUAL( 4, listRegion[1]._totalCount );

    // 세이브 → 새 그래프에 되살리기.
    vector<hashed_string> listVisited;
    vector<hashed_string> listDiscovered;
    graph.fillVisitedAreas( listVisited );
    graph.fillDiscoveredAreas( listDiscovered );
    SW_ASSERT_TRUE( listVisited.size() == 4 );
    SW_EXPECT_TRUE( listVisited[0] == cellar ); // 이름 순
    SW_EXPECT_TRUE( listVisited[3] == hashed_string( "kitchen" ) );
    AreaGraph loaded;
    SW_ASSERT_TRUE( loaded.loadFromXmlText( kAreaGraphXml, "AreaGraphTest" ) );
    loaded.restoreState( listVisited, listDiscovered );
    SW_EXPECT_NEAR_EQUAL( graph.computeExplorationRatio(), loaded.computeExplorationRatio(), 1.0e-6f );
    SW_EXPECT_TRUE( loaded.isDiscovered( hashed_string( "vault" ) ) );
    SW_EXPECT_FALSE( loaded.isVisited( hashed_string( "vault" ) ) );
    SW_EXPECT_EQUAL( 1, loaded.discoverRegion( hashed_string( "Basement" ) ) ); // 지하 지도 — 턱만 새로
    SW_EXPECT_TRUE( loaded.isDiscovered( hashed_string( "ledge" ) ) );
}

SW_TEST_CASE( WorldSystemsTest, InteractionScalesWithParticipantsRegressesAndResets )
{
    InteractionConfig config;
    config._duration             = 10.0f;
    config._maxParticipants      = 2;
    config._listParticipantScale = { 1.0f, 1.8f };
    config._regressionRate       = 0.05f;
    SW_EXPECT_NEAR_EQUAL( 1.8f, config.computeParticipantScale( 5 ), 1.0e-6f ); // 목록보다 많으면 마지막 값
    InteractionConfig formula;
    formula._extraParticipantScale = 0.5f;
    SW_EXPECT_NEAR_EQUAL( 2.0f, formula.computeParticipantScale( 3 ), 1.0e-6f );

    InteractionProgress generator;
    generator.initialize( config, nullptr, 1 );
    SW_EXPECT_TRUE( generator.join( 1 ) );
    SW_EXPECT_FALSE( generator.join( 1 ) );
    SW_EXPECT_TRUE( generator.join( 2 ) );
    SW_EXPECT_FALSE( generator.join( 3 ) ); // 가득
    generator.update( 1.0f );
    SW_EXPECT_NEAR_EQUAL( 0.18f, generator.getProgress(), 1.0e-5f );
    SW_EXPECT_TRUE( generator.leave( 2 ) );
    SW_EXPECT_FALSE( generator.leave( 2 ) );
    generator.update( 1.0f );
    SW_EXPECT_NEAR_EQUAL( 0.28f, generator.getProgress(), 1.0e-5f );

    // 모두 떠나면 끊김 — 발전기는 유지하고, 아무도 없으니 줄어든다(퇴행 알림은 한 번).
    vector<InteractionEvent> listEvent;
    generator.drainEvents( listEvent );
    listEvent.clear();
    SW_EXPECT_TRUE( generator.leave( 1 ) );
    SW_EXPECT_NEAR_EQUAL( 0.28f, generator.getProgress(), 1.0e-5f );
    generator.update( 2.0f );
    SW_EXPECT_NEAR_EQUAL( 0.18f, generator.getProgress(), 1.0e-5f );
    generator.update( 1.0f );
    SW_EXPECT_NEAR_EQUAL( 0.13f, generator.getProgress(), 1.0e-5f );
    generator.drainEvents( listEvent );
    int32 interruptedCount = 0;
    int32 regressionCount  = 0;
    for ( const InteractionEvent& event : listEvent )
    {
        interruptedCount += event._kind == InteractionEvent::Kind::Interrupted ? 1 : 0;
        regressionCount += event._kind == InteractionEvent::Kind::RegressionStarted ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 1, interruptedCount );
    SW_EXPECT_EQUAL( 1, regressionCount );

    // 다시 붙으면 퇴행이 멈추고 차오르며, 끝나면 더는 붙을 수 없다.
    SW_EXPECT_TRUE( generator.join( 1 ) );
    int32 steps = 0;
    while ( generator.isCompleted() == false && steps < 1000 )
    {
        generator.update( 0.1f );
        ++steps;
    }
    SW_EXPECT_TRUE( 86 <= steps && steps <= 88 ); // 0.87 남음 / 초당 0.1
    SW_EXPECT_NEAR_EQUAL( 1.0f, generator.getProgress(), 1.0e-6f );
    SW_EXPECT_FALSE( generator.join( 2 ) );
    generator.update( 5.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, generator.getProgress(), 1.0e-6f );
    listEvent.clear();
    generator.drainEvents( listEvent );
    SW_ASSERT_TRUE( listEvent.empty() == false );
    SW_EXPECT_TRUE( listEvent.back()._kind == InteractionEvent::Kind::Completed );

    // 문 따기는 끊기면 처음부터.
    config._bResetOnInterrupt = SW_TRUE;
    InteractionProgress lockpick;
    lockpick.initialize( config, nullptr, 1 );
    (void)lockpick.join( 4 );
    lockpick.update( 5.0f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, lockpick.getProgress(), 1.0e-5f );
    (void)lockpick.leave( 4 );
    SW_EXPECT_NEAR_EQUAL( 0.0f, lockpick.getProgress(), 1.0e-6f );
}

SW_TEST_CASE( WorldSystemsTest, SkillChecksJudgeTimingAndRepeatWithTheSameSeed )
{
    TimingJudge judge;
    SW_ASSERT_TRUE( judge.loadFromXmlText( kSkillCheckWindowXml, "SkillCheck" ) );

    vector<SkillCheckRecord> listFirst;
    vector<SkillCheckRecord> listSecond;
    vector<SkillCheckRecord> listOther;
    float32                  firstProgress  = 0.0f;
    float32                  secondProgress = 0.0f;
    float32                  otherProgress  = 0.0f;
    int32                    firstNoise     = 0;
    int32                    secondNoise    = 0;
    int32                    otherNoise     = 0;
    runSkillChecks( 1234u, judge, listFirst, firstProgress, firstNoise );
    runSkillChecks( 1234u, judge, listSecond, secondProgress, secondNoise );
    runSkillChecks( 98765u, judge, listOther, otherProgress, otherNoise );

    // 30 초에 평균 2 초 간격(+ 응답 대기) — 여러 번 뜬다. 맞힘 · 늦게 누름 · 응답 없음이 차례로.
    SW_ASSERT_TRUE( listFirst.size() >= 6 );
    for ( size_t index = 0; index + 1 < listFirst.size(); ++index )
    {
        SW_EXPECT_TRUE( listFirst[index]._bSuccess == ( index % 3 == 0 ) );
    }
    int32 failedCount = 0;
    for ( size_t index = 0; index + 1 < listFirst.size(); ++index )
    {
        failedCount += listFirst[index]._bSuccess ? 0 : 1;
    }
    SW_EXPECT_TRUE( firstNoise >= failedCount ); // 실패는 소음을 낸다

    // 같은 씨앗이면 같은 때 같은 사람에게 — 다른 씨앗이면 다르다.
    SW_ASSERT_TRUE( listFirst.size() == listSecond.size() );
    for ( size_t index = 0; index < listFirst.size(); ++index )
    {
        SW_EXPECT_NEAR_EQUAL( listFirst[index]._startTime, listSecond[index]._startTime, 1.0e-6f );
        SW_EXPECT_EQUAL( listFirst[index]._actorId, listSecond[index]._actorId );
    }
    SW_EXPECT_NEAR_EQUAL( firstProgress, secondProgress, 1.0e-6f );
    SW_EXPECT_EQUAL( firstNoise, secondNoise );
    bool bDiffers = listFirst.size() != listOther.size();
    for ( size_t index = 0; bDiffers == false && index < listFirst.size(); ++index )
    {
        bDiffers = MathUtil::abs( listFirst[index]._startTime - listOther[index]._startTime ) > 1.0e-4f || listFirst[index]._actorId != listOther[index]._actorId;
    }
    SW_EXPECT_TRUE( bDiffers );
}

SW_TEST_CASE( WorldSystemsTest, SpawnDirectorSpendsBudgetWithinLimitsAndRepeatsWithTheSameSeed )
{
    SpawnTable table;
    SW_ASSERT_TRUE( table.loadFromXmlText( kSpawnTableXml, "SpawnTest" ) );
    SpawnDirector director;
    director.initialize( &table, 7u );
    vector<SpawnEvent> listEvent;

    // 초당 1 씩 쌓인다 — 1 이 되면 벌레.
    SW_EXPECT_EQUAL( 0, director.update( 0.5f ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, director.getBudget(), 1.0e-6f );
    SW_EXPECT_EQUAL( 1, director.update( 0.5f ) );
    SW_EXPECT_EQUAL( 1, director.update( 1.0f ) );
    SW_EXPECT_EQUAL( 2, director.getAliveCount( hashed_string( "bug" ) ) );
    // 벌레는 상한(2), 거인은 아직 10 초 전 — 예산만 쌓이고 상한(3)에서 멈춘다.
    for ( int32 step = 0; step < 6; ++step )
    {
        SW_EXPECT_EQUAL( 0, director.update( 1.0f ) );
    }
    SW_EXPECT_NEAR_EQUAL( 3.0f, director.getBudget(), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 8.0f, director.getTime(), 1.0e-5f );
    SW_EXPECT_EQUAL( 0, director.update( 1.5f ) );
    SW_EXPECT_EQUAL( 1, director.update( 0.5f ) ); // 10 초 — 거인
    SW_EXPECT_EQUAL( 1, director.getAliveCount( hashed_string( "giant" ) ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, director.getBudget(), 1.0e-5f );

    director.drainEvents( listEvent );
    SW_ASSERT_TRUE( listEvent.size() == 3 );
    SW_EXPECT_TRUE( listEvent[0]._entryId == hashed_string( "bug" ) );
    SW_EXPECT_EQUAL( 1u, listEvent[0]._spawnId );
    SW_EXPECT_TRUE( listEvent[2]._entryId == hashed_string( "giant" ) );

    // 벌레가 죽으면 자리가 나서 다시 나온다(환불 없음 — 예산을 다시 모은다).
    SW_EXPECT_TRUE( director.notifyDespawned( listEvent[0]._spawnId ) );
    SW_EXPECT_FALSE( director.notifyDespawned( listEvent[0]._spawnId ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, director.getBudget(), 1.0e-5f );
    SW_EXPECT_EQUAL( 0, director.update( 0.5f ) );
    SW_EXPECT_EQUAL( 1, director.update( 0.5f ) );
    SW_EXPECT_EQUAL( 2, director.getAliveCount( hashed_string( "bug" ) ) );
    // 환불을 켜면 죽은 거인의 비용이 돌아온다(상한까지).
    director.setRefundOnDespawn( true );
    SW_EXPECT_TRUE( director.notifyDespawned( listEvent[2]._spawnId ) );
    SW_EXPECT_NEAR_EQUAL( 3.0f, director.getBudget(), 1.0e-5f );

    // 태그 거르기: 실내만 — 실외 거인은 나오지 않고, 태그 없는 벌레는 나온다.
    SpawnDirector indoor;
    indoor.initialize( &table, 7u );
    indoor.setAllowedTags( { hashed_string( "Indoor" ) } );
    for ( int32 step = 0; step < 20; ++step )
    {
        (void)indoor.update( 1.0f );
    }
    SW_EXPECT_EQUAL( 0, indoor.getAliveCount( hashed_string( "giant" ) ) );
    SW_EXPECT_EQUAL( 2, indoor.getAliveCount( hashed_string( "bug" ) ) );
    indoor.setAllowedTags( {} );
    SW_EXPECT_EQUAL( 1, indoor.update( 0.0f ) );
    SW_EXPECT_EQUAL( 1, indoor.getAliveCount( hashed_string( "giant" ) ) );

    // 곡선은 선형 보간, 끝 밖은 끝 값(읽는 순서와 상관없이 시각 순).
    SpawnTable mix;
    SW_ASSERT_TRUE( mix.loadFromXmlText( kSpawnMixXml, "SpawnMix" ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, mix.computeScale( -5.0f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.25f, mix.computeScale( 300.0f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, mix.computeScale( 900.0f ), 1.0e-6f );

    // 같은 씨앗이면 같은 순서 · 같은 때, 다른 씨앗이면 다르다. 상한과 최소 시각은 어느 씨앗에서도 지킨다.
    vector<SpawnEvent> listFirst;
    vector<SpawnEvent> listSecond;
    vector<SpawnEvent> listOther;
    runSpawnMix( 42u, listFirst );
    runSpawnMix( 42u, listSecond );
    runSpawnMix( 4242u, listOther );
    SW_ASSERT_TRUE( listFirst.size() == listSecond.size() );
    SW_ASSERT_TRUE( listFirst.size() > 10 );
    for ( size_t index = 0; index < listFirst.size(); ++index )
    {
        SW_EXPECT_TRUE( listFirst[index]._entryId == listSecond[index]._entryId );
        SW_EXPECT_NEAR_EQUAL( listFirst[index]._time, listSecond[index]._time, 1.0e-6f );
    }
    bool bDiffers = listFirst.size() != listOther.size();
    for ( size_t index = 0; bDiffers == false && index < listFirst.size(); ++index )
    {
        bDiffers = ( listFirst[index]._entryId == listOther[index]._entryId ) == false;
    }
    SW_EXPECT_TRUE( bDiffers );
    for ( const SpawnEvent& event : listFirst )
    {
        if ( event._kind == SpawnEvent::Kind::Spawned && event._entryId == hashed_string( "c" ) )
            SW_EXPECT_TRUE( event._time >= 20.0f );
    }
}

SW_TEST_CASE( WorldSystemsTest, AreaGraphBuildsFromCodeAndEmbeddedNodes )
{
    AreaGraph graph;
    AreaDef   hall;
    hall._id = hashed_string( "hall" );
    AreaDef vault;
    vault._id     = hashed_string( "vault" );
    vault._region = hashed_string( "Basement" );
    SW_ASSERT_TRUE( graph.addArea( hall ) );
    SW_ASSERT_TRUE( graph.addArea( vault ) );
    SW_EXPECT_FALSE( graph.addArea( hall ) );                                                      // 같은 id
    SW_EXPECT_TRUE( graph.findArea( hashed_string( "hall" ) )->_name == hashed_string( "hall" ) ); // 이름이 없으면 id

    bool bInvalid = false;
    SW_EXPECT_FALSE( graph.addLink( hashed_string( "hall" ), hashed_string( "nowhere" ), hashed_string( "Door" ), "", false, bInvalid ) );
    SW_ASSERT_TRUE( graph.addLink( hashed_string( "hall" ), hashed_string( "vault" ), hashed_string( "Door" ), "hasKey", false, bInvalid ) );
    SW_EXPECT_FALSE( bInvalid );
    GameFlags flags;
    SW_EXPECT_FALSE( graph.canTraverse( hashed_string( "hall" ), hashed_string( "vault" ), flags ) );
    flags.setFlag( hashed_string( "hasKey" ) );
    SW_EXPECT_TRUE( graph.canTraverse( hashed_string( "hall" ), hashed_string( "vault" ), flags ) );
    SW_EXPECT_TRUE( graph.enterArea( hashed_string( "vault" ) ) );

    // 다른 키트의 XML 안에 적은 그래프를 더한다(지우지 않는다 — 탐색 상태도 남는다).
    XmlDocument doc;
    SW_ASSERT_TRUE( doc.parse( "<Dungeon><Map><Area id=\"crypt\" region=\"Basement\"/><Link from=\"vault\" to=\"crypt\" oneWay=\"true\"/></Map></Dungeon>" ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( graph.loadFromNode( doc.getRoot().findChild( "Map" ), "AreaGraphTest" ) ) );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( graph.getAreas().size() ) );
    SW_EXPECT_TRUE( graph.isVisited( hashed_string( "vault" ) ) );
    SW_EXPECT_FALSE( graph.isVisited( hashed_string( "crypt" ) ) );
    SW_EXPECT_TRUE( graph.canTraverse( hashed_string( "vault" ), hashed_string( "crypt" ), flags ) );
    SW_EXPECT_FALSE( graph.canTraverse( hashed_string( "crypt" ), hashed_string( "vault" ), flags ) ); // 일방통행

    graph.clear();
    SW_EXPECT_EQUAL( 0, static_cast<int32>( graph.getAreas().size() ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( graph.getLinks().size() ) );
}
