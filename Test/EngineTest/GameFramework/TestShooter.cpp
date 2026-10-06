#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Input/RawInputEvent.h"

#include "GameFramework/Base/Combat/Weapon.h"
#include "GameFramework/Base/Combat/WeaponMath.h"
#include "GameFramework/Base/Input/FirstPersonLook.h"
#include "GameFramework/Base/Movement/LocomotionMath.h"
#include "GameFramework/Base/Utility/OrientationUtil.h"
#include "GameFramework/Base/Utility/RayMath.h"

#include "TestFramework/TestFramework.h"

// 슈터 키트 — 히트스캔(구 · 상자 · 캡슐), 1인칭 시점, 이동 방향 가르기, Shooter3D 입력 맵, 무기의 연사 간격 · 탄창 · 재장전 · 반자동 · 산탄 · 탄 퍼짐(씨앗이 같으면 같은 탄).

using namespace sw;

namespace
{
    WeaponDef makeShooterTestRifle()
    {
        WeaponDef weapon;
        weapon._id             = hashed_string( "rifle" );
        weapon._fireInterval   = 0.1f;
        weapon._reloadTime     = 1.5f;
        weapon._magazineSize   = 5;
        weapon._maxReserveAmmo = 7;
        weapon._minSpread      = 0.0f;
        weapon._maxSpread      = 3.0f;
        weapon._spreadPerShot  = 1.0f;
        weapon._spreadRecovery = 2.0f;
        return weapon;
    }

    GameRay makeForwardRay()
    {
        GameRay ray;
        ray._origin    = float3{ 0.0f, 0.0f, 0.0f };
        ray._direction = float3{ 0.0f, 0.0f, 1.0f };
        return ray;
    }

    /** @brief 고정 dt 로 @p seconds 동안 방아쇠를 당긴 채 쏜 발 수입니다(프레임마다 시간을 흘리고 한 번 당긴다 — 게임 루프 순서). */
    int32 countShotsHeldFor( const WeaponDef& weapon, float32 framesPerSecond, float32 seconds )
    {
        WeaponState state;
        state.equip( weapon, 0 );
        WeaponShot    shot;
        const GameRay aim        = makeForwardRay();
        const float32 deltaTime  = 1.0f / framesPerSecond;
        const int32   frameCount = static_cast<int32>( seconds * framesPerSecond + 0.5f );
        int32         shotCount  = 0;
        for ( int32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            state.update( deltaTime );
            if ( state.pullTrigger( aim, frameIndex == 0, shot ) == WeaponFireResult::Fired )
                ++shotCount;
        }
        return shotCount;
    }
} // namespace

/**
 * @brief [ShooterTest] 광선은 앞에 있는 구 · 상자의 가까운 면에서 맞고, 뒤 · 옆 · 사거리 밖은 놓친다 · 안에서 쏘면 거리 0
 */
