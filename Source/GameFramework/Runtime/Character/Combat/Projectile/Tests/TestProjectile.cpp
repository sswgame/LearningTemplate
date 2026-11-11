#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileSimulation.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ResolvedShot.h"
#include "GameFramework/Runtime/Character/Combat/Weapon/Weapon.h"
#include "GameFramework/Runtime/Foundation/Condition/GameConditions.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"

// ProjectileTest — 무기 · 탄 · 수정자 조합이 결과를 바꾸는지(받음 조건 · 합치는 순서 · 맞음 표 · 맞음 동작)와 단일 발사체 시뮬레이션의 규칙(관통 · 튕김 · 유도 · 사거리 · 수명 · 평면 · 상태 왕복). nogpu.

using namespace sw;

namespace
{
    constexpr float32 kProjectileTolerance = 1.0e-3f;

    // 동작 글(`flag.add(x, 1)`)에 `)"` 가 들어 있어 원시 문자열의 구분자를 둔다.
    constexpr const utf8* kProjectileXML = R"xml(<ProjectileCatalog>
  <Projectile id="rifle_round" tags="Ammo.Rifle" damage="10" speed="0" range="90"/>
  <Projectile id="incendiary" tags="Ammo.Rifle,Ammo.Incendiary" damage="8" speed="0" range="90">
    <OnHit when="tag(Enemy.Armored)" call="flag.add(armorBurns, 1)"/>
    <OnHit call="flag.add(burns, 1)"/>
  </Projectile>
  <Projectile id="shell" tags="Ammo.Shell" damage="60" speed="40" range="200"/>
  <Impact id="body_fire" surface="Body" when="Ammo.Incendiary" size="0.3"/>
  <Impact id="body" surface="Body" size="0.18"/>
  <Impact id="cover" surface="Cover" size="0.1"/>
</ProjectileCatalog>)xml";

    /** @brief x 축 위의 점 대상 몇 개와 x = `_wallX` 의 벽입니다. 대상 번호는 목록 인덱스입니다. */
    class LineWorld : public IProjectileWorld
    {
    public:
        vector<float3> _listTarget{};
        float32        _wallX{ 1000.0f };
        float32        _hitRadius{ 0.5f };

        bool findTargetPoint( const ProjectileState& projectile, float3& outPoint ) const override
        {
            if ( projectile._target < 0 || projectile._target >= static_cast<int32>( _listTarget.size() ) )
                return false;
            outPoint = _listTarget[static_cast<size_t>( projectile._target )];
            return true;
        }

        bool sweep( const ProjectileState& projectile, const float3& from, const float3& to, ProjectileHit& outHit ) const override
        {
            const float3  segment = to - from;
            const float32 length  = segment.getLength();
            float32       best    = length;
            bool          bHit    = false;
            for ( size_t index = 0; index < _listTarget.size(); ++index )
            {
                if ( static_cast<int32>( index ) == projectile._lastHit || length <= 0.0f )
                    continue;
                // 선분에서 대상에 가장 가까운 점까지의 거리로 본다(시험용 단순 판정).
                const float3  direction = segment / length;
                const float32 along     = MathUtil::clamp( ( _listTarget[index] - from ).dot( direction ), 0.0f, length );
                const float3  closest   = from + direction * along;
                if ( float3::getDistance( closest, _listTarget[index] ) <= _hitRadius && along <= best )
                {
                    best             = along;
                    outHit._position = closest;
                    outHit._other    = static_cast<int32>( index );
                    outHit._kind     = ProjectileHitKind::Target;
                    bHit             = true;
                }
            }
            if ( bHit == false && to._x >= _wallX && from._x < _wallX )
            {
                const float32 ratio = ( _wallX - from._x ) / ( to._x - from._x );
                outHit._position    = from + segment * ratio;
                outHit._normal      = float3{ -1.0f, 0.0f, 0.0f };
                outHit._surface     = hashed_string( "Wall" );
                outHit._kind        = ProjectileHitKind::Surface;
                bHit                = true;
            }
            return bHit;
        }
    };

    struct ProjectileTestUtil
    {
        static WeaponDef makeRifle( const utf8* pAccepts )
        {
            WeaponDef rifle;
            rifle._id           = hashed_string( "rifle" );
            rifle._projectileID = hashed_string( "rifle_round" );
            rifle._accepts      = pAccepts;
            return rifle;
        }

        static ProjectileState makeBullet( float32 speed )
        {
            ProjectileState bullet;
            bullet._velocity = float3{ speed, 0.0f, 0.0f };
            bullet._damage   = 1.0f;
            return bullet;
        }

        /** @brief 한 걸음의 사건을 @p inoutListEvent 뒤에 모읍니다. 여러 걸음을 한 번에 세는 테스트가 씁니다. */
        static void stepCollect( ProjectileSimulation& simulation, float32 deltaTime, const IProjectileWorld& world, vector<ProjectileEvent>& inoutListEvent )
        {
            const vector<ProjectileEvent>& listStep = simulation.step( deltaTime, world );
            inoutListEvent.insert( inoutListEvent.end(), listStep.begin(), listStep.end() );
        }

        static int32 countKind( const vector<ProjectileEvent>& listEvent, ProjectileEvent::Kind kind )
        {
            int32 count = 0;
            for ( const ProjectileEvent& event : listEvent )
            {
                count += event._kind == kind ? 1 : 0;
            }
            return count;
        }
    };
    using Util = ProjectileTestUtil;
} // namespace

