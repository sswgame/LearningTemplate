#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/VFX/VFXParticleOrder.h"

// 입자 순서 목록(PS-1) — 뷰 깊이는 먼 것부터, 반대편 눈은 순서를 뒤집고, 같은 깊이는 번호 순, 나이 순서는 눈을 보지 않는다. 구간은 목록 끝에 붙는다.

using namespace sw;

namespace
{
    GPUParticle makeParticle( float32 z, float32 lifeRatio )
    {
        GPUParticle particle;
        particle._positionSize = float4{ 0.0f, 0.0f, z, 1.0f };
        particle._rotationLife = float4{ 0.0f, lifeRatio, 0.0f, 0.0f };
        return particle;
    }

    /** @brief z 축 위에 놓인 입자 넷(번호 0..3 의 z = 1, 4, 2, 3)입니다. 나이는 z 와 엇갈리게 둔다. */
    vector<GPUParticle> makeLine()
    {
        vector<GPUParticle> listParticle;
        listParticle.push_back( makeParticle( 1.0f, 0.5f ) );
        listParticle.push_back( makeParticle( 4.0f, 0.1f ) );
        listParticle.push_back( makeParticle( 2.0f, 0.9f ) );
        listParticle.push_back( makeParticle( 3.0f, 0.3f ) );
        return listParticle;
    }

    bool isOrder( const vector<uint32>& listOrder, const VFXParticleOrder& range, const vector<uint32>& listExpected )
    {
        if ( range._count != listExpected.size() || range._first + range._count > listOrder.size() )
            return false;
        for ( uint32 index = 0; index < range._count; ++index )
        {
            if ( listOrder[range._first + index] != listExpected[index] )
                return false;
        }
        return true;
    }
} // namespace

/** @brief [VFXParticleOrderTest] 눈이 -z 쪽에 있으면 z 가 큰 입자가 멀다 — 먼 것부터 그린다. 구간은 목록의 앞 내용 뒤에 붙는다. */
SW_TEST_CASE( VFXParticleOrderTest, ViewDepthDrawsFarthestFirst )
{
    const vector<GPUParticle> listParticle = makeLine();
    vector<uint32>            listOrder{ 99u }; // 앞 방출기의 구간
    VFXParticleSortScratch    scratch;          // 깊이 작업 버퍼(호출마다 다시 쓴다)
    const float3              eye{ 0.0f, 0.0f, -10.0f };
    const float3              axis{ 0.0f, 0.0f, 1.0f };
    const VFXParticleOrder    range = VFXParticleOrder::appendViewDepth( listParticle, float4x4::Identity, eye, axis, scratch, listOrder );
    SW_EXPECT_EQUAL( range._first, 1u );
    SW_EXPECT_TRUE( isOrder( listOrder, range, vector<uint32>{ 1u, 3u, 2u, 0u } ) );
    SW_EXPECT_EQUAL( listOrder[0], 99u );

    // 축이 없으면 눈까지의 거리로 잰다 — 같은 순서다.
    const VFXParticleOrder distanceRange = VFXParticleOrder::appendViewDepth( listParticle, float4x4::Identity, eye, float3{}, scratch, listOrder );
    SW_EXPECT_TRUE( isOrder( listOrder, distanceRange, vector<uint32>{ 1u, 3u, 2u, 0u } ) );
}

/** @brief [VFXParticleOrderTest] 180° 반대편 눈(PiP)은 순서를 뒤집는다 — 뷰마다 구간이 따로인 까닭이다. 방출기 행렬의 이동도 깊이에 든다. */
SW_TEST_CASE( VFXParticleOrderTest, OppositeEyeReversesOrder )
{
    const vector<GPUParticle> listParticle = makeLine();
    vector<uint32>            listOrder;
    VFXParticleSortScratch    scratch;
    const float4x4            world = float4x4::makeTranslation( float3{ 0.0f, 0.0f, 100.0f } );
    const VFXParticleOrder    front = VFXParticleOrder::appendViewDepth( listParticle, world, float3{ 0.0f, 0.0f, 90.0f }, float3{ 0.0f, 0.0f, 1.0f }, scratch, listOrder );
    const VFXParticleOrder    back  = VFXParticleOrder::appendViewDepth( listParticle, world, float3{ 0.0f, 0.0f, 110.0f }, float3{ 0.0f, 0.0f, -1.0f }, scratch, listOrder );
    SW_EXPECT_TRUE( isOrder( listOrder, front, vector<uint32>{ 1u, 3u, 2u, 0u } ) );
    SW_EXPECT_TRUE( isOrder( listOrder, back, vector<uint32>{ 0u, 2u, 3u, 1u } ) );
}

