// 기체 대전 키트 — 카탈로그 · 덱 코스트 상한, 부스트 오버히트 잠금과 회복, 사격 다운치 → 다운 → 기상 무적, 근접 콤보 캔슬 창 · 분류 보정,
// 유도 대 직선 특수기(변형으로 무기 세트 교체), 팀 전력 게이지 · 기체 교체 · 승패, 체력 조건 스킬, 상태 바이트 왕복 · 결정성.
#include "pch.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Combat/FrameData.h"
#include "GameFramework/Combat/Weapon.h"
#include "GameFramework/Kits/Action/MechArena/MechArenaSnapshot.h"
#include "GameFramework/Kits/Action/MechArena/MechArenaWorld.h"
#include "GameFramework/Kits/Action/MechArena/MechCatalog.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr float32 kMechArenaStep = 1.0f / 60.0f;

    constexpr const utf8* kMechWeaponXml = R"(<WeaponCatalog>
        <Weapon id="rifle" damage="50" fireInterval="0.2" reloadTime="1" magazineSize="10" maxReserveAmmo="30" minSpread="0" maxSpread="0"
                spreadPerShot="0" recoilPitch="0" automatic="false"/>
      </WeaponCatalog>)";

    // slash1: 3 + 2 + 10 프레임, 4..14 프레임에 slash2 로 캔슬.
    constexpr const utf8* kMechMoveXml = R"(<MoveCatalog>
        <Move id="slash1" startup="3" active="2" recovery="10" damage="40" hitstun="20" blockstun="10" height="Mid"><Cancel from="4" to="14" moves="slash2"/></Move>
        <Move id="slash2" startup="3" active="2" recovery="12" damage="60" hitstun="20" blockstun="10" height="Mid"/>
      </MoveCatalog>)";

    constexpr const utf8* kMechCatalogXml = R"(<MechCatalog deckCostLimit="1200">
        <Class id="Near" melee="1.5" shot="1"/>
        <Class id="Far" down="2"/>
        <Mech id="striker" name="Striker" class="Near" rank="A" cost="300" hp="300" boost="100" boostRegen="50" boostRegenDelay="0.5" overheatPenalty="1"
              dashCost="25" jumpCost="20" dashTime="0.3" dashSpeed="20" down="100" downRecovery="0" downRecoveryDelay="10" downTime="1" wakeInvulnerable="1"
              skillSlots="1" speed="8">
          <Weapon id="rifle" kind="Shot" weapon="rifle" down="30" stagger="0"/>
          <Weapon id="saber" kind="Melee" moves="slash1,slash2" range="3" down="0"/>
        </Mech>
        <Mech id="sniper" class="Far" rank="B" cost="200" hp="1000" down="100" downRecovery="0">
          <Mode id="ms" speed="6"><Weapon id="bazooka" kind="Special" damage="80" speed="30" homing="0" range="100" cooldown="2" down="0" stagger="0"/></Mode>
          <Mode id="ma" speed="20"><Weapon id="homing" kind="Special" damage="80" speed="30" homing="180" range="100" cooldown="2" down="0" stagger="0"/></Mode>
        </Mech>
        <Mech id="heavy" class="Mid" rank="S" cost="600" hp="1000" down="100" downRecovery="0" downTime="1" wakeInvulnerable="1"/>
        <Skill id="berserk" trigger="HealthBelow" threshold="0.5" duration="10" attack="2"/>
      </MechCatalog>)";

    /** @brief 카탈로그 셋을 함께 쥡니다(월드가 빌려 쓴다). */
    struct MechFixture
    {
        MechCatalog   _mechCatalog{};
        WeaponCatalog _weaponCatalog{};
        MoveCatalog   _moveCatalog{};

        bool load()
        {
            return _weaponCatalog.loadFromXmlText( kMechWeaponXml, "MechArenaTest" ) && _moveCatalog.loadFromXmlText( kMechMoveXml, "MechArenaTest" ) &&
                   _mechCatalog.loadFromXmlText( kMechCatalogXml, "MechArenaTest" );
        }

        void initialize( MechArenaWorld& outWorld, const MechArenaSettings& settings = MechArenaSettings{} ) const
        {
            outWorld.initialize( settings, &_mechCatalog, &_weaponCatalog, &_moveCatalog );
            (void)outWorld.addTeam( hashed_string( "Federation" ) );
            (void)outWorld.addTeam( hashed_string( "Zeon" ) );
        }
    };

    MechPilotConfig makePilot( std::initializer_list<const utf8*> listMech, int32 team, const float3& position )
    {
        MechPilotConfig config;
        for ( const utf8* pMech : listMech )
            config._listMechId.push_back( hashed_string( pMech ) );
        config._team          = team;
        config._spawnPosition = position;
        return config;
    }

    void runSteps( MechArenaWorld& world, int32 stepCount )
    {
        for ( int32 index = 0; index < stepCount; ++index )
            world.update( kMechArenaStep );
    }

    /** @brief 버튼을 한 걸음 누르고 한 걸음 뗍니다(눌린 순간이 한 번 생긴다). */
    template <typename TSetter>
    void tapButton( MechArenaWorld& world, int32 pilot, TSetter setter )
    {
        MechInput input;
        setter( input );
        world.setInput( pilot, input );
        world.update( kMechArenaStep );
        world.setInput( pilot, MechInput{} );
        world.update( kMechArenaStep );
    }

    int32 countEvents( const vector<MechArenaEvent>& listEvent, MechArenaEvent::Kind kind, int32 pilot )
    {
        int32 count = 0;
        for ( const MechArenaEvent& event : listEvent )
            count += event._kind == kind && ( pilot < 0 || event._pilot == pilot ) ? 1 : 0;
        return count;
    }
} // namespace