/** @brief [ProjectileTest] 같은 무기에 다른 탄을 넣으면 피해 · 탄속 · 사거리가 탄을 따른다(조합이 결과를 바꾼다) */
SW_TEST_CASE( ProjectileTest, ProjectileDecidesTheShot )
{
    ProjectileCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kProjectileXML, "test" ) );
    WeaponDef rifle = Util::makeRifle( "" );
    SW_REQUIRE_TRUE( rifle.compileAccepts( "test" ) );

    WeaponState state;
    SW_REQUIRE_TRUE( state.equip( rifle, *catalog.find( hashed_string( "rifle_round" ) ), 0 ) );
    SW_EXPECT_NEAR( 10.0f, state.getShot()._damage, kProjectileTolerance );
    SW_EXPECT_TRUE( state.getShot().isHitscan() );

    SW_REQUIRE_TRUE( state.loadProjectile( *catalog.find( hashed_string( "shell" ) ), {} ) );
    SW_EXPECT_NEAR( 60.0f, state.getShot()._damage, kProjectileTolerance );
    SW_EXPECT_NEAR( 40.0f, state.getShot()._speed, kProjectileTolerance );
    SW_EXPECT_NEAR( 200.0f, state.getShot()._range, kProjectileTolerance );
    SW_EXPECT_FALSE( state.getShot().isHitscan() );
}

/** @brief [ProjectileTest] 받음 조건은 탄 태그에 대한 식이다 — 소총탄 · 소이탄은 받고 포탄은 거절한다(거절하면 조합이 그대로다) */
SW_TEST_CASE( ProjectileTest, AcceptsIsATagCondition )
{
    ProjectileCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kProjectileXML, "test" ) );
    WeaponDef rifle = Util::makeRifle( "Ammo.Rifle" );
    SW_REQUIRE_TRUE( rifle.compileAccepts( "test" ) );
    SW_EXPECT_TRUE( rifle.canAccept( *catalog.find( hashed_string( "rifle_round" ) ) ) );
    SW_EXPECT_TRUE( rifle.canAccept( *catalog.find( hashed_string( "incendiary" ) ) ) );
    SW_EXPECT_FALSE( rifle.canAccept( *catalog.find( hashed_string( "shell" ) ) ) );

    WeaponState state;
    SW_REQUIRE_TRUE( state.equip( rifle, *catalog.find( hashed_string( "rifle_round" ) ), 0 ) );
    SW_EXPECT_LOG( "Weapon.LoadRefusedProjectile" );
    SW_EXPECT_FALSE( state.loadProjectile( *catalog.find( hashed_string( "shell" ) ), {} ) );
    SW_EXPECT_TRUE( state.getShot()._projectileID == hashed_string( "rifle_round" ) );

    WeaponDef broken = Util::makeRifle( "Ammo.Rifle &&" );
    SW_EXPECT_LOG( "ConditionExpr.Invalid" );
    SW_EXPECT_FALSE( broken.compileAccepts( "test" ) );
}

