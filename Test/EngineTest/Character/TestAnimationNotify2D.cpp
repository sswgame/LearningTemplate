#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Animation/Sprite/SpriteClipPlayable.h"
#include "Engine/Character/AnimationNotify/AnimationNotifyComponent.h"
#include "Engine/Character/AnimationNotify/AnimationNotifyTable.h"
#include "Engine/Character/Hit/MeleeHitLedger.h"
#include "Engine/Character/Hit/MeleeHitWindow.h"
#include "Engine/Character/Socket/SocketSetComponent.h"
#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/Physics/RigidBody2DComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourcePaths.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// AnimationNotify2DTest — 2D 도 같은 알림 디스패치: 스프라이트 클립 구간의 알림(구간 알림 포함), 스프라이트 애니메이터 → 받는 쪽(틱 뒤 게임 스레드),
// Box2D 바닥 재질의 발소리, 칼날 궤적이 Box2D 바디의 히트 존을 맞힌다(자기 바디는 건너뜀).

namespace sw
{
    /** @brief 맞음을 적는 컴포넌트입니다(2D 테스트용). */
    class MockHitRecorder2DComponent : public Component
    {
    public:
        REFLECT_BODY();

        vector<HitInfo> _listHit;

        const TypeInfo* getTypeInfo() const override { return StaticType(); }
        void            onHitReceived( const HitInfo& hit ) override { _listHit.push_back( hit ); }
    };

    inline const TypeInfo* MockHitRecorder2DComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockHitRecorder2DComponent>, hashed_string( "MockHitRecorder2DComponent" ),
                                          hashed_string( "sw::MockHitRecorder2DComponent" ), sizeof( MockHitRecorder2DComponent ) );
    }
} // namespace sw

namespace
{
    struct TestAnimationNotify2DInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        static RigidBody2DComponent* spawnBox( GameObjectManager& manager, const utf8* pName, const float2& position, const float2& halfExtents, PhysicsBodyType type,
                                               const utf8* pMaterial )
        {
            GameObject*           pObject = manager.createGameObject( hashed_string( pName ) );
            RigidBody2DComponent* pBody   = pObject != nullptr ? pObject->addComponent<RigidBody2DComponent>() : nullptr;
            if ( pBody == nullptr )
                return nullptr;
            PhysicsShapeDesc2D box;
            box._halfExtents = halfExtents;
            pBody->setShape( box );
            pBody->setBodyType( type );
            pBody->setMaterial( hashed_string( pMaterial ) );
            pBody->setLocalPosition( float3{ position._x, position._y, 0.0f } );
            return pBody;
        }
    };
} // namespace

/**
 * @brief [AnimationNotify2DTest] 스프라이트 클립의 구간이 알림(시각 · 길이)을 들고 파일을 왕복하며, 재생할 것(구간)이 그 알림 트랙을 낸다
 */
SW_TEST_CASE( AnimationNotify2DTest, SpriteClipRangeCarriesNotifies )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    SpriteClipAsset clip;
    clip._listFrame.resize( 6 );
    SpriteClipAnimation attack;
    attack._name       = "Attack";
    attack._firstFrame = 2;
    attack._frameCount = 4;
    attack._listNotify.push_back( AnimationNotifyEvent{ hashed_string( "Swing" ), 0.1f, 0.2f } );
    clip._listAnimation.push_back( attack );
    const string path = test::makeTempPath( "attack.sprite.xml" );
    SW_ASSERT_TRUE( clip.saveToFile( path ) );
    SpriteClipAsset loaded;
    SW_ASSERT_TRUE( loaded.loadFromFile( sw::ResourceID{ ::test::makeTempResourceID( path ) } ) );
    SW_ASSERT_EQUAL( size_t( 1 ), loaded._listAnimation.size() );
    SW_ASSERT_EQUAL( size_t( 1 ), loaded._listAnimation[0]._listNotify.size() );
    SW_EXPECT_NEAR_EQUAL( 0.2f, loaded._listAnimation[0]._listNotify[0]._duration, 1e-6f );

    SpriteClipPlayable playable;
    SW_ASSERT_TRUE( playable.configure( &loaded, 2, 4, true, 0.1f ) );
    SW_EXPECT_FALSE( playable.configure( &loaded, -1, 2, true, 0.1f ) ); // 음수 첫 프레임은 맞추지 않고 거절한다
    SW_ASSERT_NOT_NULL( playable.findNotifyTrack() );
    SW_EXPECT_TRUE( playable.findNotifyTrack()->getEvents()[0]._name == hashed_string( "Swing" ) );
    SW_ASSERT_TRUE( playable.configure( &loaded, 0, 2, true, 0.1f ) ); // 이름 붙은 구간이 아니다 — 알림 없음
    SW_EXPECT_TRUE( playable.findNotifyTrack() == nullptr );
}

