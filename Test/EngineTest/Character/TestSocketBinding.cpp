#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Socket/ResolvedSocketTable.h"
#include "Engine/Character/Socket/SocketBindingComponent.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Object/Component/Physics/SocketPhysicsBody.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// 소켓 부착 컴포넌트 — 붙음 · 떼기(애니메이션 · 물리) · 되돌아가기 블렌드 · 주인 바꾸기, 쉬는 단위의 비용, 2D 소켓.

namespace
{
    struct SocketBindingTestInternal
    {
        /** @brief 물리 바디 흉내 — 시작 변환에서 속도로 움직인다(시간은 시험이 넘김). */
        class FakePhysicsBody final : public ISocketPhysicsBody
        {
        public:
            void beginPhysics( const float4x4& worldTransform, const float3& linearVelocity ) override
            {
                _startWorld = worldTransform;
                _velocity   = linearVelocity;
                _elapsed    = 0.0f;
                _bRunning   = true;
                ++_beginCount;
            }
            void endPhysics() override
            {
                _bRunning = false;
                ++_endCount;
            }
            bool findBodyWorldTransform( float4x4& outWorldTransform ) const override
            {
                if ( _bRunning == false )
                    return false;
                outWorldTransform = _startWorld;
                outWorldTransform.setTranslation( _startWorld.getTranslation() + _velocity * _elapsed );
                return true;
            }
            void advance( float32 seconds ) { _elapsed += seconds; }

            float4x4 _startWorld{};
            float3   _velocity{};
            float32  _elapsed{ 0.0f };
            int32    _beginCount{ 0 };
            int32    _endCount{ 0 };
            bool     _bRunning{ false };
        };

        /** @brief 소켓을 가진 쪽(캐릭터)과 붙을 단위(무기) 한 벌. */
        struct Rig
        {
            GameObjectManager       _manager;
            GameObject*             _pHolder{ nullptr };
            GameObject*             _pUnit{ nullptr };
            SceneComponent*         _pHolderScene{ nullptr };
            SceneComponent*         _pUnitScene{ nullptr };
            SocketBindingComponent* _pBinding{ nullptr };

            Rig()
            {
                _pHolder      = _manager.createGameObject( hashed_string( "Holder" ) );
                _pUnit        = _manager.createGameObject( hashed_string( "Sword" ) );
                _pHolderScene = _pHolder->addComponent<SceneComponent>();
                _pUnitScene   = _pUnit->addComponent<SceneComponent>();
                _pBinding     = _pUnit->addComponent<SocketBindingComponent>();
                _pHolderScene->setLocalPosition( float3( 1.0f, 2.0f, 3.0f ) );
                _pHolderScene->setLocalRotation( float3( 0.0f, MathUtil::HalfPi, 0.0f ) );
                _manager.beginPlay();
            }
        };

        static float4x4 makeSocket() { return float4x4::createTranslation( 0.0f, 1.0f, 0.5f ); }

        static bool isNear( const float4x4& expected, const float4x4& actual, float32 tolerance )
        {
            for ( uint32 element = 0; element < 16; ++element )
            {
                if ( MathUtil::abs( expected.data()[element] - actual.data()[element] ) > tolerance )
                    return false;
            }
            return true;
        }

        static bool isNear( const float3& expected, const float3& actual, float32 tolerance ) { return float3::getDistance( expected, actual ) <= tolerance; }
    };
} // namespace

/**
 * @brief [SocketBindingTest] 붙으면 holder 를 따라가고 틱이 꺼져 있다(쉬는 단위는 비용 없음), 소켓 변환이 같으면 아무것도 안 한다
 */