SW_TEST_CASE( MechArenaTest, CatalogReadsModesAndDeckCostLimit )
{
    MechFixture fixture;
    SW_ASSERT_TRUE( fixture.load() );
    const MechCatalog& catalog = fixture._mechCatalog;
    SW_EXPECT_EQUAL( static_cast<int32>( catalog.getMechs().size() ), 3 );
    SW_EXPECT_EQUAL( catalog.getDeckCostLimit(), 1200 );

    const MechDef* pSniper = catalog.findMech( hashed_string( "sniper" ) );
    SW_ASSERT_TRUE( pSniper != nullptr );
    SW_EXPECT_EQUAL( static_cast<int32>( pSniper->_listMode.size() ), 2 );
    SW_EXPECT_EQUAL( pSniper->computeSlotCount(), 2 );
    SW_EXPECT_EQUAL( pSniper->computeSlotOffset( 1 ), 1 );
    SW_EXPECT_TRUE( pSniper->_rangeClass == MechRangeClass::Far );
    SW_EXPECT_NEAR_EQUAL( pSniper->_listMode[1]._speed, 20.0f, 0.001f );

    // 근거리 분류는 근접 1.5 배, 원거리 분류는 받는 다운치 2 배 — 적지 않은 이름은 1.
    SW_EXPECT_NEAR_EQUAL( catalog.getClassModifier( MechRangeClass::Near ).getValue( hashed_string( "melee" ), 1.0f ), 1.5f, 0.001f );
    SW_EXPECT_NEAR_EQUAL( catalog.getClassModifier( MechRangeClass::Far ).getValue( hashed_string( "down" ), 1.0f ), 2.0f, 0.001f );
    SW_EXPECT_NEAR_EQUAL( catalog.getClassModifier( MechRangeClass::Mid ).getValue( hashed_string( "melee" ), 1.0f ), 1.0f, 0.001f );

    const MechDef* pStriker = catalog.findMech( hashed_string( "striker" ) );
    SW_ASSERT_TRUE( pStriker != nullptr );
    SW_EXPECT_EQUAL( static_cast<int32>( pStriker->_listMode.size() ), 1 ); // <Mode> 없이 적은 무기는 형태 하나
    SW_EXPECT_EQUAL( static_cast<int32>( pStriker->_listMode[0]._listWeapon[1]._listMoveId.size() ), 2 );

    MechArenaWorld world;
    fixture.initialize( world );
    SW_EXPECT_EQUAL( world.addPilot( makePilot( { "heavy", "heavy", "heavy" }, 0, float3{} ) ), -1 ); // 1800 > 1200
    SW_EXPECT_EQUAL( world.addPilot( makePilot( { "unknown" }, 0, float3{} ) ), -1 );
    SW_EXPECT_EQUAL( world.addPilot( makePilot( { "striker" }, 5, float3{} ) ), -1 );
    SW_EXPECT_EQUAL( world.addPilot( makePilot( { "striker", "sniper", "heavy" }, 0, float3{} ) ), 0 ); // 1100
}

