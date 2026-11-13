#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/MechArena/MechArenaSnapshot.h"
#include "GameFramework/Kits/Genre/Action/MechArena/MechArenaWorld.h"

#include <cstring>

// 기체 대전 키트의 규칙: 사격 · 탄창 · 유도탄 · 근접 콤보 · 다운과 기상 · 부스트 과열 · 점프와 부유 · 변형 · 스킬 · 팀 게이지와 리스폰 · 기체 교체 · 승패 · 스냅샷 · 상태 왕복 · 카탈로그.
// 결함: 격추 중 기체를 바꾸면 스냅샷이 새 기체의 슬롯 번호로 옛 기체의 무기 목록 밖을 읽었다 · 셋 이상의 판에서 게이지가 바닥난 팀의 조종사가 내려가지 않았다 · 모르는 스킬을 말없이 뺐다 ·
// 없는 무기 · 기술을 가리키는 슬롯이 아무 일도 하지 않았다 · 판이 끝나면 근접 슬롯이 남았다 · 겹친 기체 · 스킬 id 가 앞 정의를 덮어썼다 · 상태 읽기가 범위 밖 근접 슬롯 · 참가자 · 록온 대상과 음수 전적 · 탄 수치를 받았다. 키트 GF_MechArena(nogpu).

using namespace sw;

namespace
{
    constexpr const utf8* kMechWeaponXML = R"(<WeaponCatalog>
  <Weapon id="rifle" projectile="beam" accepts="Ammo.Beam" fireInterval="0.1" reloadTime="0.2" minSpread="0" maxSpread="0" spreadPerShot="0" magazineSize="3" maxReserveAmmo="3"/>
</WeaponCatalog>)";

    // 주무기의 탄(무기와 탄은 따로 적고 무기가 받음 조건으로 고른다)과 특수기의 탄(슬롯이 `projectile` 로 가리킨다). 광선 · 미사일은 중력을 받지 않는다. `flat` 은 사거리가 없어 특수기가 받지 않는다.
    constexpr const utf8* kMechProjectileXML = R"(<ProjectileCatalog>
  <Projectile id="beam" tags="Ammo.Beam" damage="10" range="100" speed="0" gravityScale="0"/>
  <Projectile id="missile" tags="Ammo.Missile" damage="20" range="100" speed="10" gravityScale="0"/>
  <Projectile id="flat" tags="Ammo.Missile" damage="20" range="0" speed="10" gravityScale="0"/>
</ProjectileCatalog>)";

    constexpr const utf8* kMechMoveXML = R"(<MoveCatalog>
  <Move id="slash1" startup="2" active="2" recovery="6" damage="15" hitstun="4"><Cancel moves="slash2" from="1" to="9"/></Move>
  <Move id="slash2" startup="2" active="2" recovery="2" damage="20" hitstun="4" knockdown="true"/>
</MoveCatalog>)";

    constexpr const utf8* kMechCatalogXML = R"(<MechCatalog deckCostLimit="2000">
  <Class id="Near" melee="2" shot="0.5"/>
  <Class id="Far" down="2"/>
  <Mech id="gunner" name="Gunner" cost="300" hp="100" down="100" downRecovery="0" downTime="1" wakeInvulnerable="1" boostRegen="0" overheatPenalty="0" dashSpeed="20" dashTime="0.1" dashCost="25" jumpCost="15" jumpSpeed="10" hoverPerSecond="30">
    <Weapon id="rifle" kind="Shot" weapon="rifle" down="10" stagger="0.2" knockback="1"/>
    <Weapon id="missile" kind="Special" projectile="missile" homing="90" cooldown="1" down="0" stagger="0"/>
    <Weapon id="saber" kind="Melee" moves="slash1,slash2" range="2" down="0"/>
  </Mech>
  <Mech id="brawler" name="Brawler" class="Near" cost="300" hp="100" down="100" downTime="1" boostRegen="0">
    <Weapon id="saber" kind="Melee" moves="slash1,slash2" range="2" down="0"/>
  </Mech>
  <Mech id="sniper" name="Sniper" class="Far" cost="300" hp="100" down="100" downRecovery="0" boostRegen="0"/>
  <Mech id="plain" name="Plain" cost="200" hp="100" boostRegen="0"/>
  <Mech id="heavy" name="Heavy" cost="900" hp="300" boostRegen="0">
    <Weapon id="rifle" kind="Shot" weapon="rifle"/>
  </Mech>
  <Mech id="flyer" name="Flyer" cost="300" hp="100" transformTime="1" skillSlots="1" boostRegen="0">
    <Mode id="ms" speed="9"><Weapon id="rifle" kind="Shot" weapon="rifle"/></Mode>
    <Mode id="jet" speed="20"><Weapon id="missile" kind="Special" projectile="missile" down="0"/></Mode>
  </Mech>
  <Mech id="ghostspecial" name="GhostSpecial" cost="100"><Weapon id="x" kind="Special" projectile="ghost"/></Mech>
  <Mech id="flatspecial" name="FlatSpecial" cost="100"><Weapon id="x" kind="Special" projectile="flat"/></Mech>
  <Mech id="ghostgun" name="GhostGun" cost="100"><Weapon id="x" kind="Shot" weapon="ghost"/></Mech>
  <Mech id="ghostmove" name="GhostMove" cost="100"><Weapon id="x" kind="Melee" moves="ghost" range="1"/></Mech>
  <Mech id="nomoves" name="NoMoves" cost="100"><Weapon id="x" kind="Melee" range="1"/></Mech>
  <Skill id="iron" trigger="Manual" duration="1" cooldown="5"><Stats defense="2"/></Skill>
  <Skill id="rage" trigger="HealthBelow" threshold="0.5" duration="100"><Stats attack="2"/></Skill>
  <Skill id="revenge" trigger="OnDown" duration="100"><Stats speed="2"/></Skill>
  <Skill id="reborn" trigger="OnRespawn" duration="100"><Stats boostRegen="3"/></Skill>