/**
 * @brief [AnimationNotify2DTest] 2D 오브젝트(스프라이트 애니메이터 + Box2D 바디)의 알림 — 발소리는 Box2D 바닥 재질, 칼날 구간은 지나간 Box2D 표적을 히트 존과 함께 한 번 맞히고 자기 바디는 건너뛴다
 * @details 스프라이트 클립 `Attack`(100 ms 프레임 넷, 반복): Step 0.05 초, Swing 0.1 ~ 0.3 초. 칼날 소켓은 루트 기준 (0.5, 0) ~ (1.5, 0). 공격자를 프레임마다 +X 로 옮겨
 *          칼날이 x = 3 의 표적을 지나게 한다.
 */
SW_TEST_CASE( AnimationNotify2DTest, SpriteNotifiesDriveFootstepAndHitWindowIn2D )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    SpriteClipAsset clip;
    clip._listFrame.resize( 4 );
    for ( SpriteClipFrame& frame : clip._listFrame )
    {
        frame._durationMs = 100;
    }
    SpriteClipAnimation attack;
    attack._name       = "Attack";
    attack._firstFrame = 0;
    attack._frameCount = 4;
    attack._listNotify.push_back( AnimationNotifyEvent{ hashed_string( "Step" ), 0.05f, 0.0f } );
    attack._listNotify.push_back( AnimationNotifyEvent{ hashed_string( "Swing" ), 0.1f, 0.2f } );
    clip._listAnimation.push_back( attack );
    const string clipPath = test::makeTempPath( "fighter.sprite.xml" );
    SW_ASSERT_TRUE( clip.saveToFile( clipPath ) );
    const string socketPath = test::makeTempPath( "fighter.sockets.xml" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( socketPath, R"(<SocketSet><Socket name="BladeA" translation="0.5 0 0"/><Socket name="BladeB" translation="1.5 0 0"/></SocketSet>)" ) );

    GameObjectManager     manager;
    RigidBody2DComponent* pFloor  = TestAnimationNotify2DInternal::spawnBox( manager, "Floor", float2{ 0.0f, -0.5f }, float2{ 20.0f, 0.5f }, PhysicsBodyType::Static, "Stone" );
    RigidBody2DComponent* pTarget = TestAnimationNotify2DInternal::spawnBox( manager, "Target", float2{ 3.0f, 0.0f }, float2{ 0.3f, 0.3f }, PhysicsBodyType::Static, "Flesh" );
    RigidBody2DComponent* pSelf   = TestAnimationNotify2DInternal::spawnBox( manager, "Fighter", float2{ 0.0f, 0.0f }, float2{ 1.6f, 0.4f }, PhysicsBodyType::Kinematic, "Flesh" );
    SW_ASSERT_TRUE( pFloor != nullptr && pTarget != nullptr && pSelf != nullptr );
    PhysicsHitZoneDef head;
    head._name             = hashed_string( "Head" );
    head._damageMultiplier = 2.0f;
    pTarget->setHitZone( head );
    MockHitRecorder2DComponent* pTargetHits = pTarget->getOwner()->addComponent<MockHitRecorder2DComponent>();
    GameObject*                 pFighter    = pSelf->getOwner();
    MockHitRecorder2DComponent* pSelfHits   = pFighter->addComponent<MockHitRecorder2DComponent>();
    SpriteComponent*            pSprite     = pFighter->addComponent<SpriteComponent>();
    SpriteAnimatorComponent*    pAnimator   = pFighter->addComponent<SpriteAnimatorComponent>();
    SocketSetComponent*         pSockets    = pFighter->addComponent<SocketSetComponent>();
    AnimationNotifyComponent*   pNotify     = pFighter->addComponent<AnimationNotifyComponent>();
    SW_ASSERT_TRUE( pTargetHits != nullptr && pSelfHits != nullptr && pSprite != nullptr && pAnimator != nullptr && pSockets != nullptr && pNotify != nullptr );
    pSprite->setClipPath( ::test::makeTempResourceID( clipPath ) );
    pSockets->setSocketSetPath( ::test::makeTempResourceID( socketPath ) );
    shared_ptr<AnimationNotifyTable> table = make_shared<AnimationNotifyTable>();
    SW_ASSERT_TRUE( table->loadFromXMLText( R"(<AnimationNotifies>
            <Notify name="Step" handler="Footstep" socket="Root" distance="0.6"/>
            <Notify name="Swing" handler="HitWindow" socketA="BladeA" socketB="BladeB" damage="5"/>
        </AnimationNotifies>)",
                                            "fighter", AnimationNotifyHandlerRegistry::getDefault() ) );
    pNotify->setNotifyTable( table );

    manager.beginPlay();
    SW_EXPECT_TRUE( pAnimator->getNotifyListener() != nullptr );
    bool bSawStep = false;
    for ( uint32 frameIndex = 0; frameIndex < 24; ++frameIndex )
    {
        // 칼날 구간(0.1 ~ 0.3 초) 동안 +X 로 움직인다 — 프레임마다 0.25 m.
        if ( frameIndex >= 7 && frameIndex <= 16 )
            pSelf->setWorldPosition( pSelf->getWorldPosition() + float3{ 0.25f, 0.0f, 0.0f } );
        manager.update( TestAnimationNotify2DInternal::kFrame );
        for ( const AnimationNotifyAction& action : pNotify->getActions() )
        {
            if ( action._notify == hashed_string( "Step" ) )
            {
                bSawStep = true;
                SW_EXPECT_TRUE( action._detail == hashed_string( "Stone" ) );
                SW_EXPECT_EQUAL( pFloor->getOwner()->getObjectID(), action._targetObjectID );
            }
        }
    }
    SW_EXPECT_TRUE( bSawStep );
    SW_ASSERT_EQUAL( size_t( 1 ), pTargetHits->_listHit.size() );
    SW_EXPECT_TRUE( pTargetHits->_listHit[0]._zone == hashed_string( "Head" ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pTargetHits->_listHit[0]._damageMultiplier, 1e-6f );
    SW_EXPECT_TRUE( pTargetHits->_listHit[0]._bIs2D );
    SW_EXPECT_EQUAL( size_t( 0 ), pSelfHits->_listHit.size() );
    manager.endPlay();
}