SW_TEST_CASE( ShooterTest, RaysHitSpheresAndBoxesInFrontOnly )
{
    const GameRay ray      = makeForwardRay();
    float32       distance = -1.0f;
    SW_EXPECT_TRUE( RayMath::intersectSphere( ray, float3{ 0.0f, 0.0f, 10.0f }, 1.0f, 100.0f, distance ) );
    SW_EXPECT_NEAR_EQUAL( 9.0f, distance, 1.0e-4f );
    SW_EXPECT_FALSE( RayMath::intersectSphere( ray, float3{ 0.0f, 0.0f, -10.0f }, 1.0f, 100.0f, distance ) );
    SW_EXPECT_FALSE( RayMath::intersectSphere( ray, float3{ 2.0f, 0.0f, 10.0f }, 1.0f, 100.0f, distance ) );
    SW_EXPECT_FALSE( RayMath::intersectSphere( ray, float3{ 0.0f, 0.0f, 10.0f }, 1.0f, 5.0f, distance ) );
    SW_EXPECT_TRUE( RayMath::intersectSphere( ray, float3{ 0.0f, 0.0f, 0.5f }, 1.0f, 100.0f, distance ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, distance, 1.0e-6f );

    SW_EXPECT_TRUE( RayMath::intersectAabb( ray, float3{ -1.0f, -1.0f, 4.0f }, float3{ 1.0f, 1.0f, 6.0f }, 100.0f, distance ) );
    SW_EXPECT_NEAR_EQUAL( 4.0f, distance, 1.0e-4f );
    SW_EXPECT_FALSE( RayMath::intersectAabb( ray, float3{ 2.0f, -1.0f, 4.0f }, float3{ 3.0f, 1.0f, 6.0f }, 100.0f, distance ) );
    SW_EXPECT_FALSE( RayMath::intersectAabb( ray, float3{ -1.0f, -1.0f, -6.0f }, float3{ 1.0f, 1.0f, -4.0f }, 100.0f, distance ) );
}

/**
 * @brief [ShooterTest] 1인칭 시점은 피치를 ±85° 에서 자르고 요를 감는다 — 앞 방향은 요 0 · 피치 0 에서 +Z
 */
SW_TEST_CASE( ShooterTest, FirstPersonLookClampsPitchAndWrapsYaw )
{
    FirstPersonLook look;
    SW_EXPECT_NEAR_EQUAL( 1.0f, look.getForward()._z, 1.0e-5f );
    look.addMouseDelta( 0.0f, -10000.0f, 0.01f ); // 위로 한참
    SW_EXPECT_NEAR_EQUAL( 85.0f * 3.14159265f / 180.0f, look.getPitch(), 1.0e-4f );
    SW_EXPECT_TRUE( look.getForward()._y > 0.99f );
    look.setAngles( 3.14159265f * 3.0f, 0.0f );
    SW_EXPECT_TRUE( -3.1416f <= look.getYaw() && look.getYaw() <= 3.1416f );
    look.setAngles( 3.14159265f * 0.5f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, look.getFlatForward()._x, 1.0e-5f ); // 요 90° 는 +X
    SW_EXPECT_NEAR_EQUAL( -1.0f, look.getFlatRight()._z, 1.0e-5f );
}

/**
 * @brief [ShooterTest] 연사 간격 · 탄창 · 빈 탄창의 자동 재장전 · 재장전 시간 · 예비탄이 바닥나면 쏠 수 없다
 */
SW_TEST_CASE( ShooterTest, WeaponCyclesMagazineAndReload )
{
    WeaponState weapon;
    weapon.equip( makeShooterTestRifle(), 7 );
    WeaponShot    shot;
    const GameRay aim = makeForwardRay();

    SW_EXPECT_TRUE( weapon.pullTrigger( aim, true, shot ) == WeaponFireResult::Fired );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), shot._listRay.size() );
    SW_EXPECT_TRUE( weapon.pullTrigger( aim, false, shot ) == WeaponFireResult::Cooling );
    for ( int32 shotIndex = 0; shotIndex < 4; ++shotIndex )
    {
        weapon.update( 0.1f );
        SW_EXPECT_TRUE( weapon.pullTrigger( aim, false, shot ) == WeaponFireResult::Fired );
    }
    SW_EXPECT_EQUAL( 0, weapon.getMagazineAmmo() );
    weapon.update( 0.1f );
    SW_EXPECT_TRUE( weapon.pullTrigger( aim, false, shot ) == WeaponFireResult::EmptyMagazine );
    SW_EXPECT_TRUE( weapon.isReloading() );
    SW_EXPECT_TRUE( weapon.pullTrigger( aim, false, shot ) == WeaponFireResult::Reloading );
    weapon.update( 1.0f );
    SW_EXPECT_EQUAL( 0, weapon.getMagazineAmmo() );
    weapon.update( 0.6f );
    SW_EXPECT_EQUAL( 5, weapon.getMagazineAmmo() );
    SW_EXPECT_EQUAL( 2, weapon.getReserveAmmo() );

    SW_EXPECT_FALSE( weapon.startReload() ); // 가득이다
    for ( int32 shotIndex = 0; shotIndex < 5; ++shotIndex )
    {
        (void)weapon.pullTrigger( aim, false, shot );
        weapon.update( 0.1f );
    }
    SW_EXPECT_TRUE( weapon.startReload() );
    weapon.update( 2.0f );
    SW_EXPECT_EQUAL( 2, weapon.getMagazineAmmo() );
    SW_EXPECT_EQUAL( 0, weapon.getReserveAmmo() );
    (void)weapon.pullTrigger( aim, false, shot );
    weapon.update( 0.1f );
    (void)weapon.pullTrigger( aim, false, shot );
    weapon.update( 0.1f );
    SW_EXPECT_TRUE( weapon.pullTrigger( aim, false, shot ) == WeaponFireResult::OutOfAmmo );
}

