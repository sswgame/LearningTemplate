/**
 * @file post_compute.hlsli
 * @brief 일반 풀스크린 패스의 컴퓨트 구현(`_implementation="Compute"`, define `SW_PASS_COMPUTE`)이 함께 쓰는 루트 상수와 읽기 함수입니다.
 * @details C++: FramePassExecutor::executeComputeImplementation — 같은 순서의 16 dword 이하 루트 상수다(스칼라만 — cbuffer 의 16 바이트 경계 채우기가 C++ 배치와 어긋나지 않게).
 *          원본 · 깊이는 bindless SRV 번호로 읽고 출력은 RW 번호로 쓴다. 네이티브 bindless(DX12 · Vulkan)에서만 돈다 — 에뮬 백엔드는 픽셀 구현이다.
 *          컴퓨트 PSO 에는 PassCB 가 묶이지 않으므로 효과의 값(블룸 문턱 · 렌즈 · 역뷰-투영 4 열)도 여기로 온다.
 *          include 앞에 `SW_RW_SLOT0_GL_FORMAT` 을 적는다(GL 이 묶은 포맷과 선언이 같아야 한다 — 쿠킹이 GL 로도 컴파일한다).
 */
#ifndef SW_ENGINE_POST_COMPUTE_HLSLI
#define SW_ENGINE_POST_COMPUTE_HLSLI

SW_ROOT_CONSTANTS_BEGIN
{
    uint  g_PostOutputIndex; // 출력 RW 번호
    uint  g_PostSourceIndex; // SourceColor 의 bindless 번호
    uint  g_PostDepthIndex;  // SceneDepth 의 bindless 번호(읽지 않는 패스는 kInvalidIndex)
    uint  g_PostOutputWidth; // 출력 크기(px)
    uint  g_PostOutputHeight;
    float g_PostSourceTexelX; // 원본 텍셀 크기(1 / 원본 크기)
    float g_PostSourceTexelY;
    float g_PostParam0; // 블룸 g_BloomParams.xyz · 피사계 심도 g_CameraLens.xyz
    float g_PostParam1;
    float g_PostParam2;
    float g_PostInvViewProjColumn0; // g_InvViewProj 의 4 열 — 동차 w 를 복원한다(뷰 깊이 = 1 / w)
    float g_PostInvViewProjColumn1;
    float g_PostInvViewProjColumn2;
    float g_PostInvViewProjColumn3;
};
SW_ROOT_CONSTANTS_END

struct PostCompute
{
    static float3 getParams();
    static int2 getSourceSize();
    static float4 loadSource( int2 texel );
    static float loadDeviceDepth( int2 texel );
    static float computeViewDepth( float2 pixelPosition, float deviceDepth );
    static void storeOutput( uint2 texel, float4 value );
    static bool isInsideOutput( uint2 texel );
};

static float3 PostCompute::getParams()
{
    return float3( SW_ROOT( g_PostParam0 ), SW_ROOT( g_PostParam1 ), SW_ROOT( g_PostParam2 ) );
}

static int2 PostCompute::getSourceSize()
{
    return int2( Bindless::getEngineTextureSize( SW_ROOT( g_PostSourceIndex ), 0u ) );
}

/** @brief 원본 텍셀 하나를 읽습니다. 가장자리 밖은 클램프한다(픽셀 구현의 클램프 샘플러와 같다). */
static float4 PostCompute::loadSource( int2 texel )
{
    const int2 clamped = clamp( texel, int2( 0, 0 ), PostCompute::getSourceSize() - int2( 1, 1 ) );
    return Bindless::loadEngineTexel( SW_ROOT( g_PostSourceIndex ), 0u, clamped );
}

/** @brief 장면 깊이 텍셀 하나(장치 깊이)를 읽습니다. 원본과 같은 크기다(피사계 심도). */
static float PostCompute::loadDeviceDepth( int2 texel )
{
    const int2 clamped = clamp( texel, int2( 0, 0 ), PostCompute::getSourceSize() - int2( 1, 1 ) );
    return Bindless::loadEngineTexel( SW_ROOT( g_PostDepthIndex ), 0u, clamped ).r;
}

/**
 * @brief 화면 픽셀 위치(px)와 장치 깊이에서 뷰 깊이(m)를 구합니다. 픽셀 구현(depth_of_field.hlsl computeViewDepth)과 같은 식이고 동차 w 만 쓴다.
 */
static float PostCompute::computeViewDepth( float2 pixelPosition, float deviceDepth )
{
    const float2 uv     = pixelPosition * float2( SW_ROOT( g_PostSourceTexelX ), SW_ROOT( g_PostSourceTexelY ) );
    const float2 ndcXy  = uv * float2( 2.0f, -2.0f ) + float2( -1.0f, 1.0f );
    const float4 column = float4( SW_ROOT( g_PostInvViewProjColumn0 ), SW_ROOT( g_PostInvViewProjColumn1 ), SW_ROOT( g_PostInvViewProjColumn2 ), SW_ROOT( g_PostInvViewProjColumn3 ) );
    const float  w      = dot( float4( ndcXy, deviceDepth, 1.0f ), column );
    return 1.0f / max( abs( w ), 1e-6f );
}

static void PostCompute::storeOutput( uint2 texel, float4 value )
{
    Bindless::storeRwTexture2D( SW_ROOT( g_PostOutputIndex ), texel, value );
}

static bool PostCompute::isInsideOutput( uint2 texel )
{
    return texel.x < SW_ROOT( g_PostOutputWidth ) && texel.y < SW_ROOT( g_PostOutputHeight );
}

#endif
