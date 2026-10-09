// 비대칭 공포 키트 — 다인 수리 배율 · 탈출구 전원 · 남은 발전기 막힘, 스킬 체크 성공 보너스 · 실패 소음, 살인마 공격(부상 → 빈사 · 쿨다운 · 헛방) · 출혈사,
// 치료(출혈 멈춤 · 다인 배율), 갈고리 단계 · 몸부림 · 구출 · 세 번째 걸림, 판자 기절 · 몸부림 탈출 · 판자 부수기, 빠른/보통 넘기 · 창 막힘 · 사물함,
// 탈출구 · 해치 · 붕괴 · 결과, 상태 바이트 왕복 · 결정성.
#include "pch.h"

#include "Core/Network/BitStream.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/AI/AiPerception.h"
#include "GameFramework/Kits/Horror/AsymmetricHorror/AsymmetricHorrorRules.h"
#include "GameFramework/Kits/Horror/AsymmetricHorror/HorrorMatch.h"
#include "GameFramework/Kits/Horror/AsymmetricHorror/HorrorSnapshot.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr float32 kAsymmetricHorrorStep = 1.0f / 30.0f;

    constexpr const utf8* kHorrorRulesXml = R"(<AsymmetricHorrorRules survivorSpeed="4" crawlSpeed="0.7" hitHasteScale="1.5" hitHasteTime="2" bleedoutTime="20" interactRange="1.5" generatorsRequired="2">
        <Generator time="10" scales="1,1.5" maxRepairers="2" skillCheckInterval="0" kickPenalty="0.1" kickRegression="0.05" noiseRadius="50"/>
        <Heal time="4" scales="1,1.6" maxHealers="2" skillCheckInterval="0"/>
        <SkillCheck leadTime="1" failPenalty="0.1">
          <Window grade="Great" early="0.05" late="0.05" score="300" bonus="0.05"/>
          <Window grade="Good" early="0.15" late="0.15" score="100" bonus="0"/>
        </SkillCheck>
        <Hook stageTime="5" stages="3" struggleGrace="1" wiggleTime="4" wiggleStunTime="3"/>
        <Pallet stunTime="2" stunRange="1.5" breakTime="2"/>
        <Window fastTime="0.5" mediumTime="1.2" killerTime="1.7" fastSpeedRatio="0.9" noiseRadius="40" blockCount="3" blockTime="15"/>
        <Locker searchTime="1"/>
        <Endgame gateTime="3" collapseTime="10" slowScale="0.5" hatchRemaining="1"/>
        <Killer id="trapper" speedRatio="1.15" lungeRange="2.5" lungeAngle="90" hitCooldown="2.7" missCooldown="1.5" cooldownSpeedScale="0.3" terrorRadius="32"
                abilityCooldown="10" carrySpeedScale="0.8"/>
        <Category id="Objectives" cap="5000"/>
        <Category id="Brutality" cap="0"/>
        <Score action="Repair" category="Objectives" points="1000"/>
        <Score action="SkillCheck" category="Objectives" points="50"/>
        <Score action="Hit" category="Brutality" points="300"/>
        <Score action="Hook" category="Brutality" points="500"/>
      </AsymmetricHorrorRules>)";

    void runSeconds( HorrorMatch& match, float32 seconds )
    {
        const int32 stepCount = static_cast<int32>( seconds / kAsymmetricHorrorStep + 0.5f );
        for ( int32 index = 0; index < stepCount; ++index )
        {
            match.update( kAsymmetricHorrorStep );
        }
    }

    int32 countEvents( const vector<AsymmetricHorrorEvent>& listEvent, AsymmetricHorrorEvent::Kind kind )
    {
        int32 count = 0;
        for ( const AsymmetricHorrorEvent& event : listEvent )
        {
            count += event._kind == kind ? 1 : 0;
        }
        return count;
    }

    const AsymmetricHorrorEvent* findEvent( const vector<AsymmetricHorrorEvent>& listEvent, AsymmetricHorrorEvent::Kind kind )
    {
        for ( const AsymmetricHorrorEvent& event : listEvent )
        {
            if ( event._kind == kind )
                return &event;
        }
        return nullptr;
    }

    [[nodiscard]] bool loadRules( AsymmetricHorrorRulesCatalog& outCatalog, const utf8* pXml = kHorrorRulesXml ) { return outCatalog.loadFromXmlText( pXml, "AsymmetricHorrorTest" ); }

    bool beginMatch( HorrorMatch& outMatch, const AsymmetricHorrorRulesCatalog& catalog )
    {
        HorrorMatchSettings settings;
        settings._killerId  = hashed_string( "trapper" );
        settings._fixedStep = kAsymmetricHorrorStep;
        return outMatch.initialize( settings, &catalog );
    }

    /** @brief 살인마를 생존자 바로 뒤(−z 쪽)에 세우고 앞(+z)을 보게 한 뒤 휘두릅니다. */
    KillerAttackResult swingAt( HorrorMatch& match, const float3& survivorPosition )
    {
        match.setKillerPosition( float3{ survivorPosition._x, 0.0f, survivorPosition._z - 1.0f } );
        match.moveKiller( float3{ 0.0f, 0.0f, 1.0f }, 0.0f ); // 방향만 돌린다
        return match.killerAttack();
    }
} // namespace