SW_TEST_CASE( MechArenaTest, BoostOverheatLocksDashUntilCooled )
{
    MechFixture fixture;
    SW_ASSERT_TRUE( fixture.load() );
    MechArenaWorld world;
    fixture.initialize( world );
    const int32 pilot = world.addPilot( makePilot( { "striker" }, 0, float3{} ) );
    (void)world.addPilot( makePilot( { "heavy" }, 1, float3{ 150.0f, 0.0f, 150.0f } ) );
    world.start();

    // 대시 25 × 4 = 100 — 네 번째에 열이 가득 차 오버히트.
    for ( int32 dash = 0; dash < 4; ++dash )
        tapButton( world, pilot, []( MechInput& input )
        { input._bDash = SW_TRUE; } );
    vector<MechArenaEvent> listEvent;
    world.drainEvents( listEvent );
    SW_EXPECT_EQUAL( countEvents( listEvent, MechArenaEvent::Kind::Overheated, pilot ), 1 );
    SW_EXPECT_TRUE( world.findPilot( pilot )->_boost.isOverheated() );
    runSteps( world, 30 ); // 대시가 다 끝나게

    // 오버히트 동안 대시 · 점프는 나가지 않는다.
    const float3 before = world.findPilot( pilot )->_position;
    tapButton( world, pilot, []( MechInput& input )
    { input._bDash = SW_TRUE; } );
    tapButton( world, pilot, []( MechInput& input )
    { input._bJump = SW_TRUE; } );
    runSteps( world, 10 );
    const float3 after = world.findPilot( pilot )->_position;
    SW_EXPECT_NEAR_EQUAL( float3::getDistance( before, after ), 0.0f, 0.001f );

    // 벌칙 1 초 + 100 ÷ 50 = 2 초 식히면 다시 쓸 수 있다.
    runSteps( world, 60 * 4 );
    SW_EXPECT_FALSE( world.findPilot( pilot )->_boost.isOverheated() );
    tapButton( world, pilot, []( MechInput& input )
    { input._bDash = SW_TRUE; } );
    runSteps( world, 10 );
    SW_EXPECT_TRUE( float3::getDistance( after, world.findPilot( pilot )->_position ) > 3.0f );
}

