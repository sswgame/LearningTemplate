// ShadingMathTest ???곗씠??紐⑤뜽 ?ㅼ뭡????shading_math.hlsli, ?곗씠?붿? 媛숈? ?뚯씪)??臾쇰━ ?깆쭏怨??덈룞??쨌 ????엫 洹쒖튃(nogpu).
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Lighting/ShadingMath.h"

using namespace sw;
using shadermath::ShadingMath;

namespace
{
    struct ShadingMathTestUtil
    {
        static constexpr int32 kThetaSteps = 512;
        static constexpr int32 kPhiSteps   = 256;

        /** @brief 諛섍뎄 ????f(cos罐) d? 瑜?以묒젏 洹쒖튃?쇰줈 ?쎈땲??f 媛 諛⑹쐞媛곸뿉 臾닿?????. */
        template <typename TFunction>
        [[nodiscard]] static float64 integrateHemisphere( TFunction pfnValue )
        {
            float64 sum = 0.0;
            for ( int32 step = 0; step < kThetaSteps; ++step )
            {
                const float64 theta = ( step + 0.5 ) / kThetaSteps * MathUtil::kHalfPi;
                sum += pfnValue( static_cast<float32>( std::cos( theta ) ) ) * std::sin( theta ) * ( MathUtil::kHalfPi / kThetaSteps );
            }
            return sum * 2.0 * MathUtil::kPi;
        }

        /** @brief 援??꾩껜 ????f(cos罐) d? ?낅땲?? */
        template <typename TFunction>
        [[nodiscard]] static float64 integrateSphere( TFunction pfnValue )
        {
            float64 sum = 0.0;
            for ( int32 step = 0; step < kThetaSteps * 2; ++step )
            {
                const float64 theta = ( step + 0.5 ) / ( kThetaSteps * 2 ) * MathUtil::kPi;
                sum += pfnValue( static_cast<float32>( std::cos( theta ) ) ) * std::sin( theta ) * ( MathUtil::kPi / ( kThetaSteps * 2 ) );
            }
            return sum * 2.0 * MathUtil::kPi;
        }

        /** @brief ??移?二쇨린 1)??怨좊Ⅴ寃??묒? ??엫???됯퇏?낅땲?? */
        [[nodiscard]] static float64 computeAverageCoverage( float32 halfWidth, float32 footprint )
        {
            constexpr int32 kSamples = 4096;
            float64         sum      = 0.0;
            for ( int32 sample = 0; sample < kSamples; ++sample )
                sum += ShadingMath::computeLineCoverage( ( sample + 0.5f ) / kSamples, halfWidth, footprint );
            return sum / kSamples;
        }
    };
} // namespace

/** @brief [ShadingMathTest] GGX ? ?쒖뿏(Charlie) 遺꾪룷???뺢퇋?붾릺???덈떎 ????D cos罐h d? = 1(?붿쭊 ?⑥쐞 D쨌? 瑜?? 濡??섎늿 媛? */
SW_TEST_CASE( ShadingMathTest, DistributionsAreNormalized )
{
    for ( const float32 alpha : { 0.2f, 0.5f, 0.9f } )
    {
        const float64 ggx = ShadingMathTestUtil::integrateHemisphere( [alpha]( float32 cosine )
        { return ShadingMath::computeDistributionGGXTimesPi( cosine, alpha ) / MathUtil::kPi * cosine; } );
        SW_EXPECT_NEAR( 1.0, ggx, 0.02 );
    }
    for ( const float32 sheenAlpha : { 0.1f, 0.4f, 1.0f } )
    {
        const float64 charlie = ShadingMathTestUtil::integrateHemisphere( [sheenAlpha]( float32 cosine )
        { return ShadingMath::computeDistributionCharlieTimesPi( cosine, sheenAlpha ) / MathUtil::kPi * cosine; } );
        SW_EXPECT_NEAR( 1.0, charlie, 0.02 );
    }
}

