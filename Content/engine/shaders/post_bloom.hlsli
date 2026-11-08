/**
 * @file post_bloom.hlsli
 * @brief 블룸을 **함수 하나**로 내놓는다 — 전용 패스(post_bloom.hlsl)와 합친 체인(post_chain.hlsl)이 같은 코드를 쓴다.
 * @details 효과를 패스가 아니라 함수로 두는 것이 합치기의 전제다. 전체화면 패스는 픽셀마다
 *          "읽고 · 계산하고 · 쓴다" 인데, 쓰고 다시 읽는 왕복이 계산보다 비싸다(720p 한 왕복이
 *          약 57 us). 함수로 두면 파이프라인 XML 이 퍼뮤테이션으로 켜고 끄면서도 왕복은 한 번이다.
 *          밝은 부분을 반해상도로 나누는 파이프라인(deferredpipeline.xml)은 BloomDownsample 패스가 Bloom::computeBrightDownsampled 를,
 *          Bloom 패스가 Bloom::applyBright 를 쓴다(언리얼 블룸 셋업 → 다운샘플 → 합성과 같은 순서).
 */
#ifndef SW_ENGINE_POSTBLOOM_HLSLI
#define SW_ENGINE_POSTBLOOM_HLSLI

struct Bloom
{
    static float3 computeBrightFromBlur( float3 blur );
    static float3 computeBright( float2 uv, float2 texel );
    static float3 computeBrightDownsampled( float2 uv, float2 sourceTexel );
    static float3 apply( float2 uv, float2 texel, float3 color );
    static float3 applyBright( float2 uv, float3 color );
    static float3 sampleBright( float2 uv, float2 texel, bool bDownsampled );
    static float computeLensWeight( float2 uv, float sharpness );
    static float3 computeLensFlare( float2 uv, float2 texel, bool bDownsampled );
    static float3 applyDirt( float2 uv, float3 bloom );
};

/// @brief 플레어 고스트 수입니다(언리얼 기본 렌즈 플레어의 보케 여덟 개보다 적다 — 블룸 한 장에서 다시 읽는 수가 비용이다).
static const int kLensFlareGhostCount = 4;

/**
 * @brief 흐린 색에서 문턱(g_BloomParams.x)을 넘는 밝은 부분을 부드러운 무릎(z)과 세기(y)로 냅니다.
 */
static float3 Bloom::computeBrightFromBlur( float3 blur )
{
    float peakChannel = max( max( blur.r, blur.g ), blur.b );
    float soft        = saturate( ( peakChannel - g_BloomParams.x + g_BloomParams.z ) / max( g_BloomParams.z, 1e-4 ) );
    return blur * soft * soft * g_BloomParams.y;
}

/**
 * @brief 원본과 같은 해상도에서 밝은 부분을 구합니다(블러 두 탭).
 */
static float3 Bloom::computeBright( float2 uv, float2 texel )
{
    // 블러는 **바이리니어 두 번**으로 끝낸다. 텍셀 중심에서 반 칸 비낀 곳을 찍으면 샘플러가 이웃 네
    // 텍셀을 공짜로 평균해 준다 — 두 번이면 여덟 텍셀이다. 여기만은 `PassTexture::sampleSource`(선형)여야 한다:
    // 점 샘플러로 바꾸면 섞이지 않아 블러가 아니게 된다.
    float2 halfTexel = texel * 0.5f;
    float3 blur      = PassTexture::sampleSource( uv - halfTexel ).rgb;
    blur += PassTexture::sampleSource( uv + halfTexel ).rgb;
    blur *= 0.5f;
    return Bloom::computeBrightFromBlur( blur );
}

/**
 * @brief 반해상도 출력 픽셀 하나의 밝은 부분을 구합니다(원본 4x4 텍셀을 바이리니어 네 탭으로 평균).
 * @param sourceTexel 원본(SourceColor) 텍셀 크기 — `g_SourceTexel.xy`.
 * @details 반해상도 픽셀의 중심은 원본 2x2 의 모서리다. 거기서 대각으로 한 텍셀 비킨 네 곳은 각각 다른 2x2 의 모서리라
 *          바이리니어 한 번이 2x2 를 고르게 섞는다 — 네 탭이 원본 4x4 를 덮는다.
 */
static float3 Bloom::computeBrightDownsampled( float2 uv, float2 sourceTexel )
{
    float3 blur = PassTexture::sampleSource( uv + float2( -sourceTexel.x, -sourceTexel.y ) ).rgb;
    blur += PassTexture::sampleSource( uv + float2( sourceTexel.x, -sourceTexel.y ) ).rgb;
    blur += PassTexture::sampleSource( uv + float2( -sourceTexel.x, sourceTexel.y ) ).rgb;
    blur += PassTexture::sampleSource( uv + float2( sourceTexel.x, sourceTexel.y ) ).rgb;
    blur *= 0.25f;
    return Bloom::computeBrightFromBlur( blur );
}

