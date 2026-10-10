#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/FrameArenaAllocator.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/Upload/GpuUploadQueue.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/AABB.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/RenderFramePacket.h"
#include "Engine/Renderer/Graph/RenderGraph.h"
#include "Engine/Renderer/Pipeline/RenderPassAsset.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAssetCache.h"
#include "Engine/Renderer/Scene/GpuScene.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Window/IWindow.h"

#include "TestFramework/TestFramework.h"

// MeshPrimitiveTest — MeshUtil 이 만드는 기본 도형의 위상 · 노멀 · UV. 디바이스 없음(nogpu).
/**
 * @brief [MeshPrimitiveTest] 같은 id 는 **같은 메시 객체**를 돌려주고, create 는 매번 새로 만드는지 검증
 *
 * @details 배치 키가 메시 **포인터**라, 씬에서 온 컴포넌트가 저마다 제 Mesh 를 만들면 같은 큐브
 *          8000 개가 배치 8000 개로 갈린다(GPU 정점 버퍼도 8000 벌). 벤치는 메시 하나를 나눠 쓰므로
 *          **벤치로는 이것이 보이지 않는다.**
 *
 *          `createPrimitive` 가 매번 새로 만드는 성질도 같이 지킨다 — 벤치가 그것으로 도형 변종을
 *          갈라 배치를 일부러 나눈다(`BenchScene`).
 */
SW_TEST_CASE( MeshPrimitiveTest, AcquireSharesOneMeshPerIDWhileCreateMakesNew )
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
 * @brief [MeshPrimitiveTest] 메시는 자기 경계를 알고, 메시 컴포넌트의 경계(컬링)는 그것을 덮는다
 * @details `setVertices` 가 경계 반지름을 구하고, 컴포넌트는 적어 둔 값과 그것 중 큰 쪽을 쓴다. 모든 도형이 단위 상자의 반지름(0.866)을 쓰면
 *          그보다 큰 도형(캡슐 끝 · 평면)이 화면 가장자리에서 보이는데도 GPU 컬링에 잘린다.
 */
SW_TEST_CASE( MeshPrimitiveTest, MeshBoundsCoverEveryVertexOfEveryPrimitive )
{
    sw::GameObjectManager manager;
    for ( const utf8* pMeshID : { "Cube", "Sphere", "Cylinder", "Capsule", "Plane", "Quad", "Cone" } )
    {
        const sw::shared_ptr<sw::Mesh> mesh = sw::MeshUtil::acquirePrimitive( pMeshID );
        SW_ASSERT_NOT_NULL( mesh );
        float32 farthest = 0.0f;
        for ( const sw::RHIVertex& vertex : mesh->getVertices() )
        {
            const sw::float3 position( vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] );
            farthest = sw::MathUtil::max( farthest, position.getLength() );
        }
        SW_EXPECT_NEAR_EQUAL( farthest, mesh->getBoundingRadius(), 1e-4f );

        sw::GameObject*    pObj  = manager.createGameObject( sw::hashed_string( pMeshID ) );
        sw::MeshComponent* pMesh = pObj->addComponent<sw::MeshComponent>();
        SW_ASSERT_NOT_NULL( pMesh );
        pMesh->setMesh( mesh );
        SW_EXPECT_TRUE_MSG( pMesh->getBoundsRadius() >= farthest - 1e-4f, pMeshID );
        sw::float3 center{};
        float32    worldRadius{ 0.0f };
        SW_ASSERT_TRUE( pMesh->getWorldBounds( center, worldRadius ) );
        SW_EXPECT_TRUE_MSG( worldRadius >= farthest - 1e-4f, pMeshID );
    }
}

/**
 * @brief [MeshPrimitiveTest] 오브젝트의 월드 상자는 메시의 실제 크기와 부모의 회전 · 스케일을 받는다
 * @details 크기는 `GameObject::getWorldBox`(컴포넌트마다 `getWorldBox`) 하나다. "메시면 로컬 스케일 × 단위 상자" 로 셈하면 두께 없는 평면이
 *          반 칸 뜨고, 부모가 키운 바닥은 부모 스케일만큼 작게 잡힌다.
 */
