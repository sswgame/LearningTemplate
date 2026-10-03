#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/FirstPersonLook.h"
#include "GameFramework/Kits/Shooter/ShooterMath.h"
#include "GameFramework/Kits/Shooter/Weapon.h"

#include "TestFramework/TestFramework.h"

// 슈터 키트 — 히트스캔(구 · 상자), 1인칭 시점, 무기의 연사 간격 · 탄창 · 재장전 · 반자동 · 산탄 · 탄 퍼짐(씨앗이 같으면 같은 탄).

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
