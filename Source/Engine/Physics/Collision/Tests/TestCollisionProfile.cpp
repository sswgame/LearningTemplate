// CollisionProfileTest — 충돌 채널 · 응답 · 프로필(DesignBacklog "충돌 채널 · 응답 · 프리셋"). 응답 규칙(약한 쪽 · 대칭), 데이터 검사, 배포 설정이 옛 레이어 표와 같은지,
// Jolt · Box2D 에서 막음은 막고 겹침은 밀지 않으며 무시는 아무것도 없는지, 트레이스 채널 질의 거르기. 디바이스 없음(nogpu) — 서버 단위(`Server --test`)에도 돈다.
#include "pch.h"

#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/CollisionLayers.h"
#include "Engine/Physics/Collision/PhysicsQuery.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

using namespace sw;

namespace
{
    struct TestCollisionProfileInternal
    {
        static constexpr float32 kStep = 1.0f / 60.0f;

        /**
         * @brief 시험 설정입니다 — 오브젝트 채널 Floor · Solid · Ghost · Mist, 트레이스 채널 Look(기본 막음) · Camera(기본 무시).
         *        프로필: Floor(Solid 막음 · Ghost 겹침 · Mist 막음), Solid(모두 막음 — 채널 기본), Ghost(모두 막음이라 적지만 Floor 쪽이 겹침이라 겹침), Mist(Floor 무시 → 무시),
         *        Probe(질의 꺼짐), Phantom(물리 꺼짐).
         */
        static const utf8* getSettingsText()
        {
            return R"(<PhysicsSettings _maxBodyCount="1024" _maxBodyPairCount="1024" _maxContactCount="1024" _tempAllocatorBytes="1048576" _maxDebrisBodyCount="64" _streamingFloorProbeDistance="1">
                <_listChannel>
                    <CollisionChannelDef _name="Floor" />
                    <CollisionChannelDef _name="Solid" />
                    <CollisionChannelDef _name="Ghost" />
                    <CollisionChannelDef _name="Mist" />
                    <CollisionChannelDef _name="Look" _kind="Trace" _defaultResponse="Block" />
                    <CollisionChannelDef _name="Camera" _kind="Trace" _defaultResponse="Ignore" />
                </_listChannel>
                <_listProfile>
                    <CollisionProfileDef _name="Floor" _objectChannel="Floor">
                        <_listResponse><CollisionResponseDef _channel="Ghost" _response="Overlap" /><CollisionResponseDef _channel="Camera" _response="Block" /></_listResponse>
                    </CollisionProfileDef>
                    <CollisionProfileDef _name="Solid" _objectChannel="Solid" />
                    <CollisionProfileDef _name="Ghost" _objectChannel="Ghost" />
                    <CollisionProfileDef _name="Mist" _objectChannel="Mist">
                        <_listResponse><CollisionResponseDef _channel="Floor" _response="Ignore" /><CollisionResponseDef _channel="Look" _response="Overlap" /></_listResponse>
                    </CollisionProfileDef>
                    <CollisionProfileDef _name="Probe" _objectChannel="Solid" _bQueryEnabled="false" />
                    <CollisionProfileDef _name="Phantom" _objectChannel="Solid" _bPhysicsEnabled="false" />
                </_listProfile>
                <_listMaterial><PhysicsMaterialDef _name="Default" /></_listMaterial>
                <_listGroundQueryLayer><item>Floor</item></_listGroundQueryLayer>
            </PhysicsSettings>)";
        }

        static uint8 findLayer( const PhysicsSettings& settings, const utf8* pName ) { return settings.findLayerIndex( hashed_string( pName ) ).value_or( uint8{ 255 } ); }

        static PhysicsBodyDesc3D makeBox3D( const float3& position, const float3& halfExtents, PhysicsBodyType type, uint8 layer )
        {
            PhysicsShapeDesc3D box;
            box._type        = PhysicsShapeType3D::Box;
            box._halfExtents = halfExtents;
            PhysicsBodyDesc3D desc;
            desc._listShape.push_back( box );
            desc._position = position;
            desc._type     = type;
            desc._layer    = layer;
            return desc;
        }

        static PhysicsBodyDesc2D makeBox2D( const float2& position, const float2& halfExtents, PhysicsBodyType type, uint8 layer )
        {
            PhysicsShapeDesc2D box;
            box._type        = PhysicsShapeType2D::Box;
            box._halfExtents = halfExtents;
            PhysicsBodyDesc2D desc;
            desc._listShape.push_back( box );
            desc._position = position;
            desc._type     = type;
            desc._layer    = layer;
            return desc;
        }