SW_TEST_CASE( MechArenaTest, ShotsBuildDownThenWakeGrantsInvulnerability )
{
    MechFixture fixture;
    SW_ASSERT_TRUE( fixture.load() );
    MechArenaWorld world;
    fixture.initialize( world );
    const int32 shooter = world.addPilot( makePilot( { "striker" }, 0, float3{} ) );
    const int32 target  = world.addPilot( makePilot( { "heavy" }, 1, float3{ 0.0f, 0.0f, 20.0f } ) );
    world.start();

    tapButton( world, shooter, []( MechInput& input )
    { input._bLockOn = SW_TRUE; } );
    SW_EXPECT_TRUE( world.findPilot( shooter )->_lockOn.hasTarget() );

    // 다운치 30 씩 — 셋째까지는 서 있고, 넷째(120 ≥ 100)에 눕는다.
    vector<MechArenaEvent> listEvent;
    for ( int32 shot = 0; shot < 4; ++shot )
    {
        tapButton( world, shooter, []( MechInput& input )
        { input._bFire = SW_TRUE; } );
        runSteps( world, 12 ); // 연사 간격 0.2 초
        if ( shot < 3 )
            SW_EXPECT_TRUE( world.findPilot( target )->_state == MechPilotState::Active );
    }
    world.drainEvents( listEvent );
    SW_EXPECT_EQUAL( countEvents( listEvent, MechArenaEvent::Kind::Hit, target ), 4 );
    SW_EXPECT_EQUAL( countEvents( listEvent, MechArenaEvent::Kind::Downed, target ), 1 );
    SW_EXPECT_TRUE( world.findPilot( target )->_state == MechPilotState::Down );
    SW_EXPECT_NEAR_EQUAL( world.findPilot( target )->_vitality.getHealth(), 1000.0f - 200.0f, 0.01f );

    // 누운 시간 1 초가 지나면 일어나고 1 초 무적 — 그동안의 사격은 체력을 깎지 않는다.
    runSteps( world, 60 );
    listEvent.clear();
    world.drainEvents( listEvent );
    SW_EXPECT_EQUAL( countEvents( listEvent, MechArenaEvent::Kind::WokeUp, target ), 1 );
    SW_EXPECT_TRUE( world.findPilot( target )->_state == MechPilotState::Active );
    SW_EXPECT_TRUE( world.findPilot( target )->_vitality.isInvulnerable() );
    tapButton( world, shooter, []( MechInput& input )
    { input._bFire = SW_TRUE; } );
    SW_EXPECT_NEAR_EQUAL( world.findPilot( target )->_vitality.getHealth(), 800.0f, 0.01f );

    // 무적이 끝나면 다시 맞는다.
    runSteps( world, 60 );
    tapButton( world, shooter, []( MechInput& input )
    { input._bFire = SW_TRUE; } );
    SW_EXPECT_NEAR_EQUAL( world.findPilot( target )->_vitality.getHealth(), 750.0f, 0.01f );
}

SW_TEST_CASE( MechArenaTest, MeleeComboAdvancesOnlyInsideCancelWindow )
{
    MechFixture fixture;
    SW_ASSERT_TRUE( fixture.load() );

    // 한 번만 누르면 1 단(40 × 근거리 근접 1.5 = 60)에서 끝난다.
    {
        MechArenaWorld world;
        fixture.initialize( world );
        const int32 attacker = world.addPilot( makePilot( { "striker" }, 0, float3{} ) );
        const int32 target   = world.addPilot( makePilot( { "heavy" }, 1, float3{ 0.0f, 0.0f, 3.0f } ) );
        world.start();
        tapButton( world, attacker, []( MechInput& input )
        { input._bMelee = SW_TRUE; } );
        runSteps( world, 40 );
        SW_EXPECT_NEAR_EQUAL( world.findPilot( target )->_vitality.getHealth(), 1000.0f - 60.0f, 0.01f );
        SW_EXPECT_EQUAL( world.findPilot( attacker )->_comboStage, -1 );
    }

    // 1 단이 닿은 뒤 캔슬 창에서 다시 누르면 2 단(60 × 1.5 = 90)까지 — 합 150.
    {
        MechArenaWorld world;
        fixture.initialize( world );
        const int32 attacker = world.addPilot( makePilot( { "striker" }, 0, float3{} ) );
        const int32 target   = world.addPilot( makePilot( { "heavy" }, 1, float3{ 0.0f, 0.0f, 3.0f } ) );
        world.start();
        tapButton( world, attacker, []( MechInput& input )
        { input._bMelee = SW_TRUE; } );
        runSteps( world, 4 );
        tapButton( world, attacker, []( MechInput& input )
        { input._bMelee = SW_TRUE; } );
        runSteps( world, 40 );
        vector<MechArenaEvent> listEvent;
        world.drainEvents( listEvent );
        SW_EXPECT_EQUAL( countEvents( listEvent, MechArenaEvent::Kind::ComboAdvanced, attacker ), 2 );
        SW_EXPECT_NEAR_EQUAL( world.findPilot( target )->_vitality.getHealth(), 1000.0f - 150.0f, 0.01f );
    }

    // 1 단이 다 끝난 뒤(창 밖) 누르면 콤보가 아니라 새 1 단이다.
    {
        MechArenaWorld world;
        fixture.initialize( world );
        const int32 attacker = world.addPilot( makePilot( { "striker" }, 0, float3{} ) );
        const int32 target   = world.addPilot( makePilot( { "heavy" }, 1, float3{ 0.0f, 0.0f, 3.0f } ) );
        world.start();
        tapButton( world, attacker, []( MechInput& input )
        { input._bMelee = SW_TRUE; } );
        runSteps( world, 30 );
        tapButton( world, attacker, []( MechInput& input )
        { input._bMelee = SW_TRUE; } );
        runSteps( world, 30 );
        SW_EXPECT_NEAR_EQUAL( world.findPilot( target )->_vitality.getHealth(), 1000.0f - 120.0f, 0.01f );
    }
}