</MechCatalog>)";

    constexpr float32 kMechStep      = 0.05f;
    constexpr float32 kMechTolerance = 1.0e-3f;

    // `writePilot` 의 머리 배치(조종사 머리부터의 바이트). 꾸민 바이트를 쓰려고 적는다.
    constexpr size_t kMechFieldTeam        = 0;
    constexpr size_t kMechFieldDeck        = 4;
    constexpr size_t kMechFieldNextDeck    = 8;
    constexpr size_t kMechFieldMode        = 12;
    constexpr size_t kMechFieldCombo       = 16;
    constexpr size_t kMechFieldMeleeSlot   = 20;
    constexpr size_t kMechFieldKills       = 24;
    constexpr size_t kMechFieldDeaths      = 28;
    constexpr size_t kMechFieldState       = 32;
    constexpr size_t kMechFieldGrounded    = 33;
    constexpr size_t kMechFieldParticipant = 37; ///< 개수(uint32) 뒤에 번호들
    constexpr size_t kMechPositionsSize    = 48; ///< 위치, 속도, 앞, 대시 방향
    constexpr size_t kMechTimersSize       = 12; ///< 대시, 경직, 변형 쿨다운
    constexpr size_t kMechInputSize        = 20; ///< 이동 둘, 스킬 슬롯, 깃발 여덟
    // `ProjectileSimulation::writeState` 의 탄 하나(정의 이름 길이 4 + "missile" 7, 위치 12, 속도 12 뒤에 실수 11 개, id, 반응 번호, 정수 6 개, 깃발). 특수기 탄의 정의 이름은 슬롯의 탄이다
    constexpr size_t kMechProjectileSize        = 112;
    constexpr size_t kMechProjectileVelocity    = 23;
    constexpr size_t kMechProjectileRange       = 51;
    constexpr size_t kMechProjectileRestitution = 75;
    constexpr size_t kMechProjectilePayload     = 83;
    constexpr size_t kMechProjectileOwner       = 87;

    /** @brief 카탈로그와 판 하나입니다. 팀은 A(0), B(1)로 시작합니다. */
    struct MechArenaRulesTest
    {
        WeaponCatalog          _weapons{};
        ProjectileCatalog      _projectiles{};
        MoveCatalog            _moves{};
        MechCatalog            _mechs{};
        MechArenaSettings      _settings{};
        MechArenaWorld         _world{};
        vector<MechArenaEvent> _listEvent{};

        MechArenaRulesTest()
        {
            SW_REQUIRE_TRUE( _weapons.loadFromXMLText( kMechWeaponXML, "MechArenaTest" ) );
            SW_REQUIRE_TRUE( _projectiles.loadFromXMLText( kMechProjectileXML, "MechArenaTest" ) );
            SW_REQUIRE_TRUE( _moves.loadFromXMLText( kMechMoveXML, "MechArenaTest" ) );
            SW_REQUIRE_TRUE( _mechs.loadFromXMLText( kMechCatalogXML, "MechArenaTest" ) );
            _settings._fixedStep           = kMechStep;
            _settings._respawnDelay        = 0.5f;
            _settings._respawnInvulnerable = 0.0f;
        }

        [[nodiscard]] bool open()
        {
            if ( _world.initialize( _settings, &_mechs, &_weapons, &_projectiles, &_moves ) == false )
                return false;
            (void)_world.addTeam( hashed_string( "A" ) );
            (void)_world.addTeam( hashed_string( "B" ) );
            return true;
        }

        expected<int32> addPilot( int32 team, const float3& position, const vector<hashed_string>& listMech, const vector<hashed_string>& listSkill = {} )
        {
            MechPilotConfig config;
            config._listMechID    = listMech;
            config._listSkillID   = listSkill;
            config._spawnPosition = position;
            config._team          = team;
            return _world.addPilot( config );
        }

        /** @brief 0 번(A, 원점)과 1 번(B, +Z 로 @p distance)을 세우고 시작합니다. */
        [[nodiscard]] bool openDuel( const utf8* pMech0, const utf8* pMech1, float32 distance = 10.0f )
        {
            if ( open() == false )
                return false;
            if ( addPilot( 0, float3{}, { hashed_string( pMech0 ) } ) != 0 || addPilot( 1, float3{ 0.0f, 0.0f, distance }, { hashed_string( pMech1 ) } ) != 1 )
                return false;
            _world.start();
            return true;
        }

        /** @brief 걸음 수를 세어 진행합니다(시간을 넘기면 float 누적이 걸음 수를 흔든다). */
        void step( int32 stepCount )
        {
            const uint32 target = _world.getTick() + static_cast<uint32>( stepCount );
            for ( int32 guard = 0; guard < stepCount * 2 && _world.getTick() < target; ++guard )
            {
                _world.update( kMechStep );
            }
        }

        const MechPilot& pilot( int32 index ) const { return *_world.findPilot( index ); }

        void kill( int32 attacker, int32 victim ) { _world.applyDamage( attacker, victim, 1.0e6f, 0.0f, 0.0f ); }

        int32 countEvents( MechArenaEvent::Kind kind )
        {
            vector<MechArenaEvent> listNew;
            _world.pollEvents( listNew );
            for ( const MechArenaEvent& event : listNew )
            {
                _listEvent.push_back( event );
            }
            int32 count = 0;
            for ( const MechArenaEvent& event : _listEvent )
            {
                count += event._kind == kind ? 1 : 0;
            }
            return count;
        }

        vector<uint8> writeBytes( const MechArenaWorld& world ) const
        {
            Archive written;
            world.writeState( written );
            vector<uint8> listByte;
            written.writeData( listByte );
            return listByte;
        }

        /** @brief 0 번 조종사 머리가 상태 바이트에서 시작하는 자리입니다. 앞의 구간을 같은 함수로 다시 써서 잽니다. */
        size_t computePilotHead() const
        {
            Archive prefix;
            prefix << static_cast<uint32>( 2 );
            StateIO::writeName( prefix, hashed_string( "A" ) );
            StateIO::writeName( prefix, hashed_string( "B" ) );
            prefix << static_cast<uint8>( SW_TRUE );
            prefix << static_cast<uint8>( SW_FALSE );
            prefix << _world.getTick();
            prefix << 0.0f; // 고정 걸음 타이머
            _world.getMatch().writeState( prefix );
            prefix << static_cast<uint32>( _world.getPilotCount() );
            const MechPilot& first = pilot( 0 );
            prefix << static_cast<uint32>( first._listDeckMech.size() );
            for ( const MechDef* pMech : first._listDeckMech )
            {
                StateIO::writeName( prefix, pMech->_id );
            }
            prefix << static_cast<uint32>( first._listSkill.size() );
            for ( const MechSkillDef* pSkill : first._listSkill )
            {
                StateIO::writeName( prefix, pSkill->_id );
            }
            prefix << first._spawnPosition;
            return static_cast<size_t>( prefix.getSize() );
        }

        /** @brief 0 번 조종사의 록온 대상이 상태 바이트에서 시작하는 자리입니다. 스킬 없는 조종사만 됩니다. */
        size_t computeLockOnOffset() const
        {
            const MechPilot& first = pilot( 0 );
            Archive          healthMachine;
            first._healthMachine.writeState( healthMachine );
            Archive boost;
            first._boost.writeState( boost );
            return computePilotHead() + kMechFieldParticipant + 4 + 4 * first._listParticipant.size() + kMechPositionsSize + kMechTimersSize + 2 * kMechInputSize +
                   static_cast<size_t>( healthMachine.getSize() ) + static_cast<size_t>( boost.getSize() );
        }

        /** @brief 같은 카탈로그의 새 판에 바이트를 읽습니다. */
        bool readInFreshWorld( const vector<uint8>& listByte )
        {
            MechArenaWorld target;
            if ( target.initialize( _settings, &_mechs, &_weapons, &_projectiles, &_moves ) == false )
                return false;
            Archive reading( listByte.data(), listByte.size() );
            return target.readState( reading );
        }

        template <typename T>
        bool readPatched( size_t offset, T value )
        {
            vector<uint8> listByte = writeBytes( _world );
            std::memcpy( listByte.data() + offset, &value, sizeof( T ) );
            return readInFreshWorld( listByte );
        }
    };
} // namespace

/**
 * @brief [MechArenaRulesTest] 쏘면 앞의 적을 자동으로 잡아 광선이 맞고, 피해 · 다운치 · 경직 · 넉백이 들어간다
 */
SW_TEST_CASE_F( MechArenaRulesTest, ShotLocksOnAndHits )
{
    SW_REQUIRE_TRUE( openDuel( "gunner", "gunner" ) );
    MechInput fire;
    fire._bFire = SW_TRUE;
    _world.setInput( 0, fire );
    step( 1 );
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::Fired ) );
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::Hit ) );
    SW_EXPECT_NEAR( 90.0f, pilot( 1 )._healthMachine.getHealth(), kMechTolerance );
    SW_EXPECT_NEAR( 90.0f, pilot( 1 )._healthMachine.getPoise(), kMechTolerance );
    SW_EXPECT_NEAR( 11.0f, pilot( 1 )._position._z, kMechTolerance );
    SW_EXPECT_FALSE( pilot( 1 ).canAct() );
    SW_EXPECT_EQUAL( uint64( 2 ), pilot( 0 )._lockOn.getTarget() ); // 조종사 번호 + 1
    SW_EXPECT_EQUAL( 2, pilot( 0 )._listWeaponState[0].getMagazineAmmo() );
}

/**
 * @brief [MechArenaRulesTest] 연사는 간격마다 한 발이고, 탄창이 비면 예비탄으로 재장전한다
 */
SW_TEST_CASE_F( MechArenaRulesTest, MagazineEmptiesThenReloads )
{
    SW_REQUIRE_TRUE( openDuel( "gunner", "heavy" ) );
    MechInput fire;
    fire._bFire = SW_TRUE;
    _world.setInput( 0, fire );
    step( 6 ); // 0.1 초 간격이면 1, 3, 5 걸음째에 쏜다
    SW_EXPECT_EQUAL( 3, countEvents( MechArenaEvent::Kind::Fired ) );
    SW_EXPECT_EQUAL( 0, pilot( 0 )._listWeaponState[0].getMagazineAmmo() );
    step( 1 ); // 빈 탄창으로 당겼다
    SW_EXPECT_TRUE( pilot( 0 )._listWeaponState[0].isReloading() );
    _world.setInput( 0, MechInput{} );
    step( 6 );
    SW_EXPECT_FALSE( pilot( 0 )._listWeaponState[0].isReloading() );
    SW_EXPECT_EQUAL( 3, pilot( 0 )._listWeaponState[0].getMagazineAmmo() );
    SW_EXPECT_EQUAL( 0, pilot( 0 )._listWeaponState[0].getReserveAmmo() );
}

