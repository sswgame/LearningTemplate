// LensFlareTest — 렌즈 플레어 스프라이트의 투영 · 가장자리 감쇠 · 축 위 자리 · 가림 몫과, 빛 하나가 원소 수만큼의 스프라이트로 펼쳐지는지(nogpu).
// 뷰-투영은 단위 행렬이다 — 월드 xy 가 곧 NDC 이고 z 가 깊이, w 는 1(점) · 0(방향)이다. GPU 의 가림(lens_flare.hlsl)은 같은 식의 C++ 짝으로 본다.
#include "pch.h"

#include "Core/Math/MatrixMath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Lighting/LensFlareSpriteBuffer.h"

using namespace sw;

namespace
{
    struct LensFlareTestUtil
    {
        /** @brief 원소 둘(빛 자리의 빛무리 · 가운데 너머 고스트)인 플레어입니다. */
        static shared_ptr<const LensFlareAsset> makeAsset( float32 edgeFade )
        {
            shared_ptr<LensFlareAsset> pAsset = make_shared<LensFlareAsset>();
            LensFlareElement           glow;
            glow._shape        = LensFlareShape::Glow;
            glow._axisPosition = 0.0f;
            glow._size         = 0.1f;
            glow._color        = float3{ 1.0f, 1.0f, 1.0f };
            LensFlareElement ghost;
            ghost._shape        = LensFlareShape::Disc;
            ghost._axisPosition = 1.5f;
            ghost._size         = 0.05f;
            ghost._color        = float3{ 0.5f, 0.25f, 1.0f };
            pAsset->_listElement.push_back( glow );
            pAsset->_listElement.push_back( ghost );
            pAsset->_edgeFade        = edgeFade;
            pAsset->_occlusionRadius = 0.02f;
            return pAsset;
        }

        static LensFlareRequest makePointRequest( const float3& position )
        {
            LensFlareRequest request;
            request._position = position;
            request._color    = float3{ 2.0f, 2.0f, 2.0f };
            request._pAsset   = makeAsset( 0.2f );
            return request;
        }
    };
} // namespace

/** @brief [LensFlareTest] 점광은 화면 안이면 NDC 와 깊이를 낸다. 화면 밖 · 깊이 범위 밖은 플레어가 없다 */
SW_TEST_CASE( LensFlareTest, ProjectsPointLightInsideScreenOnly )
{
    float3 ndcDepth{};
    SW_EXPECT_TRUE( LensFlareSpriteBuffer::projectLight( LensFlareTestUtil::makePointRequest( float3{ 0.5f, -0.25f, 0.3f } ), float4x4::Identity, ndcDepth ) );
    SW_EXPECT_NEAR( 0.5f, ndcDepth._x, 1e-6f );
    SW_EXPECT_NEAR( -0.25f, ndcDepth._y, 1e-6f );
    SW_EXPECT_NEAR( 0.3f, ndcDepth._z, 1e-6f );
    SW_EXPECT_FALSE( LensFlareSpriteBuffer::projectLight( LensFlareTestUtil::makePointRequest( float3{ 1.5f, 0.0f, 0.3f } ), float4x4::Identity, ndcDepth ) );
    SW_EXPECT_FALSE( LensFlareSpriteBuffer::projectLight( LensFlareTestUtil::makePointRequest( float3{ 0.0f, 0.0f, 1.5f } ), float4x4::Identity, ndcDepth ) );
}

/** @brief [LensFlareTest] 방향광은 빛이 오는 쪽(나아가는 방향의 반대)을 무한히 먼 점으로 투영하고 깊이는 1 이다. w 가 0 이하(카메라 뒤)면 없다 */
SW_TEST_CASE( LensFlareTest, ProjectsDirectionalLightAtFarPlane )
{
    LensFlareRequest request = LensFlareTestUtil::makePointRequest( float3{} );
    request._bDirectional    = SW_TRUE;
    request._direction       = float3{ 0.0f, 0.0f, -1.0f }; // 빛은 +z 쪽에서 온다
    float3 ndcDepth{};
    // 단위 행렬에서 방향(w 0)의 투영 w 는 0 이다 — 카메라 면 위의 점이라 그릴 수 없다.
    SW_EXPECT_FALSE( LensFlareSpriteBuffer::projectLight( request, float4x4::Identity, ndcDepth ) );

    float4x4 viewProj  = float4x4::Identity;
    viewProj._34       = 1.0f; // w = z + w(원근의 w 처럼 — 방향은 w 0 이라 z 가 된다)
    request._direction = float3{ -0.2f, 0.0f, -1.0f };
    SW_REQUIRE_TRUE( LensFlareSpriteBuffer::projectLight( request, viewProj, ndcDepth ) );
    SW_EXPECT_NEAR( 0.2f, ndcDepth._x, 1e-5f );
    SW_EXPECT_NEAR( 1.0f, ndcDepth._z, 0.0f );
    request._direction = float3{ 0.0f, 0.0f, 1.0f }; // 빛이 카메라 뒤에서 온다
    SW_EXPECT_FALSE( LensFlareSpriteBuffer::projectLight( request, viewProj, ndcDepth ) );
}