/**
 * @brief [AnimationNotify2DTest] 근접 판정은 칼날에 걸린 표적을 모두 찾고, 클라이언트(예측)는 찾기만 하고 맞음을 전달하지 않는다 — 서버 권위
 * @details 칼날 (0, 0) ~ (3, 0) 에 표적 둘이 앞뒤로 있다. 예측 스윕은 둘을 돌려주지만 받는 쪽은 아무것도 받지 않는다. 권위 스윕은 둘 모두에 한 번씩
 *          전달하고, 같은 창(이미 맞힌 목록)으로 다시 쓸면 새로 맞는 것이 없다. 공격자 자신의 바디는 칼날 안에 있어도 맞지 않는다.
 */
SW_TEST_CASE( AnimationNotify2DTest, MeleeHitWindowDeliversOnlyWithAuthority )
{
    GameObjectManager     manager;
    RigidBody2DComponent* pNear = TestAnimationNotify2DInternal::spawnBox( manager, "Near", float2{ 1.0f, 0.0f }, float2{ 0.3f, 0.3f }, PhysicsBodyType::Static, "Flesh" );
    RigidBody2DComponent* pFar  = TestAnimationNotify2DInternal::spawnBox( manager, "Far", float2{ 2.5f, 0.0f }, float2{ 0.3f, 0.3f }, PhysicsBodyType::Static, "Flesh" );
    RigidBody2DComponent* pSelf = TestAnimationNotify2DInternal::spawnBox( manager, "Fighter", float2{ 0.0f, 0.0f }, float2{ 0.5f, 0.5f }, PhysicsBodyType::Kinematic, "Flesh" );
    SW_ASSERT_TRUE( pNear != nullptr && pFar != nullptr && pSelf != nullptr );
    MockHitRecorder2DComponent* pNearHits = pNear->getOwner()->addComponent<MockHitRecorder2DComponent>();
    MockHitRecorder2DComponent* pFarHits  = pFar->getOwner()->addComponent<MockHitRecorder2DComponent>();
    MockHitRecorder2DComponent* pSelfHits = pSelf->getOwner()->addComponent<MockHitRecorder2DComponent>();
    SW_ASSERT_TRUE( pNearHits != nullptr && pFarHits != nullptr && pSelfHits != nullptr );
    manager.beginPlay();
    manager.update( TestAnimationNotify2DInternal::kFrame ); // 바디를 만든다

    vector<float3> listBlade;
    MeleeHitWindowUtil::makeBlade( float3{ 0.0f, 0.0f, 0.0f }, float3{ 3.0f, 0.0f, 0.0f }, 3, listBlade );
    MeleeHitParams params;
    params._kind         = hashed_string( "Slash" );
    params._radius       = 0.1f;
    params._damage       = 7.0f;
    params._bIs2D        = true;
    GameObject& attacker = *pSelf->getOwner();

    vector<GameObjectHandle> listPredicted;
    vector<MeleeHit>         listPredictedHit;
    SW_EXPECT_EQUAL( 2u, MeleeHitWindowUtil::sweep( attacker, {}, listBlade, params, MeleeHitAuthority::Predicted, listPredicted, listPredictedHit ) );
    SW_EXPECT_EQUAL( size_t( 0 ), pNearHits->_listHit.size() );
    SW_EXPECT_EQUAL( size_t( 0 ), pFarHits->_listHit.size() );

    vector<GameObjectHandle> listHitTarget;
    vector<MeleeHit>         listHit;
    SW_EXPECT_EQUAL( 2u, MeleeHitWindowUtil::sweep( attacker, {}, listBlade, params, MeleeHitAuthority::Authority, listHitTarget, listHit ) );
    SW_ASSERT_EQUAL( size_t( 1 ), pNearHits->_listHit.size() );
    SW_ASSERT_EQUAL( size_t( 1 ), pFarHits->_listHit.size() );
    SW_EXPECT_TRUE( pFarHits->_listHit[0]._kind == hashed_string( "Slash" ) );
    SW_EXPECT_NEAR_EQUAL( 7.0f, pFarHits->_listHit[0]._damage, 1e-6f );
    SW_EXPECT_TRUE( pFarHits->_listHit[0]._pInstigator == &attacker );
    SW_EXPECT_EQUAL( size_t( 0 ), pSelfHits->_listHit.size() );

    // 같은 창으로 다시 쓸면 이미 맞힌 표적은 건너뛴다.
    SW_EXPECT_EQUAL( 0u, MeleeHitWindowUtil::sweep( attacker, listBlade, listBlade, params, MeleeHitAuthority::Authority, listHitTarget, listHit ) );
    SW_EXPECT_EQUAL( size_t( 1 ), pNearHits->_listHit.size() );
    manager.endPlay();
}