/**
 * @brief [MechArenaRulesTest] 같은 팀은 고르지도 맞히지도 않는다
 */
SW_TEST_CASE_F( MechArenaRulesTest, TeammatesAreNeitherLockedNorHit )
{
    SW_REQUIRE_TRUE( open() );
    SW_REQUIRE_TRUE( addPilot( 0, float3{}, { hashed_string( "gunner" ) } ) == 0 );
    SW_REQUIRE_TRUE( addPilot( 0, float3{ 0.0f, 0.0f, 10.0f }, { hashed_string( "gunner" ) } ) == 1 );
    _world.start();
    MechInput fire;
    fire._bFire = SW_TRUE;
    _world.setInput( 0, fire );
    step( 1 );
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::Fired ) );
    SW_EXPECT_EQUAL( 0, countEvents( MechArenaEvent::Kind::Hit ) );
    SW_EXPECT_FALSE( pilot( 0 )._lockOn.hasTarget() );
}

/**
 * @brief [MechArenaRulesTest] 특수기는 탄을 날리고 쿨다운이 돌며, 유도탄은 옮겨 간 대상을 따라가 맞힌다
 */
SW_TEST_CASE_F( MechArenaRulesTest, HomingSpecialFollowsItsTarget )
{
    SW_REQUIRE_TRUE( openDuel( "gunner", "gunner" ) );
    MechInput special;
    special._bSpecial = SW_TRUE;
    _world.setInput( 0, special );
    step( 1 );
    SW_REQUIRE_EQUAL( size_t( 1 ), _world.getProjectiles().size() );
    SW_EXPECT_EQUAL( 1, _world.getProjectiles()[0]._target );
    SW_EXPECT_TRUE( pilot( 0 )._listSpecialCooldown[1].isActive() );

    _world.setInput( 0, MechInput{} );
    step( 1 );
    _world.setInput( 0, special ); // 쿨다운 중이라 나가지 않는다
    step( 1 );
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::Fired ) );
    _world.setInput( 0, MechInput{} );

    _world.teleport( 1, float3{ 5.0f, 0.0f, 10.0f } );
    step( 40 );
    SW_EXPECT_NEAR( 80.0f, pilot( 1 )._healthMachine.getHealth(), kMechTolerance );
    SW_EXPECT_TRUE( _world.getProjectiles().empty() );
}

/**
 * @brief [MechArenaRulesTest] 사거리가 끝나거나 판 밖으로 나간 탄은 사라진다
 */
SW_TEST_CASE_F( MechArenaRulesTest, ProjectileExpiresAtRange )
{
    _settings._arenaHalfSize = 2.0f;
    SW_REQUIRE_TRUE( open() );
    SW_REQUIRE_TRUE( addPilot( 0, float3{}, { hashed_string( "gunner" ) } ) == 0 );
    SW_REQUIRE_TRUE( addPilot( 1, float3{ 2.0f, 0.0f, -2.0f }, { hashed_string( "gunner" ) } ) == 1 ); // 뒤에 있어 잡지 않으니 앞으로 곧게 간다
    _world.start();
    MechInput special;
    special._bSpecial = SW_TRUE;
    _world.setInput( 0, special );
    step( 1 );
    SW_REQUIRE_EQUAL( size_t( 1 ), _world.getProjectiles().size() );
    SW_EXPECT_EQUAL( -1, _world.getProjectiles()[0]._target );
    step( 10 );
    SW_EXPECT_TRUE( _world.getProjectiles().empty() );
    SW_EXPECT_EQUAL( 0, countEvents( MechArenaEvent::Kind::Hit ) );
}

/**
 * @brief [MechArenaRulesTest] 근접은 발생 프레임에 맞고 눌러 둔 다음 단이 캔슬 창에서 나가며, 눕히는 기술은 다운치를 채운다. 콤보 중에는 움직이지 않고 끝나면 풀린다
 */
SW_TEST_CASE_F( MechArenaRulesTest, MeleeComboAdvancesAndKnocksDown )
{
    SW_REQUIRE_TRUE( openDuel( "brawler", "gunner", 3.0f ) );
    MechInput melee;
    melee._bMelee = SW_TRUE;
    melee._moveX  = 1.0f;
    MechInput hold;
    hold._moveX = 1.0f;
    _world.setInput( 0, melee );
    step( 1 );
    SW_EXPECT_EQUAL( 0, pilot( 0 )._comboStage );
    SW_EXPECT_NEAR( 100.0f, pilot( 1 )._healthMachine.getHealth(), kMechTolerance );

    _world.setInput( 0, hold );
    step( 1 );
    SW_EXPECT_NEAR( 70.0f, pilot( 1 )._healthMachine.getHealth(), kMechTolerance ); // 15 x 근거리 근접 2
    SW_EXPECT_TRUE( pilot( 1 )._staggerRemaining.isActive() );

    _world.setInput( 0, melee );
    step( 1 );
    SW_EXPECT_EQUAL( 1, pilot( 0 )._comboStage );
    SW_EXPECT_EQUAL( 2, countEvents( MechArenaEvent::Kind::ComboAdvanced ) );

    _world.setInput( 0, hold );
    step( 1 );
    SW_EXPECT_NEAR( 30.0f, pilot( 1 )._healthMachine.getHealth(), kMechTolerance ); // 20 x 2
    SW_EXPECT_TRUE( pilot( 1 )._state == MechPilotState::Down );
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::Downed ) );
    SW_EXPECT_NEAR( 0.0f, pilot( 0 )._position._x, kMechTolerance );

    step( 4 );
    SW_EXPECT_EQUAL( -1, pilot( 0 )._comboStage );
    SW_EXPECT_EQUAL( -1, pilot( 0 )._meleeSlot );
    step( 1 );
    SW_EXPECT_TRUE( pilot( 0 )._position._x > 0.0f ); // 다시 움직인다
}

/**
 * @brief [MechArenaRulesTest] 다운치가 차면 누워 있다가 기상 무적을 받고 일어난다. 누운 동안 조작이 없고, 분류 보정이 받는 다운치를 바꾼다
 */
SW_TEST_CASE_F( MechArenaRulesTest, DownThenWakeWithInvulnerability )
{
    SW_REQUIRE_TRUE( openDuel( "gunner", "gunner" ) );
    _world.applyDamage( 0, 1, 1.0f, 100.0f, 0.0f );
    SW_EXPECT_TRUE( pilot( 1 )._state == MechPilotState::Down );
    SW_EXPECT_FALSE( pilot( 1 ).canAct() );
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::Downed ) );
    _world.applyDamage( 0, 1, 1.0f, 100.0f, 0.0f ); // 누운 채 맞아도 다시 쓰러지지 않는다
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::Downed ) );
    SW_EXPECT_NEAR( 98.0f, pilot( 1 )._healthMachine.getHealth(), kMechTolerance );

    step( 21 );
    SW_EXPECT_TRUE( pilot( 1 )._state == MechPilotState::Active );
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::WokeUp ) );
    SW_EXPECT_TRUE( pilot( 1 )._healthMachine.isInvulnerable() );
    SW_EXPECT_NEAR( 100.0f, pilot( 1 )._healthMachine.getPoise(), kMechTolerance );
    _world.applyDamage( 0, 1, 10.0f, 0.0f, 0.0f );
    SW_EXPECT_NEAR( 98.0f, pilot( 1 )._healthMachine.getHealth(), kMechTolerance );
    SW_EXPECT_EQUAL( 2, countEvents( MechArenaEvent::Kind::Hit ) );

    SW_REQUIRE_TRUE( openDuel( "gunner", "sniper" ) );
    _world.applyDamage( 0, 1, 0.0f, 30.0f, 0.0f );
    SW_EXPECT_NEAR( 40.0f, pilot( 1 )._healthMachine.getPoise(), kMechTolerance ); // 원거리 보정 x2
}

/**
 * @brief [MechArenaRulesTest] 대시는 열을 올리고, 열이 다 차면 과열로 잠겨 대시와 점프가 나가지 않는다
 */