/** @brief [ShadingMathTest] 慣 媛 媛숈? 鍮꾨벑諛?GGX ???깅갑 GGX ? 媛숇떎(鍮꾨떒 쨌 ?붿쭏 湲덉냽???앷컪) */
SW_TEST_CASE( ShadingMathTest, AnisotropicGGXMatchesIsotropicAtZeroAnisotropy )
{
    const float32 alpha          = 0.3f;
    const float32 alphaTangent   = ShadingMath::computeAlphaTangent( alpha, 0.0f );
    const float32 alphaBitangent = ShadingMath::computeAlphaBitangent( alpha, 0.0f );
    SW_EXPECT_NEAR( alpha, alphaTangent, 1e-6 );
    // 諛섍컖 踰≫꽣 (sin 罐 cos ?, sin 罐 sin ?, cos 罐) ?먯꽌 媛숈? 媛?
    const float32 normalDotHalf = 0.9f;
    const float32 sinHalf       = std::sqrt( 1.0f - normalDotHalf * normalDotHalf );
    const float32 anisotropic   = ShadingMath::computeDistributionAnisotropicGGXTimesPi( normalDotHalf, sinHalf * 0.6f, sinHalf * 0.8f, alphaTangent, alphaBitangent );
    SW_EXPECT_NEAR( ShadingMath::computeDistributionGGXTimesPi( normalDotHalf, alpha ), anisotropic, 1e-3 );
    // 鍮꾨벑諛⑹씠 耳쒖?硫??꾩젨??履쎌쑝濡??섏뼱?쒕떎 ???꾩젨??諛⑺뼢 諛섍컖?먯꽌 媛믪씠 而ㅼ쭊??湲멸쾶 ?뚮┛ ?섏씠?쇱씠??.
    const float32 stretched = ShadingMath::computeDistributionAnisotropicGGXTimesPi( normalDotHalf, sinHalf, 0.0f, ShadingMath::computeAlphaTangent( alpha, 0.8f ),
                                                                                     ShadingMath::computeAlphaBitangent( alpha, 0.8f ) );
    SW_EXPECT_TRUE( stretched > anisotropic );
}

/** @brief [ShadingMathTest] 媛먯떬 ?뺤궛? 鍮쏆쓣 ??만 肉??뷀븯吏 ?딅뒗????援??꾩껜 ?곷텇???⑤쾭???)? 媛숆퀬, 媛먯뙂 0 ? ?⑤쾭?몃떎 */
SW_TEST_CASE( ShadingMathTest, WrappedDiffuseConservesEnergy )
{
    for ( const float32 wrap : { 0.0f, 0.3f, 0.8f } )
    {
        const float64 integral = ShadingMathTestUtil::integrateSphere( [wrap]( float32 cosine )
        { return ShadingMath::computeWrappedDiffuse( cosine, wrap ); } );
        SW_EXPECT_NEAR( MathUtil::kPi, integral, 0.01 );
    }
    SW_EXPECT_NEAR( 0.5, ShadingMath::computeWrappedDiffuse( 0.5f, 0.0f ), 1e-6 );
    SW_EXPECT_NEAR( 0.0, ShadingMath::computeWrappedDiffuse( -0.2f, 0.0f ), 1e-6 );
    SW_EXPECT_TRUE( ShadingMath::computeWrappedDiffuse( -0.2f, 0.5f ) > 0.0f ); // ?쇰?: 洹몃뒛 履쎌쑝濡?鍮쏆씠 踰덉쭊??    // ?ш낵???ㅼ뿉???ㅻ뒗 鍮쏅쭔 諛쏅뒗??
    SW_EXPECT_NEAR( 0.0, ShadingMath::computeDiffuseTransmission( 1.0f, 0.0f ), 1e-6 );
    SW_EXPECT_NEAR( 1.0, ShadingMath::computeDiffuseTransmission( -1.0f, 0.0f ), 1e-6 );
}

/** @brief [ShadingMathTest] ?먮┃ ?꾨젅?ш낵 援댁젅瑜?F0 ???섏쭅? F0, ?ㅼ튂硫?1, ?좊━(1.5)??0.04 */
SW_TEST_CASE( ShadingMathTest, FresnelEndpoints )
{
    SW_EXPECT_NEAR( 0.04, ShadingMath::computeDielectricF0( 1.5f ), 1e-4 );
    SW_EXPECT_NEAR( 0.04, ShadingMath::computeFresnelSchlick( 0.04f, 1.0f ), 1e-6 );
    SW_EXPECT_NEAR( 1.0, ShadingMath::computeFresnelSchlick( 0.04f, 0.0f ), 1e-6 );
    SW_EXPECT_NEAR( 0.0, ShadingMath::computeDielectricF0( 1.0f ), 1e-6 );
}