/**
 * @brief [AnimationNotify2DTest] 재생할 것이 바뀌었다는 표시(스프라이트가 같은 재생할 것을 다른 구간으로 다시 씀)가 오면 열린 구간을 닫는다
 */
SW_TEST_CASE( AnimationNotify2DTest, RestartedFrameClosesOpenStates )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Sprite" ) );
    pObject->addComponent<SceneComponent>();
    AnimationNotifyComponent* pNotify = pObject->addComponent<AnimationNotifyComponent>();
    SW_ASSERT_NOT_NULL( pNotify );
    shared_ptr<AnimationNotifyTable> table = make_shared<AnimationNotifyTable>();
    SW_ASSERT_TRUE( table->loadFromXMLText( R"(<AnimationNotifies><Notify name="Taunt" handler="GameplayEvent" event="Taunted"/></AnimationNotifies>)", "restart",
                                            AnimationNotifyHandlerRegistry::getDefault() ) );
    pNotify->setNotifyTable( table );

    SpriteClipPlayable                      playable;
    const vector<const IAnimationPlayable*> listActive{ &playable };
    AnimationFiredNotify                    fired;
    fired._name     = hashed_string( "Taunt" );
    fired._pSource  = &playable;
    fired._duration = 0.5f;
    fired._phase    = AnimationNotifyPhase::Begin;
    vector<AnimationFiredNotify> listFired{ fired };
    AnimationNotifyFrame         frame;
    frame._listFired          = vector_reference<const AnimationFiredNotify>{ listFired.data(), listFired.size() };
    frame._listActivePlayable = vector_reference<const IAnimationPlayable* const>{ listActive.data(), listActive.size() };
    pNotify->processFrame( frame );
    SW_EXPECT_EQUAL( 1u, pNotify->getActiveStateCount() );

    AnimationNotifyFrame restarted;
    restarted._listActivePlayable = frame._listActivePlayable; // 같은 객체가 아직 재생 중 — 표시가 없으면 끊긴 줄 모른다
    restarted._bRestarted         = SW_TRUE;
    pNotify->processFrame( restarted );
    SW_EXPECT_EQUAL( 0u, pNotify->getActiveStateCount() );
}