SW_TEST_CASE_F( MechArenaRulesTest, DashesOverheatTheBoost )
{
    SW_REQUIRE_TRUE( openDuel( "gunner", "gunner", 100.0f ) );
    MechInput dash;
    dash._bDash = SW_TRUE;
    for ( int32 press = 0; press < 4; ++press )
    {
        _world.setInput( 0, dash );
        step( 1 );
        _world.setInput( 0, MechInput{} );
        step( 1 );
    }
    SW_EXPECT_NEAR( 100.0f, pilot( 0 )._boost.getValue(), kMechTolerance );
    SW_EXPECT_TRUE( pilot( 0 )._boost.isOverheated() );
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::Overheated ) );

    step( 3 ); // 남은 대시가 끝난다
    const float32 before = pilot( 0 )._position._z;
    _world.setInput( 0, dash );
    step( 1 );
    SW_EXPECT_NEAR( before, pilot( 0 )._position._z, kMechTolerance );
    SW_EXPECT_FALSE( pilot( 0 )._dashRemaining.isActive() );
    MechInput jump;
    jump._bJump = SW_TRUE;
    _world.setInput( 0, jump );
    step( 1 );
    SW_EXPECT_EQUAL( SW_TRUE, pilot( 0 )._bGrounded );
}

/**
 * @brief [MechArenaRulesTest] 점프는 땅에서만 나가고 내려오면 땅에 닿으며, 공중에서 누르고 있으면 부유한다
 */
SW_TEST_CASE_F( MechArenaRulesTest, JumpAndHover )
{
    SW_REQUIRE_TRUE( openDuel( "gunner", "gunner", 100.0f ) );
    MechInput jump;
    jump._bJump = SW_TRUE;
    _world.setInput( 0, jump );
    step( 1 );
    SW_EXPECT_EQUAL( SW_FALSE, pilot( 0 )._bGrounded );
    SW_EXPECT_NEAR( 15.0f, pilot( 0 )._boost.getValue(), kMechTolerance );
    _world.setInput( 0, MechInput{} );
    step( 1 );
    _world.setInput( 0, jump ); // 공중의 두 번째 점프
    step( 1 );
    SW_EXPECT_NEAR( 15.0f, pilot( 0 )._boost.getValue(), kMechTolerance );

    _world.setInput( 0, MechInput{} );
    step( 5 ); // 초속 10, 중력 25 면 8 걸음째에 정점이다
    MechInput hover;
    hover._bHover = SW_TRUE;
    _world.setInput( 0, hover );
    step( 1 );
    const float32 height = pilot( 0 )._position._y;
    const float32 heat   = pilot( 0 )._boost.getValue();
    step( 5 );
    SW_EXPECT_NEAR( height, pilot( 0 )._position._y, kMechTolerance );
    SW_EXPECT_NEAR( heat + 30.0f * kMechStep * 5.0f, pilot( 0 )._boost.getValue(), 0.01f );

    _world.setInput( 0, MechInput{} );
    step( 60 );
    SW_EXPECT_EQUAL( SW_TRUE, pilot( 0 )._bGrounded );
    SW_EXPECT_NEAR( 0.0f, pilot( 0 )._position._y, kMechTolerance );
}

/**
 * @brief [MechArenaRulesTest] 변형은 속도와 무기 세트를 바꾸고 쿨다운 동안 다시 되지 않으며, 슬롯 상태는 형태마다 따로다
 */
SW_TEST_CASE_F( MechArenaRulesTest, TransformSwapsSpeedAndWeapons )
{
    SW_REQUIRE_TRUE( openDuel( "flyer", "gunner", 100.0f ) );
    const MechDef* pMech = pilot( 0 ).getMech();
    SW_EXPECT_EQUAL( 0, pMech->computeSlotOffset( 0 ) );
    SW_EXPECT_EQUAL( 1, pMech->computeSlotOffset( 1 ) );
    SW_EXPECT_EQUAL( 2, pMech->computeSlotCount() );
    SW_EXPECT_EQUAL( size_t( 2 ), pilot( 0 )._listWeaponState.size() );

    MechInput transform;
    transform._bTransform = SW_TRUE;
    transform._moveX      = 1.0f;
    _world.setInput( 0, transform );
    step( 1 );
    SW_EXPECT_EQUAL( 1, pilot( 0 )._mode );
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::Transformed ) );
    SW_EXPECT_NEAR( 20.0f, pilot( 0 )._velocity._x, kMechTolerance );

    _world.setInput( 0, MechInput{} );
    step( 1 );
    _world.setInput( 0, transform );
    step( 1 );
    SW_EXPECT_EQUAL( 1, pilot( 0 )._mode ); // 쿨다운 중
    _world.setInput( 0, MechInput{} );

    MechInput special;
    special._bSpecial = SW_TRUE;
    _world.setInput( 0, special );
    step( 1 );
    SW_EXPECT_EQUAL( size_t( 1 ), _world.getProjectiles().size() ); // 비행형의 슬롯이 나간다
    _world.setInput( 0, MechInput{} );

    step( 21 );
    _world.setInput( 0, transform );
    step( 1 );
    SW_EXPECT_EQUAL( 0, pilot( 0 )._mode );
}

/**
 * @brief [MechArenaRulesTest] 수동 스킬은 슬롯을 눌러 켜고 시간이 끝나면 꺼지며, 쿨다운 중에는 다시 켜지지 않는다. 배율은 받는 피해에 든다
 */
SW_TEST_CASE_F( MechArenaRulesTest, ManualSkillAppliesThenEndsAndCoolsDown )
{
    const hashed_string defense( "defense" );
    SW_REQUIRE_TRUE( open() );
    SW_REQUIRE_TRUE( addPilot( 0, float3{}, { hashed_string( "gunner" ) }, { hashed_string( "iron" ) } ) == 0 );
    SW_REQUIRE_TRUE( addPilot( 1, float3{ 0.0f, 0.0f, 100.0f }, { hashed_string( "gunner" ) } ) == 1 );
    _world.start();
    SW_EXPECT_NEAR( 1.0f, _world.computeModifier( 0, defense ), kMechTolerance );

    MechInput useSkill;
    useSkill._skillSlot = 0;
    _world.setInput( 0, useSkill );
    step( 1 );
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::SkillStarted ) );
    SW_EXPECT_NEAR( 2.0f, _world.computeModifier( 0, defense ), kMechTolerance );
    _world.applyDamage( 1, 0, 10.0f, 0.0f, 0.0f );
    SW_EXPECT_NEAR( 95.0f, pilot( 0 )._healthMachine.getHealth(), kMechTolerance );

    _world.setInput( 0, MechInput{} );
    step( 21 );
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::SkillEnded ) );
    SW_EXPECT_NEAR( 1.0f, _world.computeModifier( 0, defense ), kMechTolerance );

    _world.setInput( 0, useSkill );
    step( 1 );
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::SkillStarted ) );       // 쿨다운 중
    SW_EXPECT_NEAR( 1.0f, _world.computeModifier( 99, defense ), kMechTolerance ); // 없는 조종사는 보정이 없다
}

/**
 * @brief [MechArenaRulesTest] 체력 조건 스킬은 문턱 아래로 내려갈 때 켜져 주는 피해를 늘리고, 다운 스킬은 쓰러질 때 켜지며, 기체의 슬롯 수만큼만 쓴다
 */
SW_TEST_CASE_F( MechArenaRulesTest, TriggeredSkillsAndSlotLimit )
{
    SW_REQUIRE_TRUE( open() );
    SW_REQUIRE_TRUE( addPilot( 0, float3{}, { hashed_string( "gunner" ) }, { hashed_string( "rage" ), hashed_string( "revenge" ) } ) == 0 );
    SW_REQUIRE_TRUE( addPilot( 1, float3{ 0.0f, 0.0f, 100.0f }, { hashed_string( "gunner" ) } ) == 1 );
    SW_REQUIRE_TRUE( addPilot( 1, float3{ 0.0f, 0.0f, 100.0f }, { hashed_string( "flyer" ) }, { hashed_string( "iron" ), hashed_string( "rage" ) } ) == 2 );
    _world.start();

    _world.applyDamage( 1, 0, 40.0f, 0.0f, 0.0f );
    SW_EXPECT_EQUAL( 0, countEvents( MechArenaEvent::Kind::SkillStarted ) ); // 아직 문턱 위(0.6)
    _world.applyDamage( 1, 0, 20.0f, 0.0f, 0.0f );
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::SkillStarted ) );
    SW_EXPECT_NEAR( 2.0f, _world.computeModifier( 0, hashed_string( "attack" ) ), kMechTolerance );
    _world.applyDamage( 0, 1, 10.0f, 0.0f, 0.0f );
    SW_EXPECT_NEAR( 80.0f, pilot( 1 )._healthMachine.getHealth(), kMechTolerance ); // 10 x 2

    _world.applyDamage( 1, 0, 1.0f, 100.0f, 0.0f );
    SW_EXPECT_EQUAL( 2, countEvents( MechArenaEvent::Kind::SkillStarted ) ); // 다운 스킬
    SW_EXPECT_NEAR( 2.0f, _world.computeModifier( 0, hashed_string( "speed" ) ), kMechTolerance );

    // 비행기는 슬롯이 하나라 둘째 스킬은 없는 것과 같다.
    _world.applyDamage( 0, 2, 40.0f, 0.0f, 0.0f );
    SW_EXPECT_NEAR( 1.0f, _world.computeModifier( 2, hashed_string( "attack" ) ), kMechTolerance );
}