SW_TEST_CASE( AsymmetricHorrorTest, RepairScalesWithHelpersAndPowersGates )
{
    AsymmetricHorrorRulesCatalog catalog;
    SW_ASSERT_TRUE( loadRules( catalog ) );
    SW_EXPECT_NEAR_EQUAL( catalog.getRules()._listRepairScale[1], 1.5f, 0.001f );
    SW_ASSERT_NOT_NULL( catalog.findKiller( hashed_string( "trapper" ) ) );

    HorrorMatch match;
    SW_ASSERT_TRUE( beginMatch( match, catalog ) );
    const int32 first  = match.addSurvivor( float3{ 0.0f, 0.0f, 0.0f } );
    const int32 second = match.addSurvivor( float3{ 1.0f, 0.0f, 0.0f } );
    const int32 third  = match.addSurvivor( float3{ 20.0f, 0.0f, 0.0f } );
    match.setKillerPosition( float3{ 100.0f, 0.0f, 100.0f } );
    const int32 duo  = match.addGenerator( float3{ 0.5f, 0.0f, 0.0f } );
    const int32 solo = match.addGenerator( float3{ 20.5f, 0.0f, 0.0f } );
    const int32 rest = match.addGenerator( float3{ 21.0f, 0.0f, 1.0f } );
    match.start();
    SW_EXPECT_FALSE( match.startRepair( first, solo ) ); // 멀다
    SW_ASSERT_TRUE( match.startRepair( first, duo ) );
    SW_ASSERT_TRUE( match.startRepair( second, duo ) );
    SW_ASSERT_TRUE( match.startRepair( third, solo ) );

    // 둘이면 10 ÷ 1.5 ≈ 6.7 초, 혼자면 10 초.
    runSeconds( match, 6.5f );
    SW_EXPECT_FALSE( match.findGenerator( duo )->_progress.isCompleted() );
    runSeconds( match, 0.5f );
    SW_EXPECT_TRUE( match.findGenerator( duo )->_progress.isCompleted() );
    SW_EXPECT_FALSE( match.findGenerator( solo )->_progress.isCompleted() );
    SW_EXPECT_TRUE( match.findSurvivor( first )->_activity == SurvivorActivity::None );
    SW_EXPECT_FALSE( match.areGatesPowered() );

    runSeconds( match, 3.5f );
    SW_EXPECT_TRUE( match.findGenerator( solo )->_progress.isCompleted() );
    SW_EXPECT_EQUAL( match.getCompletedGeneratorCount(), 2 );
    SW_EXPECT_TRUE( match.areGatesPowered() );
    SW_EXPECT_TRUE( match.findGenerator( rest )->_bBlocked == SW_TRUE ); // 필요한 수를 채우면 남은 것은 막힌다
    SW_EXPECT_FALSE( match.startRepair( third, rest ) );

    vector<AsymmetricHorrorEvent> listEvent;
    match.drainEvents( listEvent );
    SW_EXPECT_EQUAL( countEvents( listEvent, AsymmetricHorrorEvent::Kind::GeneratorCompleted ), 2 );
    SW_EXPECT_EQUAL( countEvents( listEvent, AsymmetricHorrorEvent::Kind::GatesPowered ), 1 );
    // 혼자 한 대를 다 고친 쪽은 1000 점, 둘이 나눈 쪽은 그 절반쯤.
    SW_EXPECT_NEAR_EQUAL( match.findSurvivor( third )->_score.getValue( hashed_string( "Objectives" ) ), 1000.0f, 1.0f );
    SW_EXPECT_NEAR_EQUAL( match.findSurvivor( first )->_score.getValue( hashed_string( "Objectives" ) ), 500.0f, 1.0f );
}

