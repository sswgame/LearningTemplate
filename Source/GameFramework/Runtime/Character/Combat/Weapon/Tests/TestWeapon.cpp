#include "pch.h"

#include "Core/Math/Ray.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Character/Combat/Damage/DamageMath.h"
#include "GameFramework/Runtime/Character/Combat/Weapon/Weapon.h"

// WeaponTest — 피해 공식(거리 감쇠 · 머리 · 방어)과 총 한 자루의 상태(연사 속도 · 재장전 · 반자동 · 상태 왕복). 정의는 구조체로 직접 준다(nogpu).

using namespace sw;

namespace
{
    struct WeaponTestUtil
    {
        static WeaponDef makeRifle()
        {
            WeaponDef weapon;
            weapon._id             = hashed_string( "rifle" );
            weapon._projectileID   = hashed_string( "round" );
            weapon._fireInterval   = 0.1f;
            weapon._reloadTime     = 1.0f;
            weapon._magazineSize   = 5;
            weapon._maxReserveAmmo = 7;
            return weapon;
        }

        static ProjectileDef makeRound()
        {
            ProjectileDef round;
            round._id                 = hashed_string( "round" );
            round._damage             = 20.0f;
            round._falloffStart       = 10.0f;
            round._falloffEnd         = 30.0f;
            round._falloffMinScale    = 0.5f;
            round._headshotMultiplier = 2.0f;
            return round;
        }

        /** @brief @p seconds 동안 @p deltaTime 간격으로 방아쇠를 누르고 있어 쏜 발 수입니다. */
        static int32 holdTrigger( WeaponState& state, float32 seconds, float32 deltaTime )
        {
            const Ray  aim;
            WeaponShot shot;
            int32      firedCount = 0;
            for ( float32 time = 0.0f; time < seconds; time += deltaTime )
            {
                if ( state.pullTrigger( aim, time == 0.0f, shot ) == WeaponFireResult::Fired )
                    ++firedCount;
                state.update( deltaTime );
            }
            return firedCount;
        }
    };
} // namespace

/**
 * @brief [WeaponTest] 감쇠 구간 안은 선형이고 밖은 양 끝 값이다. 머리는 배율, 방어는 고정 → 비율 순이며 최소 피해 아래로 내려가지 않는다
 */
SW_TEST_CASE( WeaponTest, DamageFormula )
{
    const ResolvedShot shot = ResolvedShot::resolve( WeaponTestUtil::makeRound(), {}, {} );
    SW_EXPECT_EQUAL( 1.0f, shot.computeFalloffScale( 5.0f ) );
    SW_EXPECT_EQUAL( 0.75f, shot.computeFalloffScale( 20.0f ) );
    SW_EXPECT_EQUAL( 0.5f, shot.computeFalloffScale( 100.0f ) );
    SW_EXPECT_EQUAL( 15.0f, shot.computeDamage( 20.0f, false ) );
    SW_EXPECT_EQUAL( 30.0f, shot.computeDamage( 20.0f, true ) );

    SW_EXPECT_EQUAL( 12.0f, DamageMath::applyArmor( 20.0f, 5.0f, 0.2f, 1.0f ) ); // (20 - 5) × 0.8
    SW_EXPECT_EQUAL( 1.0f, DamageMath::applyArmor( 20.0f, 50.0f, 0.0f, 1.0f ) ); // 방어가 피해보다 커도 최소 피해
    SW_EXPECT_EQUAL( 0.5f, DamageMath::applyArmor( 0.5f, 50.0f, 0.0f, 1.0f ) );  // 최소 피해가 원래 피해보다 크면 원래 피해까지만
    SW_EXPECT_EQUAL( 0.0f, DamageMath::applyArmor( 0.0f, 0.0f, 0.0f, 1.0f ) );
}

/**
 * @brief [WeaponTest] 연사 속도는 프레임 간격과 무관하다 — 0.95 초 동안 60 fps 와 24 fps 가 같은 발 수(0, 0.1, …, 0.9 초의 10 발)
 */
SW_TEST_CASE( WeaponTest, FireRateIgnoresFrameRate )
{
    WeaponDef rifle       = WeaponTestUtil::makeRifle();
    rifle._magazineSize   = 100;
    rifle._maxReserveAmmo = 0;

    WeaponState fast;
    SW_REQUIRE_TRUE( fast.equip( rifle, WeaponTestUtil::makeRound(), 0 ) );
    WeaponState slow;
    SW_REQUIRE_TRUE( slow.equip( rifle, WeaponTestUtil::makeRound(), 0 ) );
    SW_EXPECT_EQUAL( 10, WeaponTestUtil::holdTrigger( fast, 0.95f, 1.0f / 60.0f ) );
    SW_EXPECT_EQUAL( 10, WeaponTestUtil::holdTrigger( slow, 0.95f, 1.0f / 24.0f ) );
}

/**
 * @brief [WeaponTest] 빈 탄창으로 당기면 재장전을 시작하고, 끝나면 빈 만큼만 예비탄에서 옮긴다. 둘 다 비면 OutOfAmmo
 */