/**
 * @brief [MechArenaRulesTest] 격추되면 게이지에서 코스트가 빠지고 대기 뒤 스폰 지점에 가득 차서 돌아오며, 게이지가 바닥난 팀이 지고 판이 끝난다. 부활 스킬이 켜진다
 */
SW_TEST_CASE_F( MechArenaRulesTest, GaugeRespawnsThenEndsTheMatch )
{
    SW_REQUIRE_TRUE( open() );
    SW_REQUIRE_TRUE( addPilot( 0, float3{}, { hashed_string( "gunner" ) } ) == 0 );
    SW_REQUIRE_TRUE( addPilot( 1, float3{ 0.0f, 0.0f, 10.0f }, { hashed_string( "gunner" ) }, { hashed_string( "reborn" ) } ) == 1 );
    _world.start();
    SW_EXPECT_EQUAL( 600, _world.getTeamGauge( 1 ) ); // 첫 기체의 코스트 x 2

    _world.teleport( 1, float3{ 3.0f, 0.0f, 10.0f } );
    kill( 0, 1 );
    SW_EXPECT_TRUE( pilot( 1 )._state == MechPilotState::Destroyed );
    SW_EXPECT_EQUAL( 300, _world.getTeamGauge( 1 ) );
    SW_EXPECT_EQUAL( 1, pilot( 0 )._kills );
    SW_EXPECT_EQUAL( 1, pilot( 1 )._deaths );
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::Destroyed ) );
    _world.applyDamage( 0, 1, 10.0f, 0.0f, 0.0f ); // 격추된 쪽은 맞지 않는다
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::Hit ) );

    step( 12 );
    SW_EXPECT_TRUE( pilot( 1 )._state == MechPilotState::Active );
    SW_EXPECT_NEAR( 100.0f, pilot( 1 )._healthMachine.getHealth(), kMechTolerance );
    SW_EXPECT_NEAR( 0.0f, pilot( 1 )._position._x, kMechTolerance );
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::Respawned ) );
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::SkillStarted ) );

    kill( 0, 1 );
    step( 1 );
    SW_EXPECT_TRUE( _world.isEnded() );
    SW_EXPECT_EQUAL( 0, _world.getMatch().getWinningTeam() );
    SW_EXPECT_TRUE( pilot( 0 )._state == MechPilotState::Retired );
    SW_EXPECT_TRUE( pilot( 1 )._state == MechPilotState::Retired );
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::MatchEnded ) );
    const uint32 tick = _world.getTick();
    step( 2 );
    SW_EXPECT_EQUAL( tick + 2, _world.getTick() ); // 걸음은 돌지만 아무도 움직이지 않는다
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::MatchEnded ) );
}

/**
 * @brief [MechArenaRulesTest] 시간 제한이 끝나면 남은 게이지가 많은 팀이 이기고, 같으면 무승부다
 */
SW_TEST_CASE_F( MechArenaRulesTest, TimeLimitComparesGauges )
{
    _settings._timeLimit = 0.2f;
    SW_REQUIRE_TRUE( openDuel( "gunner", "gunner" ) );
    kill( 0, 1 );
    step( 6 );
    SW_EXPECT_TRUE( _world.isEnded() );
    SW_EXPECT_EQUAL( 0, _world.getMatch().getWinningTeam() );

    SW_REQUIRE_TRUE( openDuel( "gunner", "gunner" ) );
    step( 6 );
    SW_EXPECT_TRUE( _world.isEnded() );
    SW_EXPECT_EQUAL( -1, _world.getMatch().getWinningTeam() );
}

/**
 * @brief [MechArenaRulesTest] 셋 이상의 판에서 게이지가 바닥난 팀은 판이 끝나지 않아도 내려간다. 결함: 남은 조종사가 계속 싸우고 격추된 조종사는 오지 않을 부활을 기다렸다
 */
SW_TEST_CASE_F( MechArenaRulesTest, EliminatedTeamRetiresWhileTheMatchGoesOn )
{
    _settings._teamGauge = 300;
    SW_REQUIRE_TRUE( open() );
    const int32 teamC = _world.addTeam( hashed_string( "C" ) );
    SW_REQUIRE_TRUE( addPilot( 0, float3{}, { hashed_string( "gunner" ) } ) == 0 );
    SW_REQUIRE_TRUE( addPilot( 1, float3{ 0.0f, 0.0f, 50.0f }, { hashed_string( "gunner" ) } ) == 1 );
    SW_REQUIRE_TRUE( addPilot( teamC, float3{ 50.0f, 0.0f, 0.0f }, { hashed_string( "gunner" ) } ) == 2 );
    SW_REQUIRE_TRUE( addPilot( teamC, float3{ -50.0f, 0.0f, 0.0f }, { hashed_string( "gunner" ) } ) == 3 );
    _world.start();

    kill( 0, 2 );
    step( 1 );
    SW_EXPECT_FALSE( _world.isEnded() );
    SW_EXPECT_TRUE( pilot( 0 )._state == MechPilotState::Active );
    SW_EXPECT_TRUE( pilot( 1 )._state == MechPilotState::Active );
    SW_EXPECT_TRUE( pilot( 2 )._state == MechPilotState::Retired );
    SW_EXPECT_TRUE( pilot( 3 )._state == MechPilotState::Retired );
    _world.applyDamage( 0, 3, 10.0f, 0.0f, 0.0f );
    SW_EXPECT_NEAR( 100.0f, pilot( 3 )._healthMachine.getHealth(), kMechTolerance );
}

/**
 * @brief [MechArenaRulesTest] 기체 교체는 격추된 동안만, 있는 슬롯만, 남은 게이지 안의 코스트만 받고 다음 출격에 장착한다.
 *        결함: 교체 즉시 지금 슬롯이 바뀌어, 무기 목록은 옛 기체인 채 스냅샷이 새 기체의 슬롯 번호로 목록 밖을 읽었다
 */