/** @brief [ProjectileTest] 합치는 순서는 탄 → 무기 → 부착물 한 벌이다 — 곱과 합의 순서가 바뀌면 값이 다르다. 음수로 내려간 값은 0 에서 멈춘다 */
SW_TEST_CASE( ProjectileTest, ModifiersApplyProjectileThenWeaponThenAttachment )
{
    ProjectileDef round;
    round._id     = hashed_string( "round" );
    round._damage = 10.0f;
    ShotModifierDef weaponScale;
    weaponScale._field = ShotField::Damage;
    weaponScale._op    = ShotModifierOp::Multiply;
    weaponScale._value = 1.5f;
    ShotModifierDef attachmentAdd;
    attachmentAdd._field = ShotField::Damage;
    attachmentAdd._op    = ShotModifierOp::Add;
    attachmentAdd._value = 5.0f;
    SW_EXPECT_NEAR( 20.0f, ResolvedShot::resolve( round, { weaponScale }, { attachmentAdd } )._damage, kProjectileTolerance ); // (10 × 1.5) + 5
    SW_EXPECT_NEAR( 22.5f, ResolvedShot::resolve( round, { attachmentAdd }, { weaponScale } )._damage, kProjectileTolerance ); // (10 + 5) × 1.5

    ShotModifierDef pierce;
    pierce._field = ShotField::Pierce;
    pierce._op    = ShotModifierOp::Add;
    pierce._value = 2.0f;
    ShotModifierDef negative;
    negative._field         = ShotField::Damage;
    negative._op            = ShotModifierOp::Add;
    negative._value         = -100.0f;
    const ResolvedShot shot = ResolvedShot::resolve( round, {}, { pierce, negative } );
    SW_EXPECT_EQUAL( 2, shot._pierceCount );
    SW_EXPECT_NEAR( 0.0f, shot._damage, kProjectileTolerance );
}

/** @brief [ProjectileTest] 맞음 효과는 탄과 표면의 조합이다 — 같은 표면도 소이탄이면 위의 특정한 줄이, 아니면 일반 줄이 이긴다. 없는 표면은 없음이다 */
SW_TEST_CASE( ProjectileTest, ImpactDependsOnSurfaceAndProjectile )
{
    ProjectileCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kProjectileXML, "test" ) );
    const hashed_string        body( "Body" );
    const ProjectileImpactDef* pFire  = catalog.findImpact( body, catalog.find( hashed_string( "incendiary" ) )->_tags );
    const ProjectileImpactDef* pPlain = catalog.findImpact( body, catalog.find( hashed_string( "rifle_round" ) )->_tags );
    SW_REQUIRE_TRUE( pFire != nullptr && pPlain != nullptr );
    SW_EXPECT_TRUE( pFire->_id == hashed_string( "body_fire" ) );
    SW_EXPECT_TRUE( pPlain->_id == hashed_string( "body" ) );
    SW_EXPECT_TRUE( catalog.findImpact( hashed_string( "Water" ), TagContainer{} ) == nullptr );
}

