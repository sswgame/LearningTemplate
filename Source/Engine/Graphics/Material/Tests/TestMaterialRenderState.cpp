// MaterialRenderStateTest — 패스 → 머티리얼 합치기, 머티리얼의 렌더 칸 읽기 · 잘못된 조합, 해시, 프리셋 데이터(nogpu).
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialTypes.h"
#include "Engine/Graphics/Material/RenderBlendPreset.h"
#include "Engine/Graphics/RHI/RHIRenderState.h"

using namespace sw;

namespace
{
    struct RenderStateTestUtil
    {
        /** @brief 두 번 읽은 것처럼 칸 하나만 적은 덮어쓰기를 만듭니다 — @p pfnWrite 가 적은 칸만 표시된다. */
        template <typename TWrite>
        [[nodiscard]] static RHIRenderStateOverride makeOverride( TWrite pfnWrite )
        {
            RHIFixedRenderState overZeros = RHIRenderStateOverride::makeFilledState( 0x00 );
            RHIFixedRenderState overOnes  = RHIRenderStateOverride::makeFilledState( 0xFF );
            pfnWrite( overZeros );
            pfnWrite( overOnes );
            return RHIRenderStateOverride::makeFromReads( overZeros, overOnes );
        }

        /** @brief 머티리얼 XML 을 임시 리소스로 읽습니다. @p attributes 는 루트 속성, @p children 은 루트 자식입니다. */
        [[nodiscard]] static bool load( Material& material, string_view attributes, string_view children = "" )
        {
            string xml = "<MaterialDef formatVersion=\"0\" name=\"StateMat\" shaderPath=\"engine/shaders/forward_lit.hlsl\" ";
            xml.append( attributes );
            xml += ">";
            xml.append( children );
            xml += "</MaterialDef>";
            const test::ScopedScratchDomain domain;
            SW_EXPECT_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "state.material" ), xml ) );
            return material.loadFromAsset( test::ScopedScratchDomain::makeID( "state.material" ) );
        }
    };
} // namespace

/** @brief [MaterialRenderStateTest] 적은 칸만 덮고 적지 않은 칸은 패스의 값이다. 비트필드 칸도 옆 칸을 건드리지 않는다 */
SW_TEST_CASE( MaterialRenderStateTest, OnlyWrittenFieldsOverride )
{
    RHIRenderState pass{};
    pass._bEnableDepthWrite = SW_TRUE;
    pass._depthCompare      = RHICompareFunc::Less;

    const RHIRenderStateOverride stencil = RenderStateTestUtil::makeOverride( []( RHIFixedRenderState& state )
    {
        state._bEnableStencil = SW_TRUE;
        state._stencilRef     = 7;
    } );
    SW_EXPECT_TRUE( stencil.hasAny() );
    const RHIRenderState merged = stencil.applyTo( pass, RHIRenderStateOverride::Scope::FixedAndBlend );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( merged._bEnableStencil ) );
    SW_EXPECT_EQUAL( 7, static_cast<int32>( merged._stencilRef ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( merged._bEnableDepthWrite ) ); // 같은 바이트의 옆 칸
    SW_EXPECT_TRUE( merged._depthCompare == RHICompareFunc::Less );
    SW_EXPECT_TRUE( merged._cullMode == RHICullMode::Back );
    SW_EXPECT_EQUAL( 255, static_cast<int32>( merged._stencilReadMask ) );

    const RHIRenderStateOverride none{};
    SW_EXPECT_FALSE( none.hasAny() );
    SW_EXPECT_TRUE( none.applyTo( pass, RHIRenderStateOverride::Scope::FixedAndBlend ) == pass );
}