SW_TEST_CASE( MeshPrimitiveTest, WorldBoxFollowsTheMeshAndItsParents )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pParent   = manager.createGameObject( sw::hashed_string( "Stage" ) );
    sw::SceneComponent*   pParentSc = pParent->addComponent<sw::SceneComponent>();
    pParentSc->setLocalScale( sw::float3( 2.0f, 2.0f, 2.0f ) );
    pParentSc->setLocalRotation( sw::float3( 0.0f, sw::MathUtil::kHalfPi, 0.0f ) );

    sw::GameObject*    pFloor = manager.createGameObject( sw::hashed_string( "Floor" ) );
    sw::MeshComponent* pMesh  = pFloor->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    const sw::shared_ptr<sw::Mesh> plane = sw::MeshUtil::acquirePrimitive( "Plane" );
    SW_ASSERT_NOT_NULL( plane );
    pMesh->setMesh( plane );
    pMesh->setLocalScale( sw::float3( 1.0f, 1.0f, 3.0f ) );
    SW_ASSERT_TRUE( pFloor->attachToParent( pParent ) );
    manager.flushSceneTransforms();

    sw::AABB box{};
    SW_ASSERT_TRUE( pFloor->getWorldBox( box ) );
    // 평면은 두께가 없다 — 위아래가 같은 높이다.
    SW_EXPECT_NEAR_EQUAL( 0.0f, box._max._y - box._min._y, 1e-4f );
    // 로컬 Z(×3)가 부모의 90° 요로 월드 X 가 되고, 부모 스케일 2 를 받는다.
    const float32 planeHalfZ = ( plane->getLocalBoundsMax()._z - plane->getLocalBoundsMin()._z ) * 0.5f;
    SW_EXPECT_NEAR_EQUAL( planeHalfZ * 3.0f * 2.0f, ( box._max._x - box._min._x ) * 0.5f, 1e-3f );

    // 크기 없는 것뿐이면 상자가 없다.
    sw::GameObject* pEmpty = manager.createGameObject( sw::hashed_string( "Empty" ) );
    SW_ASSERT_NOT_NULL( pEmpty->addComponent<sw::SceneComponent>() );
    SW_EXPECT_FALSE( pEmpty->getWorldBox( box ) );
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
        const utf8* _pID;
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
        sw::shared_ptr<sw::Mesh> mesh = sw::MeshUtil::createPrimitive( testCase._pID );
        SW_EXPECT_TRUE_MSG( mesh != nullptr, testCase._pID );
        SW_ASSERT_TRUE( mesh != nullptr );

        const sw::vector<sw::RHIVertex>& listVertex = mesh->getVertices();
        SW_EXPECT_TRUE_MSG( listVertex.empty() == false, testCase._pID );
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
            if ( normal.getLengthSquared() <= sw::MathUtil::kEpsilon )
            {
                ++degenerateCount;
                continue;
            }
            const sw::float3 centroid = ( a + b + c ) * ( 1.0f / 3.0f );
            if ( normal.dot( centroid ) <= 0.0f )
                ++inwardCount;
        }

        SW_EXPECT_TRUE_MSG( degenerateCount == 0, testCase._pID );
        SW_EXPECT_TRUE_MSG( inwardCount == 0, testCase._pID );
        if ( inwardCount != 0 || degenerateCount != 0 )
        {
            SW_LOG_ERROR( "[MeshPrimitiveTest] %# — 정점 %#, 안쪽 향함 %#, 면적 0 %#",
                          testCase._pID, static_cast<uint32>( listVertex.size() ), inwardCount, degenerateCount );
        }
    }
}