/** @brief [VFXParticleOrderTest] 같은 깊이는 입자 번호 순이다(안정) — 프레임마다 앞뒤가 바뀌어 깜빡이지 않는다. */
SW_TEST_CASE( VFXParticleOrderTest, EqualDepthKeepsIndexOrder )
{
    vector<GPUParticle> listParticle;
    listParticle.push_back( makeParticle( 2.0f, 0.0f ) );
    listParticle.push_back( makeParticle( 5.0f, 0.0f ) );
    listParticle.push_back( makeParticle( 2.0f, 0.0f ) );
    listParticle.push_back( makeParticle( 5.0f, 0.0f ) );
    listParticle.push_back( makeParticle( 2.0f, 0.0f ) );
    vector<uint32>         listOrder;
    VFXParticleSortScratch scratch;
    const VFXParticleOrder range = VFXParticleOrder::appendViewDepth( listParticle, float4x4::Identity, float3{ 0.0f, 0.0f, -10.0f }, float3{ 0.0f, 0.0f, 1.0f }, scratch, listOrder );
    SW_EXPECT_TRUE( isOrder( listOrder, range, vector<uint32>{ 1u, 3u, 0u, 2u, 4u } ) );
}

/** @brief [VFXParticleOrderTest] 나이 순서는 수명 비율로 정하고 눈을 보지 않는다 — OldestFirst 는 큰 비율부터, YoungestFirst 는 작은 비율부터. */
SW_TEST_CASE( VFXParticleOrderTest, AgeOrdersIgnoreEye )
{
    const vector<GPUParticle> listParticle = makeLine();
    vector<uint32>            listOrder;
    VFXParticleSortScratch    scratch;
    const VFXParticleOrder    oldest   = VFXParticleOrder::appendByAge( listParticle, true, scratch, listOrder );
    const VFXParticleOrder    youngest = VFXParticleOrder::appendByAge( listParticle, false, scratch, listOrder );
    SW_EXPECT_TRUE( isOrder( listOrder, oldest, vector<uint32>{ 2u, 0u, 3u, 1u } ) );
    SW_EXPECT_TRUE( isOrder( listOrder, youngest, vector<uint32>{ 1u, 3u, 0u, 2u } ) );
    SW_EXPECT_EQUAL( youngest._first, 4u );

    const VFXParticleOrder empty = VFXParticleOrder::appendByAge( vector<GPUParticle>{}, true, scratch, listOrder );
    SW_EXPECT_EQUAL( empty._count, 0u );
}

/**
 * @brief [VFXParticleOrderTest] 입자가 많을 때(기수 정렬)도 순서가 같은 규칙이다 — 먼 것부터, 같은 깊이는 번호 순, 축 뒤(음수 깊이)도 이어진다
 * @details 변이: 부호 비트를 뒤집지 않으면(음수 깊이가 양수 뒤로) 순서 단언이, 흩기가 안정이 아니면 같은 깊이의 번호 순이 진다.
 */
SW_TEST_CASE( VFXParticleOrderTest, ManyParticlesKeepTheSameRule )
{
    constexpr uint32    kParticleCount = 3000;
    vector<GPUParticle> listParticle;
    for ( uint32 index = 0; index < kParticleCount; ++index )
    {
        const uint32 scattered = ( index * 7919u ) % 600u; // 같은 깊이가 다섯씩
        listParticle.push_back( makeParticle( static_cast<float32>( scattered ) - 300.0f, static_cast<float32>( scattered ) / 600.0f ) );
    }
    vector<uint32>         listOrder{ 5u };
    VFXParticleSortScratch scratch;
    const VFXParticleOrder range = VFXParticleOrder::appendViewDepth( listParticle, float4x4::Identity, float3{}, float3{ 0.0f, 0.0f, 1.0f }, scratch, listOrder );
    SW_REQUIRE_EQUAL( kParticleCount, range._count );
    bool bOrdered = listOrder[0] == 5u;
    for ( uint32 index = 1; index < range._count; ++index )
    {
        const uint32  left       = listOrder[range._first + index - 1u];
        const uint32  right      = listOrder[range._first + index];
        const float32 leftDepth  = listParticle[left]._positionSize._z;
        const float32 rightDepth = listParticle[right]._positionSize._z;
        bOrdered                 = bOrdered && ( leftDepth > rightDepth || ( leftDepth == rightDepth && left < right ) );
    }
    SW_EXPECT_TRUE( bOrdered );
    const VFXParticleOrder youngest = VFXParticleOrder::appendByAge( listParticle, false, scratch, listOrder );
    bool                   bYoung   = true;
    for ( uint32 index = 1; index < youngest._count; ++index )
    {
        const uint32 left  = listOrder[youngest._first + index - 1u];
        const uint32 right = listOrder[youngest._first + index];
        bYoung             = bYoung && ( listParticle[left]._rotationLife._y < listParticle[right]._rotationLife._y || ( listParticle[left]._rotationLife._y == listParticle[right]._rotationLife._y && left < right ) );
    }
    SW_EXPECT_TRUE( bYoung );
}