/** @brief [ProjectileTest] 맞음 동작은 맞은 쪽 문맥으로 부른다 — 조건이 맞는 줄만 동작한다 */
SW_TEST_CASE( ProjectileTest, HitActionsUseTheTargetContext )
{
    ProjectileCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kProjectileXML, "test" ) );
    const ResolvedShot shot = ResolvedShot::resolve( *catalog.find( hashed_string( "incendiary" ) ), {}, {} );
    SW_REQUIRE_EQUAL( size_t( 2 ), shot._listHitAction.size() );

    GameFlags     flags;
    TagContainer  plainTarget;
    TagContainer  armoredTarget;
    ActionContext context;
    armoredTarget.addTag( TagID::request( "Enemy.Armored" ) );
    context._world.set<GameFlagsWriteSlot>( &flags );
    context._world.set<SubjectTagsSlot>( &plainTarget );
    ProjectileDef::runHitActions( shot._listHitAction, context );
    context._world.set<SubjectTagsSlot>( &armoredTarget );
    ProjectileDef::runHitActions( shot._listHitAction, context );
    SW_EXPECT_EQUAL( 2, flags.getFlag( hashed_string( "burns" ) ) );
    SW_EXPECT_EQUAL( 1, flags.getFlag( hashed_string( "armorBurns" ) ) );
}

/** @brief [ProjectileTest] 관통 수가 남으면 첫 대상을 지나 다음 대상에서 끝난다. 같은 대상은 다시 맞지 않는다 */
SW_TEST_CASE( ProjectileTest, PierceHitsTheNextTarget )
{
    LineWorld world;
    world._listTarget = {
        float3{2.0f, 0.0f, 0.0f},
        float3{6.0f, 0.0f, 0.0f}
    };
    ProjectileSimulation simulation;
    ProjectileState      bullet = Util::makeBullet( 60.0f );
    bullet._pierceLeft          = 1;
    (void)simulation.spawn( bullet ); // id 는 쓰지 않는다

    vector<ProjectileEvent> listEvent;
    for ( int32 step = 0; step < 10 && simulation.getProjectiles().empty() == false; ++step )
    {
        Util::stepCollect( simulation, 1.0f / 60.0f, world, listEvent );
    }
    SW_REQUIRE_EQUAL( 2, Util::countKind( listEvent, ProjectileEvent::Kind::Hit ) );
    SW_EXPECT_EQUAL( 0, listEvent[0]._hit._other );
    SW_EXPECT_TRUE( listEvent[0]._projectile._bAlive == SW_TRUE );
    SW_EXPECT_EQUAL( 1, listEvent[1]._hit._other );
    SW_EXPECT_TRUE( listEvent[1]._projectile._bAlive == SW_FALSE );
    SW_EXPECT_TRUE( simulation.getProjectiles().empty() );
}

/** @brief [ProjectileTest] 한 걸음에 여럿을 지나는 빠른 관통탄은 그 걸음에서 모두를 가까운 순서로 맞힌다(남은 선분을 다시 묻는다). 관통 수를 다 쓰면 그 대상에서 끝난다 */
SW_TEST_CASE( ProjectileTest, FastPierceHitsEveryTargetInOneStep )
{
    LineWorld world;
    world._listTarget = {
        float3{4.0f, 0.0f, 0.0f},
        float3{2.0f, 0.0f, 0.0f},
        float3{6.0f, 0.0f, 0.0f},
        float3{8.0f, 0.0f, 0.0f}
    };
    ProjectileSimulation simulation;
    ProjectileState      bullet = Util::makeBullet( 600.0f ); // 한 걸음(1/60 초)에 10 m — 네 대상을 모두 지난다
    bullet._pierceLeft          = 2;
    (void)simulation.spawn( bullet );

    const vector<ProjectileEvent>& listEvent = simulation.step( 1.0f / 60.0f, world );
    SW_REQUIRE_EQUAL( size_t( 3 ), listEvent.size() );
    SW_EXPECT_EQUAL( 1, listEvent[0]._hit._other ); // x = 2
    SW_EXPECT_EQUAL( 0, listEvent[1]._hit._other ); // x = 4
    SW_EXPECT_EQUAL( 2, listEvent[2]._hit._other ); // x = 6 — 관통 둘을 다 써 여기서 끝난다
    SW_EXPECT_TRUE( listEvent[0]._projectile._bAlive == SW_TRUE );
    SW_EXPECT_TRUE( listEvent[1]._projectile._bAlive == SW_TRUE );
    SW_EXPECT_TRUE( listEvent[2]._projectile._bAlive == SW_FALSE );
    SW_EXPECT_EQUAL( 0, listEvent[2]._projectile._pierceLeft );
    SW_EXPECT_NEAR( 6.0f, listEvent[2]._projectile._travelled, kProjectileTolerance );
    SW_EXPECT_TRUE( simulation.getProjectiles().empty() );
}