SW_TEST_CASE( AsymmetricHorrorTest, SkillChecksAndKicksMoveGeneratorProgress )
{
    // 스킬 체크가 2 초쯤마다 뜨는 규칙.
    string       xml( kHorrorRulesXml );
    const size_t at = xml.find( "skillCheckInterval=\"0\" kickPenalty" );
    SW_ASSERT_TRUE( at != string::npos );
    xml.replace( at, string( "skillCheckInterval=\"0\"" ).size(), "skillCheckInterval=\"2\"" );
    AsymmetricHorrorRulesCatalog catalog;
    SW_ASSERT_TRUE( loadRules( catalog, xml.c_str() ) );

    HorrorMatch match;
    SW_ASSERT_TRUE( beginMatch( match, catalog ) );
    const int32 survivor  = match.addSurvivor( float3{} );
    const int32 generator = match.addGenerator( float3{ 0.5f, 0.0f, 0.0f } );
    match.setKillerPosition( float3{ 3.0f, 0.0f, 0.0f } );
    match.start();
    SW_ASSERT_TRUE( match.startRepair( survivor, generator ) );

    // 첫 체크는 목표 시각에 맞춰 누른다 → 성공 · 보너스.
    vector<AsymmetricHorrorEvent> listEvent;
    const AsymmetricHorrorEvent*  pStarted = nullptr;
    for ( int32 frame = 0; frame < 200 && pStarted == nullptr; ++frame )
    {
        match.update( kAsymmetricHorrorStep );
        listEvent.clear();
        match.drainEvents( listEvent );
        pStarted = findEvent( listEvent, AsymmetricHorrorEvent::Kind::SkillCheckStarted );
    }
    SW_ASSERT_NOT_NULL( pStarted );
    const float32 targetTime = pStarted->_value;
    while ( match.findActivityProgress( survivor )->getTime() + kAsymmetricHorrorStep * 0.5f < targetTime )
    {
        match.update( kAsymmetricHorrorStep );
    }
    const float32 beforeHit = match.findGenerator( generator )->_progress.getProgress();
    SW_EXPECT_TRUE( match.respondSkillCheck( survivor, match.findActivityProgress( survivor )->getTime() ) );
    SW_EXPECT_TRUE( match.findGenerator( generator )->_progress.getProgress() > beforeHit + 0.04f );
    match.update( kAsymmetricHorrorStep );
    listEvent.clear();
    match.drainEvents( listEvent );
    const AsymmetricHorrorEvent* pResult = findEvent( listEvent, AsymmetricHorrorEvent::Kind::SkillCheckResult );
    SW_ASSERT_NOT_NULL( pResult );
    SW_EXPECT_NEAR_EQUAL( pResult->_value, 1.0f, 0.001f );

    // 다음 체크는 무시한다 → 실패 · 진행 감소 · 소음(살인마 자극에 실린다).
    bool    bNoiseHeard = false;
    float32 penalty     = 0.0f;
    for ( int32 frame = 0; frame < 200; ++frame )
    {
        const float32 before = match.findGenerator( generator )->_progress.getProgress();
        match.update( kAsymmetricHorrorStep );
        listEvent.clear();
        match.drainEvents( listEvent );
        const AsymmetricHorrorEvent* pNoise = findEvent( listEvent, AsymmetricHorrorEvent::Kind::Noise );
        if ( pNoise == nullptr )
            continue;
        penalty = before - match.findGenerator( generator )->_progress.getProgress();
        SW_EXPECT_NEAR_EQUAL( pNoise->_value, 50.0f, 0.001f );
        vector<AiStimulus> listStimulus;
        match.collectKillerStimuli( listStimulus );
        SW_ASSERT_EQUAL( static_cast<int32>( listStimulus.size() ), 1 );
        SW_EXPECT_NEAR_EQUAL( listStimulus[0]._noiseRadius, 50.0f, 0.001f );
        bNoiseHeard = true;
        break;
    }
    SW_EXPECT_TRUE( bNoiseHeard );
    SW_EXPECT_TRUE( penalty > 0.05f );

    // 아무도 없는 발전기를 걷어차면 바로 깎이고 계속 준다. 누가 다시 붙으면 멈춘다.
    match.stopActivity( survivor );
    match.setKillerPosition( float3{ 0.5f, 0.0f, 1.0f } );
    const float32 beforeKick = match.findGenerator( generator )->_progress.getProgress();
    SW_EXPECT_TRUE( match.kickGenerator( generator ) );
    SW_EXPECT_NEAR_EQUAL( match.findGenerator( generator )->_progress.getProgress(), beforeKick - 0.1f, 0.001f );
    runSeconds( match, 2.0f );
    SW_EXPECT_NEAR_EQUAL( match.findGenerator( generator )->_progress.getProgress(), beforeKick - 0.2f, 0.01f );
    SW_EXPECT_FALSE( match.kickGenerator( generator ) ); // 이미 차였다
    SW_ASSERT_TRUE( match.startRepair( survivor, generator ) );
    SW_EXPECT_FALSE( match.findGenerator( generator )->_bKicked == SW_TRUE );
}

SW_TEST_CASE( AsymmetricHorrorTest, KillerHitsInjureThenDownAndDyingBleedsOut )
{
    AsymmetricHorrorRulesCatalog catalog;
    SW_ASSERT_TRUE( loadRules( catalog ) );
    HorrorMatch match;
    SW_ASSERT_TRUE( beginMatch( match, catalog ) );
    const int32 survivor = match.addSurvivor( float3{ 0.0f, 0.0f, 5.0f } );
    match.start();

    SW_EXPECT_NEAR_EQUAL( match.computeKillerSpeed(), 4.0f * 1.15f, 0.001f );
    SW_EXPECT_TRUE( swingAt( match, float3{ 0.0f, 0.0f, 5.0f } ) == KillerAttackResult::Hit );
    SW_EXPECT_TRUE( match.findSurvivor( survivor )->_state == SurvivorState::Injured );
    SW_EXPECT_NEAR_EQUAL( match.computeSurvivorSpeed( survivor ), 6.0f, 0.001f );    // 맞은 직후 1.5 배로 달아난다
    SW_EXPECT_NEAR_EQUAL( match.computeKillerSpeed(), 4.0f * 1.15f * 0.3f, 0.001f ); // 칼 닦기
    SW_EXPECT_TRUE( match.killerAttack() == KillerAttackResult::NotReady );
    SW_EXPECT_NEAR_EQUAL( match.getKiller()._score.getValue( hashed_string( "Brutality" ) ), 300.0f, 0.01f );

    runSeconds( match, 3.0f );
    SW_EXPECT_NEAR_EQUAL( match.computeSurvivorSpeed( survivor ), 4.0f, 0.001f );
    // 뒤에서 보지 않으면 헛방 — 헛방 쿨다운.
    match.moveKiller( float3{ 0.0f, 0.0f, -1.0f }, 0.0f );
    SW_EXPECT_TRUE( match.killerAttack() == KillerAttackResult::Missed );
    SW_EXPECT_NEAR_EQUAL( match.getKiller()._attackCooldown.getRemaining(), 1.5f, 0.001f );
    runSeconds( match, 2.0f );
    SW_EXPECT_TRUE( swingAt( match, float3{ 0.0f, 0.0f, 5.0f } ) == KillerAttackResult::Downed );
    SW_EXPECT_TRUE( match.findSurvivor( survivor )->_state == SurvivorState::Dying );
    SW_EXPECT_NEAR_EQUAL( match.computeSurvivorSpeed( survivor ), 0.7f, 0.001f );

    // 아무도 치료하지 않으면 20 초 뒤 출혈사 — 혼자 남았으니 판도 끝난다(살인마 승).
    runSeconds( match, 19.0f );
    SW_EXPECT_TRUE( match.findSurvivor( survivor )->_state == SurvivorState::Dying );
    runSeconds( match, 1.5f );
    SW_EXPECT_TRUE( match.findSurvivor( survivor )->_state == SurvivorState::Sacrificed );
    SW_EXPECT_TRUE( match.findSurvivor( survivor )->_bBledOut == SW_TRUE );
    SW_EXPECT_TRUE( match.isEnded() );
    SW_EXPECT_EQUAL( match.getMatch().getWinningTeam(), 1 );
}

