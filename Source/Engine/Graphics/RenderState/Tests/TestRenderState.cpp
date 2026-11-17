#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RenderState/RenderState.h"

// RenderStateTest — 렌더 상태의 검사 · 머티리얼 덮어쓰기: 잘못된 조합의 이유, 읽기 두 번으로 만든 덮어쓰기의 칸 마스크, 패스 범위(컬만 · 고정 · 블렌드까지)와
// 패스가 쥔 칸(깊이 테스트 · 앞면 컬링)의 우선, 블렌드 타깃 퍼뜨리기, 불투명 그리기 띠 판정(nogpu).

using namespace sw;

namespace
{
    struct RenderStateTestUtil
    {
        /** @brief 머티리얼이 적은 칸만 바꾸는 덮어쓰기를 만듭니다 — 같은 대입을 0 · 1 로 채운 상태 둘에 하고 읽는다(`makeFromReads` 의 쓰는 법). */
        template <typename Assign>
        static RenderStateOverride makeOverride( Assign&& assign )
        {
            FixedRenderState overZeros = RenderStateOverride::makeFilledState( 0 );
            FixedRenderState overOnes  = RenderStateOverride::makeFilledState( 0xFF );
            assign( overZeros );
            assign( overOnes );
            return RenderStateOverride::makeFromReads( overZeros, overOnes );
        }
    };
    using Util = RenderStateTestUtil;
} // namespace

/** @brief [RenderStateTest] 기본 상태는 유효하고, 깊이 테스트 없는 깊이 쓰기 · 스텐실 · 깊이 바이어스와 색 계수를 쓴 알파 블렌드는 이유와 함께 거절된다 */
SW_TEST_CASE( RenderStateTest, InvalidCombinationsNameTheirReason )
{
    FixedRenderState state;
    SW_EXPECT_TRUE( state.findInvalidReason() == nullptr );
    state._bEnableDepthTest = SW_FALSE;
    SW_EXPECT_TRUE( state.findInvalidReason() != nullptr ); // 깊이 쓰기는 켜진 채다
    state._bEnableDepthWrite = SW_FALSE;
    SW_EXPECT_TRUE( state.findInvalidReason() == nullptr );
    state._bEnableStencil = SW_TRUE;
    SW_EXPECT_TRUE( state.findInvalidReason() != nullptr );
    state._bEnableStencil = SW_FALSE;
    state._depthBias      = 2;
    SW_EXPECT_TRUE( state.findInvalidReason() != nullptr );

    BlendTargetState blend = BlendTargetState::makeStraightAlpha();
    SW_EXPECT_TRUE( blend.findInvalidReason() == nullptr );
    blend._srcAlpha = BlendFactor::SrcColor;
    SW_EXPECT_TRUE( blend.findInvalidReason() != nullptr );
}

/** @brief [RenderStateTest] 덮어쓰기는 적은 칸만 바꾼다 — 적지 않은 칸은 패스의 값이고, 적은 것이 없으면 `hasAny` 가 거짓이다 */
SW_TEST_CASE( RenderStateTest, OverrideChangesOnlyWhatItWrote )
{
    const RenderStateOverride none = Util::makeOverride( []( FixedRenderState& ) {} );
    SW_EXPECT_FALSE( none.hasAny() );
    SW_EXPECT_TRUE( none.applyTo( RenderState{}, RenderStateOverride::Scope::FixedAndBlend ) == RenderState{} );

    const RenderStateOverride twoSided = Util::makeOverride( []( FixedRenderState& state )
    {
        state._cullMode     = CullMode::None;
        state._depthCompare = CompareFunc::Always;
    } );
    SW_EXPECT_TRUE( twoSided.hasAny() );
    RenderState pass;
    pass._depthBias           = 3;
    const RenderState applied = twoSided.applyTo( pass, RenderStateOverride::Scope::FixedState );
    SW_EXPECT_TRUE( applied._cullMode == CullMode::None );
    SW_EXPECT_TRUE( applied._depthCompare == CompareFunc::Always );
    SW_EXPECT_EQUAL( 3, applied._depthBias ); // 적지 않은 칸은 패스의 값
    SW_EXPECT_TRUE( applied._bEnableDepthWrite != SW_FALSE );
}

/** @brief [RenderStateTest] 컬만 범위(그림자 · 깊이 프리패스)는 컬 · 앞면 방향만 받고, 패스가 정한 앞면 컬링은 머티리얼이 풀지 못한다 */
SW_TEST_CASE( RenderStateTest, CullOnlyScopeAndPassCullWin )
{
    const RenderStateOverride twoSided = Util::makeOverride( []( FixedRenderState& state )
    {
        state._cullMode     = CullMode::None;
        state._frontFace    = FrontFace::CounterClockwise;
        state._depthCompare = CompareFunc::Always;
    } );
    const RenderState         shadow   = twoSided.applyTo( RenderState{}, RenderStateOverride::Scope::CullOnly );
    SW_EXPECT_TRUE( shadow._cullMode == CullMode::None );
    SW_EXPECT_TRUE( shadow._frontFace == FrontFace::CounterClockwise );
    SW_EXPECT_TRUE( shadow._depthCompare == CompareFunc::LessEqual ); // 비교는 패스의 것

    RenderState outline;
    outline._cullMode         = CullMode::Front; // 뒤집은 껍질
    const RenderState applied = twoSided.applyTo( outline, RenderStateOverride::Scope::FixedState );
    SW_EXPECT_TRUE( applied._cullMode == CullMode::Front );
}

