#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/FrameArenaAllocator.h"
#include "Core/String/hashed_string.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/Renderer/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/RenderFramePacket.h"
#include "Engine/Graphics/Renderer/Graph/RenderGraph.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassManager.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassResource.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineResource.h"
#include "Engine/Graphics/Renderer/Scene/GpuScene.h"
#include "Engine/Graphics/Upload/GpuUploadQueue.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Window/IWindow.h"

#include "TestFramework/TestFramework.h"

// MeshPrimitiveTest — MeshUtil 이 만드는 기본 도형의 위상 · 노멀 · UV. 디바이스 없음(nogpu).
/**
 * @brief [MeshPrimitiveTest] 같은 id 는 **같은 메시 객체**를 돌려주고, create 는 매번 새로 만드는지 검증
 *
 * @details 배치 키가 메시 **포인터**라, 씬에서 온 컴포넌트가 저마다 제 Mesh 를 만들면 같은 큐브
 *          8000 개가 배치 8000 개로 갈린다(GPU 정점 버퍼도 8000 벌). 실제로 그랬고, 벤치는 메시
 *          하나를 나눠 써서 배치가 2 개였기 때문에 **벤치에는 한 번도 안 보였다.**
 *
 *          고친 뒤 정적 8002 엔티티 씬에서 배치 8000 -> 6, 프레임 2469 -> 1462 us 였고,
 *          백엔드 사이 픽셀 불일치도 15~30% -> 0.1~0.8% 로 줄었다(버퍼 8000 벌이 백엔드마다
 *          다르게 무너지고 있었다).
 *
 *          `createPrimitive` 가 매번 새로 만드는 성질도 같이 지킨다 — 벤치가 그것으로 도형 변종을
 *          갈라 배치를 일부러 나눈다(`BenchScene`).
 */

SW_TEST_CASE( MeshPrimitiveTest, AcquireSharesOneMeshPerIdWhileCreateMakesNew )
{
    // 1. 같은 id -> 같은 객체.
    sw::shared_ptr<sw::Mesh> sharedA = sw::MeshUtil::acquirePrimitive( "Cube" );
    sw::shared_ptr<sw::Mesh> sharedB = sw::MeshUtil::acquirePrimitive( "Cube" );
    SW_ASSERT_TRUE( sharedA != nullptr );
    SW_EXPECT_TRUE( sharedA.get() == sharedB.get() );

    // 2. 별칭도 같은 자리다 — "Quad" 와 "Rect" 는 같은 기하이므로 배치가 갈리면 안 된다.
    sw::shared_ptr<sw::Mesh> quad = sw::MeshUtil::acquirePrimitive( "Quad" );
    sw::shared_ptr<sw::Mesh> rect = sw::MeshUtil::acquirePrimitive( "Rect" );
    SW_ASSERT_TRUE( quad != nullptr );
    SW_EXPECT_TRUE( quad.get() == rect.get() );
    SW_EXPECT_TRUE( quad.get() != sharedA.get() );

    // 3. 대소문자가 달라도 같은 자리다.
    SW_EXPECT_TRUE( sw::MeshUtil::acquirePrimitive( "cube" ).get() == sharedA.get() );

    // 4. create 는 **매번 새로** 만든다 — 벤치가 이 성질로 배치를 나눈다.
    sw::shared_ptr<sw::Mesh> freshA = sw::MeshUtil::createPrimitive( "Cube" );
    sw::shared_ptr<sw::Mesh> freshB = sw::MeshUtil::createPrimitive( "Cube" );
    SW_ASSERT_TRUE( freshA != nullptr && freshB != nullptr );
    SW_EXPECT_TRUE( freshA.get() != freshB.get() );
    SW_EXPECT_TRUE( freshA.get() != sharedA.get() );

    // 5. 공유본과 새로 만든 것의 기하는 같아야 한다.
    SW_EXPECT_EQUAL( freshA->getVertices().size(), sharedA->getVertices().size() );

    // 6. 모르는 id 는 둘 다 nullptr.
    SW_EXPECT_TRUE( sw::MeshUtil::acquirePrimitive( "NoSuchShape" ) == nullptr );
}