SW_TEST_CASE( SocketBindingTest, BoundUnitFollowsHolderWithoutTicking )
{
    using Internal = SocketBindingTestInternal;
    Internal::Rig rig;
    SW_ASSERT_TRUE( rig._pBinding->bindToSocket( rig._pHolder, hashed_string( "HandR" ), Internal::makeSocket() ) );
    SW_EXPECT_TRUE( rig._pBinding->getState() == SocketBindingState::Bound );
    SW_EXPECT_FALSE( rig._pBinding->canEverTick() );
    SW_EXPECT_TRUE( rig._pUnitScene->getParent() == rig._pHolderScene );
    SW_EXPECT_TRUE( Internal::isNear( Internal::makeSocket() * rig._pHolderScene->getWorldMatrix(), rig._pUnitScene->getWorldMatrix(), 1.0e-4f ) );

    rig._pHolderScene->setLocalPosition( float3( -4.0f, 0.0f, 0.0f ) );
    rig._manager.tick( 0.016f );
    SW_EXPECT_TRUE( Internal::isNear( Internal::makeSocket() * rig._pHolderScene->getWorldMatrix(), rig._pUnitScene->getWorldMatrix(), 1.0e-4f ) );
    SW_EXPECT_FALSE( rig._pBinding->canEverTick() );

    // 본이 움직이면 애니메이션 시스템이 한 번 알린다.
    const float4x4 movedSocket = float4x4::createTranslation( 0.2f, 1.0f, 0.5f );
    rig._pBinding->updateSocketTransform( movedSocket );
    SW_EXPECT_TRUE( Internal::isNear( movedSocket * rig._pHolderScene->getWorldMatrix(), rig._pUnitScene->getWorldMatrix(), 1.0e-4f ) );
}

/**
 * @brief [SocketBindingTest] 떼어 애니메이션 — 뗀 직후의 월드는 붙어 있던 월드와 같고(튀지 않음) 이후 holder 를 따르지 않는다
 */
SW_TEST_CASE( SocketBindingTest, ReleaseKeepsBoundWorldTransform )
{
    using Internal = SocketBindingTestInternal;
    Internal::Rig rig;
    SW_ASSERT_TRUE( rig._pBinding->bindToSocket( rig._pHolder, hashed_string( "HandR" ), Internal::makeSocket() ) );
    const float4x4 boundWorld = rig._pUnitScene->getWorldMatrix();
    SW_EXPECT_FALSE( rig._pBinding->release( SocketReleaseMode::Physics ) ); // 바디가 없으면 물리로 뗄 수 없다
    SW_ASSERT_TRUE( rig._pBinding->release( SocketReleaseMode::Animated ) );
    SW_EXPECT_TRUE( rig._pBinding->getState() == SocketBindingState::ReleasedAnimated );
    SW_EXPECT_TRUE( rig._pUnitScene->getParent() == nullptr );
    SW_EXPECT_TRUE( Internal::isNear( boundWorld, rig._pUnitScene->getWorldMatrix(), 1.0e-4f ) );
    rig._manager.tick( 0.016f );
    SW_EXPECT_TRUE( Internal::isNear( boundWorld, rig._pUnitScene->getWorldMatrix(), 1.0e-4f ) );
    rig._pHolderScene->setLocalPosition( float3( 9.0f, 0.0f, 0.0f ) );
    SW_EXPECT_TRUE( Internal::isNear( boundWorld, rig._pUnitScene->getWorldMatrix(), 1.0e-4f ) );
    SW_EXPECT_FALSE( rig._pBinding->release( SocketReleaseMode::Animated ) ); // 이미 뗐다
}

/**
 * @brief [SocketBindingTest] 떼어 물리 — 바디가 붙어 있던 월드에서 시작하고, 첫 프레임은 그 자리, 이후 바디 변환을 읽는다
 */
SW_TEST_CASE( SocketBindingTest, PhysicsReleaseStartsBodyAtBoundTransform )
{
    using Internal = SocketBindingTestInternal;
    Internal::Rig             rig;
    Internal::FakePhysicsBody body;
    rig._pBinding->setPhysicsBody( rig._pUnitScene, &body );
    SW_ASSERT_TRUE( rig._pBinding->bindToSocket( rig._pHolder, hashed_string( "HandR" ), Internal::makeSocket() ) );
    SW_EXPECT_EQUAL( 1, body._endCount ); // 붙으면 바디는 손을 따른다(물리를 끈다)
    const float4x4 boundWorld = rig._pUnitScene->getWorldMatrix();
    SW_ASSERT_TRUE( rig._pBinding->release( SocketReleaseMode::Physics, float3( 0.0f, -2.0f, 0.0f ) ) );
    SW_EXPECT_EQUAL( 1, body._beginCount );
    SW_EXPECT_TRUE( Internal::isNear( boundWorld, body._startWorld, 1.0e-5f ) );
    SW_EXPECT_TRUE( rig._pBinding->canEverTick() );

    rig._manager.tick( 0.016f ); // 바디가 아직 움직이지 않았다 — 첫 프레임은 붙어 있던 자리
    SW_EXPECT_TRUE( Internal::isNear( boundWorld, rig._pUnitScene->getWorldMatrix(), 1.0e-4f ) );
    body.advance( 0.5f );
    rig._manager.tick( 0.016f );
    SW_EXPECT_TRUE( Internal::isNear( boundWorld.getTranslation() + float3( 0.0f, -1.0f, 0.0f ), rig._pUnitScene->getWorldPosition(), 1.0e-4f ) );

    // 되돌아가면 바디를 끝낸다.
    SW_ASSERT_TRUE( rig._pBinding->returnToSocket() );
    SW_EXPECT_EQUAL( 2, body._endCount );
}