SW_TEST_CASE_F( MechArenaRulesTest, MechSwapWaitsForTheNextSortie )
{
    SW_REQUIRE_TRUE( open() );
    SW_REQUIRE_TRUE( addPilot( 0, float3{}, { hashed_string( "sniper" ), hashed_string( "gunner" ), hashed_string( "heavy" ) } ) == 0 );
    SW_REQUIRE_TRUE( addPilot( 1, float3{ 0.0f, 0.0f, 10.0f }, { hashed_string( "gunner" ) } ) == 1 );
    _world.start();
    SW_EXPECT_EQUAL( 600, _world.getTeamGauge( 0 ) ); // 첫 기체(sniper) 코스트 x 2
    SW_EXPECT_TRUE( _world.requestMechSwap( 0, 1 ) == MechSwapResult::NotDestroyed );
    SW_EXPECT_TRUE( _world.requestMechSwap( 99, 1 ) == MechSwapResult::InvalidSlot );

    kill( 1, 0 );
    SW_EXPECT_EQUAL( 300, _world.getTeamGauge( 0 ) );
    SW_EXPECT_TRUE( _world.requestMechSwap( 0, 9 ) == MechSwapResult::InvalidSlot );
    SW_EXPECT_TRUE( _world.requestMechSwap( 0, -1 ) == MechSwapResult::InvalidSlot );
    SW_EXPECT_TRUE( _world.requestMechSwap( 0, 2 ) == MechSwapResult::OverCost ); // 900 > 남은 300
    SW_EXPECT_TRUE( _world.requestMechSwap( 0, 1 ) == MechSwapResult::Ok );
    SW_EXPECT_EQUAL( 1, countEvents( MechArenaEvent::Kind::MechSwapped ) );
    SW_EXPECT_EQUAL( 0, pilot( 0 )._deckIndex );
    SW_EXPECT_EQUAL( 1, pilot( 0 )._nextDeckIndex );

    MechArenaSnapshot snapshot;
    _world.makeSnapshot( snapshot ); // 옛 기체에는 무기가 없고 새 기체의 0 번은 주무기다
    SW_REQUIRE_EQUAL( size_t( 2 ), snapshot._listPilot.size() );
    SW_EXPECT_EQUAL( _mechs.findMechIndex( hashed_string( "sniper" ) ).value(), snapshot._listPilot[0]._mechIndex );
    SW_EXPECT_EQUAL( 0, snapshot._listPilot[0]._magazineAmmo );

    const vector<uint8> listSaved = writeBytes( _world );
    SW_EXPECT_TRUE( readInFreshWorld( listSaved ) ); // 교체를 기다리는 상태도 읽힌다

    step( 12 );
    SW_EXPECT_TRUE( pilot( 0 )._state == MechPilotState::Active );
    SW_EXPECT_EQUAL( 1, pilot( 0 )._deckIndex );
    SW_EXPECT_TRUE( pilot( 0 ).getMech()->_id == hashed_string( "gunner" ) );
    SW_REQUIRE_EQUAL( size_t( 3 ), pilot( 0 )._listWeaponState.size() );
    SW_EXPECT_EQUAL( 3, pilot( 0 )._listWeaponState[0].getMagazineAmmo() );

    kill( 1, 0 ); // 바뀐 기체의 코스트가 빠진다. 게이지가 바닥나 팀이 진다
    SW_EXPECT_EQUAL( 0, _world.getMatch().findTeam( 0 )->_costPool );
    step( 1 );
    SW_EXPECT_EQUAL( 1, _world.getMatch().getWinningTeam() );
}

/**
 * @brief [MechArenaRulesTest] 시작 전에는 시간도 피해도 들어가지 않고, 없는 조종사는 무시하며, 시작은 한 번이다
 */
SW_TEST_CASE_F( MechArenaRulesTest, NothingRunsBeforeStartAndUnknownPilotsAreIgnored )
{
    SW_REQUIRE_TRUE( open() );
    SW_REQUIRE_TRUE( addPilot( 0, float3{}, { hashed_string( "gunner" ) } ) == 0 );
    SW_REQUIRE_TRUE( addPilot( 1, float3{ 0.0f, 0.0f, 10.0f }, { hashed_string( "gunner" ) } ) == 1 );
    _world.update( 1.0f );
    SW_EXPECT_EQUAL( uint32( 0 ), _world.getTick() );
    kill( 0, 1 );
    SW_EXPECT_NEAR( 100.0f, pilot( 1 )._healthMachine.getHealth(), kMechTolerance );
    SW_EXPECT_TRUE( pilot( 1 )._state == MechPilotState::Waiting );

    _world.start();
    _world.start();
    SW_EXPECT_EQUAL( 2, _world.getMatch().getTeamCount() );
    SW_EXPECT_EQUAL( 2, _world.getMatch().getParticipantCount() );
    SW_EXPECT_TRUE( pilot( 0 )._state == MechPilotState::Active );
    SW_EXPECT_FALSE( addPilot( 0, float3{}, { hashed_string( "gunner" ) } ).has_value() ); // 시작 뒤

    SW_EXPECT_NULL( _world.findPilot( 99 ) );
    SW_EXPECT_NULL( _world.findPilot( -1 ) );
    _world.setInput( 99, MechInput{} );
    _world.teleport( 99, float3{} );
    _world.applyDamage( 0, 99, 10.0f, 0.0f, 0.0f );
    SW_EXPECT_EQUAL( 0, _world.getTeamGauge( 99 ) );
    SW_EXPECT_EQUAL( 0, countEvents( MechArenaEvent::Kind::Hit ) );
}

/**
 * @brief [MechArenaRulesTest] 빈 덱 · 모르는 기체 · 스킬 · 팀, 코스트 초과, 없는 무기 · 기술을 가리키는 슬롯은 거절한다. 결함: 모르는 스킬은 말없이 빠져 뒤 스킬의 슬롯이 당겨졌고, 없는 무기 슬롯은 쏴도 아무 일이 없었다
 */
SW_TEST_CASE_F( MechArenaRulesTest, BrokenPilotConfigsAreRefused )
{
    SW_EXPECT_LOG( "MechArenaWorld.MechSlotMissingData" );
    SW_REQUIRE_TRUE( open() );
    SW_EXPECT_FALSE( addPilot( 0, float3{}, {} ).has_value() );
    SW_EXPECT_FALSE( addPilot( 0, float3{}, { hashed_string( "none" ) } ).has_value() );
    SW_EXPECT_FALSE( addPilot( 5, float3{}, { hashed_string( "gunner" ) } ).has_value() );
    SW_EXPECT_FALSE( addPilot( -1, float3{}, { hashed_string( "gunner" ) } ).has_value() );
    SW_EXPECT_FALSE( addPilot( 0, float3{}, { hashed_string( "gunner" ) }, { hashed_string( "none" ) } ).has_value() );
    SW_EXPECT_FALSE( addPilot( 0, float3{}, { hashed_string( "heavy" ), hashed_string( "heavy" ), hashed_string( "heavy" ) } ).has_value() ); // 2700 > 2000
    SW_EXPECT_FALSE( addPilot( 0, float3{}, { hashed_string( "gunner" ), hashed_string( "ghostgun" ) } ).has_value() );                       // 덱 뒤의 기체도 본다
    SW_EXPECT_FALSE( addPilot( 0, float3{}, { hashed_string( "ghostmove" ) } ).has_value() );
    SW_EXPECT_FALSE( addPilot( 0, float3{}, { hashed_string( "nomoves" ) } ).has_value() );
    SW_EXPECT_FALSE( addPilot( 0, float3{}, { hashed_string( "ghostspecial" ) } ).has_value() ); // 특수기의 탄이 탄 카탈로그에 없다
    SW_EXPECT_FALSE( addPilot( 0, float3{}, { hashed_string( "flatspecial" ) } ).has_value() );  // 특수기의 탄에 사거리가 없다
    SW_EXPECT_EQUAL( 0, _world.getPilotCount() );

    MechArenaWorld  noCatalog; // initialize 전에는 받지 않는다
    MechPilotConfig config;
    config._listMechID.push_back( hashed_string( "gunner" ) );
    SW_EXPECT_FALSE( noCatalog.addPilot( config ).has_value() );
}

/**
 * @brief [MechArenaRulesTest] 범위 밖 설정은 오류로 거절하고 판을 그대로 둔다
 */
SW_TEST_CASE_F( MechArenaRulesTest, BrokenSettingsAreRefused )
{
    SW_EXPECT_LOG( "MechArenaWorld.MechArenaWorldRefusedSettings" );
    SW_REQUIRE_TRUE( openDuel( "gunner", "gunner" ) );
    MechArenaSettings broken;
    broken._fixedStep = 0.0f;
    SW_EXPECT_FALSE( _world.initialize( broken, &_mechs, &_weapons, &_projectiles, &_moves ) );
    SW_EXPECT_EQUAL( 2, _world.getPilotCount() );
}

/**
 * @brief [MechArenaRulesTest] 스냅샷은 판 단계 · 게이지 · 조종사 · 탄을 그대로 옮긴다
 */
SW_TEST_CASE_F( MechArenaRulesTest, SnapshotMirrorsTheWorld )
{
    SW_REQUIRE_TRUE( openDuel( "gunner", "gunner" ) );
    MechInput input;
    input._bSpecial = SW_TRUE;
    input._bFire    = SW_TRUE;
    _world.setInput( 0, input );
    step( 1 );

    MechArenaSnapshot snapshot;
    _world.makeSnapshot( snapshot );
    SW_EXPECT_EQUAL( _world.getTick(), snapshot._tick );
    SW_EXPECT_TRUE( snapshot._phase == MatchPhase::InProgress );
    SW_REQUIRE_EQUAL( size_t( 2 ), snapshot._listTeamGauge.size() );
    SW_EXPECT_EQUAL( 600, snapshot._listTeamGauge[0] );
    SW_REQUIRE_EQUAL( size_t( 2 ), snapshot._listPilot.size() );
    SW_EXPECT_NEAR( 90.0f, snapshot._listPilot[1]._health, kMechTolerance );
    SW_EXPECT_EQUAL( 2, snapshot._listPilot[0]._magazineAmmo );
    SW_EXPECT_EQUAL( 1, snapshot._listPilot[0]._lockTarget );
    SW_REQUIRE_EQUAL( size_t( 1 ), snapshot._listProjectile.size() );
    SW_EXPECT_EQUAL( 0, snapshot._listProjectile[0]._owner );
}