/**
 * @brief [MeshPrimitiveTest] 내장 도형이 닫혀 있고 바깥을 향하는지 (GPU 불필요).
 * @details 감김이 뒤집힌 메시는 **화면에서 그냥 사라진다**(후면 컬링). 그림으로는 "안 그려진다" 로만
 *          보여서 렌더러 버그로 오인하기 쉬우므로, 기하 자체를 CPU 에서 본다. 원점 중심 볼록 도형이면
 *          각 삼각형의 면 법선이 그 삼각형 중심과 같은 쪽을 향해야 한다(dot > 0).
 */

SW_TEST_CASE( MeshPrimitiveTest, PrimitivesAreClosedAndOutwardFacing )
{
    struct PrimitiveCase
    {
        const utf8* _pId;
        float32     _maxRadius; ///< 원점에서 가장 먼 정점까지의 허용 거리
    };
    // 큐브는 대각선이 가장 멀다(0.5 * sqrt(3)). 곡면은 반지름 0.5, 캡슐만 원통부 때문에 더 길다.
    const PrimitiveCase arrCase[] = {
        {    "Cube", 0.8661f},
        {  "Sphere", 0.5001f},
        {"Cylinder", 0.7072f},
        { "Capsule", 1.0001f},
        {    "Cone", 0.7072f},
    };

    for ( const PrimitiveCase& testCase : arrCase )
    {
        sw::shared_ptr<sw::Mesh> mesh = sw::MeshUtil::createPrimitive( testCase._pId );
        SW_EXPECT_TRUE_MSG( mesh != nullptr, testCase._pId );
        SW_ASSERT_TRUE( mesh != nullptr );

        const sw::vector<sw::RHIVertex>& listVertex = mesh->getVertices();
        SW_EXPECT_TRUE_MSG( listVertex.empty() == false, testCase._pId );
        SW_ASSERT_TRUE( listVertex.size() >= 3 );
        SW_EXPECT_TRUE_MSG( ( listVertex.size() % 3 ) == 0, "삼각형 목록인데 정점 수가 3의 배수가 아니다" );

        uint32 inwardCount{ 0 };
        uint32 degenerateCount{ 0 };
        for ( size_t base = 0; base + 2 < listVertex.size(); base += 3 )
        {
            auto toFloat3 = []( const sw::RHIVertex& vertex )
            { return sw::float3{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] }; };

            const sw::float3 a = toFloat3( listVertex[base + 0] );
            const sw::float3 b = toFloat3( listVertex[base + 1] );
            const sw::float3 c = toFloat3( listVertex[base + 2] );

            for ( const sw::float3& point : { a, b, c } )
            {
                SW_EXPECT_TRUE_MSG( point.getLength() <= testCase._maxRadius,
                                    "정점이 도형의 단위 크기를 벗어났다" );
            }

            const sw::float3 normal = ( b - a ).cross( c - a );
            if ( normal.getLengthSquared() <= sw::MathUtil::Epsilon )
            {
                ++degenerateCount;
                continue;
            }
            const sw::float3 centroid = ( a + b + c ) * ( 1.0f / 3.0f );
            if ( normal.dot( centroid ) <= 0.0f )
                ++inwardCount;
        }

        SW_EXPECT_TRUE_MSG( degenerateCount == 0, testCase._pId );
        SW_EXPECT_TRUE_MSG( inwardCount == 0, testCase._pId );
        if ( inwardCount != 0 || degenerateCount != 0 )
        {
            SW_LOG_ERROR( "[MeshPrimitiveTest] %# — 정점 %#, 안쪽 향함 %#, 면적 0 %#",
                          testCase._pId, static_cast<uint32>( listVertex.size() ), inwardCount, degenerateCount );
        }
    }
}

/**
 * @brief [MeshPrimitiveTest] 내장 도형의 **정점 노멀·UV** 가 쓸 만한 값인지 (GPU 불필요).
 * @details 노멀과 UV 는 오래 **셰이더가 지어내고** 있었다 — 노멀은 `DemoCubeNormal( 위치 )`, UV 는
 *          `localPos.xy * 0.5 + 0.5`. 둘 다 "원점 중심 박스형 단위 도형" 에만 맞는 가정이라 평면·구·
 *          원뿔은 조용히 틀린 빛을 받고 텍스처가 엉뚱하게 붙었다. 이제 정점 속성이므로 **생성기가
 *          제대로 채웠는지**가 유일한 실패 지점이다.
 * @note 바깥을 향하는지는 `normal · position >= 0` 으로 본다 — 원점 중심 볼록 도형에서만 성립하는
 *       판정이라 평면은 따로 본다(면이 원점을 지나므로 내적이 0 이다).
 */