SW_TEST_CASE( AsymmetricHorrorTest, HealingPausesBleedAndScalesWithHealers )
{
    AsymmetricHorrorRulesCatalog catalog;
    SW_ASSERT_TRUE( loadRules( catalog ) );
    HorrorMatch match;
    SW_ASSERT_TRUE( beginMatch( match, catalog ) );
    const int32 patient = match.addSurvivor( float3{ 0.0f, 0.0f, 5.0f } );
    const int32 doctor  = match.addSurvivor( float3{ 1.0f, 0.0f, 5.0f } );
    const int32 nurse   = match.addSurvivor( float3{ -1.0f, 0.0f, 5.0f } );
    match.start();
    SW_EXPECT_TRUE( swingAt( match, float3{ 0.0f, 0.0f, 5.0f } ) == KillerAttackResult::Hit );
    runSeconds( match, 3.0f );
    SW_EXPECT_TRUE( swingAt( match, float3{ 0.0f, 0.0f, 5.0f } ) == KillerAttackResult::Downed );
    match.setKillerPosition( float3{ 100.0f, 0.0f, 100.0f } );
    runSeconds( match, 2.0f );
    const float32 bleedBefore = match.findSurvivor( patient )->_vitality.getDownedHealth();
    SW_EXPECT_NEAR_EQUAL( bleedBefore, 18.0f, 0.1f );

    // 혼자 4 초 — 그동안 출혈은 멈추고, 끝나면 빈사 → 부상.
    SW_ASSERT_TRUE( match.startHeal( doctor, patient ) );
    runSeconds( match, 2.0f );
    SW_EXPECT_NEAR_EQUAL( match.findSurvivor( patient )->_vitality.getDownedHealth(), bleedBefore, 0.001f );
    SW_EXPECT_NEAR_EQUAL( match.findSurvivor( patient )->_healing.getProgress(), 0.5f, 0.02f );
    runSeconds( match, 2.2f );
    SW_EXPECT_TRUE( match.findSurvivor( patient )->_state == SurvivorState::Injured );
    SW_EXPECT_TRUE( match.findSurvivor( doctor )->_activity == SurvivorActivity::None );

    // 둘이 치료하면 4 ÷ 1.6 = 2.5 초에 부상 → 건강.
    SW_ASSERT_TRUE( match.startHeal( doctor, patient ) );
    SW_ASSERT_TRUE( match.startHeal( nurse, patient ) );
    SW_EXPECT_FALSE( match.startHeal( patient, patient ) );
    runSeconds( match, 2.3f );
    SW_EXPECT_TRUE( match.findSurvivor( patient )->_state == SurvivorState::Injured );
    runSeconds( match, 0.3f );
    SW_EXPECT_TRUE( match.findSurvivor( patient )->_state == SurvivorState::Healthy );
    SW_EXPECT_FALSE( match.startHeal( doctor, patient ) ); // 건강하면 치료할 것이 없다

    vector<AsymmetricHorrorEvent> listEvent;
    match.drainEvents( listEvent );
    SW_EXPECT_EQUAL( countEvents( listEvent, AsymmetricHorrorEvent::Kind::Healed ), 2 );
}