/**
 * @brief [AnimationNotify2DTest] 서버 장부는 권위 스윕만 적고, 클라이언트가 예측으로 본 맞음의 주장을 그 기록으로 확인한다 — 서버 시점에 비켜선 표적도 인정하고 지어낸 주장은 거절한다
 * @details 칼날 (0, 0) ~ (3, 0). `Near` 는 서버 스윕이 직접 맞힌다. `Dodger` 는 서버 시점에는 칼날 밖(y = 5)이지만 클라이언트가 보던 틱에는 (2.5, 0) 에 있었다 —
 *          지연 보상이 되감은 자리 (2.5, 0) 를 넘기면 인정하고 한 번만 전달한다. 그 밖: 되감은 자리가 칼날 밖이면 `OutOfBlade`, 휘두른 적 없는 프레임 · 때린 쪽이면 `NoSweep`.
 */
SW_TEST_CASE( AnimationNotify2DTest, MeleeHitLedgerConfirmsClaimsAgainstServerSweeps )
{
    GameObjectManager     manager;
    RigidBody2DComponent* pNear    = TestAnimationNotify2DInternal::spawnBox( manager, "Near", float2{ 1.0f, 0.0f }, float2{ 0.3f, 0.3f }, PhysicsBodyType::Static, "Flesh" );
    RigidBody2DComponent* pDodger  = TestAnimationNotify2DInternal::spawnBox( manager, "Dodger", float2{ 2.5f, 5.0f }, float2{ 0.3f, 0.3f }, PhysicsBodyType::Static, "Flesh" );
    RigidBody2DComponent* pSelf    = TestAnimationNotify2DInternal::spawnBox( manager, "Fighter", float2{ 0.0f, 0.0f }, float2{ 0.5f, 0.5f }, PhysicsBodyType::Kinematic, "Flesh" );
    RigidBody2DComponent* pBystand = TestAnimationNotify2DInternal::spawnBox( manager, "Bystander", float2{ 8.0f, 8.0f }, float2{ 0.3f, 0.3f }, PhysicsBodyType::Static, "Flesh" );
    SW_ASSERT_TRUE( pNear != nullptr && pDodger != nullptr && pSelf != nullptr && pBystand != nullptr );
    MockHitRecorder2DComponent* pNearHits   = pNear->getOwner()->addComponent<MockHitRecorder2DComponent>();
    MockHitRecorder2DComponent* pDodgerHits = pDodger->getOwner()->addComponent<MockHitRecorder2DComponent>();
    SW_ASSERT_TRUE( pNearHits != nullptr && pDodgerHits != nullptr );
    MeleeHitLedger::ensureFor( manager );
    manager.beginPlay();
    manager.update( TestAnimationNotify2DInternal::kFrame ); // 바디를 만든다
    MeleeHitLedger* pLedger = MeleeHitLedger::find( manager );
    SW_ASSERT_TRUE( pLedger != nullptr );

    vector<float3> listBlade;
    MeleeHitWindowUtil::makeBlade( float3{ 0.0f, 0.0f, 0.0f }, float3{ 3.0f, 0.0f, 0.0f }, 3, listBlade );
    MeleeHitParams params;
    params._kind         = hashed_string( "Slash" );
    params._radius       = 0.1f;
    params._damage       = 7.0f;
    params._bIs2D        = true;
    GameObject& attacker = *pSelf->getOwner();

    // 예측(클라이언트) 스윕은 적지 않는다.
    vector<GameObjectHandle> listPredicted;
    vector<MeleeHit>         listPredictedHit;
    SW_EXPECT_EQUAL( 1u, MeleeHitWindowUtil::sweep( attacker, {}, listBlade, params, MeleeHitAuthority::Predicted, listPredicted, listPredictedHit ) );
    SW_EXPECT_EQUAL( size_t( 0 ), pLedger->getRecordCount() );

    // 권위 스윕은 맞지 않은 것도 적는다.
    vector<GameObjectHandle> listHitTarget;
    vector<MeleeHit>         listHit;
    SW_EXPECT_EQUAL( 1u, MeleeHitWindowUtil::sweep( attacker, {}, listBlade, params, MeleeHitAuthority::Authority, listHitTarget, listHit ) );
    SW_EXPECT_EQUAL( size_t( 1 ), pLedger->getRecordCount() );
    SW_EXPECT_EQUAL( size_t( 1 ), pNearHits->_listHit.size() );
    SW_EXPECT_EQUAL( size_t( 0 ), pDodgerHits->_listHit.size() );

    MeleeHitClaim claim;
    claim._frame    = pLedger->getFrame();
    claim._targetID = 77;
    const float3 rewoundCenter{ 2.5f, 0.0f, 0.0f };
    const float3 serverCenter{ 2.5f, 5.0f, 0.0f };
    SW_EXPECT_TRUE( MeleeClaimVerdict::Confirmed == pLedger->verify( attacker.getObjectID(), claim, rewoundCenter, 0.3f, nullptr ) );
    SW_EXPECT_TRUE( MeleeClaimVerdict::OutOfBlade == pLedger->verify( attacker.getObjectID(), claim, serverCenter, 0.3f, nullptr ) );   // 서버 시점의 자리는 칼날 밖
    SW_EXPECT_TRUE( MeleeClaimVerdict::NoSweep == pLedger->verify( attacker.getObjectID() + 1, claim, rewoundCenter, 0.3f, nullptr ) ); // 휘두르지 않은 쪽
    MeleeHitClaim stale = claim;
    stale._frame += MeleeHitLedger::kClaimSlackFrames + 1;
    SW_EXPECT_TRUE( MeleeClaimVerdict::NoSweep == pLedger->verify( attacker.getObjectID(), stale, rewoundCenter, 0.3f, nullptr ) ); // 그 프레임 안팎에 휘두름이 없다

    // 인정된 주장은 기록된 피해로 한 번만 전달한다.
    GameObject& dodger = *pDodger->getOwner();
    SW_EXPECT_TRUE( MeleeClaimVerdict::Confirmed == pLedger->claimHit( attacker, dodger, claim, rewoundCenter, 0.3f, listHitTarget ) );
    SW_ASSERT_EQUAL( size_t( 1 ), pDodgerHits->_listHit.size() );
    SW_EXPECT_NEAR_EQUAL( 7.0f, pDodgerHits->_listHit[0]._damage, 1e-6f );
    SW_EXPECT_TRUE( pDodgerHits->_listHit[0]._kind == hashed_string( "Slash" ) );
    SW_EXPECT_TRUE( pDodgerHits->_listHit[0]._pInstigator == &attacker );
    SW_EXPECT_TRUE( MeleeClaimVerdict::Confirmed == pLedger->claimHit( attacker, dodger, claim, rewoundCenter, 0.3f, listHitTarget ) ); // 같은 창에서 두 번째는 전달하지 않는다
    SW_EXPECT_EQUAL( size_t( 1 ), pDodgerHits->_listHit.size() );
    // 칼날 밖의 표적을 대는 주장은 전달하지 않는다.
    GameObject& bystander = *pBystand->getOwner();
    SW_EXPECT_TRUE( MeleeClaimVerdict::OutOfBlade == pLedger->claimHit( attacker, bystander, claim, float3{ 8.0f, 8.0f, 0.0f }, 0.3f, listHitTarget ) );

    // 기록은 보관 프레임이 지나면 버려진다.
    for ( uint32 frameIndex = 0; frameIndex <= MeleeHitLedger::kKeepFrameCount; ++frameIndex )
    {
        manager.update( TestAnimationNotify2DInternal::kFrame );
    }
    SW_EXPECT_EQUAL( size_t( 0 ), pLedger->getRecordCount() );

    // 주장은 전송 형식을 왕복한다.
    BitWriter writer;
    claim.write( writer );
    BitReader     reader( writer.getBytes().data(), static_cast<int32>( writer.getBytes().size() ) );
    MeleeHitClaim decoded;
    SW_ASSERT_TRUE( MeleeHitClaim::read( reader, decoded ) );
    SW_EXPECT_EQUAL( claim._frame, decoded._frame );
    SW_EXPECT_EQUAL( claim._targetID, decoded._targetID );
    manager.endPlay();
}