SW_TEST_CASE( MechArenaTest, HomingSpecialFollowsStrafingTargetAfterTransform )
{
    MechFixture fixture;
    SW_ASSERT_TRUE( fixture.load() );

    // 옆으로 달아나는 대상에게 쏜 특수기 한 발이 맞으면 1 을 돌려준다.
    auto runVolley = [&]( bool bTransform ) -> int32
    {
        MechArenaWorld world;
        fixture.initialize( world );
        const int32 shooter = world.addPilot( makePilot( { "sniper" }, 0, float3{} ) );
        const int32 target  = world.addPilot( makePilot( { "striker" }, 1, float3{ 0.0f, 0.0f, 40.0f } ) );
        world.start();
        if ( bTransform )
        {
            tapButton( world, shooter, []( MechInput& input )
            { input._bTransform = SW_TRUE; } );
            SW_EXPECT_EQUAL( world.findPilot( shooter )->_mode, 1 );
        }
        tapButton( world, shooter, []( MechInput& input )
        { input._bLockOn = SW_TRUE; } );
        MechInput strafe;
        strafe._moveX = 1.0f;
        world.setInput( target, strafe );
        tapButton( world, shooter, []( MechInput& input )
        { input._bSpecial = SW_TRUE; } );
        runSteps( world, 60 * 3 );
        vector<MechArenaEvent> listEvent;
        world.drainEvents( listEvent );
        return countEvents( listEvent, MechArenaEvent::Kind::Hit, target );
    };
    SW_EXPECT_EQUAL( runVolley( false ), 0 ); // 곧은 바주카는 쏜 자리로 날아가 빗나간다
    SW_EXPECT_EQUAL( runVolley( true ), 1 );  // 변형한 형태의 유도탄은 꺾여 맞는다
}