SW_TEST_CASE( AsymmetricHorrorTest, HookStagesStruggleRescueAndThirdHook )
{
    AsymmetricHorrorRulesCatalog catalog;
    SW_ASSERT_TRUE( loadRules( catalog ) );
    HorrorMatch match;
    SW_ASSERT_TRUE( beginMatch( match, catalog ) );
    const int32 victim  = match.addSurvivor( float3{ 0.0f, 0.0f, 5.0f } );
    const int32 rescuer = match.addSurvivor( float3{ 6.0f, 0.0f, 4.0f } );
    const int32 hook    = match.addHook( float3{ 0.0f, 0.0f, 4.0f } );
    match.start();

    // 눕히고 들어 갈고리에 건다 — 1 단계.
    auto downAndHook = [&]()
    {
        const float3 position = match.findSurvivor( victim )->_position;
        for ( int32 swing = 0; swing < 4 && match.findSurvivor( victim )->_state != SurvivorState::Dying; ++swing )
        {
            (void)swingAt( match, position );
            runSeconds( match, 3.0f );
        }
        SW_ASSERT_TRUE( match.pickUp( victim ) );
        SW_EXPECT_TRUE( match.findSurvivor( victim )->_state == SurvivorState::Carried );
        SW_EXPECT_NEAR_EQUAL( match.computeKillerSpeed(), 4.0f * 1.15f * 0.8f, 0.001f );
        match.setKillerPosition( float3{ 0.0f, 0.0f, 4.5f } );
        SW_ASSERT_TRUE( match.hookCarried( hook ) );
    };
    downAndHook();
    SW_EXPECT_TRUE( match.findSurvivor( victim )->_state == SurvivorState::Hooked );
    SW_EXPECT_EQUAL( match.findSurvivor( victim )->_hookStage, 1 );
    SW_EXPECT_NEAR_EQUAL( match.getKiller()._score.getValue( hashed_string( "Brutality" ) ), 300.0f * 2.0f + 500.0f, 0.01f );

    // 단계 시간 5 초 → 2 단계(몸부림). 몸부림을 이어 가면 버틴다.
    match.setStruggling( victim, true );
    runSeconds( match, 5.2f );
    SW_EXPECT_EQUAL( match.findSurvivor( victim )->_hookStage, 2 );
    runSeconds( match, 2.0f );
    SW_EXPECT_TRUE( match.findSurvivor( victim )->_state == SurvivorState::Hooked );

    // 구출 — 부상으로 내려온다.
    SW_EXPECT_FALSE( match.rescue( rescuer, victim ) ); // 멀다
    match.moveSurvivor( rescuer, float3{}, 0.0f );
    match.setKillerPosition( float3{ 50.0f, 0.0f, 50.0f } );
    for ( int32 frame = 0; frame < 600 && match.rescue( rescuer, victim ) == false; ++frame )
    {
        const float3  from   = match.findSurvivor( rescuer )->_position;
        const float3  to     = match.findSurvivor( victim )->_position;
        const float3  delta  = float3{ to._x - from._x, 0.0f, to._z - from._z };
        const float32 length = delta.getLength();
        match.moveSurvivor( rescuer, length > 1.0f ? delta / length : delta, kAsymmetricHorrorStep );
        match.update( kAsymmetricHorrorStep );
    }
    SW_EXPECT_TRUE( match.findSurvivor( victim )->_state == SurvivorState::Injured );

    // 2 단계까지 갔던 생존자는 다음 걸림이 세 번째 — 바로 희생.
    runSeconds( match, 3.0f );
    downAndHook();
    SW_EXPECT_TRUE( match.findSurvivor( victim )->_state == SurvivorState::Sacrificed );
    SW_EXPECT_TRUE( match.findSurvivor( victim )->_bBledOut == SW_FALSE );

    // 다른 생존자 — 2 단계에서 몸부림을 멈추면 유예 1 초 뒤 희생.
    HorrorMatch second;
    SW_ASSERT_TRUE( beginMatch( second, catalog ) );
    const int32 lone = second.addSurvivor( float3{ 0.0f, 0.0f, 5.0f } );
    (void)second.addSurvivor( float3{ 40.0f, 0.0f, 40.0f } );
    const int32 post = second.addHook( float3{ 0.0f, 0.0f, 4.0f } );
    second.start();
    (void)swingAt( second, float3{ 0.0f, 0.0f, 5.0f } );
    runSeconds( second, 3.0f );
    (void)swingAt( second, float3{ 0.0f, 0.0f, 5.0f } );
    SW_ASSERT_TRUE( second.pickUp( lone ) );
    SW_ASSERT_TRUE( second.hookCarried( post ) );
    runSeconds( second, 5.2f );
    SW_EXPECT_EQUAL( second.findSurvivor( lone )->_hookStage, 2 );
    runSeconds( second, 1.5f );
    SW_EXPECT_TRUE( second.findSurvivor( lone )->_state == SurvivorState::Sacrificed );
}