/**
 * @brief [SocketBindingTest] 되돌아가기 — 지금 자리에서 출발해 곡선대로 섞고, 끝나면 붙고 틱이 꺼진다
 */
SW_TEST_CASE( SocketBindingTest, ReturnBlendsFromCurrentTransformToSocket )
{
    using Internal = SocketBindingTestInternal;
    Internal::Rig  rig;
    BlendCurveSpec blend;
    blend._curve    = BlendCurve::Linear;
    blend._duration = 1.0f;
    rig._pBinding->setReturnBlend( blend );
    SW_ASSERT_TRUE( rig._pBinding->bindToSocket( rig._pHolder, hashed_string( "Back" ), Internal::makeSocket() ) );
    SW_ASSERT_TRUE( rig._pBinding->release( SocketReleaseMode::Animated ) );
    rig._pUnitScene->setWorldPosition( float3( 5.0f, 0.0f, 0.0f ) );
    const float3 startPosition  = rig._pUnitScene->getWorldPosition();
    const float3 socketPosition = ( Internal::makeSocket() * rig._pHolderScene->getWorldMatrix() ).getTranslation();

    SW_ASSERT_TRUE( rig._pBinding->returnToSocket() );
    SW_EXPECT_TRUE( rig._pBinding->getState() == SocketBindingState::Returning );
    SW_EXPECT_TRUE( Internal::isNear( startPosition, rig._pUnitScene->getWorldPosition(), 1.0e-5f ) ); // 켠 순간 튀지 않는다
    rig._manager.tick( 0.5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, rig._pBinding->getReturnProgress(), 1.0e-5f );
    SW_EXPECT_TRUE( Internal::isNear( float3::lerp( startPosition, socketPosition, 0.5f ), rig._pUnitScene->getWorldPosition(), 1.0e-4f ) );
    rig._manager.tick( 0.6f );
    SW_EXPECT_TRUE( rig._pBinding->getState() == SocketBindingState::Bound );
    SW_EXPECT_TRUE( rig._pUnitScene->getParent() == rig._pHolderScene );
    SW_EXPECT_TRUE( Internal::isNear( socketPosition, rig._pUnitScene->getWorldPosition(), 1.0e-4f ) );
    rig._manager.tick( 0.016f ); // 틱 끄기는 틱 뒤로 미뤄진다
    SW_EXPECT_FALSE( rig._pBinding->canEverTick() );
}

/**
 * @brief [SocketBindingTest] 주인 바꾸기 — 다시 스폰하지 않고 줍기 오브젝트로 넘기고(바로), 다른 캐릭터로 넘긴다(블렌드)
 */
SW_TEST_CASE( SocketBindingTest, TransferHandsUnitToNewHolderWithoutRespawn )
{
    using Internal = SocketBindingTestInternal;
    Internal::Rig   rig;
    GameObject*     pPickup      = rig._manager.createGameObject( hashed_string( "Pickup" ) );
    SceneComponent* pPickupScene = pPickup->addComponent<SceneComponent>();
    pPickupScene->setLocalPosition( float3( 20.0f, 0.0f, 0.0f ) );
    SW_ASSERT_TRUE( rig._pBinding->bindToSocket( rig._pHolder, hashed_string( "HandR" ), Internal::makeSocket() ) );
    const uint64 unitId = rig._pUnit->getHandle().objectId();

    SW_ASSERT_TRUE( rig._pBinding->transferTo( pPickup, hashed_string( "Rest" ), float4x4::Identity, false ) );
    SW_EXPECT_TRUE( rig._pUnitScene->getParent() == pPickupScene );
    SW_EXPECT_TRUE( rig._pBinding->getHolder() == pPickup->getHandle() );
    SW_EXPECT_TRUE( rig._pBinding->getSocketName() == hashed_string( "Rest" ) );
    SW_EXPECT_TRUE( Internal::isNear( float3( 20.0f, 0.0f, 0.0f ), rig._pUnitScene->getWorldPosition(), 1.0e-4f ) );
    SW_EXPECT_EQUAL( unitId, rig._pUnit->getHandle().objectId() ); // 같은 오브젝트

    // 다시 캐릭터 손으로 — 지금 자리에서 블렌드.
    const float3 before = rig._pUnitScene->getWorldPosition();
    SW_ASSERT_TRUE( rig._pBinding->transferTo( rig._pHolder, hashed_string( "HandR" ), Internal::makeSocket(), true ) );
    SW_EXPECT_TRUE( rig._pBinding->getState() == SocketBindingState::Returning );
    SW_EXPECT_TRUE( Internal::isNear( before, rig._pUnitScene->getWorldPosition(), 1.0e-4f ) );
    rig._manager.tick( 1.0f );
    SW_EXPECT_TRUE( rig._pBinding->getState() == SocketBindingState::Bound );
    SW_EXPECT_TRUE( rig._pUnitScene->getParent() == rig._pHolderScene );
}