/** @brief [MaterialRenderStateTest] 패스 종류의 범위 밖 칸과 패스가 정한 컬 · 깊이 테스트는 머티리얼이 덮지 못한다 */
SW_TEST_CASE( MaterialRenderStateTest, MergeFollowsThePassScope )
{
    RHIRenderStateOverride material = RenderStateTestUtil::makeOverride( []( RHIFixedRenderState& state )
    {
        state._cullMode     = RHICullMode::None;
        state._depthCompare = RHICompareFunc::Always;
    } );
    material._arrBlendTarget[0]     = RHIBlendTargetState::makePremultipliedAlpha();
    material._blendTargetCount      = 1;

    const RHIRenderState pass{};
    using Scope = RHIRenderStateOverride::Scope;

    // 깊이 패스는 컬만 받는다 — 머티리얼의 깊이 비교가 들어가면 깊이의 모양이 색 패스와 어긋난다.
    const RHIRenderState depthOnly = material.applyTo( pass, Scope::CullOnly );
    SW_EXPECT_TRUE( depthOnly._cullMode == RHICullMode::None );
    SW_EXPECT_TRUE( depthOnly._depthCompare == RHICompareFunc::LessEqual );
    SW_EXPECT_FALSE( depthOnly.isAnyBlendEnabled() );

    // G버퍼는 블렌드를 받지 않는다.
    const RHIRenderState gbuffer = material.applyTo( pass, Scope::FixedState );
    SW_EXPECT_TRUE( gbuffer._depthCompare == RHICompareFunc::Always );
    SW_EXPECT_FALSE( gbuffer.isAnyBlendEnabled() );

    // 포워드 · 반투명은 블렌드까지 받는다. 타깃 하나면 모든 타깃에 같은 값이다.
    const RHIRenderState forward = material.applyTo( pass, Scope::FixedAndBlend );
    SW_EXPECT_TRUE( forward._arrBlendTarget[3] == RHIBlendTargetState::makePremultipliedAlpha() );

    // 뒤집은 껍질(앞면 컬링)은 양면 머티리얼도 앞면을 자른다.
    RHIRenderState hull{};
    hull._cullMode = RHICullMode::Front;
    SW_EXPECT_TRUE( material.applyTo( hull, Scope::CullOnly )._cullMode == RHICullMode::Front );

    // 깊이 첨부가 없는 패스는 깊이 · 스텐실 칸을 받지 않는다.
    RHIRenderState noDepth{};
    noDepth._bEnableDepthTest                  = SW_FALSE;
    noDepth._bEnableDepthWrite                 = SW_FALSE;
    const RHIRenderStateOverride stencil       = RenderStateTestUtil::makeOverride( []( RHIFixedRenderState& state )
    {
        state._bEnableStencil    = SW_TRUE;
        state._bEnableDepthWrite = SW_TRUE;
        state._bEnableDepthTest  = SW_TRUE;
    } );
    const RHIRenderState         mergedNoDepth = stencil.applyTo( noDepth, Scope::FixedAndBlend );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( mergedNoDepth._bEnableStencil ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( mergedNoDepth._bEnableDepthTest ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( mergedNoDepth._bEnableDepthWrite ) );
    SW_EXPECT_TRUE( mergedNoDepth.findInvalidReason() == nullptr );
}

/** @brief [MaterialRenderStateTest] 해시와 비교가 칸을 따라온다 — 값이 하나 달라도 다르고, 같은 값은 같다 */
SW_TEST_CASE( MaterialRenderStateTest, HashFollowsEveryField )
{
    const RHIRenderState base{};
    RHIRenderState       same{};
    SW_EXPECT_TRUE( base == same );
    SW_EXPECT_EQUAL( base.computeHash(), same.computeHash() );

    const auto expectDifferent = [&base]( const RHIRenderState& changed )
    {
        SW_EXPECT_TRUE( base != changed );
        SW_EXPECT_TRUE( base.computeHash() != changed.computeHash() );
    };
    RHIRenderState changed{};
    changed._frontFace = RHIFrontFace::CounterClockwise;
    expectDifferent( changed );
    changed                  = RHIRenderState{};
    changed._stencilBackPass = RHIStencilOp::Invert;
    expectDifferent( changed );
    changed                   = RHIRenderState{};
    changed._bEnableDepthClip = SW_FALSE;
    expectDifferent( changed );
    changed                                    = RHIRenderState{};
    changed._arrBlendTarget[3]._colorWriteMask = RHIColorWriteMask::RGB;
    expectDifferent( changed );

    const RHIRenderStateOverride cullNone = RenderStateTestUtil::makeOverride( []( RHIFixedRenderState& state )
    { state._cullMode = RHICullMode::None; } );
    const RHIRenderStateOverride cullBack = RenderStateTestUtil::makeOverride( []( RHIFixedRenderState& state )
    { state._cullMode = RHICullMode::Back; } );
    SW_EXPECT_TRUE( cullNone.computeHash() != cullBack.computeHash() );
    SW_EXPECT_TRUE( cullBack.computeHash() != RHIRenderStateOverride{}.computeHash() ); // 패스와 같은 값이어도 "적은 칸" 은 다르다
}

/** @brief [MaterialRenderStateTest] 받아 줄 수 없는 조합의 이유를 값 형식이 답한다 */
SW_TEST_CASE( MaterialRenderStateTest, InvalidCombinationsHaveReasons )
{
    RHIFixedRenderState state{};
    SW_EXPECT_TRUE( state.findInvalidReason() == nullptr );
    state._bEnableDepthTest = SW_FALSE;
    SW_EXPECT_TRUE( state.findInvalidReason() != nullptr ); // 테스트 없는 깊이 쓰기
    state._bEnableDepthWrite = SW_FALSE;
    SW_EXPECT_TRUE( state.findInvalidReason() == nullptr );
    state._bEnableStencil = SW_TRUE;
    SW_EXPECT_TRUE( state.findInvalidReason() != nullptr ); // 첨부 없는 스텐실
    state._bEnableStencil = SW_FALSE;
    state._depthBias      = 1;
    SW_EXPECT_TRUE( state.findInvalidReason() != nullptr );

    RHIBlendTargetState target{};
    SW_EXPECT_TRUE( target.findInvalidReason() == nullptr );
    target._dstAlpha = RHIBlendFactor::DstColor;
    SW_EXPECT_TRUE( target.findInvalidReason() != nullptr );
}

/** @brief [MaterialRenderStateTest] 프리셋 데이터가 코드가 짓는 PSO 의 값과 같다. 없는 이름은 찾지 못한다 */
SW_TEST_CASE( MaterialRenderStateTest, PresetsMatchTheCodeValues )
{
    const RenderBlendPreset* const pTransparent   = RenderBlendPresetTable::find( "Transparent" );
    const RenderBlendPreset* const pPremultiplied = RenderBlendPresetTable::find( "Premultiplied" );
    const RenderBlendPreset* const pOpaque        = RenderBlendPresetTable::find( "Opaque" );
    SW_REQUIRE_NOT_NULL( pTransparent );
    SW_REQUIRE_NOT_NULL( pPremultiplied );
    SW_REQUIRE_NOT_NULL( pOpaque );
    SW_EXPECT_TRUE( static_cast<const RHIBlendTargetState&>( *pTransparent ) == RHIBlendTargetState::makeStraightAlpha() );
    SW_EXPECT_TRUE( static_cast<const RHIBlendTargetState&>( *pPremultiplied ) == RHIBlendTargetState::makePremultipliedAlpha() );
    SW_EXPECT_TRUE( static_cast<const RHIBlendTargetState&>( *pOpaque ) == RHIBlendTargetState{} );
    SW_REQUIRE_NOT_NULL( RenderBlendPresetTable::find( "Additive" ) );
    SW_REQUIRE_NOT_NULL( RenderBlendPresetTable::find( "Multiply" ) );
    SW_EXPECT_TRUE( RenderBlendPresetTable::find( "Plasma" ) == nullptr );
}

/** @brief [MaterialRenderStateTest] 칸이 배치 분류 · define · 덮어쓰기 · 해시까지 간다. 적지 않으면 해시가 define 만의 값이다 */
SW_TEST_CASE( MaterialRenderStateTest, FieldsReachBatchAndDefines )
{
    shared_ptr<Material> plain = Material::create();
    SW_REQUIRE_TRUE( RenderStateTestUtil::load( *plain, "alphaMode=\"Opaque\"" ) );
    SW_EXPECT_TRUE( plain->getBlendMode() == RHIBlendMode::Opaque );
    SW_EXPECT_TRUE( plain->getRenderFieldsHash() == 0 );

    shared_ptr<Material> blended = Material::create();
    SW_REQUIRE_TRUE( RenderStateTestUtil::load( *blended, "alphaMode=\"Blend\" blendPreset=\"Additive\" renderQueue=\"5\" enableDepthWrite=\"false\"", "<_permutations><_alwaysDefines><item>MATERIAL_BLEND_TRANSLUCENT</item></_alwaysDefines></_permutations>" ) );
    SW_EXPECT_TRUE( blended->getBlendMode() == RHIBlendMode::Transparent );
    SW_EXPECT_EQUAL( 5, blended->getRenderQueue() );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( blended->getRenderStateOverride()._blendTargetCount ) );
    SW_EXPECT_TRUE( blended->getRenderStateOverride()._arrBlendTarget[0]._dstColor == RHIBlendFactor::One );
    SW_EXPECT_TRUE( blended->getPermutationHash() != plain->getPermutationHash() );

    shared_ptr<Material> queued = Material::create();
    SW_REQUIRE_TRUE( RenderStateTestUtil::load( *queued, "alphaMode=\"Opaque\" renderQueue=\"1\"" ) );
    SW_EXPECT_TRUE( queued->getPermutationHash() != plain->getPermutationHash() ); // 순서가 다르면 다른 배치다

    shared_ptr<Material> masked = Material::create();
    SW_REQUIRE_TRUE( RenderStateTestUtil::load( *masked, "alphaMode=\"Mask\" cullMode=\"None\" twoSidedLighting=\"true\"" ) );
    const vector<string>& listDefine = masked->getCachedShaderDefines();
    SW_EXPECT_TRUE( VectorUtil::contains( listDefine, string( kMaterialAlphaCutoffDefine ) ) );
    SW_EXPECT_TRUE( VectorUtil::contains( listDefine, string( kMaterialTwoSidedDefine ) ) );
    SW_EXPECT_TRUE( masked->getBlendMode() == RHIBlendMode::Opaque );
}

