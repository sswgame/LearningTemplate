#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Animation/Sprite/SpriteClipPlayable.h"
#include "Engine/Character/AnimNotify/AnimNotifyComponent.h"
#include "Engine/Character/AnimNotify/AnimNotifyTable.h"
#include "Engine/Character/Socket/SocketSetComponent.h"
#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/Physics/RigidBody2DComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceUtil.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// AnimNotify2DTest — 2D 도 같은 알림 디스패치: 스프라이트 클립 구간의 알림(구간 알림 포함), 스프라이트 애니메이터 → 받는 쪽(틱 뒤 게임 스레드),
// Box2D 바닥 재질의 발소리, 칼날 궤적이 Box2D 바디의 히트 존을 맞힌다(자기 바디는 건너뜀).

namespace sw
{
    /** @brief 맞음을 적는 컴포넌트입니다(2D 시험용). */
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
    struct TestAnimNotify2DInternal
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
 * @brief [AnimNotify2DTest] 스프라이트 클립의 구간이 알림(시각 · 길이)을 들고 파일을 왕복하며, 재생할 것(구간)이 그 알림 트랙을 낸다
 */
SW_TEST_CASE( AnimNotify2DTest, SpriteClipRangeCarriesNotifies )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    SpriteClipAsset clip;
    clip._listFrame.resize( 6 );
    SpriteClipAnimation attack;
    attack._name       = "Attack";
    attack._firstFrame = 2;
    attack._frameCount = 4;
    attack._listNotify.push_back( AnimNotifyEvent{ hashed_string( "Swing" ), 0.1f, 0.2f } );
    clip._listAnimation.push_back( attack );
    const string path = test::makeTempPath( "attack.sprite.json" );
    SW_ASSERT_TRUE( clip.saveToFile( path ) );
    SpriteClipAsset loaded;
    SW_ASSERT_TRUE( loaded.loadFromFile( path ) );
    SW_ASSERT_EQUAL( size_t( 1 ), loaded._listAnimation.size() );
    SW_ASSERT_EQUAL( size_t( 1 ), loaded._listAnimation[0]._listNotify.size() );
    SW_EXPECT_NEAR_EQUAL( 0.2f, loaded._listAnimation[0]._listNotify[0]._duration, 1e-6f );

    SpriteClipPlayable playable;
    playable.configure( &loaded, 2, 4, true, 0.1f );
    SW_ASSERT_NOT_NULL( playable.findNotifyTrack() );
    SW_EXPECT_TRUE( playable.findNotifyTrack()->getEvents()[0]._name == hashed_string( "Swing" ) );
    playable.configure( &loaded, 0, 2, true, 0.1f ); // 이름 붙은 구간이 아니다 — 알림 없음
    SW_EXPECT_TRUE( playable.findNotifyTrack() == nullptr );
}

/**
 * @brief [AnimNotify2DTest] 2D 오브젝트(스프라이트 애니메이터 + Box2D 바디)의 알림 — 발소리는 Box2D 바닥 재질, 칼날 구간은 지나간 Box2D 표적을 히트 존과 함께 한 번 맞히고 자기 바디는 건너뛴다
 * @details 스프라이트 클립 `Attack`(100 ms 프레임 넷, 반복): Step 0.05 초, Swing 0.1 ~ 0.3 초. 칼날 소켓은 루트 기준 (0.5, 0) ~ (1.5, 0). 공격자를 프레임마다 +X 로 옮겨
 *          칼날이 x = 3 의 표적을 지나게 한다.
 */