/** @brief [RenderStateTest] 깊이 테스트는 늘 패스의 값이고, 깊이 첨부가 없는 패스에서는 깊이 쓰기 · 스텐실 · 바이어스도 패스의 값이다 */
SW_TEST_CASE( RenderStateTest, DepthAttachmentStateFollowsThePass )
{
    const RenderStateOverride noDepth = Util::makeOverride( []( FixedRenderState& state )
    {
        state._bEnableDepthTest  = SW_FALSE;
        state._bEnableDepthWrite = SW_TRUE;
        state._bEnableStencil    = SW_TRUE;
        state._depthBias         = 5;
    } );
    SW_EXPECT_TRUE( noDepth.hasDepthTestField() );
    const RenderState withDepth = noDepth.applyTo( RenderState{}, RenderStateOverride::Scope::FixedState );
    SW_EXPECT_TRUE( withDepth._bEnableDepthTest != SW_FALSE ); // 패스는 깊이를 붙인다
    SW_EXPECT_TRUE( withDepth._bEnableStencil != SW_FALSE );

    RenderState overlay;
    overlay._bEnableDepthTest  = SW_FALSE;
    overlay._bEnableDepthWrite = SW_FALSE;
    const RenderState applied  = noDepth.applyTo( overlay, RenderStateOverride::Scope::FixedState );
    SW_EXPECT_TRUE( applied._bEnableDepthTest == SW_FALSE );
    SW_EXPECT_TRUE( applied._bEnableDepthWrite == SW_FALSE );
    SW_EXPECT_TRUE( applied._bEnableStencil == SW_FALSE );
    SW_EXPECT_EQUAL( 0, applied._depthBias );
    SW_EXPECT_TRUE( applied.findInvalidReason() == nullptr ); // 패스를 따라 유효한 조합으로 남는다
}

/** @brief [RenderStateTest] 블렌드 타깃 하나는 모든 타깃에, 여럿이면 넘치는 타깃이 마지막 값을 받는다. 블렌드는 블렌드까지 범위에서만 바뀐다 */
SW_TEST_CASE( RenderStateTest, BlendTargetsSpreadOverAttachments )
{
    RenderStateOverride translucent = Util::makeOverride( []( FixedRenderState& ) {} );
    translucent._arrBlendTarget[0]  = BlendTargetState::makeStraightAlpha();
    translucent._blendTargetCount   = 1;
    const RenderState forward       = translucent.applyTo( RenderState{}, RenderStateOverride::Scope::FixedAndBlend );
    for ( const BlendTargetState& target : forward._arrBlendTarget )
    {
        SW_EXPECT_TRUE( target == BlendTargetState::makeStraightAlpha() );
    }
    SW_EXPECT_TRUE( forward.isAnyBlendEnabled() );
    SW_EXPECT_FALSE( translucent.applyTo( RenderState{}, RenderStateOverride::Scope::FixedState ).isAnyBlendEnabled() );

    translucent._arrBlendTarget[1] = BlendTargetState::makePremultipliedAlpha();
    translucent._blendTargetCount  = 2;
    const RenderState mrt          = translucent.applyTo( RenderState{}, RenderStateOverride::Scope::FixedAndBlend );
    SW_EXPECT_TRUE( mrt._arrBlendTarget[0] == BlendTargetState::makeStraightAlpha() );
    SW_EXPECT_TRUE( mrt._arrBlendTarget[kMaxColorAttachments - 1] == BlendTargetState::makePremultipliedAlpha() );
}

/** @brief [RenderStateTest] 불투명 띠: 보통 상태는 Ordinary, 깊이 Always · 블렌드 · 스텐실 비교는 ReadsTarget, 비교 없는 스텐실 쓰기는 StencilWrite 다 */
SW_TEST_CASE( RenderStateTest, OpaqueDrawBandClassification )
{
    SW_EXPECT_TRUE( Util::makeOverride( []( FixedRenderState& ) {} ).classifyOpaqueDrawBand() == OpaqueDrawBand::Ordinary );
    SW_EXPECT_TRUE( Util::makeOverride( []( FixedRenderState& state )
    { state._depthCompare = CompareFunc::Always; } )
                        .classifyOpaqueDrawBand() == OpaqueDrawBand::ReadsTarget );
    SW_EXPECT_TRUE( Util::makeOverride( []( FixedRenderState& state )
    { state._bEnableDepthWrite = SW_FALSE; } )
                        .classifyOpaqueDrawBand() == OpaqueDrawBand::ReadsTarget );

    const RenderStateOverride mask = Util::makeOverride( []( FixedRenderState& state )
    {
        state._bEnableStencil   = SW_TRUE;
        state._stencilFrontPass = StencilOp::Replace;
        state._stencilBackPass  = StencilOp::Replace;
    } );
    SW_EXPECT_TRUE( mask.classifyOpaqueDrawBand() == OpaqueDrawBand::StencilWrite );
    const RenderStateOverride masked = Util::makeOverride( []( FixedRenderState& state )
    {
        state._bEnableStencil      = SW_TRUE;
        state._stencilFrontCompare = CompareFunc::Equal;
    } );
    SW_EXPECT_TRUE( masked.classifyOpaqueDrawBand() == OpaqueDrawBand::ReadsTarget );

    RenderStateOverride blended = Util::makeOverride( []( FixedRenderState& ) {} );
    blended._arrBlendTarget[0]  = BlendTargetState::makeStraightAlpha();
    blended._blendTargetCount   = 1;
    SW_EXPECT_TRUE( blended.classifyOpaqueDrawBand() == OpaqueDrawBand::ReadsTarget );
    SW_EXPECT_TRUE( blended.computeHash() != Util::makeOverride( []( FixedRenderState& ) {} ).computeHash() );
}