/** @brief [MaterialRenderStateTest] 저장한 글을 다시 읽으면 같은 칸이다 — 적지 않은 칸은 적히지 않아 패스의 값으로 남는다 */
SW_TEST_CASE( MaterialRenderStateTest, FieldsSurviveSaving )
{
    shared_ptr<Material> material = Material::create();
    SW_REQUIRE_TRUE( RenderStateTestUtil::load( *material, "alphaMode=\"Blend\" blendPreset=\"Multiply\" depthCompare=\"Greater\" stencilRef=\"9\" enableStencil=\"true\" renderQueue=\"-2\"", "<_permutations><_alwaysDefines><item>MATERIAL_BLEND_TRANSLUCENT</item></_alwaysDefines></_permutations>" ) );
    const string saved = material->saveToString();
    SW_EXPECT_TRUE( saved.find( "cullMode" ) == string::npos ); // 적지 않은 칸
    SW_EXPECT_TRUE( saved.find( "stencilRef=\"9\"" ) != string::npos );

    shared_ptr<Material> reloaded = Material::create();
    SW_REQUIRE_TRUE( reloaded->loadFromXML( saved ) );
    SW_EXPECT_EQUAL( -2, reloaded->getRenderQueue() );
    SW_EXPECT_EQUAL( material->getRenderStateOverride().computeHash(), reloaded->getRenderStateOverride().computeHash() );
    SW_EXPECT_EQUAL( material->getPermutationHash(), reloaded->getPermutationHash() );
}