/**
 * @brief [MechArenaRulesTest] 상태 왕복은 탄 · 근접 · 쿨다운까지 이어져 같은 입력이면 같은 바이트가 나온다. 판이 끝난 상태도 읽힌다. 결함: 판이 끝나면 근접 슬롯이 남았다
 */
SW_TEST_CASE_F( MechArenaRulesTest, StateRoundTripContinuesIdentically )
{
    _settings._gaugeScale = 1.0f;
    SW_REQUIRE_TRUE( openDuel( "gunner", "gunner", 3.0f ) );
    MechInput input;
    input._bSpecial = SW_TRUE;
    _world.setInput( 0, input );
    step( 1 );
    input._bSpecial = SW_FALSE;
    input._bMelee   = SW_TRUE;
    _world.setInput( 0, input );
    _world.setInput( 1, input );
    step( 1 );
    SW_REQUIRE_TRUE( pilot( 0 )._comboStage == 0 );

    const vector<uint8> listSaved = writeBytes( _world );
    MechArenaWorld      restored;
    SW_REQUIRE_TRUE( restored.initialize( _settings, &_mechs, &_weapons, &_projectiles, &_moves ) );
    Archive reading( listSaved.data(), listSaved.size() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( writeBytes( restored ) == listSaved );

    for ( int32 frame = 0; frame < 30; ++frame )
    {
        restored.update( kMechStep );
    }
    step( 30 );
    SW_EXPECT_TRUE( writeBytes( restored ) == writeBytes( _world ) );

    // 근접 중에 판이 끝난다.
    _world.setInput( 0, MechInput{} );
    step( 1 );
    _world.setInput( 0, input );
    step( 1 );
    SW_REQUIRE_TRUE( pilot( 0 )._comboStage >= 0 );
    kill( 0, 1 );
    step( 1 );
    SW_REQUIRE_TRUE( _world.isEnded() );
    SW_EXPECT_TRUE( readInFreshWorld( writeBytes( _world ) ) );
}

/**
 * @brief [MechArenaRulesTest] 카탈로그가 없거나, 잘렸거나, 머리가 범위 밖인 바이트는 거절하고 상태를 바꾸지 않는다
 */
SW_TEST_CASE_F( MechArenaRulesTest, CraftedHeadIsRefused )
{
    SW_REQUIRE_TRUE( openDuel( "gunner", "gunner" ) );
    const size_t        head     = computePilotHead();
    const vector<uint8> listByte = writeBytes( _world );
    SW_REQUIRE_TRUE( readInFreshWorld( listByte ) );

    MechArenaWorld bare; // initialize 전
    Archive        bareReading( listByte.data(), listByte.size() );
    SW_EXPECT_FALSE( bare.readState( bareReading ) );

    Archive cut( listByte.data(), listByte.size() / 2 );
    SW_EXPECT_FALSE( _world.readState( cut ) );
    SW_EXPECT_EQUAL( 2, _world.getPilotCount() );

    SW_EXPECT_FALSE( readPatched<int32>( head + kMechFieldTeam, 1 ) ); // 참가자의 팀과 다르다
    SW_EXPECT_FALSE( readPatched<int32>( head + kMechFieldTeam, 5 ) );
    SW_EXPECT_FALSE( readPatched<int32>( head + kMechFieldDeck, 1 ) ); // 덱이 하나다
    SW_EXPECT_FALSE( readPatched<int32>( head + kMechFieldDeck, -1 ) );
    SW_EXPECT_FALSE( readPatched<int32>( head + kMechFieldNextDeck, 1 ) );
    SW_EXPECT_FALSE( readPatched<int32>( head + kMechFieldMode, 1 ) ); // 형태가 하나다
    SW_EXPECT_FALSE( readPatched<int32>( head + kMechFieldKills, -1 ) );
    SW_EXPECT_FALSE( readPatched<int32>( head + kMechFieldDeaths, -1 ) );
    SW_EXPECT_FALSE( readPatched<uint8>( head + kMechFieldState, 5 ) );
    SW_EXPECT_FALSE( readPatched<uint8>( head + kMechFieldState, static_cast<uint8>( MechPilotState::Destroyed ) ) ); // 살아 있는데 격추됐다
    SW_EXPECT_FALSE( readPatched<uint8>( head + kMechFieldGrounded, 2 ) );
}

/**
 * @brief [MechArenaRulesTest] 근접 슬롯 · 단 · 타임라인이 어긋난 바이트는 거절한다. 결함: 콤보 중인데 슬롯이 −1 이거나 지금 형태 밖이면 `stepMelee` 가 무기 목록 밖을 읽었다
 */
SW_TEST_CASE_F( MechArenaRulesTest, CraftedMeleeStateIsRefused )
{
    SW_REQUIRE_TRUE( openDuel( "gunner", "gunner" ) );
    const size_t head = computePilotHead();
    SW_EXPECT_FALSE( readPatched<int32>( head + kMechFieldCombo, 0 ) ); // 콤보인데 슬롯이 없다
    SW_EXPECT_FALSE( readPatched<int32>( head + kMechFieldCombo, -2 ) );
    SW_EXPECT_FALSE( readPatched<int32>( head + kMechFieldMeleeSlot, 2 ) ); // 콤보가 아닌데 슬롯이 있다

    MechInput melee;
    melee._bMelee = SW_TRUE;
    _world.setInput( 0, melee );
    step( 1 );
    SW_REQUIRE_EQUAL( 2, pilot( 0 )._meleeSlot );
    SW_EXPECT_TRUE( readInFreshWorld( writeBytes( _world ) ) );
    const size_t headMelee = computePilotHead();
    SW_EXPECT_FALSE( readPatched<int32>( headMelee + kMechFieldMeleeSlot, 99 ) ); // 무기 밖
    SW_EXPECT_FALSE( readPatched<int32>( headMelee + kMechFieldMeleeSlot, 0 ) );  // 주무기 슬롯
    SW_EXPECT_FALSE( readPatched<int32>( headMelee + kMechFieldCombo, 2 ) );      // 기술이 둘이다
    SW_EXPECT_FALSE( readPatched<int32>( headMelee + kMechFieldCombo, -1 ) );     // 타임라인이 도는데 콤보가 아니다
}

/**
 * @brief [MechArenaRulesTest] 참가자 번호와 록온 대상이 판 밖이면 거절한다. 결함: 받아서 격추 보고가 없는 참가자로 갔다
 */
SW_TEST_CASE_F( MechArenaRulesTest, CraftedReferencesAreRefused )
{
    SW_REQUIRE_TRUE( openDuel( "gunner", "gunner" ) );
    const size_t head        = computePilotHead();
    const size_t participant = head + kMechFieldParticipant;
    SW_EXPECT_FALSE( readPatched<uint32>( participant, 0 ) ); // 시작한 판인데 참가자가 없다
    SW_EXPECT_FALSE( readPatched<uint32>( participant, 2 ) ); // 덱은 하나다
    SW_EXPECT_FALSE( readPatched<int32>( participant + 4, 99 ) );
    SW_EXPECT_FALSE( readPatched<int32>( participant + 4, -1 ) );
    SW_EXPECT_FALSE( readPatched<int32>( participant + 4, 1 ) ); // 다른 팀의 참가자

    const size_t lockOn = computeLockOnOffset();
    SW_EXPECT_TRUE( readPatched<uint64>( lockOn, 2 ) );
    SW_EXPECT_FALSE( readPatched<uint64>( lockOn, 3 ) ); // 조종사는 둘이다
}

/**
 * @brief [MechArenaRulesTest] 탄의 번호 · 수치가 범위 밖이면 거절한다. 결함: 탄속 0 이나 음수 수치를 받았다(날지 않는 탄이 영원히 남았다). 반응은 슬롯을 가리키므로 없는 슬롯 번호도 거절한다
 */
SW_TEST_CASE_F( MechArenaRulesTest, CraftedProjectileIsRefused )
{
    SW_REQUIRE_TRUE( openDuel( "gunner", "gunner" ) );
    MechInput special;
    special._bSpecial = SW_TRUE;
    _world.setInput( 0, special );
    step( 1 );
    SW_REQUIRE_EQUAL( size_t( 1 ), _world.getProjectiles().size() );
    const size_t projectile = writeBytes( _world ).size() - kMechProjectileSize;
    SW_REQUIRE_TRUE( readInFreshWorld( writeBytes( _world ) ) );
    SW_EXPECT_FALSE( readPatched<float3>( projectile + kMechProjectileVelocity, float3{} ) ); // 날지 않는 탄
    SW_EXPECT_FALSE( readPatched<float32>( projectile + kMechProjectileRange, 0.0f ) );
    SW_EXPECT_FALSE( readPatched<float32>( projectile + kMechProjectileRange, -1.0f ) );
    SW_EXPECT_FALSE( readPatched<float32>( projectile + kMechProjectileRestitution, 2.0f ) );
    SW_EXPECT_FALSE( readPatched<uint32>( projectile + kMechProjectilePayload, 99999u ) ); // 없는 기체 · 슬롯
    SW_EXPECT_FALSE( readPatched<uint32>( projectile + kMechProjectilePayload, 2u ) );     // 근접 슬롯은 탄을 쏘지 않는다
    SW_EXPECT_FALSE( readPatched<int32>( projectile + kMechProjectileOwner, 2 ) );
    SW_EXPECT_FALSE( readPatched<uint8>( projectile + kMechProjectileSize - 1, 9 ) ); // 깃발
}

/**
 * @brief [MechArenaTest] 카탈로그는 분류 보정 · 코스트 상한 · 형태 · 무기 슬롯 · 스킬 · 기본 형태를 읽는다
 */
SW_TEST_CASE( MechArenaTest, CatalogReadsMechsAndSkills )
{
    MechCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kMechCatalogXML, "MechArenaTest" ) );
    SW_EXPECT_EQUAL( 2000, catalog.getDeckCostLimit() );
    SW_EXPECT_NEAR( 2.0f, catalog.getClassModifier( MechRangeClass::Near ).getScale( hashed_string( "melee" ) ), kMechTolerance );
    SW_EXPECT_NEAR( 1.0f, catalog.getClassModifier( MechRangeClass::Mid ).getScale( hashed_string( "melee" ) ), kMechTolerance ); // 없는 보정은 1

    const MechDef* pGunner = catalog.findMech( hashed_string( "gunner" ) );
    SW_REQUIRE_NOT_NULL( pGunner );
    SW_EXPECT_EQUAL( 300, pGunner->_cost );
    SW_EXPECT_TRUE( pGunner->_rangeClass == MechRangeClass::Mid );
    SW_REQUIRE_EQUAL( size_t( 1 ), pGunner->_listMode.size() ); // <Mode> 없이 무기를 달면 형태 하나다
    SW_EXPECT_TRUE( pGunner->_listMode[0]._id == hashed_string( "default" ) );
    const vector<MechWeaponSlotDef>& listWeapon = pGunner->_listMode[0]._listWeapon;
    SW_REQUIRE_EQUAL( size_t( 3 ), listWeapon.size() );
    SW_EXPECT_TRUE( listWeapon[1]._kind == MechWeaponKind::Special );
    SW_EXPECT_TRUE( listWeapon[1]._projectileID == hashed_string( "missile" ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), listWeapon[2]._listMoveID.size() );
    SW_EXPECT_TRUE( listWeapon[2]._listMoveID[1] == hashed_string( "slash2" ) );

    const MechDef* pFlyer = catalog.findMech( hashed_string( "flyer" ) );
    SW_REQUIRE_NOT_NULL( pFlyer );
    SW_REQUIRE_EQUAL( size_t( 2 ), pFlyer->_listMode.size() );
    SW_EXPECT_NEAR( 20.0f, pFlyer->_listMode[1]._speed, kMechTolerance );

    const MechSkillDef* pSkill = catalog.findSkill( hashed_string( "rage" ) );
    SW_REQUIRE_NOT_NULL( pSkill );
    SW_EXPECT_TRUE( pSkill->_trigger == MechSkillTrigger::HealthBelow );
    SW_EXPECT_NEAR( 2.0f, pSkill->_modifier.getScale( hashed_string( "attack" ) ), kMechTolerance );

    SW_EXPECT_NULL( catalog.findMech( hashed_string( "none" ) ) );
    SW_EXPECT_FALSE( catalog.findMechIndex( hashed_string( "none" ) ).has_value() );

    MechRangeClass rangeClass = MechRangeClass::Mid;
    SW_EXPECT_TRUE( MechCatalog::parseRangeClass( "Far", rangeClass ) );
    SW_EXPECT_TRUE( rangeClass == MechRangeClass::Far );
    SW_EXPECT_FALSE( MechCatalog::parseRangeClass( "Space", rangeClass ) );
    SW_EXPECT_TRUE( rangeClass == MechRangeClass::Far );
}