/**
 * @brief [ShooterTest] 연사 속도가 프레임률에 매이지 않는다 — 30 · 60 · 144 fps 로 10 초 쏜 발 수가 설계값(10 / 간격) ±1 발 안이다
 * @details 쿨다운이 0 아래로 내려간 몫을 다음 발 간격에서 뺀다. 버리면 간격이 `ceil( 간격 / dt ) × dt` 로 늘어 라이플(0.095 s)이
 *          30 · 60 fps 에서 10.0 발/초, 144 fps 에서 10.3 발/초가 된다(설계 10.53). 한 번 당기면 한 발이라 fps 가 1 / 간격보다
 *          낮으면 프레임마다 한 발로 떨어진다 — 멈춘 프레임 뒤에 몰아 쏘지 않는다. 쉬다가 다시 당긴 첫 발도 쉰 시간을 잇지 않는다.
 */
SW_TEST_CASE( ShooterTest, FireRateDoesNotDependOnFrameRate )
{
    WeaponDef rifle       = makeShooterTestRifle();
    rifle._fireInterval   = 0.095f;
    rifle._magazineSize   = 100000;
    rifle._spreadPerShot  = 0.0f;
    const float32 seconds = 10.0f;
    const float32 design  = seconds / rifle._fireInterval; // 105.26 발
    for ( const float32 framesPerSecond : { 30.0f, 60.0f, 144.0f } )
        SW_EXPECT_NEAR_EQUAL( design, static_cast<float32>( countShotsHeldFor( rifle, framesPerSecond, seconds ) ), 1.0f );

    // 간격보다 긴 프레임(5 fps · 200 ms)은 프레임마다 한 발 — 50 발이고 따라잡으려 몰아 쏘지 않는다.
    SW_EXPECT_EQUAL( 50, countShotsHeldFor( rifle, 5.0f, seconds ) );

    // 쉬다가 당긴 첫 발 뒤에는 온전한 간격을 기다린다 — 쉬는 동안의 시간을 다음 발로 잇지 않는다.
    WeaponState state;
    state.equip( rifle, 0 );
    WeaponShot    shot;
    const GameRay aim = makeForwardRay();
    for ( int32 frameIndex = 0; frameIndex < 10; ++frameIndex )
        state.update( 0.05f );
    SW_EXPECT_TRUE( state.pullTrigger( aim, true, shot ) == WeaponFireResult::Fired );
    state.update( 0.05f );
    SW_EXPECT_TRUE( state.pullTrigger( aim, false, shot ) == WeaponFireResult::Cooling );
    state.update( 0.05f );
    SW_EXPECT_TRUE( state.pullTrigger( aim, false, shot ) == WeaponFireResult::Fired );
}

/**
 * @brief [ShooterTest] 반자동은 누를 때마다 한 발 · 산탄은 알 수만큼 광선 · 퍼짐은 쏠수록 커지고 쉬면 돌아오며 원뿔 안에 머문다 · 씨앗이 같으면 같은 탄
 */