/** @brief 블룸의 밝은 부분을 @p uv 에서 읽습니다 — 반해상도 밝은 부분이 있으면 그것을, 아니면 원본에서 구한다. */
static float3 Bloom::sampleBright( float2 uv, float2 texel, bool bDownsampled )
{
    float3 bright;
    if ( bDownsampled )
        bright = Bindless::sampleIndex( g_BloomBrightIndex, uv ).rgb;
    else
        bright = Bloom::computeBright( uv, texel );
    return bright;
}

/** @brief 화면 가운데에서 멀수록 줄어드는 무게입니다(가운데 1, 모서리 0) — 화면 밖을 읽는 고스트가 가장자리에서 사라지게 한다. */
static float Bloom::computeLensWeight( float2 uv, float sharpness )
{
    return pow( 1.0f - saturate( length( uv - 0.5f ) * 1.41421356f ), sharpness );
}

/**
 * @brief 렌즈 플레어입니다 — 밝은 부분을 화면 가운데에 대해 뒤집은 선 위의 고스트 넷과, 그 방향으로 일정 반지름의 고리(헤일로)에서 다시 읽는다(John Chapman 식 · 언리얼 렌즈 플레어의 자리).
 * @details 세기(g_BloomLens.y)가 0 이면 읽지 않는다. 헤일로 방향은 가로/세로 비율을 보정해 원이다. 가운데 픽셀은 방향이 없어 헤일로를 더하지 않는다.
 */
static float3 Bloom::computeLensFlare( float2 uv, float2 texel, bool bDownsampled )
{
    float3 flare = float3( 0.0f, 0.0f, 0.0f );
    SW_BRANCH if ( g_BloomLens.y > 0.0f )
    {
        const float2 flipped   = 1.0f - uv;
        const float2 ghostStep = ( 0.5f - flipped ) * g_BloomLens.z;
        SW_UNROLL for ( int ghost = 0; ghost < kLensFlareGhostCount; ++ghost )
        {
            const float2 ghostUv = flipped + ghostStep * float( ghost );
            flare += Bloom::sampleBright( ghostUv, texel, bDownsampled ) * Bloom::computeLensWeight( ghostUv, 4.0f );
        }
        const float  aspect     = max( g_CameraFrame.y, 1e-4f );
        const float2 toCenter   = ( 0.5f - flipped ) * float2( aspect, 1.0f );
        const float  distance   = length( toCenter );
        const float2 haloOffset = distance > 1e-5f ? ( toCenter / distance ) * g_BloomLens.w / float2( aspect, 1.0f ) : float2( 0.0f, 0.0f );
        const float2 haloUv     = flipped + haloOffset;
        flare += distance > 1e-5f ? Bloom::sampleBright( haloUv, texel, bDownsampled ) * Bloom::computeLensWeight( haloUv, 5.0f ) : float3( 0.0f, 0.0f, 0.0f );
        flare *= g_FlareTint.rgb * g_BloomLens.y;
    }
    return flare;
}

/** @brief 블룸(밝은 부분 + 플레어)에 먼지 마스크(g_BloomDirtMaskIndex)를 곱해 더 밝힙니다. 마스크가 없거나 세기 0 이면 C++ 가 g_BloomLens.x 를 0 으로 두어 읽지 않는다. */
static float3 Bloom::applyDirt( float2 uv, float3 bloom )
{
    float3 result = bloom;
    SW_BRANCH if ( g_BloomLens.x > 0.0f )
    {
        result = bloom * ( 1.0f + g_BloomLens.x * Bindless::sampleIndex( g_BloomDirtMaskIndex, uv ).rgb );
    }
    return result;
}

/**
 * @brief 원본 색에 블룸을 더해 돌려줍니다.
 * @param color 이미 읽어 둔 이 픽셀의 원본 색 — 부르는 쪽이 한 번만 읽게 하려고 인자로 받는다.
 */
static float3 Bloom::apply( float2 uv, float2 texel, float3 color )
{
    // SSAO 가 있으면 조명 결과를 가린다(파이프라인 XML 이 AOColor 를 입력으로 선언한다). 없으면 1.
    float ambientOcclusion = PassTexture::sampleAmbientOcclusion( uv );
    return color * ambientOcclusion + Bloom::applyDirt( uv, Bloom::computeBright( uv, texel ) + Bloom::computeLensFlare( uv, texel, false ) );
}

/**
 * @brief 원본 색에 반해상도로 구해 둔 밝은 부분(g_BloomBrightIndex)을 업샘플해 더합니다. 바이리니어 한 번이 업샘플이다.
 */
static float3 Bloom::applyBright( float2 uv, float3 color )
{
    float        ambientOcclusion = PassTexture::sampleAmbientOcclusion( uv );
    const float3 bright           = Bindless::sampleIndex( g_BloomBrightIndex, uv ).rgb;
    return color * ambientOcclusion + Bloom::applyDirt( uv, bright + Bloom::computeLensFlare( uv, float2( 0.0f, 0.0f ), true ) );
}

#endif // SW_ENGINE_POSTBLOOM_HLSLI