SW_TEST_CASE( MeshPrimitiveTest, PrimitiveNormalsAndUvsAreUsable )
{
    const utf8* arrPrimitiveId[] = { "Cube", "Sphere", "Cylinder", "Capsule", "Cone" };

    for ( const utf8* pId : arrPrimitiveId )
    {
        sw::shared_ptr<sw::Mesh> mesh = sw::MeshUtil::createPrimitive( pId );
        SW_ASSERT_TRUE( mesh != nullptr );
        const sw::vector<sw::RHIVertex>& listVertex = mesh->getVertices();
        SW_ASSERT_TRUE( listVertex.size() >= 3 );

        uint32 badLengthCount = 0;
        uint32 inwardCount    = 0;
        uint32 badUvCount     = 0;
        for ( const sw::RHIVertex& vertex : listVertex )
        {
            const sw::float3 normal{ vertex._arrNormal[0], vertex._arrNormal[1], vertex._arrNormal[2] };
            const sw::float3 position{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] };

            // 정규화되어 있지 않으면 조명이 도형마다 다른 밝기를 받는다 — 셰이더가 다시 정규화해 주더라도
            // 0 벡터는 살릴 수 없다(그 정점만 까맣게 죽는다).
            if ( sw::MathUtil::abs( normal.getLength() - 1.0f ) > 0.001f )
                ++badLengthCount;
            // 안쪽을 향하는 노멀은 빛을 반대로 받는다 — 그림에서는 "저 면만 어둡다" 로만 보인다.
            if ( normal.dot( position ) < -0.001f )
                ++inwardCount;
            if ( vertex._arrUv[0] < -0.001f || vertex._arrUv[0] > 1.001f || vertex._arrUv[1] < -0.001f ||
                 vertex._arrUv[1] > 1.001f )
                ++badUvCount;
        }

        SW_EXPECT_TRUE_MSG( badLengthCount == 0, "정점 노멀이 정규화되어 있지 않다" );
        SW_EXPECT_TRUE_MSG( inwardCount == 0, "정점 노멀이 안쪽을 향한다" );
        SW_EXPECT_TRUE_MSG( badUvCount == 0, "UV 가 0..1 밖이다" );
        if ( badLengthCount != 0 || inwardCount != 0 || badUvCount != 0 )
        {
            SW_LOG_ERROR( "[MeshPrimitiveTest] %# — 노멀 길이 %#, 안쪽 %#, UV 범위 %#", pId, badLengthCount,
                          inwardCount, badUvCount );
        }

        // 곡면은 **면마다 노멀이 달라야** 한다 — 전부 같으면 평면 음영으로 되돌아간 것이다.
        if ( sw::StringUtil::equals( pId, "Sphere", true ) )
        {
            bool bFoundDifferent = false;
            for ( const sw::RHIVertex& vertex : listVertex )
            {
                if ( sw::MathUtil::abs( vertex._arrNormal[0] - listVertex[0]._arrNormal[0] ) > 0.01f )
                {
                    bFoundDifferent = true;
                    break;
                }
            }
            SW_EXPECT_TRUE_MSG( bFoundDifferent, "구의 노멀이 전부 같다 — 부드러운 음영이 아니라 평면 음영이다" );
        }
    }

    // 바닥 평면은 면이 원점을 지나므로 위 판정이 안 맞는다 — 노멀이 +Y 인지 직접 본다.
    // 이것이 틀리면 바닥이 **옆을 보는 것처럼** 칠해진다(정점 노멀이 없던 시절의 그 증상이다).
    sw::shared_ptr<sw::Mesh> plane = sw::MeshUtil::createPrimitive( "Plane" );
    SW_ASSERT_TRUE( plane != nullptr );
    const sw::vector<sw::RHIVertex>& listPlaneVertex = plane->getVertices();
    SW_ASSERT_FALSE( listPlaneVertex.empty() );
    uint32 notUpCount = 0;
    for ( const sw::RHIVertex& vertex : listPlaneVertex )
    {
        if ( vertex._arrNormal[1] < 0.999f )
            ++notUpCount;
    }
    SW_EXPECT_TRUE_MSG( notUpCount == 0, "바닥 평면의 노멀이 +Y 가 아니다" );
}