/** @brief [LensFlareTest] 가장자리 감쇠는 가운데 1, 감쇠 폭 안에서 끝까지 거리에 비례, 화면 끝에서 0 이다. 폭 0 이면 화면 안은 1 이다 */
SW_TEST_CASE( LensFlareTest, EdgeFadeFallsTowardScreenBorder )
{
    SW_EXPECT_NEAR( 1.0f, LensFlareSpriteBuffer::computeEdgeFade( float2{ 0.0f, 0.0f }, 0.2f ), 0.0f );
    SW_EXPECT_NEAR( 0.5f, LensFlareSpriteBuffer::computeEdgeFade( float2{ 0.9f, 0.1f }, 0.2f ), 1e-5f );
    SW_EXPECT_NEAR( 0.0f, LensFlareSpriteBuffer::computeEdgeFade( float2{ 0.2f, -1.0f }, 0.2f ), 1e-6f );
    SW_EXPECT_NEAR( 1.0f, LensFlareSpriteBuffer::computeEdgeFade( float2{ 0.99f, 0.0f }, 0.0f ), 0.0f );
}

/** @brief [LensFlareTest] 원소 자리는 빛(0) → 가운데(1) 축 위이고 1 너머는 반대쪽이다 */
SW_TEST_CASE( LensFlareTest, ElementCenterFollowsLightAxis )
{
    const float2 light{ 0.6f, -0.4f };
    const float2 atLight  = LensFlareSpriteBuffer::computeElementCenter( light, 0.0f );
    const float2 atCenter = LensFlareSpriteBuffer::computeElementCenter( light, 1.0f );
    const float2 mirrored = LensFlareSpriteBuffer::computeElementCenter( light, 2.0f );
    SW_EXPECT_NEAR( 0.6f, atLight._x, 1e-6f );
    SW_EXPECT_NEAR( 0.0f, atCenter._x, 1e-6f );
    SW_EXPECT_NEAR( -0.6f, mirrored._x, 1e-6f );
    SW_EXPECT_NEAR( 0.4f, mirrored._y, 1e-6f );
}

/** @brief [LensFlareTest] 가림 몫은 빛 깊이 - 여유 이상인 표본의 비율이다 — 모두 앞에 있으면 0, 하늘(1)이면 방향광이 다 보인다 */
SW_TEST_CASE( LensFlareTest, VisibilityCountsUnoccludedSamples )
{
    const float32 arrHalf[4]{ 0.2f, 0.9f, 0.9f, 0.2f };
    SW_EXPECT_NEAR( 0.5f, LensFlareSpriteBuffer::computeVisibility( arrHalf, 4, 0.5f ), 1e-6f );
    const float32 arrSky[3]{ 1.0f, 1.0f, 1.0f };
    SW_EXPECT_NEAR( 1.0f, LensFlareSpriteBuffer::computeVisibility( arrSky, 3, 1.0f ), 0.0f );
    const float32 arrBlocked[2]{ 0.3f, 0.3f };
    SW_EXPECT_NEAR( 0.0f, LensFlareSpriteBuffer::computeVisibility( arrBlocked, 2, 0.6f ), 0.0f );
    // 빛 자리의 표면(같은 깊이)은 가리지 않는다 — 여유 안이다.
    const float32 arrSame[1]{ 0.5f - LensFlareSpriteBuffer::kDepthBias * 0.5f };
    SW_EXPECT_NEAR( 1.0f, LensFlareSpriteBuffer::computeVisibility( arrSame, 1, 0.5f ), 0.0f );
}

/** @brief [LensFlareTest] 화면 안 빛 하나는 원소 수만큼의 스프라이트가 되고(색 = 빛 × 원소, 크기는 화면비로 가로를 줄인다), 화면 밖 빛은 스프라이트가 없다 */
SW_TEST_CASE( LensFlareTest, BuildsOneSpritePerElement )
{
    vector<LensFlareRequest> listRequest;
    listRequest.push_back( LensFlareTestUtil::makePointRequest( float3{ 0.4f, 0.2f, 0.5f } ) );
    listRequest.push_back( LensFlareTestUtil::makePointRequest( float3{ 3.0f, 0.0f, 0.5f } ) ); // 화면 밖
    vector<LensFlareSprite> listSprite;
    LensFlareSpriteBuffer::buildSprites( listRequest, float4x4::Identity, 2.0f, listSprite );
    SW_REQUIRE_EQUAL( size_t( 2 ), listSprite.size() );
    const LensFlareSprite& glow  = listSprite[0];
    const LensFlareSprite& ghost = listSprite[1];
    SW_EXPECT_NEAR( 0.4f, glow._centerHalfSize._x, 1e-6f );
    SW_EXPECT_NEAR( 0.2f, glow._centerHalfSize._w, 1e-6f );   // 반지름 0.1 × NDC 높이 2
    SW_EXPECT_NEAR( 0.1f, glow._centerHalfSize._z, 1e-6f );   // 화면비 2 로 가로를 줄인다
    SW_EXPECT_NEAR( -0.2f, ghost._centerHalfSize._x, 1e-6f ); // 축 위 1.5
    SW_EXPECT_NEAR( 0.5f, ghost._colorShape._y, 1e-6f );      // 빛 2 × 원소 0.25
    SW_EXPECT_NEAR( static_cast<float32>( LensFlareShape::Disc ), ghost._colorShape._w, 0.0f );
    SW_EXPECT_NEAR( 0.7f, glow._lightUvKernel._x, 1e-6f ); // u = 0.4 / 2 + 0.5
    SW_EXPECT_NEAR( 0.4f, glow._lightUvKernel._y, 1e-6f ); // v = 0.5 - 0.2 / 2
    SW_EXPECT_NEAR( 0.5f, glow._depthFade._x, 1e-6f );
    SW_EXPECT_NEAR( 1.0f, glow._depthFade._y, 0.0f );
}