        /** @brief 이벤트 목록에서 바디 @p body 가 낀 시작 수입니다 — @p bOverlap 이 겹침이면 겹침만, 아니면 막는 접촉만. */
        template <typename TEvent>
        static uint32 countBegin( const vector<TEvent>& listEvent, PhysicsBodyHandle body, bool bOverlap )
        {
            uint32 count = 0;
            for ( const TEvent& event : listEvent )
            {
                const bool bInvolved = event._bodyA == body || event._bodyB == body;
                count += ( bInvolved && event._phase == PhysicsContactPhase::Begin && event.isOverlap() == bOverlap ) ? 1u : 0u;
            }
            return count;
        }
    };
} // namespace

/** @brief [CollisionProfileTest] 응답 표는 대칭이고 세 값을 든다 — 무시가 아닌 쌍만 서로 보고, 겹침 마스크는 그 부분집합이다. 약한 쪽은 무시 < 겹침 < 막음 */
SW_TEST_CASE( CollisionProfileTest, ResponseTableIsSymmetricAndWeakerWins )
{
    SW_EXPECT_TRUE( minResponse( CollisionResponse::Block, CollisionResponse::Overlap ) == CollisionResponse::Overlap );
    SW_EXPECT_TRUE( minResponse( CollisionResponse::Overlap, CollisionResponse::Ignore ) == CollisionResponse::Ignore );
    SW_EXPECT_TRUE( minResponse( CollisionResponse::Block, CollisionResponse::Block ) == CollisionResponse::Block );

    CollisionLayers layers;
    SW_EXPECT_TRUE( layers.getResponse( 2, 3 ) == CollisionResponse::Block ); // 기본은 막음
    layers.setResponse( 2, 3, CollisionResponse::Overlap );
    SW_EXPECT_TRUE( layers.getResponse( 3, 2 ) == CollisionResponse::Overlap );
    SW_EXPECT_TRUE( layers.shouldCollide( 2, 3 ) );
    SW_EXPECT_EQUAL( 1u << 3, layers.getOverlapMask( 2 ) );
    SW_EXPECT_EQUAL( 1u << 2, layers.getOverlapMask( 3 ) );
    layers.setResponse( 3, 2, CollisionResponse::Ignore );
    SW_EXPECT_FALSE( layers.shouldCollide( 2, 3 ) );
    SW_EXPECT_EQUAL( 0u, layers.getOverlapMask( 2 ) );
    layers.setLayerCollision( 2, 3, true );
    SW_EXPECT_TRUE( layers.getResponse( 2, 3 ) == CollisionResponse::Block );
    SW_EXPECT_TRUE( layers.getResponse( 40, 3 ) == CollisionResponse::Ignore ); // 범위 밖
}