SW_TEST_CASE( ShooterTest, SemiAutoShotgunSpreadIsBoundedAndDeterministic )
{
    WeaponDef shotgun    = makeShooterTestRifle();
    shotgun._bAutomatic  = SW_FALSE;
    shotgun._pelletCount = 8;
    shotgun._minSpread   = 5.0f;
    shotgun._maxSpread   = 5.0f;
    WeaponState first;
    WeaponState second;
    first.equip( shotgun, 0, 42u );
    second.equip( shotgun, 0, 42u );
    WeaponShot    firstShot;
    WeaponShot    secondShot;
    const GameRay aim = makeForwardRay();

    SW_ASSERT_TRUE( first.pullTrigger( aim, true, firstShot ) == WeaponFireResult::Fired );
    SW_ASSERT_TRUE( second.pullTrigger( aim, true, secondShot ) == WeaponFireResult::Fired );
    SW_EXPECT_EQUAL( static_cast<size_t>( 8 ), firstShot._listRay.size() );
    const float32 cosLimit   = MathUtil::cos( 5.0f * 3.14159265f / 180.0f ) - 1.0e-5f;
    bool          bInCone    = true;
    bool          bSame      = true;
    bool          bAnySpread = false;
    for ( size_t rayIndex = 0; rayIndex < firstShot._listRay.size(); ++rayIndex )
    {
        const float3& direction = firstShot._listRay[rayIndex]._direction;
        bInCone                 = bInCone && direction.dot( aim._direction ) >= cosLimit && MathUtil::abs( direction.getLength() - 1.0f ) < 1.0e-4f;
        bSame                   = bSame && float3::getDistance( direction, secondShot._listRay[rayIndex]._direction ) < 1.0e-6f;
        bAnySpread              = bAnySpread || direction.dot( aim._direction ) < 0.99999f;
    }
    SW_EXPECT_TRUE( bInCone );
    SW_EXPECT_TRUE( bSame );
    SW_EXPECT_TRUE( bAnySpread );

    first.update( 1.0f );
    SW_EXPECT_TRUE( first.pullTrigger( aim, false, firstShot ) == WeaponFireResult::SemiAutoHeld );
    SW_EXPECT_TRUE( first.pullTrigger( aim, true, firstShot ) == WeaponFireResult::Fired );

    // 퍼짐 — 자동 소총은 쏠수록 커지고(상한) 쉬면 돌아온다.
    WeaponState rifle;
    rifle.equip( makeShooterTestRifle(), 0 );
    for ( int32 shotIndex = 0; shotIndex < 5; ++shotIndex )
    {
        (void)rifle.pullTrigger( aim, false, firstShot );
        rifle.update( 0.1f );
    }
    SW_EXPECT_TRUE( rifle.getCurrentSpread() > 1.5f && rifle.getCurrentSpread() <= 3.0f );
    rifle.update( 5.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, rifle.getCurrentSpread(), 1.0e-5f );
}

/**
 * @brief [ShooterTest] 무기 XML 은 속성 이름이 필드 이름이고 빠진 것은 기본값이다
 */
SW_TEST_CASE( ShooterTest, WeaponCatalogReadsXml )
{
    constexpr const utf8* kWeaponXml = R"(
<WeaponCatalog>
  <Weapon id="shotgun" name="Shotgun" fireInterval="0.8" pelletCount="9" automatic="false" magazineSize="6" damage="7"/>
  <Weapon name="NoId"/>
</WeaponCatalog>
)";
    WeaponCatalog         catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kWeaponXml, "ShooterTest" ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), catalog.getWeapons().size() );
    const WeaponDef* pShotgun = catalog.findWeapon( "shotgun" );
    SW_ASSERT_NOT_NULL( pShotgun );
    SW_EXPECT_EQUAL( 9, pShotgun->_pelletCount );
    SW_EXPECT_TRUE( pShotgun->_bAutomatic == SW_FALSE );
    SW_EXPECT_NEAR_EQUAL( 0.8f, pShotgun->_fireInterval, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 80.0f, pShotgun->_range, 1.0e-5f );
}