/** @brief [ShadingMathTest] 諛뺣쭑 ?먭퍡 0 ? 留??녿뒗 諛뷀깢???꾨젅?ъ씠怨? 諛섏궗?⑥? 0..1 ?대ŉ ?먭퍡???곕씪 ?뚯옣留덈떎 ?ㅻⅤ??臾댁?媛?튆) */
SW_TEST_CASE( ShadingMathTest, ThinFilmReducesToBaseFresnelAndIridesces )
{
    const float32 baseIor = 1.68f; // 吏꾩＜痢??꾨씪怨좊굹?댄듃)
    SW_EXPECT_NEAR( ShadingMath::computeDielectricF0( baseIor ), ShadingMath::computeThinFilmReflectance( 1.0f, 0.0f, 1.33f, baseIor, 550.0f ), 1e-4 );
    for ( float32 thickness = 0.0f; thickness <= 1000.0f; thickness += 50.0f )
    {
        for ( const float32 cosine : { 1.0f, 0.5f, 0.1f } )
        {
            const float32 reflectance = ShadingMath::computeThinFilmReflectance( cosine, thickness, 1.33f, baseIor, 532.0f );
            SW_EXPECT_TRUE( reflectance >= 0.0f && reflectance <= 1.0f );
        }
    }
    const float32 red  = ShadingMath::computeThinFilmReflectance( 1.0f, 250.0f, 1.33f, baseIor, 650.0f );
    const float32 blue = ShadingMath::computeThinFilmReflectance( 1.0f, 250.0f, 1.33f, baseIor, 450.0f );
    SW_EXPECT_TRUE( std::abs( red - blue ) > 0.005f );
}

/** @brief [ShadingMathTest] ?곸옄濡?嫄곕Ⅸ ????엫 ??諛쒖옄援?씠 移??댁긽?대㈃ ??諛??2w, ?묒쑝硫?怨꾨떒, ?대뒓 諛쒖옄援?뿉?쒕룄 ?됯퇏? 2w(硫由ъ꽌 洹몃Ъ???щ씪吏嫄곕굹 源쒕컯?댁? ?딅뒗?? */
SW_TEST_CASE( ShadingMathTest, LineCoverageIsStableAcrossFootprints )
{
    const float32 halfWidth = 0.05f;
    SW_EXPECT_NEAR( 2.0f * halfWidth, ShadingMath::computeLineCoverage( 0.37f, halfWidth, 1.0f ), 1e-4 );
    SW_EXPECT_NEAR( 2.0f * halfWidth, ShadingMath::computeLineCoverage( 0.81f, halfWidth, 3.0f ), 1e-4 );
    SW_EXPECT_NEAR( 1.0, ShadingMath::computeLineCoverage( 0.0f, halfWidth, 0.001f ), 1e-4 );
    SW_EXPECT_NEAR( 0.0, ShadingMath::computeLineCoverage( 0.5f, halfWidth, 0.001f ), 1e-4 );
    for ( const float32 footprint : { 0.002f, 0.03f, 0.2f, 0.7f } )
        SW_EXPECT_NEAR( 2.0f * halfWidth, ShadingMathTestUtil::computeAverageCoverage( halfWidth, footprint ), 2e-3 );
}

/** @brief [ShadingMathTest] 癒몃━寃?諛섏궗 ???꾩젨?몄뿉 ?섏쭅??諛섍컖?먯꽌 媛??諛앷퀬, 嫄곗튌?섎줉 吏?섍? ?묐떎 */
SW_TEST_CASE( ShadingMathTest, StrandSpecularPeaksPerpendicular )
{
    const float32 exponent = ShadingMath::computeStrandExponent( 0.3f );
    SW_EXPECT_NEAR( 1.0, ShadingMath::computeStrandSpecular( 0.0f, exponent ), 1e-6 );
    SW_EXPECT_TRUE( ShadingMath::computeStrandSpecular( 0.5f, exponent ) < 1.0f );
    SW_EXPECT_TRUE( ShadingMath::computeStrandExponent( 0.8f ) < exponent );
    // ?쒖뿏 媛?쒖꽦? ?묒닔?닿퀬 ?좏븳?섎떎.
    SW_EXPECT_TRUE( ShadingMath::computeVisibilityNeubelt( 0.0f, 0.0f ) > 0.0f );
}