/** @brief [MaterialRenderStateTest] 받아 줄 수 없는 머티리얼 조합은 읽기 오류다 */
SW_TEST_CASE( MaterialRenderStateTest, ImpossibleCombinationsAreLoadErrors )
{
    SW_EXPECT_LOG( "Material.DepthTestNotOverridable" );
    SW_EXPECT_LOG( "Material.BlendWithoutAlphaModeBlend" );
    SW_EXPECT_LOG( "Material.BlendPresetUnknown" );
    SW_EXPECT_LOG( "Material.BlendPresetAndTargets" );
    SW_EXPECT_LOG( "Material.BlendTargetInvalid" );
    SW_EXPECT_LOG( "Material.TwoSidedLightingNeedsNoCull" );
    SW_EXPECT_LOG( "Material.AlphaCutoffWithoutMask" );
    SW_EXPECT_LOG( "Material.DefineBelongsToField" );
    SW_EXPECT_LOG( "XMLFields.UnknownField" );
    SW_EXPECT_LOG( "XMLFields.ValueUnreadable" );
    SW_EXPECT_LOG( "Serialization.UnknownEnumerator" );
    const auto expectRejected = []( string_view attributes, string_view children = "" )
    {
        shared_ptr<Material> material = Material::create();
        SW_EXPECT_FALSE( RenderStateTestUtil::load( *material, attributes, children ) );
    };
    expectRejected( "enableDepthTest=\"false\"" );
    expectRejected( "alphaMode=\"Opaque\" blendPreset=\"Transparent\"" );
    expectRejected( "alphaMode=\"Blend\" blendPreset=\"Plasma\"" );
    expectRejected( "alphaMode=\"Blend\" blendPreset=\"Additive\"", "<_listBlendTarget><RHIBlendTargetState enableBlend=\"true\"/></_listBlendTarget>" );
    expectRejected( "alphaMode=\"Blend\"", "<_listBlendTarget><RHIBlendTargetState enableBlend=\"true\" srcAlpha=\"SrcColor\"/></_listBlendTarget>" );
    expectRejected( "twoSidedLighting=\"true\"" );
    expectRejected( "alphaMode=\"Opaque\" alphaCutoff=\"0.25\"" );
    expectRejected( "alphaMode=\"Opaque\"", "<_permutations><_alwaysDefines><item>MATERIAL_TWO_SIDED</item></_alwaysDefines></_permutations>" );
    expectRejected( "blendMode=\"Opaque\"" ); // 옛 칸은 모르는 칸이다
    expectRejected( "alphaMode=\"Opaqe\"" );  // 값 이름 표는 enum 하나다
    expectRejected( "alphaMode=\"Opaque\" cullMode=\"Sideways\"" );

    // 쓰기 마스크만 바꾸는 것은 불투명도 받는다.
    shared_ptr<Material> writeMask = Material::create();
    SW_EXPECT_TRUE( RenderStateTestUtil::load( *writeMask, "alphaMode=\"Opaque\"", "<_listBlendTarget><RHIBlendTargetState colorWriteMask=\"RGB\"/></_listBlendTarget>" ) );
}