/** @brief [ProjectileTest] 튕김 수가 남으면 표면에서 반사되어 돌아오고, 다 쓰면 다음 표면에서 끝난다. 튕기지 않는 탄은 첫 표면에서 끝난다 */
SW_TEST_CASE( ProjectileTest, BounceReflectsOffSurfaces )
{
    LineWorld world;
    world._wallX = 1.0f;
    ProjectileSimulation bouncy;
    ProjectileState      ball = Util::makeBullet( 30.0f );
    ball._bounceLeft          = 1;
    ball._restitution         = 0.5f;
    (void)bouncy.spawn( ball );
    vector<ProjectileEvent> listEvent;
    for ( int32 step = 0; step < 5; ++step )
    {
        Util::stepCollect( bouncy, 1.0f / 60.0f, world, listEvent );
    }
    SW_REQUIRE_EQUAL( size_t( 1 ), bouncy.getProjectiles().size() );
    SW_EXPECT_EQUAL( 1, Util::countKind( listEvent, ProjectileEvent::Kind::Bounced ) );
    SW_EXPECT_NEAR( -15.0f, bouncy.getProjectiles()[0]._velocity._x, kProjectileTolerance );
    SW_EXPECT_TRUE( bouncy.getProjectiles()[0]._position._x < 1.0f );

    ProjectileSimulation plain;
    (void)plain.spawn( Util::makeBullet( 30.0f ) );
    listEvent.clear();
    for ( int32 step = 0; step < 5; ++step )
    {
        Util::stepCollect( plain, 1.0f / 60.0f, world, listEvent );
    }
    SW_EXPECT_TRUE( plain.getProjectiles().empty() );
    SW_REQUIRE_EQUAL( 1, Util::countKind( listEvent, ProjectileEvent::Kind::Hit ) );
    SW_EXPECT_TRUE( listEvent[0]._hit._kind == ProjectileHitKind::Surface );
}

/** @brief [ProjectileTest] 유도탄은 대상 쪽으로 꺾이고 속력은 그대로다. 평면 제약이 있으면 평면 밖으로 나가지 않는다 */
SW_TEST_CASE( ProjectileTest, HomingTurnsTowardTheTargetInThePlane )
{
    LineWorld world;
    world._listTarget = {
        float3{ 0.0f, 10.0f, 10.0f }
    };
    world._hitRadius = 0.0f;
    ProjectileSimulation simulation;
    simulation.setPlaneNormal( float3{ 0.0f, 0.0f, 1.0f } ); // 2D(XY) 키트와 같은 평면
    ProjectileState missile = Util::makeBullet( 10.0f );
    missile._target         = 0;
    missile._turnRate       = MathUtil::toRadian( 90.0f );
    (void)simulation.spawn( missile );
    vector<ProjectileEvent> listEvent;
    for ( int32 step = 0; step < 30; ++step )
    {
        Util::stepCollect( simulation, 1.0f / 60.0f, world, listEvent );
    }
    SW_REQUIRE_EQUAL( size_t( 1 ), simulation.getProjectiles().size() );
    const ProjectileState& flying = simulation.getProjectiles()[0];
    SW_EXPECT_TRUE( flying._velocity._y > 0.0f );
    SW_EXPECT_NEAR( 0.0f, flying._velocity._z, kProjectileTolerance );
    SW_EXPECT_NEAR( 0.0f, flying._position._z, kProjectileTolerance );
    SW_EXPECT_NEAR( 10.0f, flying.getSpeed(), kProjectileTolerance );
}