SW_TEST_CASE( AsymmetricHorrorTest, PalletsStunAndWiggleFreesCarriedSurvivor )
{
    AsymmetricHorrorRulesCatalog catalog;
    SW_ASSERT_TRUE( loadRules( catalog ) );
    HorrorMatch match;
    SW_ASSERT_TRUE( beginMatch( match, catalog ) );
    const int32 victim = match.addSurvivor( float3{ 0.0f, 0.0f, 5.0f } );
    const int32 savior = match.addSurvivor( float3{ 0.0f, 0.0f, 6.0f } );
    const int32 pallet = match.addPallet( float3{ 0.0f, 0.0f, 5.5f } );
    match.start();

    (void)swingAt( match, float3{ 0.0f, 0.0f, 5.0f } );
    runSeconds( match, 3.0f );
    (void)swingAt( match, float3{ 0.0f, 0.0f, 5.0f } );
    match.setKillerPosition( float3{ 0.0f, 0.0f, 5.0f } );
    SW_ASSERT_TRUE( match.pickUp( victim ) );

    // 들고 있는 살인마 옆에 판자를 내리면 기절하고 생존자를 떨어뜨린다.
    SW_ASSERT_TRUE( match.dropPallet( savior, pallet ) );
    SW_EXPECT_TRUE( match.getPalletState( pallet ) == PalletState::Dropped );
    SW_EXPECT_NEAR_EQUAL( match.getKiller()._stunRemaining.getRemaining(), 2.0f, 0.001f );
    SW_EXPECT_EQUAL( match.getKiller()._carrying, -1 );
    SW_EXPECT_TRUE( match.findSurvivor( victim )->_state == SurvivorState::Injured );
    SW_EXPECT_FALSE( match.dropPallet( savior, pallet ) ); // 이미 내렸다
    SW_EXPECT_FALSE( match.breakPallet( pallet ) );        // 기절 중
    SW_EXPECT_TRUE( match.computeKillerSpeed() == 0.0f );

    // 기절이 풀리면 판자를 부순다(2 초).
    runSeconds( match, 2.1f );
    SW_ASSERT_TRUE( match.breakPallet( pallet ) );
    runSeconds( match, 1.0f );
    SW_EXPECT_TRUE( match.getPalletState( pallet ) == PalletState::Dropped );
    runSeconds( match, 1.1f );
    SW_EXPECT_TRUE( match.getPalletState( pallet ) == PalletState::Broken );

    // 다시 눕혀 들고 다닐 때 몸부림 4 초 → 빠져나오고 살인마가 3 초 기절.
    runSeconds( match, 3.0f );
    (void)swingAt( match, match.findSurvivor( victim )->_position );
    match.setKillerPosition( match.findSurvivor( victim )->_position );
    SW_ASSERT_TRUE( match.pickUp( victim ) );
    match.setWiggling( victim, true );
    runSeconds( match, 3.5f );
    SW_EXPECT_TRUE( match.findSurvivor( victim )->_state == SurvivorState::Carried );
    runSeconds( match, 0.6f );
    SW_EXPECT_TRUE( match.findSurvivor( victim )->_state == SurvivorState::Injured );
    SW_EXPECT_TRUE( match.getKiller()._stunRemaining.getRemaining() > 2.5f );

    vector<AsymmetricHorrorEvent> listEvent;
    match.drainEvents( listEvent );
    SW_EXPECT_EQUAL( countEvents( listEvent, AsymmetricHorrorEvent::Kind::KillerStunned ), 2 );
    SW_EXPECT_EQUAL( countEvents( listEvent, AsymmetricHorrorEvent::Kind::WiggledFree ), 1 );
    SW_EXPECT_EQUAL( countEvents( listEvent, AsymmetricHorrorEvent::Kind::PalletBroken ), 1 );
}

SW_TEST_CASE( AsymmetricHorrorTest, VaultSpeedWindowBlockAndLockerGrab )
{
    AsymmetricHorrorRulesCatalog catalog;
    SW_ASSERT_TRUE( loadRules( catalog ) );
    HorrorMatch match;
    SW_ASSERT_TRUE( beginMatch( match, catalog ) );
    const int32 runner = match.addSurvivor( float3{ 0.0f, 0.0f, -1.0f } );
    (void)match.addSurvivor( float3{ 50.0f, 0.0f, 50.0f } );
    const int32 window = match.addWindow( float3{ 0.0f, 0.0f, 0.0f } );
    const int32 locker = match.addLocker( float3{ 5.0f, 0.0f, 5.0f } );
    match.setKillerPosition( float3{ 0.0f, 0.0f, -10.0f } ); // 위협 반경 안 — 추격 중
    match.start();

    // 달려 들어가면 빠른 넘기(0.5 초 · 소음), 서서 넘으면 보통 넘기(1.2 초).
    match.moveSurvivor( runner, float3{ 0.0f, 0.0f, 1.0f }, kAsymmetricHorrorStep );
    SW_ASSERT_TRUE( match.vault( runner, window ) );
    vector<AsymmetricHorrorEvent> listEvent;
    match.drainEvents( listEvent );
    const AsymmetricHorrorEvent* pVault = findEvent( listEvent, AsymmetricHorrorEvent::Kind::Vaulted );
    SW_ASSERT_NOT_NULL( pVault );
    SW_EXPECT_NEAR_EQUAL( pVault->_value, 1.0f, 0.001f );
    SW_EXPECT_EQUAL( countEvents( listEvent, AsymmetricHorrorEvent::Kind::Noise ), 1 );
    SW_EXPECT_NEAR_EQUAL( match.findSurvivor( runner )->_vaultRemaining.getRemaining(), 0.5f, 0.001f );
    runSeconds( match, 0.6f );
    SW_EXPECT_TRUE( match.findSurvivor( runner )->_activity == SurvivorActivity::None );
    SW_EXPECT_TRUE( match.findSurvivor( runner )->_position._z > 0.5f ); // 건너편

    SW_ASSERT_TRUE( match.vault( runner, window ) );
    listEvent.clear();
    match.drainEvents( listEvent );
    SW_EXPECT_NEAR_EQUAL( findEvent( listEvent, AsymmetricHorrorEvent::Kind::Vaulted )->_value, 0.0f, 0.001f );
    SW_EXPECT_NEAR_EQUAL( match.findSurvivor( runner )->_vaultRemaining.getRemaining(), 1.2f, 0.001f );
    runSeconds( match, 1.3f );

    // 추격 중 세 번째 넘기에 창이 막힌다.
    SW_ASSERT_TRUE( match.vault( runner, window ) );
    runSeconds( match, 1.3f );
    SW_EXPECT_TRUE( match.findWindow( window )->_blockedRemaining > 0.0f );
    SW_EXPECT_FALSE( match.vault( runner, window ) );

    // 사물함에 숨으면 자극에서 빠지고, 살인마가 열면 바로 들린다.
    for ( int32 frame = 0; frame < 300; ++frame )
    {
        const float3  from   = match.findSurvivor( runner )->_position;
        const float3  delta  = float3{ 5.0f - from._x, 0.0f, 5.0f - from._z };
        const float32 length = delta.getLength();
        if ( length < 1.0f )
            break;
        match.moveSurvivor( runner, delta / length, kAsymmetricHorrorStep );
        match.update( kAsymmetricHorrorStep );
    }
    SW_ASSERT_TRUE( match.enterLocker( runner, locker ) );
    vector<AiStimulus> listStimulus;
    match.collectKillerStimuli( listStimulus );
    SW_EXPECT_EQUAL( static_cast<int32>( listStimulus.size() ), 1 );
    match.setKillerPosition( float3{ 5.0f, 0.0f, 4.0f } );
    SW_EXPECT_TRUE( match.searchLocker( locker ) );
    SW_EXPECT_TRUE( match.findSurvivor( runner )->_state == SurvivorState::Carried );
    SW_EXPECT_EQUAL( match.getKiller()._carrying, runner );
}

