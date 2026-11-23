// ClothWindTest — 씬의 바람(`WindComponent`)이 천을 민다: 바람 없는 씬은 곧게 걸리고, 바람이 있으면 바람 쪽으로 날리며, `_windDrag` 0 인 천은 바람을 받지 않는다. 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Wind/WindComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Cloth/ClothComponent.h"

using namespace sw;

namespace
{
    struct ClothWindTestUtil
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        /** @brief 아랫줄 가운데가 걸린 자리(원점)에서 +Z 로 벗어난 거리(m)입니다. */
        static float32 computeBottomZ( const ClothComponent& cloth )
        {
            const vector<float3>& listVertex = cloth.getVertices();
            const size_t          columns    = cloth.getColumnCount();
            if ( listVertex.size() < columns )
                return 0.0f;
            float32 sum = 0.0f;
            for ( size_t column = 0; column < columns; ++column )
                sum += listVertex[listVertex.size() - columns + column]._z;
            return sum / static_cast<float32>( columns );
        }

        /** @brief 원점에 건 천 하나를 +Z 풍속 @p windSpeed(0 이면 바람 컴포넌트 없음)에서 2 초 돌린 뒤 아랫줄의 +Z 거리입니다. */
        static float32 simulateCloth( float32 windSpeed, float32 windDrag )
        {
            GameObjectManager manager;
            GameObject*       pObject     = manager.createGameObject( hashed_string( "Curtain" ) );
            GameObject*       pWindObject = manager.createGameObject( hashed_string( "Wind" ) );
            ClothComponent*   pCloth      = pObject != nullptr ? pObject->addComponent<ClothComponent>() : nullptr;
            WindComponent*    pWind       = pWindObject != nullptr && windSpeed > 0.0f ? pWindObject->addComponent<WindComponent>() : nullptr;
            SW_EXPECT_TRUE( pCloth != nullptr );
            if ( pCloth == nullptr )
                return MathUtil::kMaxFloat;
            pCloth->setWindDrag( windDrag );
            if ( pWind != nullptr )
            {
                pWind->setDirection( MathUtil::kHalfPi );
                pWind->setSpeed( windSpeed );
            }
            manager.beginPlay();
            for ( uint32 frame = 0; frame < 120; ++frame )
                manager.update( kFrame );
            SW_EXPECT_TRUE( pCloth->isClothCreated() );
            return computeBottomZ( *pCloth );
        }
    };
} // namespace

/** @brief [ClothWindTest] 바람 없는 씬은 곧게 걸리고, 바람은 천을 바람 쪽으로 날리며, 항력 0 은 바람을 받지 않는다 */
SW_TEST_CASE( ClothWindTest, WindBlowsTheClothDownwind )
{
    SW_EXPECT_NEAR( 0.0f, ClothWindTestUtil::simulateCloth( 0.0f, 1.0f ), 0.01f );
    SW_EXPECT_TRUE( ClothWindTestUtil::simulateCloth( 8.0f, 1.0f ) > 0.1f );
    SW_EXPECT_NEAR( 0.0f, ClothWindTestUtil::simulateCloth( 8.0f, 0.0f ), 0.01f );
}

/** @brief [ClothWindTest] 풍속은 돌풍만큼 커지고 방향은 그대로다 — 돌풍 0 이면 풍속 그대로 */
SW_TEST_CASE( ClothWindTest, GustRaisesTheWindSpeed )
{
    WindSettings wind;
    wind._direction     = float2{ 0.0f, 1.0f };
    wind._speed         = 10.0f;
    wind._gustStrength  = 0.0f;
    wind._gustFrequency = 0.25f;
    const float3 steady = wind.computeVelocity( float3{}, 3.0f );
    SW_EXPECT_NEAR( 10.0f, steady._z, 1.0e-3f );
    SW_EXPECT_NEAR( 0.0f, steady._x, 1.0e-6f );
    wind._gustStrength = 0.5f;
    // 시각 1 초 · 0.25 Hz 는 사인의 꼭대기(1)라 돌풍이 다 찬다.
    SW_EXPECT_NEAR( 15.0f, wind.computeVelocity( float3{}, 1.0f )._z, 1.0e-3f );
}