/** @brief [CollisionProfileTest] 프로필 쌍은 서로의 오브젝트 채널에 준 응답 중 약한 쪽, 적지 않은 채널은 채널 기본 응답, 물리가 꺼진 프로필은 모두 무시. 트레이스 마스크는 막는 프로필만(질의 꺼짐 제외) */
SW_TEST_CASE( CollisionProfileTest, ProfilesResolveWeakerResponseAndTraceMasks )
{
    using Internal = TestCollisionProfileInternal;
    PhysicsSettings settings;
    SW_REQUIRE_TRUE( settings.loadFromXMLText( Internal::getSettingsText() ) );
    const CollisionLayers layers    = settings.makeCollisionLayers();
    const uint8           floor     = Internal::findLayer( settings, "Floor" );
    const uint8           solid     = Internal::findLayer( settings, "Solid" );
    const uint8           ghost     = Internal::findLayer( settings, "Ghost" );
    const uint8           ignored   = Internal::findLayer( settings, "Mist" );
    const uint8           probe     = Internal::findLayer( settings, "Probe" );
    const uint8           queryOnly = Internal::findLayer( settings, "Phantom" );
    SW_EXPECT_TRUE( layers.getResponse( floor, solid ) == CollisionResponse::Block );
    SW_EXPECT_TRUE( layers.getResponse( floor, ghost ) == CollisionResponse::Overlap ); // Ghost 는 막음이라 하지만 Floor 쪽이 겹침
    SW_EXPECT_TRUE( layers.getResponse( ghost, floor ) == CollisionResponse::Overlap );
    SW_EXPECT_TRUE( layers.getResponse( floor, ignored ) == CollisionResponse::Ignore ); // Mist 쪽이 무시
    SW_EXPECT_TRUE( layers.getResponse( solid, ignored ) == CollisionResponse::Block );
    SW_EXPECT_TRUE( layers.getResponse( queryOnly, solid ) == CollisionResponse::Ignore ); // 물리 꺼짐
    SW_EXPECT_TRUE( layers.getResponse( probe, solid ) == CollisionResponse::Block );      // 질의만 꺼짐

    const optional<uint32> lookMask = settings.makeTraceLayerMask( hashed_string( "Look" ) );
    SW_REQUIRE_TRUE( lookMask.has_value() );
    SW_EXPECT_TRUE( ( *lookMask & ( 1u << floor ) ) != 0 );
    SW_EXPECT_TRUE( ( *lookMask & ( 1u << ignored ) ) == 0 ); // 겹침은 광선을 막지 않는다
    SW_EXPECT_TRUE( ( *lookMask & ( 1u << probe ) ) == 0 );   // 질의 꺼짐
    SW_EXPECT_TRUE( ( *lookMask & ( 1u << queryOnly ) ) != 0 );
    const optional<uint32> cameraMask = settings.makeTraceLayerMask( hashed_string( "Camera" ) );
    SW_REQUIRE_TRUE( cameraMask.has_value() );
    SW_EXPECT_EQUAL( 1u << floor, *cameraMask ); // 기본 무시 — Floor 만 적었다

    SW_EXPECT_LOG( "PhysicsSettings.UnknownTraceChannel" );
    SW_EXPECT_FALSE( settings.makeTraceLayerMask( hashed_string( "Solid" ) ).has_value() ); // 오브젝트 채널은 트레이스가 아니다
}