SW_TEST_CASE( AsymmetricHorrorTest, GatesHatchCollapseAndStateBytes )
{
    AsymmetricHorrorRulesCatalog catalog;
    SW_ASSERT_TRUE( loadRules( catalog ) );

    // 발전기 두 대 → 탈출구 → 한 명 탈출 → 남은 한 명에게 해치.
    auto runEscape = [&]( BitWriter& outWriter, bool bLeaveSecond )
    {
        HorrorMatch match;
        SW_ASSERT_TRUE( beginMatch( match, catalog ) );
        const int32 first  = match.addSurvivor( float3{ 0.0f, 0.0f, 0.0f } );
        const int32 second = match.addSurvivor( float3{ 10.0f, 0.0f, 0.0f } );
        const int32 genA   = match.addGenerator( float3{ 0.5f, 0.0f, 0.0f } );
        const int32 genB   = match.addGenerator( float3{ 10.5f, 0.0f, 0.0f } );
        const int32 gate   = match.addGate( float3{ 0.0f, 0.0f, 1.0f } );
        match.setHatchPosition( float3{ 10.0f, 0.0f, 1.0f } );
        match.setKillerPosition( float3{ 200.0f, 0.0f, 200.0f } );
        match.start();
        SW_EXPECT_FALSE( match.startOpenGate( first, gate ) ); // 전원이 없다
        SW_ASSERT_TRUE( match.startRepair( first, genA ) );
        SW_ASSERT_TRUE( match.startRepair( second, genB ) );
        runSeconds( match, 10.2f );
        SW_ASSERT_TRUE( match.areGatesPowered() );
        SW_ASSERT_TRUE( match.startOpenGate( first, gate ) );
        runSeconds( match, 3.2f );
        SW_EXPECT_TRUE( match.findGate( gate )->_progress.isCompleted() );
        SW_EXPECT_TRUE( match.isCollapseStarted() );
        SW_EXPECT_FALSE( match.escape( second ) ); // 탈출구에서 멀다
        SW_ASSERT_TRUE( match.escape( first ) );
        match.update( kAsymmetricHorrorStep );
        SW_EXPECT_TRUE( match.isHatchOpen() ); // 한 명만 남았다
        match.writeState( outWriter );
        if ( bLeaveSecond )
        {
            SW_ASSERT_TRUE( match.escape( second ) );
            SW_EXPECT_TRUE( match.isEnded() );
            SW_EXPECT_EQUAL( match.getMatch().getWinningTeam(), 0 );
        }
        else
        {
            // 붕괴 10 초가 다 하면 남은 생존자는 희생된다 — 1 대 1 무승부.
            match.setHatchPosition( float3{ 500.0f, 0.0f, 500.0f } );
            runSeconds( match, 10.5f );
            SW_EXPECT_TRUE( match.findSurvivor( second )->_state == SurvivorState::Sacrificed );
            SW_EXPECT_TRUE( match.isEnded() );
            SW_EXPECT_EQUAL( match.getMatch().getWinningTeam(), -1 );
        }
    };

    BitWriter firstRun;
    BitWriter secondRun;
    runEscape( firstRun, true );
    runEscape( secondRun, false );
    SW_ASSERT_EQUAL( firstRun.getByteCount(), secondRun.getByteCount() );
    SW_EXPECT_TRUE( firstRun.getBytes() == secondRun.getBytes() ); // 같은 입력까지는 같은 바이트

    HorrorSnapshot snapshot;
    BitReader      reader( firstRun.getBytes().data(), firstRun.getByteCount() );
    SW_ASSERT_TRUE( HorrorSnapshotCodec::read( reader, snapshot ) );
    SW_EXPECT_EQUAL( static_cast<int32>( snapshot._listSurvivor.size() ), 2 );
    SW_EXPECT_TRUE( snapshot._listSurvivor[0]._state == SurvivorState::Escaped );
    SW_EXPECT_TRUE( snapshot._bGatesPowered == SW_TRUE );
    SW_EXPECT_TRUE( snapshot._bHatchOpen == SW_TRUE );
    SW_EXPECT_EQUAL( static_cast<int32>( snapshot._listGeneratorFlag.size() ), 2 );
    SW_EXPECT_EQUAL( static_cast<int32>( snapshot._listGeneratorFlag[0] & 1u ), 1 );
    BitWriter rewritten;
    HorrorSnapshotCodec::write( snapshot, rewritten );
    SW_EXPECT_TRUE( rewritten.getBytes() == firstRun.getBytes() );

    HorrorSnapshot broken;
    BitReader      shortReader( firstRun.getBytes().data(), firstRun.getByteCount() / 2 );
    SW_EXPECT_FALSE( HorrorSnapshotCodec::read( shortReader, broken ) );
}