SW_TEST_CASE( MechArenaTest, TeamGaugePaysRespawnCostAndSwapRespectsIt )
{
    MechFixture fixture;
    SW_ASSERT_TRUE( fixture.load() );
    MechArenaSettings settings;
    settings._respawnDelay        = 1.0f;
    settings._respawnInvulnerable = 0.5f;
    MechArenaWorld world;
    fixture.initialize( world, settings );
    const int32 pilot = world.addPilot( makePilot( { "striker", "sniper", "heavy" }, 0, float3{} ) );
    const int32 enemy = world.addPilot( makePilot( { "heavy" }, 1, float3{ 0.0f, 0.0f, 50.0f } ) );
    world.start();
    SW_EXPECT_EQUAL( world.getTeamGauge( 0 ), 600 ); // 첫 기체 코스트 300 × 2
    SW_EXPECT_EQUAL( world.getTeamGauge( 1 ), 1200 );
    SW_EXPECT_TRUE( world.requestMechSwap( pilot, 1 ) == MechSwapResult::NotDestroyed );

    world.applyDamage( enemy, pilot, 10000.0f, 0.0f, 0.0f );
    SW_EXPECT_TRUE( world.findPilot( pilot )->_state == MechPilotState::Destroyed );
    SW_EXPECT_EQUAL( world.getTeamGauge( 0 ), 300 );
    SW_EXPECT_EQUAL( world.findPilot( enemy )->_kills, 1 );
    SW_EXPECT_TRUE( world.requestMechSwap( pilot, 2 ) == MechSwapResult::OverCost ); // 600 > 300
    SW_EXPECT_TRUE( world.requestMechSwap( pilot, 7 ) == MechSwapResult::InvalidSlot );
    SW_EXPECT_TRUE( world.requestMechSwap( pilot, 1 ) == MechSwapResult::Ok );

    runSteps( world, 60 + 5 );
    vector<MechArenaEvent> listEvent;
    world.drainEvents( listEvent );
    SW_EXPECT_EQUAL( countEvents( listEvent, MechArenaEvent::Kind::Respawned, pilot ), 1 );
    SW_ASSERT_TRUE( world.findPilot( pilot )->getMech() != nullptr );
    SW_EXPECT_TRUE( world.findPilot( pilot )->getMech()->_id == hashed_string( "sniper" ) );
    SW_EXPECT_TRUE( world.findPilot( pilot )->_vitality.isInvulnerable() );

    // 바꾼 기체(200)가 격추되면 그 코스트가 빠지고, 게이지가 바닥나면 판이 끝난다.
    runSteps( world, 40 );
    world.applyDamage( enemy, pilot, 10000.0f, 0.0f, 0.0f );
    SW_EXPECT_EQUAL( world.getTeamGauge( 0 ), 100 );
    SW_EXPECT_FALSE( world.isEnded() );
    runSteps( world, 60 + 40 );
    world.applyDamage( enemy, pilot, 10000.0f, 0.0f, 0.0f );
    runSteps( world, 2 );
    SW_EXPECT_TRUE( world.isEnded() );
    SW_EXPECT_EQUAL( world.getMatch().getWinningTeam(), 1 );
    listEvent.clear();
    world.drainEvents( listEvent );
    SW_EXPECT_EQUAL( countEvents( listEvent, MechArenaEvent::Kind::MatchEnded, -1 ), 1 );
}

