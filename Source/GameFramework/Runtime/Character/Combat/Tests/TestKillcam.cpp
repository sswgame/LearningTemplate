// KillcamTest — 킬캠 기록(관통 다중 명중 · 상한 · 투사체 사건)과 연출 시간표, 재생 컴포넌트의 컷 카메라(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"

#include "GameFramework/Runtime/Character/Combat/Killcam/KillcamComponent.h"
#include "GameFramework/Runtime/Character/Combat/Killcam/KillcamShot.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileSimulation.h"

using namespace sw;

namespace
{
    struct KillcamTestUtil
    {
        /** @brief +Z 로 쏜 관통탄 — 10 m · 4 m(뒤에 넣어도 거리 순) · 끝 30 m, 10 m 명중이 쓰러뜨렸다. */
        static KillcamShot makePiercingShot()
        {
            KillcamShot shot;
            shot.begin( float3{ 0.0f, 1.0f, 0.0f }, float3{ 0.0f, 0.0f, 2.0f }, 0.0 );
            KillcamHit farHit;
            farHit._position = float3{ 0.0f, 1.0f, 10.0f };
            farHit._distance = 10.0f;
            farHit._bKilled  = SW_TRUE;
            KillcamHit nearHit;
            nearHit._position = float3{ 0.0f, 1.0f, 4.0f };
            nearHit._distance = 4.0f;
            (void)shot.addHit( farHit );
            (void)shot.addHit( nearHit );
            shot.finish( float3{ 0.0f, 1.0f, 30.0f } );
            return shot;
        }
    };
} // namespace

/** @brief [KillcamTest] 명중은 거리 순으로 쌓이고 상한에서 멈추며, 방향은 단위로 맞춘다 */
SW_TEST_CASE( KillcamTest, HitsAreOrderedAndBounded )
{
    KillcamShot shot = KillcamTestUtil::makePiercingShot();
    SW_REQUIRE_EQUAL( size_t{ 2 }, shot.getHits().size() );
    SW_EXPECT_NEAR( 4.0f, shot.getHits()[0]._distance, 1e-5f );
    SW_EXPECT_NEAR( 10.0f, shot.getHits()[1]._distance, 1e-5f );
    SW_EXPECT_NEAR( 1.0f, shot.getDirection().getLength(), 1e-5f );
    SW_EXPECT_TRUE( shot.hasKill() );
    for ( uint32 index = 2; index < KillcamShot::kMaxHits; ++index )
    {
        SW_EXPECT_TRUE( shot.addHit( KillcamHit{} ) );
    }
    SW_EXPECT_FALSE( shot.addHit( KillcamHit{} ) );
    SW_EXPECT_EQUAL( size_t{ KillcamShot::kMaxHits }, shot.getHits().size() );
}