/**
 * @brief [ShooterTest] 캡슐 히트박스 — 옆면은 반지름만큼 앞에서, 끝 반구는 구처럼 맞고, 머리 위 · 뒤 · 사거리 밖은 놓친다 · 안에서 쏘면 거리 0
 */
SW_TEST_CASE( ShooterTest, RaysHitCapsulesOnTheSideAndTheCaps )
{
    const float3  bottom{ 0.0f, 0.45f, 5.0f };
    const float3  top{ 0.0f, 1.55f, 5.0f };
    const float32 radius   = 0.45f;
    float32       distance = 0.0f;
    GameRay       ray      = makeForwardRay();

    ray._origin = float3{ 0.0f, 1.0f, 0.0f }; // 몸통 높이 — 옆면
    SW_ASSERT_TRUE( RayMath::intersectCapsule( ray, bottom, top, radius, 100.0f, distance ) );
    SW_EXPECT_NEAR_EQUAL( 5.0f - radius, distance, 1.0e-4f );

    ray._origin = float3{ 0.0f, 1.9f, 0.0f }; // 머리 — 위 반구(가운데에서 0.35 위)
    SW_ASSERT_TRUE( RayMath::intersectCapsule( ray, bottom, top, radius, 100.0f, distance ) );
    SW_EXPECT_NEAR_EQUAL( 5.0f - MathUtil::sqrt( radius * radius - 0.35f * 0.35f ), distance, 1.0e-4f );

    ray._origin = float3{ 0.0f, 2.2f, 0.0f }; // 머리 위 — 캡슐 꼭대기(2.0)보다 높다
    SW_EXPECT_FALSE( RayMath::intersectCapsule( ray, bottom, top, radius, 100.0f, distance ) );

    ray._origin    = float3{ 0.0f, 5.0f, 5.0f }; // 위에서 축을 따라 — 옆면 판정이 없고 위 반구에서 맞는다
    ray._direction = float3{ 0.0f, -1.0f, 0.0f };
    SW_ASSERT_TRUE( RayMath::intersectCapsule( ray, bottom, top, radius, 100.0f, distance ) );
    SW_EXPECT_NEAR_EQUAL( 5.0f - ( 1.55f + radius ), distance, 1.0e-4f );

    ray         = makeForwardRay();
    ray._origin = float3{ 0.0f, 1.0f, 5.1f }; // 안
    SW_ASSERT_TRUE( RayMath::intersectCapsule( ray, bottom, top, radius, 100.0f, distance ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, distance, 1.0e-6f );

    ray._origin = float3{ 0.0f, 1.0f, 10.0f }; // 뒤에 있다
    SW_EXPECT_FALSE( RayMath::intersectCapsule( ray, bottom, top, radius, 100.0f, distance ) );
    ray._origin = float3{ 0.0f, 1.0f, 0.0f }; // 사거리 4 — 닿지 않는다
    SW_EXPECT_FALSE( RayMath::intersectCapsule( ray, bottom, top, radius, 4.0f, distance ) );
    ray._origin = float3{ 0.6f, 1.0f, 0.0f }; // 옆으로 비켜 간다
    SW_EXPECT_FALSE( RayMath::intersectCapsule( ray, bottom, top, radius, 100.0f, distance ) );
}

/**
 * @brief [ShooterTest] 이동 방향은 보는 쪽 기준이다 — 요 0 이면 +Z 앞 · +X 오른쪽, 요 90° 면 +X 앞 · +Z 왼쪽, 대각선은 앞뒤, 느리면 서기, 공중은 공중
 */
SW_TEST_CASE( ShooterTest, LocomotionDirectionFollowsTheFacing )
{
    const float32 idle = 0.4f;
    SW_EXPECT_TRUE( LocomotionMath::classify( float3{ 0.0f, 0.0f, 3.0f }, 0.0f, true, idle ) == LocomotionDirection::Forward );
    SW_EXPECT_TRUE( LocomotionMath::classify( float3{ 0.0f, 0.0f, -2.0f }, 0.0f, true, idle ) == LocomotionDirection::Backward );
    SW_EXPECT_TRUE( LocomotionMath::classify( float3{ 2.0f, 0.0f, 0.0f }, 0.0f, true, idle ) == LocomotionDirection::Right );
    SW_EXPECT_TRUE( LocomotionMath::classify( float3{ -2.0f, 0.0f, 0.0f }, 0.0f, true, idle ) == LocomotionDirection::Left );
    SW_EXPECT_TRUE( LocomotionMath::classify( float3{ 2.0f, 0.0f, 2.0f }, 0.0f, true, idle ) == LocomotionDirection::Forward );
    SW_EXPECT_TRUE( LocomotionMath::classify( float3{ 0.1f, 0.0f, 0.2f }, 0.0f, true, idle ) == LocomotionDirection::Idle );
    SW_EXPECT_TRUE( LocomotionMath::classify( float3{ 0.0f, 0.0f, 3.0f }, 0.0f, false, idle ) == LocomotionDirection::Airborne );

    const float32 facingRight = MathUtil::kHalfPi;
    SW_EXPECT_TRUE( LocomotionMath::classify( float3{ 3.0f, 0.0f, 0.0f }, facingRight, true, idle ) == LocomotionDirection::Forward );
    SW_EXPECT_TRUE( LocomotionMath::classify( float3{ 0.0f, 0.0f, 3.0f }, facingRight, true, idle ) == LocomotionDirection::Left );
    SW_EXPECT_TRUE( LocomotionMath::classify( float3{ 0.0f, 0.0f, -3.0f }, facingRight, true, idle ) == LocomotionDirection::Right );
    const float2 local = LocomotionMath::computeLocalVelocity( float3{ 3.0f, 0.0f, 1.0f }, facingRight );
    SW_EXPECT_NEAR_EQUAL( 3.0f, local._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, local._y, 1.0e-5f );
}

/**
 * @brief [ShooterTest] Shooter3D 입력 맵(gamesettings 의 inputMap)은 게임이 묻는 액션을 모두 묶고, Look 은 마우스 이동량(`<mouseDelta>`)이다
 */
SW_TEST_CASE( ShooterTest, InputMapBindsEveryGameplayAction )
{
    InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    InputMap& inputMap = input.getInputMap();
    SW_ASSERT_TRUE( inputMap.loadFromResource( "game/shooter3d/data/shooter.input.xml" ) );
    const utf8* const arrAction[] = { "Move", "Look", "Fire", "Jump", "Sprint", "Reload", "CycleCamera", "SwitchWeapon", "Weapon1", "Weapon2", "Weapon3" };
    for ( const utf8* pAction : arrAction )
    {
        SW_EXPECT_TRUE_MSG( inputMap.hasAction( hashed_string( pAction ) ), pAction );
    }
    const ActionBinding* pLook = inputMap.getBinding( "Look", 0 );
    SW_ASSERT_NOT_NULL( pLook );
    SW_EXPECT_TRUE( pLook->_kind == BindingKind::MouseDelta2D );

    // 마우스를 움직인 프레임에 Look 이 그 이동량(배율 1)을 낸다.
    input.postRawEvent( RawInputEvent::makeMouseMove( 12, -4 ) );
    input.beginFrame( 0.016f );
    const float2 look = inputMap.getVector2D( "Look" );
    SW_EXPECT_TRUE( look._x > 0.0f );
    input.shutdown();
}

/**
 * @brief [ShooterTest] 이동 방향 거르기 — 대각선 근처는 지금 방향을 지키고(히스테리시스), 바뀐 방향은 유지 시간만큼 이어져야 받아들이며, 공중은 바로다
 */
SW_TEST_CASE( ShooterTest, LocomotionFilterHoldsTheDirectionThroughFlicker )
{
    const float32 idle = 0.4f;
    // 오른쪽 성분이 앞 성분의 1.25 배 — 앞으로 가던 중이면 앞(1.3 배를 넘어야 옆), 옆으로 가던 중이면 옆(1/1.3 아래로 내려가야 앞).
    const float3 diagonal{ 2.5f, 0.0f, 2.0f };
    SW_EXPECT_TRUE( LocomotionMath::classifyFrom( LocomotionDirection::Forward, diagonal, 0.0f, true, idle ) == LocomotionDirection::Forward );
    SW_EXPECT_TRUE( LocomotionMath::classifyFrom( LocomotionDirection::Right, diagonal, 0.0f, true, idle ) == LocomotionDirection::Right );

    LocomotionDirectionFilter filter( 0.25f );
    SW_EXPECT_TRUE( filter.update( LocomotionDirection::Forward, 0.3f ) == LocomotionDirection::Forward );
    // 한 프레임씩 오가는 깜빡임은 받아들이지 않는다.
    for ( uint32 frameIndex = 0; frameIndex < 20; ++frameIndex )
    {
        const LocomotionDirection candidate = ( frameIndex % 2u ) == 0u ? LocomotionDirection::Right : LocomotionDirection::Forward;
        SW_EXPECT_TRUE( filter.update( candidate, 0.05f ) == LocomotionDirection::Forward );
    }
    // 이어지면 유지 시간 뒤에 바뀐다.
    SW_EXPECT_TRUE( filter.update( LocomotionDirection::Right, 0.1f ) == LocomotionDirection::Forward );
    SW_EXPECT_TRUE( filter.update( LocomotionDirection::Right, 0.1f ) == LocomotionDirection::Forward );
    SW_EXPECT_TRUE( filter.update( LocomotionDirection::Right, 0.1f ) == LocomotionDirection::Right );
    // 공중은 바로, 내려와도 바로.
    SW_EXPECT_TRUE( filter.update( LocomotionDirection::Airborne, 0.01f ) == LocomotionDirection::Airborne );
    SW_EXPECT_TRUE( filter.update( LocomotionDirection::Idle, 0.01f ) == LocomotionDirection::Idle );
}

/**
 * @brief [ShooterTest] 요 돌리기는 짧은 길로 최대 걸음만큼만 간다 — ±π 를 건너는 쪽, 걸음보다 가까우면 목표, 걸음 0 이면 그대로
 */
SW_TEST_CASE( ShooterTest, TurnTowardAngleTakesTheShortWayAndCapsTheStep )
{
    SW_EXPECT_NEAR_EQUAL( 0.5f, OrientationUtil::turnTowardAngle( 0.0f, 2.0f, 0.5f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.2f, OrientationUtil::turnTowardAngle( 0.0f, 0.2f, 0.5f ), 1.0e-6f );
    // 3.0 → -3.0 은 +쪽으로 0.283 rad 가 짧다 — π 를 넘어 -π 근처로 감긴다.
    const float32 crossed = OrientationUtil::turnTowardAngle( 3.0f, -3.0f, 0.1f );
    SW_EXPECT_NEAR_EQUAL( 3.1f, crossed, 1.0e-5f );
    const float32 wrapped = OrientationUtil::turnTowardAngle( 3.1f, -3.1f, 0.2f );
    SW_EXPECT_NEAR_EQUAL( -3.1f, wrapped, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, OrientationUtil::turnTowardAngle( 1.0f, -2.0f, 0.0f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( -MathUtil::kHalfPi, OrientationUtil::wrapAngle( 3.0f * MathUtil::kHalfPi ), 1.0e-5f );
}