/** @brief [CollisionProfileTest] 프로필의 오브젝트 채널이 트레이스 채널이거나 응답이 모르는 채널을 가리키면 읽기 오류다(폴백 없음) */
SW_TEST_CASE( CollisionProfileTest, BadProfileIsLoadError )
{
    SW_EXPECT_LOG( "PhysicsSettings.ProfileObjectChannel" );
    SW_EXPECT_LOG( "PhysicsSettings.PhysicsLayerCollidesUnknown" );
    PhysicsSettings settings;
    SW_EXPECT_FALSE( settings.loadFromXMLText( R"(<PhysicsSettings _maxBodyCount="1024" _maxBodyPairCount="1024" _maxContactCount="1024" _tempAllocatorBytes="65536" _maxDebrisBodyCount="64" _streamingFloorProbeDistance="1">
        <_listChannel><CollisionChannelDef _name="Solid" /><CollisionChannelDef _name="Look" _kind="Trace" /></_listChannel>
        <_listProfile>
            <CollisionProfileDef _name="Default" _objectChannel="Look" />
            <CollisionProfileDef _name="Other" _objectChannel="Solid"><_listResponse><CollisionResponseDef _channel="Nowhere" /></_listResponse></CollisionProfileDef>
        </_listProfile>
        <_listMaterial><PhysicsMaterialDef _name="Default" /></_listMaterial>
        <_listGroundQueryLayer><item>Default</item></_listGroundQueryLayer>
    </PhysicsSettings>)" ) );
}

/**
 * @brief [CollisionProfileTest] 배포 설정(engine/physics/physicssettings.xml)의 프로필은 옛 레이어 표(막음 · 무시 대칭 행렬)를 그대로 낸다 — 레이어 이름 · 번호로 적은 씬이 같은 동작이다
 * @details 옛 표: Default 는 모두와, Static 은 Character · Ragdoll · Debris · Projectile 과, Character 는 Character · Trigger 와, Ragdoll 은 Ragdoll · Debris · Projectile 과,
 *          Debris 는 Debris 와 부딪혔다(대칭). Trigger 쌍은 이제 겹침이다(트리거 바디는 센서라 동작은 같다). 일부러 바꾼 칸 둘: Projectile 은 Pawn(Character)에 막히고
 *          Trigger 와 겹친다(옛 표는 둘 다 무시 — Projectile 프로필을 쓰는 콘텐츠가 없어 옮긴 씬의 동작은 같다, 시나리오 collisionprofiles).
 */
SW_TEST_CASE( CollisionProfileTest, ShippedSettingsKeepTheOldLayerTable )
{
    const PhysicsSettings& settings  = engine::getSubsystem<PhysicsSystem>().getSettings();
    const CollisionLayers  layers    = settings.makeCollisionLayers();
    const utf8* const      arrName[] = { "Default", "Static", "Character", "Ragdoll", "Debris", "Trigger", "Projectile" };
    // 옛 표(행 · 열은 위 차례) — 1 은 부딪힘
    const uint8 arrOld[7][7] = {
        {1, 1, 1, 1, 1, 1, 1},
        {1, 0, 1, 1, 1, 0, 1},
        {1, 1, 1, 0, 0, 1, 1},
        {1, 1, 0, 1, 1, 0, 1},
        {1, 1, 0, 1, 1, 0, 0},
        {1, 0, 1, 0, 0, 0, 1},
        {1, 1, 1, 1, 0, 1, 0},
    };
    for ( uint32 row = 0; row < 7; ++row )
    {
        const optional<uint8> layerA = settings.findLayerIndex( hashed_string( arrName[row] ) );
        SW_REQUIRE_TRUE( layerA.has_value() );
        SW_EXPECT_EQUAL( row, static_cast<uint32>( *layerA ) ); // 차례가 옛 레이어 번호다(번호로 적은 2D 콜라이더가 같은 프로필을 고른다)
        for ( uint32 column = 0; column < 7; ++column )
        {
            const uint8 layerB = settings.findLayerIndex( hashed_string( arrName[column] ) ).value_or( uint8{ 255 } );
            SW_EXPECT_EQUAL( arrOld[row][column] == 1, layers.shouldCollide( *layerA, layerB ) );
        }
    }
    const uint8 trigger    = settings.findLayerIndex( hashed_string( "Trigger" ) ).value_or( uint8{ 255 } );
    const uint8 projectile = settings.findLayerIndex( hashed_string( "Projectile" ) ).value_or( uint8{ 255 } );
    const uint8 character  = settings.findLayerIndex( hashed_string( "Character" ) ).value_or( uint8{ 255 } );
    SW_EXPECT_TRUE( layers.getResponse( 0, trigger ) == CollisionResponse::Overlap );
    SW_EXPECT_TRUE( layers.getResponse( projectile, trigger ) == CollisionResponse::Overlap );
    SW_EXPECT_TRUE( layers.getResponse( projectile, character ) == CollisionResponse::Block );
    const optional<uint32> cameraMask = settings.makeTraceLayerMask( hashed_string( "Camera" ) );
    SW_REQUIRE_TRUE( cameraMask.has_value() );
    SW_EXPECT_EQUAL( ( 1u << 0 ) | ( 1u << 1 ), *cameraMask ); // 카메라는 세상(Default · Static)만 본다 — 캐릭터 · 래그돌 · 파편은 무시
}

/** @brief [CollisionProfileTest] Jolt: 바닥 위에 떨어진 상자 — 막음 프로필은 바닥에 서고(막는 접촉), 겹침 프로필은 지나가며 겹침 이벤트를 내고, 무시 프로필은 이벤트 없이 지나간다 */
SW_TEST_CASE( CollisionProfileTest, JoltBlockOverlapIgnore )
{
    using Internal = TestCollisionProfileInternal;
    PhysicsSettings settings;
    SW_REQUIRE_TRUE( settings.loadFromXMLText( Internal::getSettingsText() ) );
    const unique_ptr<IPhysicsScene3D> pScene = engine::getSubsystem<PhysicsSystem>().createScene3D( settings );
    SW_REQUIRE_NOT_NULL( pScene.get() );
    (void)pScene->createBody( Internal::makeBox3D( float3{ 0.0f, -0.5f, 0.0f }, float3{ 20.0f, 0.5f, 20.0f }, PhysicsBodyType::Static, Internal::findLayer( settings, "Floor" ) ) );
    const PhysicsBodyHandle solid      = pScene->createBody( Internal::makeBox3D( float3{ -4.0f, 1.0f, 0.0f }, float3{ 0.5f, 0.5f, 0.5f }, PhysicsBodyType::Dynamic, Internal::findLayer( settings, "Solid" ) ) );
    const PhysicsBodyHandle ghost      = pScene->createBody( Internal::makeBox3D( float3{ 0.0f, 1.0f, 0.0f }, float3{ 0.5f, 0.5f, 0.5f }, PhysicsBodyType::Dynamic, Internal::findLayer( settings, "Ghost" ) ) );
    const PhysicsBodyHandle ignored    = pScene->createBody( Internal::makeBox3D( float3{ 4.0f, 1.0f, 0.0f }, float3{ 0.5f, 0.5f, 0.5f }, PhysicsBodyType::Dynamic, Internal::findLayer( settings, "Mist" ) ) );
    uint32                  solidBlock = 0, ghostOverlap = 0, ghostBlock = 0, ignoredAny = 0;
    for ( uint32 step = 0; step < 90; ++step )
    {
        pScene->step( Internal::kStep );
        const vector<PhysicsContactEvent3D>& listEvent = pScene->getContactEvents();
        solidBlock += Internal::countBegin( listEvent, solid, false );
        ghostOverlap += Internal::countBegin( listEvent, ghost, true );
        ghostBlock += Internal::countBegin( listEvent, ghost, false );
        ignoredAny += Internal::countBegin( listEvent, ignored, false ) + Internal::countBegin( listEvent, ignored, true );
    }
    float3     position{};
    quaternion rotation{};
    SW_REQUIRE_TRUE( pScene->getBodyTransform( solid, position, rotation ) );
    SW_EXPECT_NEAR( 0.5f, position._y, 0.05f ); // 바닥에 섰다
    SW_REQUIRE_TRUE( pScene->getBodyTransform( ghost, position, rotation ) );
    SW_EXPECT_TRUE( position._y < -1.0f ); // 밀리지 않고 지나갔다
    SW_REQUIRE_TRUE( pScene->getBodyTransform( ignored, position, rotation ) );
    SW_EXPECT_TRUE( position._y < -1.0f );
    SW_EXPECT_EQUAL( 1u, solidBlock );
    SW_EXPECT_EQUAL( 1u, ghostOverlap );
    SW_EXPECT_EQUAL( 0u, ghostBlock );
    SW_EXPECT_EQUAL( 0u, ignoredAny );
}

/**
 * @brief [CollisionProfileTest] Box2D: 막음 프로필은 바닥에 서고, 겹침 · 무시 프로필은 밀리지 않고 지나간다. 막는 셰이프끼리의 겹침은 Box2D 가 이벤트를 내지 못한다(센서만 — 유니티 2D 와 같다)
 * @details 겹침 쌍을 마스크에서 빼지 않는다 — 트리거(센서)는 겹침 응답 상대를 본다(사용자 거름이 막는 셰이프끼리만 접촉을 뺀다).
 */
SW_TEST_CASE( CollisionProfileTest, Box2DBlockOverlapIgnore )
{
    using Internal = TestCollisionProfileInternal;
    PhysicsSettings settings;
    SW_REQUIRE_TRUE( settings.loadFromXMLText( Internal::getSettingsText() ) );
    const unique_ptr<IPhysicsScene2D> pScene = engine::getSubsystem<PhysicsSystem>().createScene2D( settings );
    SW_REQUIRE_NOT_NULL( pScene.get() );
    (void)pScene->createBody( Internal::makeBox2D( float2{ 0.0f, -0.5f }, float2{ 20.0f, 0.5f }, PhysicsBodyType::Static, Internal::findLayer( settings, "Floor" ) ) );
    PhysicsBodyDesc2D sensorDesc        = Internal::makeBox2D( float2{ 0.0f, -3.0f }, float2{ 20.0f, 1.0f }, PhysicsBodyType::Static, Internal::findLayer( settings, "Floor" ) );
    sensorDesc._bTrigger                = true; // 바닥 아래 트리거 — 겹침 상대(Ghost)를 본다
    const PhysicsBodyHandle sensor      = pScene->createBody( sensorDesc );
    const PhysicsBodyHandle solid       = pScene->createBody( Internal::makeBox2D( float2{ -4.0f, 1.0f }, float2{ 0.5f, 0.5f }, PhysicsBodyType::Dynamic, Internal::findLayer( settings, "Solid" ) ) );
    const PhysicsBodyHandle ghost       = pScene->createBody( Internal::makeBox2D( float2{ 0.0f, 1.0f }, float2{ 0.5f, 0.5f }, PhysicsBodyType::Dynamic, Internal::findLayer( settings, "Ghost" ) ) );
    const PhysicsBodyHandle ignored     = pScene->createBody( Internal::makeBox2D( float2{ 4.0f, 1.0f }, float2{ 0.5f, 0.5f }, PhysicsBodyType::Dynamic, Internal::findLayer( settings, "Mist" ) ) );
    uint32                  ghostSensor = 0, ignoredSensor = 0;
    for ( uint32 step = 0; step < 90; ++step )
    {
        pScene->step( Internal::kStep );
        for ( const PhysicsContactEvent2D& event : pScene->getContactEvents() )
        {
            const bool bSensorBegin = event._phase == PhysicsContactPhase::Begin && ( event._bodyA == sensor || event._bodyB == sensor );
            ghostSensor += ( bSensorBegin && ( event._bodyA == ghost || event._bodyB == ghost ) ) ? 1u : 0u;
            ignoredSensor += ( bSensorBegin && ( event._bodyA == ignored || event._bodyB == ignored ) ) ? 1u : 0u;
        }
    }
    float2  position{};
    float32 rotation = 0.0f;
    SW_REQUIRE_TRUE( pScene->getBodyTransform( solid, position, rotation ) );
    SW_EXPECT_NEAR( 0.5f, position._y, 0.05f );
    SW_REQUIRE_TRUE( pScene->getBodyTransform( ghost, position, rotation ) );
    SW_EXPECT_TRUE( position._y < -1.0f );
    SW_REQUIRE_TRUE( pScene->getBodyTransform( ignored, position, rotation ) );
    SW_EXPECT_TRUE( position._y < -1.0f );
    SW_EXPECT_EQUAL( 1u, ghostSensor );   // 트리거는 겹침 응답 상대를 본다
    SW_EXPECT_EQUAL( 0u, ignoredSensor ); // 무시 응답 상대는 보지 않는다
}

/** @brief [CollisionProfileTest] 트레이스 채널 질의: Camera 는 캐릭터(Pawn)를 무시하고 벽(Static)에 맞고, Visibility 는 앞의 캐릭터에 맞는다(배포 설정) */
SW_TEST_CASE( CollisionProfileTest, TraceChannelFiltersRaycast )
{
    GameObjectManager manager;
    const auto        addBox = [&manager]( const utf8* pName, const float3& position, const utf8* pProfile )
    {
        RigidBodyComponent* pBody = manager.createGameObject( hashed_string( pName ) )->addComponent<RigidBodyComponent>();
        PhysicsShapeDesc3D  box;
        box._type        = PhysicsShapeType3D::Box;
        box._halfExtents = float3{ 0.5f, 1.0f, 0.5f };
        pBody->setShape( box );
        pBody->setBodyType( PhysicsBodyType::Static );
        pBody->setLayer( hashed_string( pProfile ) );
        pBody->setLocalPosition( position );
        return pBody;
    };
    (void)addBox( "Hero", float3{ 0.0f, 0.0f, 0.0f }, "Character" );
    (void)addBox( "Wall", float3{ 5.0f, 0.0f, 0.0f }, "Static" );
    manager.beginPlay();
    manager.update( TestCollisionProfileInternal::kStep );
    ScenePhysics&          physics = manager.getSubsystem<ScenePhysics>();
    const IPhysicsScene3D* pScene  = physics.findScene3D();
    SW_REQUIRE_NOT_NULL( pScene );

    const optional<PhysicsQueryFilter> camera = physics.makeTraceFilter( hashed_string( "Camera" ) );
    SW_REQUIRE_TRUE( camera.has_value() );
    PhysicsCastHit3D hit;
    SW_REQUIRE_TRUE( pScene->raycast( float3{ -5.0f, 0.0f, 0.0f }, float3{ 1.0f, 0.0f, 0.0f }, 20.0f, *camera, hit ) );
    SW_EXPECT_NEAR( 9.5f, hit._distance, 0.01f ); // 캐릭터를 지나 벽 앞면(x 4.5)

    const optional<PhysicsQueryFilter> visibility = physics.makeTraceFilter( hashed_string( "Visibility" ) );
    SW_REQUIRE_TRUE( visibility.has_value() );
    SW_REQUIRE_TRUE( pScene->raycast( float3{ -5.0f, 0.0f, 0.0f }, float3{ 1.0f, 0.0f, 0.0f }, 20.0f, *visibility, hit ) );
    SW_EXPECT_NEAR( 4.5f, hit._distance, 0.01f ); // 캐릭터 앞면(x -0.5)

    SW_EXPECT_LOG( "PhysicsSettings.UnknownTraceChannel" );
    SW_EXPECT_FALSE( physics.makeTraceFilter( hashed_string( "Pawn" ) ).has_value() );
}