/** @brief [MaterialRenderStateTest] 불투명 그리기 띠는 덮은 렌더 상태가 정한다 — 그려진 것에 기대는 머티리얼은 차례를 적지 않아도 뒤 띠, 스텐실 마스크는 그 앞이다 */
SW_TEST_CASE( MaterialRenderStateTest, OrderDependentStatesDrawInALaterBand )
{
    struct BandCase
    {
        const utf8*       _pAttributes;
        const utf8*       _pChildren;
        RHIOpaqueDrawBand _expected;
    };
    const BandCase arrCase[] = {
        {                                                                                                                           "","",     RHIOpaqueDrawBand::Ordinary                                                                                                                                       },
        {                                                                                                          "cullMode=\"None\"",                                                                                   "",     RHIOpaqueDrawBand::Ordinary},
        {                                                                                                                           "", "<_listBlendTarget><RHIBlendTargetState colorWriteMask=\"RGB\"/></_listBlendTarget>",     RHIOpaqueDrawBand::Ordinary}, // 알파만 빼는 것은 덮어쓰기다
        {                                                                                                                           "",   "<_listBlendTarget><RHIBlendTargetState colorWriteMask=\"R\"/></_listBlendTarget>",  RHIOpaqueDrawBand::ReadsTarget}, // 초록 · 파랑은 뒤의 색이 남는다
        {                                                                                                    "depthCompare=\"Always\"",                                                                                   "",  RHIOpaqueDrawBand::ReadsTarget},
        {                                                                                                 "enableDepthWrite=\"false\"",                                                                                   "",  RHIOpaqueDrawBand::ReadsTarget},
        {"enableDepthWrite=\"false\" enableStencil=\"true\" stencilRef=\"1\" stencilFrontPass=\"Replace\" stencilBackPass=\"Replace\"",
         "<_listBlendTarget><RHIBlendTargetState colorWriteMask=\"None\"/></_listBlendTarget>", RHIOpaqueDrawBand::StencilWrite                                                                                                                              },
        {                         "enableStencil=\"true\" stencilRef=\"1\" stencilFrontCompare=\"Equal\" stencilBackCompare=\"Equal\"",                                                                                   "",  RHIOpaqueDrawBand::ReadsTarget},
    };
    for ( const BandCase& bandCase : arrCase )
    {
        shared_ptr<Material> material = Material::create();
        SW_REQUIRE_TRUE( RenderStateTestUtil::load( *material, bandCase._pAttributes, bandCase._pChildren ) );
        SW_EXPECT_EQUAL( static_cast<int32>( bandCase._expected ), static_cast<int32>( material->getOpaqueDrawBand() ) );
    }
}