/** @brief [ProjectileTest] 사거리 끝에서 정확히 끝나고(실수 합이 모자라 남지 않는다), 수명이 다하면 움직이지 않은 채 끝난다 */
SW_TEST_CASE( ProjectileTest, RangeAndLifetimeEndTheProjectile )
{
    LineWorld            world;
    ProjectileSimulation simulation;
    ProjectileState      ranged = Util::makeBullet( 7.0f );
    ranged._range               = 1.0f;
    (void)simulation.spawn( ranged );
    ProjectileState timed = Util::makeBullet( 0.0f );
    timed._lifetime       = 0.05f;
    (void)simulation.spawn( timed );

    vector<ProjectileEvent> listEvent;
    for ( int32 step = 0; step < 20; ++step )
    {
        Util::stepCollect( simulation, 1.0f / 60.0f, world, listEvent );
    }
    SW_EXPECT_TRUE( simulation.getProjectiles().empty() );
    SW_REQUIRE_EQUAL( 2, Util::countKind( listEvent, ProjectileEvent::Kind::Expired ) );
    for ( const ProjectileEvent& event : listEvent )
    {
        if ( event._projectile._range > 0.0f )
            SW_EXPECT_NEAR( 1.0f, event._projectile._position._x, kProjectileTolerance );
    }
}

/** @brief [ProjectileTest] 상태를 쓰고 읽은 시뮬레이션은 같은 걸음을 같은 결과로 돈다. 깨진 값(다음 id 이상의 id)은 받지 않는다 */
SW_TEST_CASE( ProjectileTest, StateRoundTripIsDeterministic )
{
    LineWorld world;
    world._listTarget = {
        float3{ 3.0f, 1.0f, 0.0f }
    };
    ProjectileSimulation original;
    ProjectileState      missile = Util::makeBullet( 5.0f );
    missile._target              = 0;
    missile._turnRate            = 2.0f;
    missile._gravity             = 1.0f;
    missile._drag                = 0.1f;
    (void)original.spawn( missile );
    vector<ProjectileEvent> listEvent;
    Util::stepCollect( original, 1.0f / 60.0f, world, listEvent );

    Archive written;
    original.writeState( written );
    ProjectileSimulation restored;
    Archive              reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    for ( int32 step = 0; step < 10; ++step )
    {
        Util::stepCollect( original, 1.0f / 60.0f, world, listEvent );
        Util::stepCollect( restored, 1.0f / 60.0f, world, listEvent );
    }
    SW_REQUIRE_EQUAL( original.getProjectiles().size(), restored.getProjectiles().size() );
    SW_EXPECT_TRUE( original.getProjectiles()[0]._position == restored.getProjectiles()[0]._position );
    SW_EXPECT_EQUAL( original.getNextID(), restored.getNextID() );

    Archive again;
    original.writeState( again );
    vector<uint8> listByte;
    again.writeData( listByte );
    const uint32 forgedNextID = 1; // 다음 id 가 1 이면 id 1 인 탄은 있을 수 없다
    std::memcpy( listByte.data(), &forgedNextID, sizeof( forgedNextID ) );
    ProjectileSimulation refused;
    Archive              forgedReading( listByte.data(), listByte.size() );
    SW_EXPECT_FALSE( refused.readState( forgedReading ) );
    SW_EXPECT_TRUE( refused.getProjectiles().empty() );
}

/** @brief [ProjectileTest] step 은 이번 걸음의 사건만 돌려준다 — 지난 걸음의 사건이 남지 않고, 시간이 흐르지 않은 걸음은 비어 있다 */
SW_TEST_CASE( ProjectileTest, StepReturnsOnlyThisStepEvents )
{
    LineWorld world;
    world._listTarget = {
        float3{ 1.0f, 0.0f, 0.0f }
    };
    ProjectileSimulation simulation;
    (void)simulation.spawn( Util::makeBullet( 120.0f ) ); // id 는 쓰지 않는다

    int32 hitCount = 0;
    for ( int32 step = 0; step < 10; ++step )
    {
        hitCount += Util::countKind( simulation.step( 1.0f / 60.0f, world ), ProjectileEvent::Kind::Hit );
    }
    SW_EXPECT_EQUAL( 1, hitCount );
    SW_EXPECT_TRUE( simulation.step( 0.0f, world ).empty() );
}