SW_TEST_CASE( AnimNotify2DTest, SpriteNotifiesDriveFootstepAndHitWindowIn2D )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
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
    attack._listNotify.push_back( AnimNotifyEvent{ hashed_string( "Step" ), 0.05f, 0.0f } );
    attack._listNotify.push_back( AnimNotifyEvent{ hashed_string( "Swing" ), 0.1f, 0.2f } );
    clip._listAnimation.push_back( attack );
    const string clipPath = test::makeTempPath( "fighter.sprite.json" );
    SW_ASSERT_TRUE( clip.saveToFile( clipPath ) );
    const string socketPath = test::makeTempPath( "fighter.sockets.xml" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( socketPath, R"(<SocketSet><Socket name="BladeA" translation="0.5 0 0"/><Socket name="BladeB" translation="1.5 0 0"/></SocketSet>)" ) );

    GameObjectManager     manager;
    RigidBody2DComponent* pFloor  = TestAnimNotify2DInternal::spawnBox( manager, "Floor", float2{ 0.0f, -0.5f }, float2{ 20.0f, 0.5f }, PhysicsBodyType::Static, "Stone" );
    RigidBody2DComponent* pTarget = TestAnimNotify2DInternal::spawnBox( manager, "Target", float2{ 3.0f, 0.0f }, float2{ 0.3f, 0.3f }, PhysicsBodyType::Static, "Flesh" );
    RigidBody2DComponent* pSelf   = TestAnimNotify2DInternal::spawnBox( manager, "Fighter", float2{ 0.0f, 0.0f }, float2{ 1.6f, 0.4f }, PhysicsBodyType::Kinematic, "Flesh" );
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
    AnimNotifyComponent*        pNotify     = pFighter->addComponent<AnimNotifyComponent>();
    SW_ASSERT_TRUE( pTargetHits != nullptr && pSelfHits != nullptr && pSprite != nullptr && pAnimator != nullptr && pSockets != nullptr && pNotify != nullptr );
    pSprite->setClipPath( clipPath );
    pSockets->setSocketSetPath( socketPath );
    shared_ptr<AnimNotifyTable> table = make_shared<AnimNotifyTable>();
    SW_ASSERT_TRUE( table->loadFromXMLText( R"(<AnimNotifies>
            <Notify name="Step" handler="Footstep" socket="Root" distance="0.6"/>
            <Notify name="Swing" handler="HitWindow" socketA="BladeA" socketB="BladeB" damage="5"/>
        </AnimNotifies>)",
                                            "fighter", AnimNotifyHandlerRegistry::getDefault() ) );
    pNotify->setNotifyTable( table );

    manager.beginPlay();
    SW_EXPECT_TRUE( pAnimator->getNotifyListener() != nullptr );
    bool bSawStep = false;
    for ( uint32 frameIndex = 0; frameIndex < 24; ++frameIndex )
    {
        // 칼날 구간(0.1 ~ 0.3 초) 동안 +X 로 움직인다 — 프레임마다 0.25 m.
        if ( frameIndex >= 7 && frameIndex <= 16 )
            pSelf->setWorldPosition( pSelf->getWorldPosition() + float3{ 0.25f, 0.0f, 0.0f } );
        manager.tick( TestAnimNotify2DInternal::kFrame );
        for ( const AnimNotifyAction& action : pNotify->getActions() )
        {
            if ( action._notify == hashed_string( "Step" ) )
            {
                bSawStep = true;
                SW_EXPECT_TRUE( action._detail == hashed_string( "Stone" ) );
                SW_EXPECT_EQUAL( pFloor->getOwner()->getObjectId(), action._targetObjectId );
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
 * @brief [AnimNotify2DTest] 재생할 것이 바뀌었다는 표시(스프라이트가 같은 재생할 것을 다른 구간으로 다시 씀)가 오면 열린 구간을 닫는다
 */
SW_TEST_CASE( AnimNotify2DTest, RestartedFrameClosesOpenStates )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Sprite" ) );
    pObject->addComponent<SceneComponent>();
    AnimNotifyComponent* pNotify = pObject->addComponent<AnimNotifyComponent>();
    SW_ASSERT_NOT_NULL( pNotify );
    shared_ptr<AnimNotifyTable> table = make_shared<AnimNotifyTable>();
    SW_ASSERT_TRUE( table->loadFromXMLText( R"(<AnimNotifies><Notify name="Taunt" handler="GameplayEvent" event="Taunted"/></AnimNotifies>)", "restart",
                                            AnimNotifyHandlerRegistry::getDefault() ) );
    pNotify->setNotifyTable( table );

    SpriteClipPlayable                 playable;
    const vector<const IAnimPlayable*> listActive{ &playable };
    AnimFiredNotify                    fired;
    fired._name     = hashed_string( "Taunt" );
    fired._pSource  = &playable;
    fired._duration = 0.5f;
    fired._phase    = AnimNotifyPhase::Begin;
    vector<AnimFiredNotify> listFired{ fired };
    AnimNotifyFrame         frame;
    frame._listFired          = vector_reference<const AnimFiredNotify>{ listFired.data(), listFired.size() };
    frame._listActivePlayable = vector_reference<const IAnimPlayable* const>{ listActive.data(), listActive.size() };
    pNotify->processFrame( frame );
    SW_EXPECT_EQUAL( 1u, pNotify->getActiveStateCount() );

    AnimNotifyFrame restarted;
    restarted._listActivePlayable = frame._listActivePlayable; // 같은 객체가 아직 재생 중 — 표시가 없으면 끊긴 줄 모른다
    restarted._bRestarted         = SW_TRUE;
    pNotify->processFrame( restarted );
    SW_EXPECT_EQUAL( 0u, pNotify->getActiveStateCount() );
}