/** @brief [KillcamTest] 날아가는 탄의 맞음 사건은 명중으로, 튕김 · 끝 사건은 무시한다 — 한 걸음의 관통 명중 여럿이 그대로 쌓인다 */
SW_TEST_CASE( KillcamTest, ProjectileEventsBecomeHits )
{
    KillcamShot shot;
    shot.begin( float3{}, float3{ 1.0f, 0.0f, 0.0f }, 1.0 );
    ProjectileEvent hit;
    hit._kind              = ProjectileEvent::Kind::Hit;
    hit._hit._position     = float3{ 3.0f, 0.0f, 0.0f };
    ProjectileEvent second = hit;
    second._hit._position  = float3{ 5.0f, 0.0f, 0.0f };
    ProjectileEvent bounce = hit;
    bounce._kind           = ProjectileEvent::Kind::Bounced;
    SW_EXPECT_TRUE( shot.addProjectileEvent( hit, GameObjectHandle::make( 7 ), false ) );
    SW_EXPECT_TRUE( shot.addProjectileEvent( second, GameObjectHandle::make( 8 ), true ) );
    SW_EXPECT_FALSE( shot.addProjectileEvent( bounce, GameObjectHandle{}, false ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, shot.getHits().size() );
    SW_EXPECT_NEAR( 5.0f, shot.getHits()[1]._distance, 1e-5f );
    SW_EXPECT_TRUE( shot.getHits()[1]._target == GameObjectHandle::make( 8 ) );
    SW_EXPECT_TRUE( shot.hasKill() );
}

/** @brief [KillcamTest] 시간표: 첫 명중까지 따라가고 멈춘 뒤 다음 명중으로 간다. 마지막 명중에서 끝나고, 상한에서 자른다 */
SW_TEST_CASE( KillcamTest, TimelineFollowsThenHoldsEachHit )
{
    const KillcamShot shot = KillcamTestUtil::makePiercingShot();
    KillcamSettings   settings;
    settings._bulletSpeed    = 10.0f;
    settings._hitHoldSeconds = 0.5f;
    settings._maxSeconds     = 10.0f;
    // 4 m 를 0.4 초, 멈춤 0.5 초, 6 m 를 0.6 초, 멈춤 0.5 초 = 2.0 초
    SW_EXPECT_NEAR( 2.0f, KillcamShot::computeDuration( shot, settings ), 1e-4f );

    const KillcamPose start = KillcamShot::sample( shot, settings, 0.2f );
    SW_EXPECT_TRUE( start._phase == KillcamPhase::Follow );
    SW_EXPECT_NEAR( 2.0f, start._bullet._z, 1e-4f ); // 0.2 초 × 10 m/s, 출발 z 0 에서
    SW_EXPECT_EQUAL( 0, start._hitIndex );
    SW_EXPECT_TRUE( start._position._z < start._bullet._z ); // 탄 뒤에서 따라간다

    const KillcamPose hold = KillcamShot::sample( shot, settings, 0.6f );
    SW_EXPECT_TRUE( hold._phase == KillcamPhase::HitHold );
    SW_EXPECT_EQUAL( 0, hold._hitIndex );
    SW_EXPECT_NEAR( 4.0f, hold._lookAt._z, 1e-4f );

    const KillcamPose second = KillcamShot::sample( shot, settings, 1.2f );
    SW_EXPECT_TRUE( second._phase == KillcamPhase::Follow );
    SW_EXPECT_EQUAL( 1, second._hitIndex );
    SW_EXPECT_NEAR( 7.0f, second._bullet._z, 1e-4f ); // 4 m 에서 0.3 초 더

    const KillcamPose last = KillcamShot::sample( shot, settings, 1.8f );
    SW_EXPECT_TRUE( last._phase == KillcamPhase::HitHold );
    SW_EXPECT_EQUAL( 1, last._hitIndex );
    SW_EXPECT_TRUE( KillcamShot::sample( shot, settings, 2.0f )._phase == KillcamPhase::Done );

    settings._maxSeconds = 0.5f; // 길이 상한
    SW_EXPECT_NEAR( 0.5f, KillcamShot::computeDuration( shot, settings ), 1e-4f );
    SW_EXPECT_TRUE( KillcamShot::sample( shot, settings, 0.5f )._phase == KillcamPhase::Done );
}

/** @brief [KillcamTest] 재생은 그 오브젝트의 카메라를 컷 카메라로 걸고 끝나면 푼다. 카메라가 없으면 오류이고, 기록은 고정 용량이다 */
SW_TEST_CASE( KillcamTest, ComponentCutsToItsCameraAndReleases )
{
    GameObjectManager manager;
    GameObject*       pKillcamObject = manager.createGameObject( hashed_string( "Killcam" ) );
    SW_REQUIRE_NOT_NULL( pKillcamObject );
    CameraComponent* pCamera = pKillcamObject->addComponent<CameraComponent>();
    SW_REQUIRE_NOT_NULL( pCamera );
    pCamera->setRole( CameraRole::Custom );
    KillcamComponent* pKillcam = pKillcamObject->addComponent<KillcamComponent>();
    SW_REQUIRE_NOT_NULL( pKillcam );
    SW_EXPECT_TRUE( KillcamComponent::findInScene( manager ) == pKillcam );

    SW_EXPECT_FALSE( pKillcam->playLastKill() ); // 기록이 없다
    pKillcam->recordShot( KillcamTestUtil::makePiercingShot() );
    SW_EXPECT_EQUAL( 2u, pKillcam->getLastHitCount() );
    SW_REQUIRE_TRUE( pKillcam->playLastKill() );
    SW_EXPECT_TRUE( pKillcam->isPlaying() );
    SW_EXPECT_TRUE( manager.getCameraRegistry().getCutCamera() == pCamera );
    for ( int32 tickIndex = 0; tickIndex < 400 && pKillcam->isPlaying(); ++tickIndex )
    {
        {
            TickContext context{ *pKillcam->getOwner(), 0.05f };
            pKillcam->onTick( context );
        }
    }
    SW_EXPECT_FALSE( pKillcam->isPlaying() );
    SW_EXPECT_TRUE( manager.getCameraRegistry().getCutCamera() == nullptr );

    for ( uint32 index = 0; index < 20; ++index )
    {
        pKillcam->recordShot( KillcamShot{} );
    }
    SW_EXPECT_EQUAL( 8u, pKillcam->getShotCount() ); // 기본 용량
    SW_EXPECT_FALSE( pKillcam->playLastKill() );     // 쓰러뜨린 발은 덮였다

    SW_EXPECT_LOG( "Killcam.NoCamera" );
    GameObject*       pBare        = manager.createGameObject( hashed_string( "Bare" ) );
    KillcamComponent* pBareKillcam = pBare->addComponent<KillcamComponent>();
    SW_EXPECT_FALSE( pBareKillcam->play( KillcamTestUtil::makePiercingShot() ) );
}