/**
 * @brief [AsymmetricHorrorTest] 세이브(Archive) 왕복 — 넷 스냅숏과 달리 전체 상태(무대 · 수리 진행의 스킬 체크 난수 · 체력 · 살인마 쿨다운)를 실어,
 *        새로 연 판이 같은 바이트로 돌아오고 같은 걸음을 더 돌려도 같다. 잘린 바이트는 거절하고 그대로 둔다
 */
SW_TEST_CASE( AsymmetricHorrorTest, StateRoundTripContinuesTheSameMatch )
{
    // 스킬 체크가 2 초쯤마다 뜨는 규칙 — 응답하지 않아 실패 · 소음 · 난수가 계속 움직인다.
    string       xml( kHorrorRulesXml );
    const size_t at = xml.find( "skillCheckInterval=\"0\" kickPenalty" );
    SW_ASSERT_TRUE( at != string::npos );
    xml.replace( at, string( "skillCheckInterval=\"0\"" ).size(), "skillCheckInterval=\"2\"" );
    AsymmetricHorrorRulesCatalog catalog;
    SW_ASSERT_TRUE( loadRules( catalog, xml.c_str() ) );
    auto toBytes = []( const HorrorMatch& match )
    {
        Archive written;
        match.writeState( written );
        vector<uint8> bytes;
        written.writeData( bytes );
        return bytes;
    };

    HorrorMatch match;
    SW_ASSERT_TRUE( beginMatch( match, catalog ) );
    const int32 first  = match.addSurvivor( float3{ 0.0f, 0.0f, 0.0f } );
    const int32 second = match.addSurvivor( float3{ 1.0f, 0.0f, 0.0f } );
    const int32 third  = match.addSurvivor( float3{ 20.0f, 0.0f, 0.0f } );
    const int32 genA   = match.addGenerator( float3{ 0.5f, 0.0f, 0.0f } );
    (void)match.addGenerator( float3{ 40.0f, 0.0f, 0.0f } );
    (void)match.addHook( float3{ 30.0f, 0.0f, 0.0f } );
    (void)match.addPallet( float3{ 25.0f, 0.0f, 0.0f } );
    (void)match.addWindow( float3{ 26.0f, 0.0f, 5.0f } );
    (void)match.addLocker( float3{ 27.0f, 0.0f, -5.0f } );
    (void)match.addGate( float3{ 0.0f, 0.0f, 10.0f } );
    match.setHatchPosition( float3{ -10.0f, 0.0f, 0.0f } );
    match.start();
    SW_ASSERT_TRUE( match.startRepair( first, genA ) );
    SW_ASSERT_TRUE( match.startRepair( second, genA ) );
    SW_EXPECT_TRUE( swingAt( match, match.findSurvivor( third )->_position ) == KillerAttackResult::Hit );
    runSeconds( match, 3.0f );

    const vector<uint8> listWritten = toBytes( match );
    HorrorMatch         restored;
    SW_ASSERT_TRUE( beginMatch( restored, catalog ) );
    Archive reader( listWritten.data(), listWritten.size() );
    SW_ASSERT_TRUE( restored.readState( reader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, reader.getRemainingBytes() );
    SW_EXPECT_EQUAL( match.getTick(), restored.getTick() );
    SW_EXPECT_TRUE( listWritten == toBytes( restored ) );
    SW_EXPECT_TRUE( restored.findSurvivor( third )->_state == SurvivorState::Injured );

    for ( HorrorMatch* pMatch : { &match, &restored } )
    {
        pMatch->moveSurvivor( third, float3{ 1.0f, 0.0f, 0.0f }, 0.5f );
        pMatch->moveKiller( float3{ 1.0f, 0.0f, 0.0f }, 0.5f );
        runSeconds( *pMatch, 4.0f );
    }
    SW_EXPECT_NEAR_EQUAL( match.findGenerator( genA )->_progress.getProgress(), restored.findGenerator( genA )->_progress.getProgress(), 1.0e-6f );
    SW_EXPECT_TRUE( toBytes( match ) == toBytes( restored ) );

    HorrorMatch truncated;
    SW_ASSERT_TRUE( beginMatch( truncated, catalog ) );
    Archive cut( listWritten.data(), listWritten.size() - 1 );
    SW_EXPECT_FALSE( truncated.readState( cut ) );
    SW_EXPECT_EQUAL( 0, truncated.getSurvivorCount() );
}