/**
 * @brief [Socket2DTest] 2D 본(Z 축 회전)의 소켓 — 같은 에셋 · 표 · 부착 컴포넌트가 평면 안에서 돈다
 */
SW_TEST_CASE( Socket2DTest, SocketOnSpriteBoneBindsInPlane )
{
    using Internal = SocketBindingTestInternal;
    CharacterBoneArray bones2D;
    const int32        root = bones2D.addBone( hashed_string( "root" ), -1, float4x4::Identity );
    (void)bones2D.addBone( hashed_string( "arm" ), root, float4x4::createRotationZ( MathUtil::HalfPi ) * float4x4::createTranslation( 1.0f, 0.0f, 0.0f ) );
    SocketKindTable kinds;
    kinds.addKind( hashed_string( "Attach" ) );
    SocketSet sockets;
    SW_ASSERT_TRUE( sockets.loadFromXmlText( "<SocketSet><Socket name='Tip' parent='arm' kind='Attach' translation='0.5 0 0'/></SocketSet>", "hero2d.sockets.xml", kinds,
                                             &bones2D ) );
    ResolvedSocketTable table;
    table.beginResolve();
    SW_ASSERT_TRUE( table.addUnit( hashed_string(), 0, sockets, bones2D, nullptr, nullptr ) );
    table.endResolve();
    float4x4 tipInUnit;
    SW_ASSERT_TRUE( table.computeUnitTransform( table.findSocket( hashed_string( "Tip" ) ), bones2D, tipInUnit ) );
    SW_EXPECT_TRUE( Internal::isNear( float3( 1.0f, 0.5f, 0.0f ), tipInUnit.getTranslation(), 1.0e-5f ) );

    // 2D 오브젝트 — 씬 컴포넌트가 X · Y 와 Z 축 회전만 쓴다.
    GameObjectManager       manager;
    GameObject*             pHero       = manager.createGameObject( hashed_string( "Hero2D" ) );
    GameObject*             pTorch      = manager.createGameObject( hashed_string( "Torch2D" ) );
    SceneComponent*         pHeroScene  = pHero->addComponent<SceneComponent>();
    SceneComponent*         pTorchScene = pTorch->addComponent<SceneComponent>();
    SocketBindingComponent* pBinding    = pTorch->addComponent<SocketBindingComponent>();
    pHeroScene->setLocalPosition( float3( 10.0f, 0.0f, 0.0f ) );
    pHeroScene->setLocalRotation( float3( 0.0f, 0.0f, MathUtil::HalfPi ) );
    manager.beginPlay();

    const SocketId tipId = table.findSocket( hashed_string( "Tip" ) );
    SW_ASSERT_TRUE( pBinding->bindToSocket( pHero, table.getSocketName( tipId ), tipInUnit ) );
    const float3 world = pTorchScene->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 0.0f, world._z, 1.0e-5f );
    SW_EXPECT_TRUE( Internal::isNear( ( tipInUnit * pHeroScene->getWorldMatrix() ).getTranslation(), world, 1.0e-4f ) );
    SW_EXPECT_TRUE( Internal::isNear( float3( 9.5f, 1.0f, 0.0f ), world, 1.0e-4f ) );

    // 떼도 평면 안 그 자리.
    SW_ASSERT_TRUE( pBinding->release( SocketReleaseMode::Animated ) );
    SW_EXPECT_TRUE( Internal::isNear( float3( 9.5f, 1.0f, 0.0f ), pTorchScene->getWorldPosition(), 1.0e-4f ) );
}