SW_TEST_CASE( MechArenaTest, HealthBelowSkillDoublesAttackOncePerLife )
{
    MechFixture fixture;
    SW_ASSERT_TRUE( fixture.load() );
    MechArenaWorld world;
    fixture.initialize( world );
    MechPilotConfig config = makePilot( { "striker" }, 0, float3{} );
    config._listSkillId.push_back( hashed_string( "berserk" ) );
    const int32 pilot = world.addPilot( config );
    const int32 enemy = world.addPilot( makePilot( { "heavy" }, 1, float3{ 0.0f, 0.0f, 50.0f } ) );
    world.start();

    world.applyDamage( pilot, enemy, 100.0f, 0.0f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( world.findPilot( enemy )->_vitality.getHealth(), 900.0f, 0.01f );
    SW_EXPECT_NEAR_EQUAL( world.computeModifier( pilot, hashed_string( "attack" ) ), 1.0f, 0.001f );

    world.applyDamage( enemy, pilot, 100.0f, 0.0f, 0.0f ); // 200 / 300 — 아직 절반 위
    SW_EXPECT_NEAR_EQUAL( world.computeModifier( pilot, hashed_string( "attack" ) ), 1.0f, 0.001f );
    world.applyDamage( enemy, pilot, 60.0f, 0.0f, 0.0f ); // 140 / 300 — 절반 아래
    vector<MechArenaEvent> listEvent;
    world.drainEvents( listEvent );
    SW_EXPECT_EQUAL( countEvents( listEvent, MechArenaEvent::Kind::SkillStarted, pilot ), 1 );
    SW_EXPECT_NEAR_EQUAL( world.computeModifier( pilot, hashed_string( "attack" ) ), 2.0f, 0.001f );

    world.applyDamage( pilot, enemy, 100.0f, 0.0f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( world.findPilot( enemy )->_vitality.getHealth(), 700.0f, 0.01f );

    // 지속 10 초가 끝나면 꺼지고, 같은 목숨에서는 다시 켜지지 않는다.
    runSteps( world, 60 * 11 );
    world.applyDamage( enemy, pilot, 10.0f, 0.0f, 0.0f );
    listEvent.clear();
    world.drainEvents( listEvent );
    SW_EXPECT_EQUAL( countEvents( listEvent, MechArenaEvent::Kind::SkillEnded, pilot ), 1 );
    SW_EXPECT_EQUAL( countEvents( listEvent, MechArenaEvent::Kind::SkillStarted, pilot ), 0 );
    SW_EXPECT_NEAR_EQUAL( world.computeModifier( pilot, hashed_string( "attack" ) ), 1.0f, 0.001f );
}

SW_TEST_CASE( MechArenaTest, StateBytesRoundTripAndReplayDeterministically )
{
    MechFixture fixture;
    SW_ASSERT_TRUE( fixture.load() );

    auto runScript = [&]( BitWriter& outWriter )
    {
        MechArenaWorld world;
        fixture.initialize( world );
        const int32 first  = world.addPilot( makePilot( { "striker" }, 0, float3{ -5.0f, 0.0f, 0.0f } ) );
        const int32 second = world.addPilot( makePilot( { "sniper" }, 1, float3{ 5.0f, 0.0f, 30.0f } ) );
        world.start();
        for ( int32 frame = 0; frame < 240; ++frame )
        {
            MechInput inputFirst;
            inputFirst._moveX   = frame < 120 ? 1.0f : -0.5f;
            inputFirst._moveZ   = 0.5f;
            inputFirst._bLockOn = frame == 2 ? SW_TRUE : SW_FALSE;
            inputFirst._bFire   = ( frame % 20 ) == 5 ? SW_TRUE : SW_FALSE;
            inputFirst._bDash   = frame == 60 ? SW_TRUE : SW_FALSE;
            MechInput inputSecond;
            inputSecond._moveZ    = -1.0f;
            inputSecond._bLockOn  = frame == 3 ? SW_TRUE : SW_FALSE;
            inputSecond._bSpecial = frame == 30 ? SW_TRUE : SW_FALSE;
            world.setInput( first, inputFirst );
            world.setInput( second, inputSecond );
            world.update( kMechArenaStep );
        }
        world.writeState( outWriter );
        return world.getTick();
    };

    BitWriter firstRun;
    BitWriter secondRun;
    SW_EXPECT_EQUAL( runScript( firstRun ), 240u );
    (void)runScript( secondRun );
    SW_ASSERT_EQUAL( firstRun.getByteCount(), secondRun.getByteCount() );
    SW_EXPECT_TRUE( firstRun.getBytes() == secondRun.getBytes() );

    // 읽어서 다시 쓰면 같은 바이트다.
    MechArenaSnapshot snapshot;
    BitReader         reader( firstRun.getBytes().data(), firstRun.getByteCount() );
    SW_ASSERT_TRUE( MechArenaSnapshotCodec::read( reader, snapshot ) );
    SW_EXPECT_EQUAL( snapshot._tick, 240u );
    SW_EXPECT_EQUAL( static_cast<int32>( snapshot._listPilot.size() ), 2 );
    SW_EXPECT_EQUAL( static_cast<int32>( snapshot._listTeamGauge.size() ), 2 );
    SW_EXPECT_TRUE( snapshot._phase == MatchPhase::InProgress );
    BitWriter rewritten;
    MechArenaSnapshotCodec::write( snapshot, rewritten );
    SW_EXPECT_TRUE( rewritten.getBytes() == firstRun.getBytes() );

    // 모자란 바이트는 거절한다.
    MechArenaSnapshot broken;
    BitReader         shortReader( firstRun.getBytes().data(), firstRun.getByteCount() / 2 );
    SW_EXPECT_FALSE( MechArenaSnapshotCodec::read( shortReader, broken ) );
}