/** @brief [ShadingMathTest] ?띿콈 ?쒖감 ??媛곷쭑 援댁젅? 湲곗슱湲곕? 以꾩씠怨?援댁젅瑜?1 ?대㈃ 洹몃?濡?, ?섏쭅?쇰줈 蹂대㈃ 0 ?대떎 */
SW_TEST_CASE( ShadingMathTest, IrisParallaxFollowsSnell )
{
    const float32 sinIncident = 0.5f;
    const float32 tanIncident = sinIncident / std::sqrt( 1.0f - sinIncident * sinIncident );
    SW_EXPECT_NEAR( tanIncident, ShadingMath::computeRefractedTangent( sinIncident, 1.0f ), 1e-4 );
    SW_EXPECT_TRUE( ShadingMath::computeRefractedTangent( sinIncident, 1.376f ) < tanIncident );
    SW_EXPECT_NEAR( 0.0, ShadingMath::computeRefractedTangent( 0.0f, 1.376f ), 1e-6 );
}

/** @brief [ShadingMathTest] ?숆났 ???뺤옣??諛섏?由꾩쓣 ?뺥븯怨? 諛앹쓣?섎줉 醫곸쑝硫? 紐⑥뼇留덈떎 媛?대뜲????쨌 ?띿콈 媛?μ옄由щ뒗 諛뽰씠??*/
SW_TEST_CASE( ShadingMathTest, PupilShapesAndDilation )
{
    SW_EXPECT_NEAR( 0.2, ShadingMath::computePupilRadius( 0.2f, 0.6f, 0.0f ), 1e-6 );
    SW_EXPECT_NEAR( 0.6, ShadingMath::computePupilRadius( 0.2f, 0.6f, 1.0f ), 1e-6 );
    SW_EXPECT_NEAR( 1.0, ShadingMath::computeDilationFromLuminance( 0.001f, 0.01f, 1000.0f ), 1e-6 );
    SW_EXPECT_NEAR( 0.0, ShadingMath::computeDilationFromLuminance( 5000.0f, 0.01f, 1000.0f ), 1e-6 );
    SW_EXPECT_TRUE( ShadingMath::computeDilationFromLuminance( 1.0f, 0.01f, 1000.0f ) > ShadingMath::computeDilationFromLuminance( 100.0f, 0.01f, 1000.0f ) );

    for ( float32 shape = 0.0f; shape < shadermath::kPupilShapeCount; shape += 1.0f )
    {
        // 諛섏? ?대┛ ?숆났: ?띿콈 媛?μ옄由?諛섏?由?0.95)????諛뽰씠??
        SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shape, 0.95f, 0.0f, 0.2f, 0.6f, 0.5f ) > 0.0f );
        SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shape, 0.0f, 0.95f, 0.2f, 0.6f, 0.5f ) > 0.0f );
    }
    // ?κ렐 쨌 ??쨌 留됰???媛?대뜲媛 ?덉씠??W ?먮뒗 媛?대뜲媛 ?잛? ?좊씪 媛?대뜲 ?꾧? ?덉씠??.
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeRound, 0.0f, 0.0f, 0.2f, 0.6f, 0.5f ) < 0.0f );
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeVerticalSlit, 0.0f, 0.0f, 0.2f, 0.6f, 0.0f ) < 0.0f );
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeHorizontalBar, 0.0f, 0.0f, 0.2f, 0.6f, 0.0f ) < 0.0f );
    // ?몃줈 ?? 醫곸쑝硫??놁? 諛? ?꾩븘?섎뒗 ?? ?쒖쭩 ?대━硫??녿룄 ??
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeVerticalSlit, 0.2f, 0.0f, 0.2f, 0.6f, 0.0f ) > 0.0f );
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeVerticalSlit, 0.0f, 0.5f, 0.2f, 0.6f, 0.0f ) < 0.0f );
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeVerticalSlit, 0.2f, 0.0f, 0.2f, 0.6f, 1.0f ) < 0.0f );
    // 媛濡?留됰?: 醫곸쑝硫??꾨뒗 諛? ?놁? ??
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeHorizontalBar, 0.0f, 0.2f, 0.2f, 0.6f, 0.0f ) > 0.0f );
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeHorizontalBar, 0.5f, 0.0f, 0.2f, 0.6f, 0.0f ) < 0.0f );
    // W ?? 怨⑥쭨湲?x = 짹0.3, 怨≪꽑 ?????덉씠怨??꾩そ ?앹? 諛?
    const float32 valleyY = shadermath::kPupilShapeCephalopodCurve * ( 0.0f - 0.125f * 0.36f );
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeCephalopod, 0.3f, valleyY, 0.2f, 0.6f, 0.5f ) < 0.0f );
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeCephalopod, 0.3f, 0.6f, 0.2f, 0.6f, 0.5f ) > 0.0f );
}