/**
 * @brief [MeshPrimitiveTest] 내장 도형의 **정점 노멀·UV** 가 쓸 만한 값인지 (GPU 불필요).
 * @details 노멀과 UV 는 정점 속성이므로 **생성기가 제대로 채웠는지**가 유일한 실패 지점이다. 셰이더가 위치로 지어내면
 *          "원점 중심 박스형 단위 도형" 에만 맞아 평면·구·원뿔은 조용히 틀린 빛을 받고 텍스처가 엉뚱하게 붙는다.
 * @note 바깥을 향하는지는 `normal · position >= 0` 으로 본다 — 원점 중심 볼록 도형에서만 성립하는
 *       판정이라 평면은 따로 본다(면이 원점을 지나므로 내적이 0 이다).
 */
SW_TEST_CASE( MeshPrimitiveTest, PrimitiveNormalsAndUvsAreUsable )
{
    const utf8* arrPrimitiveID[] = { "Cube", "Sphere", "Cylinder", "Capsule", "Cone" };

    for ( const utf8* pID : arrPrimitiveID )
    {
        sw::shared_ptr<sw::Mesh> mesh = sw::MeshUtil::createPrimitive( pID );
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
            SW_LOG_ERROR( "[MeshPrimitiveTest] %# — 노멀 길이 %#, 안쪽 %#, UV 범위 %#", pID, badLengthCount,
                          inwardCount, badUvCount );
        }

        // 곡면은 **면마다 노멀이 달라야** 한다 — 전부 같으면 평면 음영으로 되돌아간 것이다.
        if ( sw::StringUtil::equals( pID, "Sphere", true ) )
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

/**
 * @brief [MeshPrimitiveTest] 스프라이트 사각형은 양면이고, 어느 면이든 그 면을 보는 카메라의 화면 오른쪽으로 u 가 는다(글자가 뒤집히지 않는다)
 * @details 3D 쿼드(+Z 한 면, u 가 +X 로 는다)를 쓰면 그 면은 -Z 를 보는 카메라에서만 보이는데 그 카메라의 화면 오른쪽은 -X 라 모든 스프라이트가
 *          좌우로 뒤집히고(데미지 숫자 "123" 이 거울 글자가 된다), +Z 를 보는 2D 카메라에서는 후면 컬링으로 사라진다.
 *          카메라는 +Z 를 볼 때 화면 오른쪽이 +X 이고(왼손 좌표계, `CameraComponent::getViewMatrix`), -Z 를 볼 때는 -X 다. 면의 노멀이 -Z 면
 *          +Z 를 보는 카메라가 그 면을 본다. 그래서 노멀 -Z 면은 u 가 +X 로, 노멀 +Z 면은 u 가 -X 로 늘어야 한다. v 는 두 면 모두 위가 0 이다.
 *          감김은 엔진의 앞면 규약((b - a) x (c - a) 가 노멀)을 따라야 컬링이 맞는 면을 남긴다.
 */
SW_TEST_CASE( MeshPrimitiveTest, SpriteQuadReadsTheSameFromBothSides )
{
    sw::shared_ptr<sw::Mesh> sprite = sw::MeshUtil::createPrimitive( "Sprite" );
    SW_ASSERT_NOT_NULL( sprite.get() );
    SW_EXPECT_TRUE( sw::MeshUtil::acquirePrimitive( "sprite" ) == sw::MeshUtil::acquirePrimitive( "Sprite" ) );
    const sw::vector<sw::RHIVertex>& listVertex = sprite->getVertices();
    SW_ASSERT_EQUAL( 12u, static_cast<uint32>( listVertex.size() ) );

    uint32 frontCount{ 0 };
    uint32 backCount{ 0 };
    for ( size_t base = 0; base + 2 < listVertex.size(); base += 3 )
    {
        const sw::RHIVertex& vertexA = listVertex[base + 0];
        const sw::RHIVertex& vertexB = listVertex[base + 1];
        const sw::RHIVertex& vertexC = listVertex[base + 2];
        const sw::float3     positionA{ vertexA._arrPosition[0], vertexA._arrPosition[1], vertexA._arrPosition[2] };
        const sw::float3     positionB{ vertexB._arrPosition[0], vertexB._arrPosition[1], vertexB._arrPosition[2] };
        const sw::float3     positionC{ vertexC._arrPosition[0], vertexC._arrPosition[1], vertexC._arrPosition[2] };
        const sw::float3     faceNormal = ( positionB - positionA ).cross( positionC - positionA );
        // 감김의 노멀과 정점 노멀이 같은 쪽이다 — 컬링이 남기는 면이 정점 노멀이 말하는 면이다.
        SW_EXPECT_TRUE( faceNormal._z * vertexA._arrNormal[2] > 0.0f );
        const bool bFront = vertexA._arrNormal[2] < 0.0f;
        if ( bFront )
            ++frontCount;
        else
            ++backCount;

        for ( const sw::RHIVertex* pVertex : { &vertexA, &vertexB, &vertexC } )
        {
            // 화면 오른쪽: 노멀 -Z 면(+Z 를 보는 카메라)은 +X, 노멀 +Z 면(-Z 를 보는 카메라)은 -X.
            const float32 screenRight = bFront ? pVertex->_arrPosition[0] : -pVertex->_arrPosition[0];
            SW_EXPECT_NEAR_EQUAL( screenRight + 0.5f, pVertex->_arrUv[0], 1e-6f );
            SW_EXPECT_NEAR_EQUAL( 0.5f - pVertex->_arrPosition[1], pVertex->_arrUv[1], 1e-6f );
        }
    }
    SW_EXPECT_EQUAL( 2u, frontCount );
    SW_EXPECT_EQUAL( 2u, backCount );
}

/**
 * @brief [MeshPrimitiveTest] 이름으로 받는 내장 도형(게임 · 씬 · 에디터)은 정점 색이 흰색이고, 검증 색은 따로 고를 때만 온다
 * @details 셰이더는 정점 색에 머티리얼 색을 곱한다. 생성기의 검증 색(큐브의 면별 색 · 곡면의 노멀 음영 · 평면의 바둑판)이 게임으로 새면
 *          회색으로 칠한 벽이 면마다 초록 · 주황으로 그려진다(Shooter3D 의 벽이 그랬다).
 */
SW_TEST_CASE( MeshPrimitiveTest, NamedPrimitivesAreWhiteUnlessDiagnosticIsAsked )
{
    const utf8* const arrID[] = { "Cube", "Quad", "Sprite", "Plane", "Sphere", "Cylinder", "Capsule", "Cone" };
    for ( const utf8* pID : arrID )
    {
        const sw::shared_ptr<sw::Mesh> acquired = sw::MeshUtil::acquirePrimitive( pID );
        const sw::shared_ptr<sw::Mesh> created  = sw::MeshUtil::createPrimitive( pID );
        SW_ASSERT_NOT_NULL( acquired.get() );
        SW_ASSERT_NOT_NULL( created.get() );
        for ( const sw::Mesh* pMesh : { acquired.get(), created.get() } )
        {
            bool bAllWhite = true;
            for ( const sw::RHIVertex& vertex : pMesh->getVertices() )
            {
                bAllWhite = bAllWhite && vertex._arrColor[0] == 1.0f && vertex._arrColor[1] == 1.0f && vertex._arrColor[2] == 1.0f && vertex._arrColor[3] == 1.0f;
            }
            SW_EXPECT_TRUE_MSG( bAllWhite, ( sw::string( "vertex colors are not white: " ) + pID ).c_str() );
        }
    }

    // 검증 색을 고르면 큐브의 면이 서로 다른 색이다(벤치 · 렌더 시험이 면을 가려 본다).
    const sw::shared_ptr<sw::Mesh> diagnostic = sw::MeshUtil::createPrimitive( "Cube", sw::PrimitiveVertexColor::Diagnostic );
    SW_ASSERT_NOT_NULL( diagnostic.get() );
    const sw::vector<sw::RHIVertex>& listVertex = diagnostic->getVertices();
    SW_ASSERT_TRUE( listVertex.size() >= 12u );
    SW_EXPECT_TRUE( listVertex[0]._arrColor[0] != listVertex[6]._arrColor[0] || listVertex[0]._arrColor[1] != listVertex[6]._arrColor[1] ||
                    listVertex[0]._arrColor[2] != listVertex[6]._arrColor[2] );
}
