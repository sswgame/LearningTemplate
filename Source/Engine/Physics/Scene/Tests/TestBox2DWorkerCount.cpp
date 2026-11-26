// Box2DWorkerCountTest — 2D 씬을 전용 워커(`Box2DWorkerPool`)로 나눠 돌려도 결과가 한 스레드와 비트까지 같은지. 워커 대기열 잠금(스핀)을
// 일꾼 넷이 함께 두드리는 유일한 단위 테스트다(설정 기본값은 일꾼 하나라 다른 테스트는 워커를 띄우지 않는다). 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/String/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/PhysicsSystem.h"

using namespace sw;

namespace
{
    struct TestBox2DWorkerCountInternal
    {
        static constexpr float32 kStep      = 1.0f / 60.0f;
        static constexpr uint32  kStepCount = 120;
        static constexpr uint32  kColumn    = 16;
        static constexpr uint32  kRow       = 16;

        static const utf8* getSettingsText()
        {
            return R"(<PhysicsSettings _maxBodyCount="1024" _maxBodyPairCount="1024" _maxContactCount="1024" _tempAllocatorBytes="1048576" _maxDebrisBodyCount="64" _streamingFloorProbeDistance="1">
                <_listChannel><CollisionChannelDef _name="Solid" /></_listChannel>
                <_listProfile><CollisionProfileDef _name="Solid" _objectChannel="Solid" /></_listProfile>
                <_listMaterial><PhysicsMaterialDef _name="Default" /></_listMaterial>
                <_listGroundQueryLayer><item>Solid</item></_listGroundQueryLayer>
            </PhysicsSettings>)";
        }

        static PhysicsBodyDesc2D makeBox( const float2& position, const float2& halfExtents, PhysicsBodyType type, uint8 layer )
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

        /** @brief 상자 더미를 @p workerCount 일꾼으로 떨어뜨리고 끝난 자리(x, y, 회전)를 차례로 돌려줍니다. */
        static vector<float32> simulatePile( uint32 workerCount )
        {
            PhysicsSettings settings;
            const bool      bLoaded = settings.loadFromXMLText( getSettingsText() );
            SW_EXPECT_TRUE( bLoaded );
            if ( bLoaded == false )
                return {};
            settings._workerCount2D                  = workerCount;
            const uint8                       layer  = settings.findLayerIndex( hashed_string( "Solid" ) ).value_or( uint8{ 0 } );
            const unique_ptr<IPhysicsScene2D> pScene = engine::getSubsystem<PhysicsSystem>().createScene2D( settings );
            if ( pScene == nullptr )
            {
                SW_EXPECT_NOT_NULL( pScene.get() );
                return {};
            }

            (void)pScene->createBody( makeBox( float2{ 0.0f, -0.5f }, float2{ 40.0f, 0.5f }, PhysicsBodyType::Static, layer ) );
            vector<PhysicsBodyHandle> listBody;
            for ( uint32 row = 0; row < kRow; ++row )
            {
                for ( uint32 column = 0; column < kColumn; ++column )
                {
                    // 줄마다 반 칸 어긋나게 쌓아 접촉이 많이(솔버 블록이 여럿) 생기게 한다.
                    const float32 x = ( static_cast<float32>( column ) - kColumn * 0.5f ) * 1.05f + ( ( row & 1u ) != 0 ? 0.5f : 0.0f );
                    const float32 y = 0.5f + static_cast<float32>( row ) * 1.05f;
                    listBody.push_back( pScene->createBody( makeBox( float2{ x, y }, float2{ 0.5f, 0.5f }, PhysicsBodyType::Dynamic, layer ) ) );
                }
            }
            for ( uint32 step = 0; step < kStepCount; ++step )
            {
                pScene->step( kStep );
            }

            vector<float32> listResult;
            listResult.reserve( listBody.size() * 3 );
            for ( const PhysicsBodyHandle body : listBody )
            {
                float2  position{};
                float32 rotation = 0.0f;
                SW_EXPECT_TRUE( pScene->getBodyTransform( body, position, rotation ) );
                listResult.push_back( position._x );
                listResult.push_back( position._y );
                listResult.push_back( rotation );
            }
            return listResult;
        }
    };
} // namespace

/** @brief [Box2DWorkerCountTest] 일꾼 넷으로 돌린 상자 더미가 한 스레드로 돌린 것과 비트까지 같다(Box2D v3 의 결정성 + 워커 대기열이 조각을 잃거나 두 번 내지 않음) */
SW_TEST_CASE( Box2DWorkerCountTest, StepIsDeterministicAcrossWorkerCounts )
{
    using Internal                     = TestBox2DWorkerCountInternal;
    const vector<float32> listSingle   = Internal::simulatePile( 1 );
    const vector<float32> listParallel = Internal::simulatePile( 4 );
    SW_REQUIRE_EQUAL( static_cast<size_t>( Internal::kColumn * Internal::kRow * 3 ), listSingle.size() );
    SW_REQUIRE_EQUAL( listSingle.size(), listParallel.size() );
    uint32 mismatchCount = 0;
    for ( size_t index = 0; index < listSingle.size(); ++index )
    {
        mismatchCount += ( listSingle[index] != listParallel[index] ) ? 1u : 0u;
    }
    SW_EXPECT_EQUAL( 0u, mismatchCount );
}