/**
 * @brief [MechArenaTest] 범위 없는 근접 · 탄 없는 특수기, 형태와 섞인 속도, 형태 밖 무기, 모르는 원소 · 분류는 오류로 그 원소를 버린다. 결함: 형태 밖 무기를 말없이 버렸다
 */
SW_TEST_CASE( MechArenaTest, CatalogRefusesBrokenElements )
{
    SW_EXPECT_LOG( "MechCatalog.WeaponIdKindMelee" );
    SW_EXPECT_LOG( "MechCatalog.SpecialNeedsProjectile" );
    SW_EXPECT_LOG( "MechCatalog.MechIdErrorsNot" );
    SW_EXPECT_LOG( "MechCatalog.MechIdSpeedBelongs" );
    SW_EXPECT_LOG( "MechCatalog.MechWeaponOutsideMode" );
    SW_EXPECT_LOG( "MechCatalog.UnknownMechClassNear" );
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    MechCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<MechCatalog>
  <Mech id="good" name="Good"/>
  <Mech id="norange" name="A"><Weapon id="x" kind="Melee" moves="m"/></Mech>
  <Mech id="noprojectile" name="D"><Weapon id="x" kind="Special" cooldown="1"/></Mech>
  <Mech id="speedy" name="B" speed="3"><Mode id="ms"/></Mech>
  <Mech id="mixed" name="C"><Mode id="ms"/><Weapon id="x" kind="Shot"/></Mech>
  <Class id="Space" melee="2"/>
  <Pilot id="who"/>
</MechCatalog>)",
                                              "MechArenaTest" ) );
    SW_EXPECT_EQUAL( size_t( 1 ), catalog.getMechs().size() );
    SW_EXPECT_NOT_NULL( catalog.findMech( hashed_string( "good" ) ) );
}

/**
 * @brief [MechArenaTest] 겹친 기체 · 스킬 id 는 오류로 버리고 앞 정의를 지킨다. 결함: 말없이 앞 정의를 덮어썼다(주석은 "뒤의 것을 버린다")
 */
SW_TEST_CASE( MechArenaTest, CatalogRefusesDuplicateIDs )
{
    SW_EXPECT_LOG( "GameDataXML.DuplicateID" );
    MechCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<MechCatalog>
  <Mech id="a" name="First" cost="100"/>
  <Mech id="a" name="Second" cost="200"/>
  <Skill id="s" duration="1"/>
  <Skill id="s" duration="2"/>
</MechCatalog>)",
                                              "MechArenaTest" ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), catalog.getMechs().size() );
    SW_EXPECT_EQUAL( 100, catalog.findMech( hashed_string( "a" ) )->_cost );
    SW_REQUIRE_EQUAL( size_t( 1 ), catalog.getSkills().size() );
    SW_EXPECT_NEAR( 1.0f, catalog.findSkill( hashed_string( "s" ) )->_duration, kMechTolerance );
}

/**
 * @brief [MechArenaTest] 루트 속성이 틀리면 파일을 읽지 않는다. 결함: 코스트 상한이 제한 없음으로 남은 채 기체를 읽었다
 */
SW_TEST_CASE( MechArenaTest, CatalogRefusesBrokenRoot )
{
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    MechCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<MechCatalog deckCostLimit="-1"><Mech id="a" name="A"/></MechCatalog>)", "MechArenaTest" ) );
    SW_EXPECT_TRUE( catalog.getMechs().empty() );
}