/** @brief [MaterialRenderStateTest] 그리기 목록과 셰이더가 어긋난 머티리얼은 로드 오류다 — 불투명의 굴절(사본이 없는 패스), 블렌드 define 과 알파 모드의 어긋남 */
SW_TEST_CASE( MaterialRenderStateTest, DrawListMustMatchTheShader )
{
    SW_EXPECT_LOG( "Material.DrawListMismatch" );
    const utf8* const kRefraction  = "<_permutations><_alwaysDefines><item>MATERIAL_REFRACTION</item></_alwaysDefines></_permutations>";
    const utf8* const kTranslucent = "<_permutations><_alwaysDefines><item>MATERIAL_BLEND_TRANSLUCENT</item></_alwaysDefines></_permutations>";
    const utf8* const kGlass       = "<_permutations><_alwaysDefines><item>MATERIAL_BLEND_TRANSLUCENT</item><item>MATERIAL_REFRACTION</item></_alwaysDefines></_permutations>";
    SW_EXPECT_FALSE( RenderStateTestUtil::load( *Material::create(), "", kRefraction ) );       // 불투명 목록의 굴절
    SW_EXPECT_FALSE( RenderStateTestUtil::load( *Material::create(), "", kTranslucent ) );      // 블렌드 define 인데 불투명
    SW_EXPECT_FALSE( RenderStateTestUtil::load( *Material::create(), "alphaMode=\"Blend\"" ) ); // 블렌드인데 define 이 없다
    shared_ptr<Material> glass = Material::create();
    SW_REQUIRE_TRUE( RenderStateTestUtil::load( *glass, "alphaMode=\"Blend\"", kGlass ) );
    SW_EXPECT_TRUE( glass->readsSceneColorCopy() );
}