SW_TEST_CASE( WeaponTest, ReloadMovesOnlyWhatIsMissing )
{
    WeaponState state;
    SW_REQUIRE_TRUE( state.equip( WeaponTestUtil::makeRifle(), WeaponTestUtil::makeRound(), 7 ) );
    const Ray  aim;
    WeaponShot shot;
    for ( int32 shotIndex = 0; shotIndex < 5; ++shotIndex )
    {
        SW_REQUIRE_TRUE( state.pullTrigger( aim, true, shot ) == WeaponFireResult::Fired );
        state.update( 0.1f );
    }
    SW_EXPECT_TRUE( state.pullTrigger( aim, true, shot ) == WeaponFireResult::EmptyMagazine );
    SW_EXPECT_TRUE( state.isReloading() );
    SW_EXPECT_TRUE( state.pullTrigger( aim, true, shot ) == WeaponFireResult::Reloading );
    state.update( 1.0f );
    SW_EXPECT_EQUAL( 5, state.getMagazineAmmo() );
    SW_EXPECT_EQUAL( 2, state.getReserveAmmo() );

    state.addReserveAmmo( 100 );
    SW_EXPECT_EQUAL( 7, state.getReserveAmmo() ); // 상한(`_maxReserveAmmo`)
    SW_EXPECT_FALSE( state.startReload() );       // 탄창이 차 있다

    WeaponState empty;
    WeaponDef   oneShot   = WeaponTestUtil::makeRifle();
    oneShot._magazineSize = 1;
    SW_REQUIRE_TRUE( empty.equip( oneShot, WeaponTestUtil::makeRound(), 0 ) );
    SW_REQUIRE_TRUE( empty.pullTrigger( aim, true, shot ) == WeaponFireResult::Fired );
    empty.update( 0.2f );
    SW_EXPECT_TRUE( empty.pullTrigger( aim, true, shot ) == WeaponFireResult::OutOfAmmo );
}

/**
 * @brief [WeaponTest] 반자동은 누를 때마다 한 발이고, 산탄은 알마다 광선 하나다
 */
SW_TEST_CASE( WeaponTest, SemiAutoAndPellets )
{
    WeaponDef shotgun    = WeaponTestUtil::makeRifle();
    shotgun._bAutomatic  = SW_FALSE;
    shotgun._pelletCount = 8;
    WeaponState state;
    SW_REQUIRE_TRUE( state.equip( shotgun, WeaponTestUtil::makeRound(), 0 ) );
    const Ray  aim;
    WeaponShot shot;
    SW_REQUIRE_TRUE( state.pullTrigger( aim, true, shot ) == WeaponFireResult::Fired );
    SW_EXPECT_EQUAL( size_t( 8 ), shot._listRay.size() );
    state.update( 1.0f );
    SW_EXPECT_TRUE( state.pullTrigger( aim, false, shot ) == WeaponFireResult::SemiAutoHeld );
    SW_EXPECT_TRUE( shot._listRay.empty() );
}

/**
 * @brief [WeaponTest] 상태를 쓰고 읽은 총은 다음 발의 퍼짐 방향까지 같다(난수 상태를 함께 담는다). 다른 무기 · 다른 탄의 상태는 읽지 않는다
 */
SW_TEST_CASE( WeaponTest, StateRoundTripKeepsTheNextShot )
{
    WeaponState original;
    SW_REQUIRE_TRUE( original.equip( WeaponTestUtil::makeRifle(), WeaponTestUtil::makeRound(), 7, 1234u ) );
    const Ray  aim;
    WeaponShot shot;
    SW_REQUIRE_TRUE( original.pullTrigger( aim, true, shot ) == WeaponFireResult::Fired );
    original.update( 0.1f );

    Archive written;
    original.writeState( written );
    WeaponState restored;
    SW_REQUIRE_TRUE( restored.equip( WeaponTestUtil::makeRifle(), WeaponTestUtil::makeRound(), 0 ) );
    Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( original.getMagazineAmmo(), restored.getMagazineAmmo() );
    SW_EXPECT_EQUAL( original.getReserveAmmo(), restored.getReserveAmmo() );

    WeaponShot originalShot;
    WeaponShot restoredShot;
    SW_REQUIRE_TRUE( original.pullTrigger( aim, true, originalShot ) == WeaponFireResult::Fired );
    SW_REQUIRE_TRUE( restored.pullTrigger( aim, true, restoredShot ) == WeaponFireResult::Fired );
    SW_EXPECT_TRUE( originalShot._listRay[0]._direction == restoredShot._listRay[0]._direction );

    WeaponDef other = WeaponTestUtil::makeRifle();
    other._id       = hashed_string( "pistol" );
    WeaponState wrong;
    SW_REQUIRE_TRUE( wrong.equip( other, WeaponTestUtil::makeRound(), 0 ) );
    Archive again( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( wrong.readState( again ) );

    ProjectileDef slug = WeaponTestUtil::makeRound();
    slug._id           = hashed_string( "slug" );
    WeaponState otherRound;
    SW_REQUIRE_TRUE( otherRound.equip( WeaponTestUtil::makeRifle(), slug, 0 ) );
    Archive slugReading( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( otherRound.readState( slugReading ) ); // 다른 탄을 장전한 총은 그 상태를 받지 않는다
}
