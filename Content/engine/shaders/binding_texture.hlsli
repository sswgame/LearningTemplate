/**
 * binding_texture.hlsli — binding.hlsli 의 10 · 11 절: 샘플러 세트와 텍스처 읽기(네 백엔드의 분기가 여기서 끝난다).
 * binding.hlsli 앞 절(PassCB · 슬롯)에 기대므로 binding.hlsli 만 이 파일을 include 한다.
 */
#ifndef SW_ENGINE_BINDING_TEXTURE_HLSLI
#define SW_ENGINE_BINDING_TEXTURE_HLSLI

// ------------------------------------------------------------------------------
// 10) 샘플러 세트 — 네이티브 bindless 백엔드(DX12 · Vulkan)만 여기서 선언한다. 에뮬 백엔드(DX11 · OpenGL)는 칸 결합 샘플러(GL g_SwEngineTex#Sampler) · DX11 정적 세트라
//     여기서 s0 를 또 선언하면 같은 레지스터를 두 샘플러가 나눠 갖는다(FXC 는 안 쓰면 조용히 버려 드러나지 않았다). DX11 의 정적 세트는 11-2 절.
//     DX12 는 루트 시그니처 정적 샘플러 s0..s7, Vulkan 은 set SW_VK_TEXTURE_SET 의 immutable sampler 배열(binding 1) + 비교 샘플러(binding 2).
//     번호는 binding_slots.hlsli 4 절의 SW_SAMPLER_* 다 — 언리얼의 정적 샘플러 세트에 해당한다.
// ------------------------------------------------------------------------------
#if defined( SW_NATIVE_BINDLESS )
    #if defined( __spirv__ )
// Vulkan: set 1 binding 1 의 immutable sampler 배열 — 배열 그대로 인덱싱한다.
[[vk::binding( SW_VK_SAMPLER_BINDING, SW_VK_TEXTURE_SET )]] SamplerState                  g_SwSamplers[SW_STATIC_SAMPLER_ARRAY_COUNT] : register( s0 );
[[vk::binding( SW_VK_SHADOW_SAMPLER_BINDING, SW_VK_TEXTURE_SET )]] SamplerComparisonState g_SwSamplerShadowCmp : register( SW_CAT( s, SW_SAMPLER_SHADOW_CMP ) );
        // 매크로로 남긴 이유: 백엔드마다 선언 모양이 다르다(Vulkan 은 배열 인덱싱, DX12 는 개별 변수 + 리터럴 분기). 함수로 감싸 `SamplerState` 를 돌려주는 길은
        // 컴파일 · GL 결합 샘플러 경로를 확인할 수 없어 보류했고, 호출처는 `Bindless::sampleSlotWith` 와 fsr1 몇 곳뿐이다.
        #define SW_SAMPLER_STATE( samplerID ) g_SwSamplers[samplerID]
    #else
// DX12: 정적 샘플러는 배열 선언(s0..s6 범위)을 못 채운다 — 루트 시그니처가 범위를 디스크립터 테이블로 요구해 PSO 생성이
// "sampler descriptor range not fully bound" 로 실패한다. 하나씩 선언하고 리터럴 분기로 고른다(언리얼도 정적 샘플러를 개별 선언).
SamplerState           g_SwSampler0 : register( s0 );
SamplerState           g_SwSampler1 : register( s1 );
SamplerState           g_SwSampler2 : register( s2 );
SamplerState           g_SwSampler3 : register( s3 );
SamplerState           g_SwSampler4 : register( s4 );
SamplerState           g_SwSampler5 : register( s5 );
SamplerState           g_SwSampler6 : register( s6 );
SamplerComparisonState g_SwSamplerShadowCmp : register( SW_CAT( s, SW_SAMPLER_SHADOW_CMP ) );
    #endif
#endif

// ------------------------------------------------------------------------------
// 11) 텍스처 읽기 — 백엔드 분기는 이 절 안에서 끝난다. 셰이더는 0 절의 진입 함수만 부른다.
//     11-1 네이티브 bindless(DX12 · Vulkan): 무제한 배열 + 인덱스 마스크.
//     11-2 슬롯 에뮬(DX11 · OpenGL): 고정 슬롯 + PassCB 인덱스 값 비교.
//     11-3 공통 진입 함수: 머티리얼 텍스처와 이름 붙은 엔진 텍스처(PassTexture::sampleShadow 등) — 두 경로 위에 하나로 선다.
//     컴퓨트 RW 텍스처(Bindless::storeRwTexture2D / Bindless::loadRwTexture2D)는 컴퓨트 스테이지에서만 선언된다.
// ------------------------------------------------------------------------------
#if defined( SW_NATIVE_BINDLESS )

    // ---- 11-1) 네이티브 bindless (DX12 · Vulkan) ----------------------------------

    // 무제한 텍스처 배열 — DX12 t0 space1 (루트 시그니처 테이블), Vulkan set 1 binding 0. SM5.1 무제한 배열이지 6.6 힙 인덱싱이 아니다.
    #if defined( __spirv__ )
[[vk::binding( SW_VK_TEXTURE_BINDING, SW_VK_TEXTURE_SET )]] Texture2D g_SwBindlessTex2D[] : register( t0, SW_CAT( space, SW_SPACE_BINDLESS_TEX ) );
    #else
Texture2D g_SwBindlessTex2D[] : register( t0, SW_CAT( space, SW_SPACE_BINDLESS_TEX ) );
    #endif
/**
 * @brief bindless 배열 첨자 — 인덱스를 배열 칸 수로 마스크해 무효 인덱스(kInvalidIndex)도 배열 안을 가리키게 합니다. 유효한 인덱스는 그대로입니다.
 * @details 주의: `if ( index == kInvalidIndex )` 분기만으로는 모자라다. 드라이버 컴파일러가 서술자 읽기를 분기 앞으로 끌어올리면(서술자 로드는
 *          부작용이 없다고 본다) 0xFFFFFFFF × 서술자 크기를 32 비트로 계산한 힙 시작 + 0xFFFFFFE0 을 읽어 페이지 폴트(DEVICE_HUNG)가 난다.
 *          값을 고르는 분기는 그대로 두고 첨자만 마스크한다 — DXC 는 분기 안에서 index != kInvalidIndex 를 알아도 마스크를 지우지 못한다.
 */
static uint Bindless::textureSlot( uint index )
{
    #if defined( __spirv__ )
    return index & ( SW_VK_BINDLESS_TEXTURE_COUNT - 1u );
    #else
    return index & ( SW_DX12_DESCRIPTOR_HEAP_SIZE - 1u );
    #endif
}
/** @brief 인덱스의 텍스처를 샘플러 세트의 samplerID(SW_SAMPLER_*, 비교 샘플러 제외)로 샘플링합니다. */
static float4 Bindless::sampleIndexWith( uint index, uint samplerID, float2 uv )
{
    if ( index == kInvalidIndex )
        return float4( 0, 0, 0, 1 );
    const uint slot = Bindless::textureSlot( index );
    #if defined( __spirv__ )
    return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].Sample( SW_SAMPLER_STATE( samplerID ), uv );
    #else
    switch ( samplerID )
    {
        case 1:
            return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].Sample( g_SwSampler1, uv );
        case 2:
            return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].Sample( g_SwSampler2, uv );
        case 3:
            return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].Sample( g_SwSampler3, uv );
        case 4:
            return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].Sample( g_SwSampler4, uv );
        case 5:
            return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].Sample( g_SwSampler5, uv );
        case 6:
            return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].Sample( g_SwSampler6, uv );
        default:
            return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].Sample( g_SwSampler0, uv );
    }
    #endif
}
/** @brief 엔진 텍스처(풀스크린 입력 · G버퍼 · 깊이 · 그림자 맵)를 엔진 텍스처 샘플러(선형 · 클램프)로 읽습니다 — 에뮬 백엔드가 t0..t3 에 거는 것과 같다. */
static float4 Bindless::sampleIndex( uint index, float2 uv )
{
    return Bindless::sampleIndexWith( index, SW_ENGINE_TEXTURE_SAMPLER, uv );
}
/**
 * @brief 씬 깊이의 2x2 이웃을 **텍스처 연산 한 번**으로 읽습니다 (`GatherRed`).
 * @details 게더는 필터를 하지 않고 텍셀 넷을 그대로 돌려준다 — 값은 바이리니어 샘플 하나와 비슷한데
 *          텍셀은 넷이다. 그래서 십자 다섯 칸이 샘플 다섯 번이 아니라 게더 두 번이면 된다.
 * @note 샘플러 3 번은 `SW_SAMPLER_POINT_CLAMP` 다 (binding_slots.hlsli 4 의 표).
 *       게더는 필터 모드를 안 보지만 **주소 모드는 본다** — 화면 가장자리에서 WRAP 이면 반대편을 읽는다.
 */
static float4 PassTexture::gatherDepthRed( float2 uv )
{
    if ( g_SceneDepthIndex == kInvalidIndex )
        return float4( 1, 1, 1, 1 );
    const uint slot = Bindless::textureSlot( g_SceneDepthIndex );
    #if defined( __spirv__ )
    return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].GatherRed( SW_SAMPLER_STATE( SW_SAMPLER_POINT_CLAMP ), uv );
    #else
    return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].GatherRed( g_SwSampler3, uv );
    #endif
}
/** @brief 깊이 텍스처를 비교 샘플러(LESS_EQUAL)로 읽습니다 — 1 이면 depth 가 저장값 이하(빛 받음). */
static float Bindless::sampleShadowComparison( uint index, float2 uv, float depth )
{
    if ( index == kInvalidIndex )
        return 1.0f;
    return g_SwBindlessTex2D[NonUniformResourceIndex( Bindless::textureSlot( index ) )].SampleCmpLevelZero( g_SwSamplerShadowCmp, uv, depth );
}

    #if defined( SW_STAGE_COMPUTE )
        // 컴퓨트 RW 텍스처 배열 — DX12 u0 space1 (텍스처 테이블의 두 번째 범위), Vulkan set 1 binding 3 (STORAGE_IMAGE[]).
        // 인덱스는 registerBindlessTextureUAV 가 준다. 컴퓨트에서만 선언한다 — 그래픽스 스테이지에 UAV 배열을 두면
        // DX12 가 PS UAV 슬롯을, Vulkan 이 vertexPipelineStores 기능을 요구한다.
        #if defined( __spirv__ )
[[vk::binding( SW_VK_RWTEXTURE_BINDING, SW_VK_TEXTURE_SET )]] RWTexture2D<float4> g_SwBindlessRWTex2D[] : register( u0, SW_CAT( space, SW_SPACE_BINDLESS_TEX ) );
        #else
RWTexture2D<float4> g_SwBindlessRWTex2D[] : register( u0, SW_CAT( space, SW_SPACE_BINDLESS_TEX ) );
        #endif
/** @brief RW 텍스처 배열 첨자 — `Bindless::textureSlot` 과 같은 이유로 배열 칸 수로 마스크합니다. */
static uint Bindless::rwTextureSlot( uint index )
{
        #if defined( __spirv__ )
    return index & ( SW_VK_BINDLESS_STORAGE_IMAGE_COUNT - 1u );
        #else
    return index & ( SW_DX12_DESCRIPTOR_HEAP_SIZE - 1u );
        #endif
}
static void Bindless::storeRwTexture2D( uint index, uint2 texelPosition, float4 value )
{
    if ( index == kInvalidIndex )
        return;
    g_SwBindlessRWTex2D[NonUniformResourceIndex( Bindless::rwTextureSlot( index ) )][texelPosition] = value;
}
static float4 Bindless::loadRwTexture2D( uint index, uint2 texelPosition )
{
    if ( index == kInvalidIndex )
        return float4( 0, 0, 0, 0 );
    return g_SwBindlessRWTex2D[NonUniformResourceIndex( Bindless::rwTextureSlot( index ) )][texelPosition];
}
    #endif // SW_STAGE_COMPUTE

static float4 Bindless::loadEngineTexel( uint index, [[maybe_unused]] uint emulatedSlot, int2 texel )
{
    return g_SwBindlessTex2D[NonUniformResourceIndex( Bindless::textureSlot( index ) )].Load( int3( texel, 0 ) );
}
static uint2 Bindless::getEngineTextureSize( uint index, [[maybe_unused]] uint emulatedSlot )
{
    uint2 size;
    g_SwBindlessTex2D[NonUniformResourceIndex( Bindless::textureSlot( index ) )].GetDimensions( size.x, size.y );
    return size;
}
/** @brief 게더는 필터를 보지 않고 주소 모드만 본다 — 점 · 클램프 샘플러(3 번, PassTexture::gatherDepthRed 와 같다)로 가장자리 밖을 반대편에서 읽지 않는다. */
static float4 Bindless::gatherEngineChannel( uint index, [[maybe_unused]] uint emulatedSlot, uint channel, float2 uv )
{
    const uint slot = Bindless::textureSlot( index );
    #if defined( __spirv__ )
    if ( channel == 0u )
        return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].GatherRed( SW_SAMPLER_STATE( SW_SAMPLER_POINT_CLAMP ), uv );
    if ( channel == 1u )
        return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].GatherGreen( SW_SAMPLER_STATE( SW_SAMPLER_POINT_CLAMP ), uv );
    return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].GatherBlue( SW_SAMPLER_STATE( SW_SAMPLER_POINT_CLAMP ), uv );
    #else
    if ( channel == 0u )
        return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].GatherRed( g_SwSampler3, uv );
    if ( channel == 1u )
        return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].GatherGreen( g_SwSampler3, uv );
    return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].GatherBlue( g_SwSampler3, uv );
    #endif
}

#else // !SW_NATIVE_BINDLESS

    // ---- 11-2) 슬롯 에뮬 (DX11 · OpenGL) ------------------------------------------
    #if !defined( SW_STAGE_COMPUTE )
// 에뮬 백엔드의 샘플 슬롯은 그래픽스 스테이지 전용이다 — GL 은 텍스처 유닛과 이미지 유닛이 SPIR-V 에서 같은 binding 번호를
// 쓰므로 컴퓨트에서 둘을 함께 선언하면 DXC 가 RW 텍스처를 결합 샘플러와 합쳐 버린다(엔진도 컴퓨트에는 엔진 텍스처를 걸지 않는다).
// 엔진 텍스처는 역할 하나가 한 장이다(칸 번호 = C++ 표 shaderslot::emulated::kEngineTextureNames 의 순서, 컴파일러가 SW_EMU_SLOT_<이름> 으로 넣는다).
// 칸을 나눠 쓰지 않으므로 한 패스가 어떤 역할을 함께 읽어도 겹치지 않는다(언리얼 D3D11 RHI 가 셰이더 매개변수마다 레지스터를 따로 두는 것과 같다).
// 레지스터는 DX11 t32.., GL 텍스처 유닛 0..(binding_slots.hlsli 2). DX11 은 샘플러 슬롯이 16 개뿐이라 짝이 없고 정적 세트의 엔진 텍스처 샘플러로 읽는다.
SW_DECLARE_ENGINE_TEXTURE( 0 );
SW_DECLARE_ENGINE_TEXTURE( 1 );
SW_DECLARE_ENGINE_TEXTURE( 2 );
SW_DECLARE_ENGINE_TEXTURE( 3 );
SW_DECLARE_ENGINE_TEXTURE( 4 );
SW_DECLARE_ENGINE_TEXTURE( 5 );
SW_DECLARE_ENGINE_TEXTURE( 6 );
SW_DECLARE_ENGINE_TEXTURE( 7 );
SW_DECLARE_ENGINE_TEXTURE( 8 );
SW_DECLARE_ENGINE_TEXTURE( 9 );
SW_DECLARE_ENGINE_TEXTURE( 10 );
SW_DECLARE_ENGINE_TEXTURE( 11 );
SW_DECLARE_ENGINE_TEXTURE( 12 );
SW_DECLARE_ENGINE_TEXTURE( 13 );
SW_DECLARE_ENGINE_TEXTURE( 14 );
SW_DECLARE_ENGINE_TEXTURE( 15 );
SW_DECLARE_ENGINE_TEXTURE( 16 );
        #if SW_ENGINE_TEXTURE_COUNT != 17
            #error "SW_ENGINE_TEXTURE_COUNT changed - match the declarations above and SW_ENGINE_TEXTURE_SWITCH"
        #endif

        // 머티리얼 텍스처 고정 슬롯 t16..t31(16 칸) — 번호는 binding_slots.hlsli 가 정한다 (C++ shaderslot::kMaterialTexture0 와 같은 파일).
        // DX11 은 샘플러 슬롯이 16 개뿐이라 머티리얼 슬롯에 짝 샘플러를 두지 않는다 — 정적 세트의 머티리얼 샘플러(SW_MATERIAL_TEXTURE_SAMPLER)로 읽는다.
        // GL 은 결합 샘플러뿐이라 슬롯마다 짝을 두고, 엔진이 그 유닛에 머티리얼 샘플러를 건다(OpenGLRHIDevice 초기화).
        #if defined( SW_SEPARATE_SAMPLER_EMULATION )
SW_DECLARE_TEXTURE2D( g_SwMaterialTex0, SW_SLOT_MATERIAL_TEX0 );
SW_DECLARE_TEXTURE2D( g_SwMaterialTex1, SW_SLOT_MATERIAL_TEX1 );
SW_DECLARE_TEXTURE2D( g_SwMaterialTex2, SW_SLOT_MATERIAL_TEX2 );
SW_DECLARE_TEXTURE2D( g_SwMaterialTex3, SW_SLOT_MATERIAL_TEX3 );
SW_DECLARE_TEXTURE2D( g_SwMaterialTex4, SW_SLOT_MATERIAL_TEX4 );
SW_DECLARE_TEXTURE2D( g_SwMaterialTex5, SW_SLOT_MATERIAL_TEX5 );
SW_DECLARE_TEXTURE2D( g_SwMaterialTex6, SW_SLOT_MATERIAL_TEX6 );
SW_DECLARE_TEXTURE2D( g_SwMaterialTex7, SW_SLOT_MATERIAL_TEX7 );
SW_DECLARE_TEXTURE2D( g_SwMaterialTex8, SW_SLOT_MATERIAL_TEX8 );
SW_DECLARE_TEXTURE2D( g_SwMaterialTex9, SW_SLOT_MATERIAL_TEX9 );
SW_DECLARE_TEXTURE2D( g_SwMaterialTex10, SW_SLOT_MATERIAL_TEX10 );
SW_DECLARE_TEXTURE2D( g_SwMaterialTex11, SW_SLOT_MATERIAL_TEX11 );
SW_DECLARE_TEXTURE2D( g_SwMaterialTex12, SW_SLOT_MATERIAL_TEX12 );
SW_DECLARE_TEXTURE2D( g_SwMaterialTex13, SW_SLOT_MATERIAL_TEX13 );
SW_DECLARE_TEXTURE2D( g_SwMaterialTex14, SW_SLOT_MATERIAL_TEX14 );
SW_DECLARE_TEXTURE2D( g_SwMaterialTex15, SW_SLOT_MATERIAL_TEX15 );
        #else
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex0, g_SwMaterialTex0Sampler, SW_SLOT_MATERIAL_TEX0 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex1, g_SwMaterialTex1Sampler, SW_SLOT_MATERIAL_TEX1 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex2, g_SwMaterialTex2Sampler, SW_SLOT_MATERIAL_TEX2 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex3, g_SwMaterialTex3Sampler, SW_SLOT_MATERIAL_TEX3 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex4, g_SwMaterialTex4Sampler, SW_SLOT_MATERIAL_TEX4 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex5, g_SwMaterialTex5Sampler, SW_SLOT_MATERIAL_TEX5 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex6, g_SwMaterialTex6Sampler, SW_SLOT_MATERIAL_TEX6 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex7, g_SwMaterialTex7Sampler, SW_SLOT_MATERIAL_TEX7 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex8, g_SwMaterialTex8Sampler, SW_SLOT_MATERIAL_TEX8 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex9, g_SwMaterialTex9Sampler, SW_SLOT_MATERIAL_TEX9 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex10, g_SwMaterialTex10Sampler, SW_SLOT_MATERIAL_TEX10 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex11, g_SwMaterialTex11Sampler, SW_SLOT_MATERIAL_TEX11 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex12, g_SwMaterialTex12Sampler, SW_SLOT_MATERIAL_TEX12 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex13, g_SwMaterialTex13Sampler, SW_SLOT_MATERIAL_TEX13 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex14, g_SwMaterialTex14Sampler, SW_SLOT_MATERIAL_TEX14 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex15, g_SwMaterialTex15Sampler, SW_SLOT_MATERIAL_TEX15 );
        #endif

        #if defined( SW_SEPARATE_SAMPLER_EMULATION )
// DX11 정적 샘플러 세트 s9..s15 (binding_slots.hlsli 4) — 엔진이 디바이스 초기화 때 건다(D3D11RHIDevice::bindStaticSamplers).
// DX12 와 같은 표(SW_SAMPLER_*)라 셰이더가 고른 samplerID 가 DX11 에서도 존중된다.
SamplerState g_SwSampler0 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER0 ) );
SamplerState g_SwSampler1 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER1 ) );
SamplerState g_SwSampler2 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER2 ) );
SamplerState g_SwSampler3 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER3 ) );
SamplerState g_SwSampler4 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER4 ) );
SamplerState g_SwSampler5 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER5 ) );
SamplerState g_SwSampler6 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER6 ) );
/** @brief 슬롯 텍스처를 정적 샘플러 세트의 samplerID 로 샘플링합니다 — SM5.0 은 샘플러 배열 동적 인덱싱이 없어 리터럴 분기. */
static float4 Bindless::sampleSlotWith( Texture2D slotTexture, uint samplerID, float2 uv )
{
    switch ( samplerID )
    {
        case 1:
            return slotTexture.Sample( g_SwSampler1, uv );
        case 2:
            return slotTexture.Sample( g_SwSampler2, uv );
        case 3:
            return slotTexture.Sample( g_SwSampler3, uv );
        case 4:
            return slotTexture.Sample( g_SwSampler4, uv );
        case 5:
            return slotTexture.Sample( g_SwSampler5, uv );
        case 6:
            return slotTexture.Sample( g_SwSampler6, uv );
        default:
            return slotTexture.Sample( g_SwSampler0, uv );
    }
}
        #endif

        // 엔진 텍스처 칸 ordinal 의 텍스처 · 샘플러로 OP( 텍스처, 샘플러 ) 문장을 펼친다 — SM5.0 은 리소스 배열 동적 인덱싱이 없어 리터럴 분기다. 칸 밖이면 아무것도 하지 않는다.
        #define SW_ENGINE_TEXTURE_SWITCH( ordinal, OP )                    \
            switch ( ordinal )                                             \
            {                                                              \
                case 0u:                                                   \
                    OP( SW_ENGINE_TEXTURE( 0 ), SW_ENGINE_SAMPLER( 0 ) )   \
                    break;                                                 \
                case 1u:                                                   \
                    OP( SW_ENGINE_TEXTURE( 1 ), SW_ENGINE_SAMPLER( 1 ) )   \
                    break;                                                 \
                case 2u:                                                   \
                    OP( SW_ENGINE_TEXTURE( 2 ), SW_ENGINE_SAMPLER( 2 ) )   \
                    break;                                                 \
                case 3u:                                                   \
                    OP( SW_ENGINE_TEXTURE( 3 ), SW_ENGINE_SAMPLER( 3 ) )   \
                    break;                                                 \
                case 4u:                                                   \
                    OP( SW_ENGINE_TEXTURE( 4 ), SW_ENGINE_SAMPLER( 4 ) )   \
                    break;                                                 \
                case 5u:                                                   \
                    OP( SW_ENGINE_TEXTURE( 5 ), SW_ENGINE_SAMPLER( 5 ) )   \
                    break;                                                 \
                case 6u:                                                   \
                    OP( SW_ENGINE_TEXTURE( 6 ), SW_ENGINE_SAMPLER( 6 ) )   \
                    break;                                                 \
                case 7u:                                                   \
                    OP( SW_ENGINE_TEXTURE( 7 ), SW_ENGINE_SAMPLER( 7 ) )   \
                    break;                                                 \
                case 8u:                                                   \
                    OP( SW_ENGINE_TEXTURE( 8 ), SW_ENGINE_SAMPLER( 8 ) )   \
                    break;                                                 \
                case 9u:                                                   \
                    OP( SW_ENGINE_TEXTURE( 9 ), SW_ENGINE_SAMPLER( 9 ) )   \
                    break;                                                 \
                case 10u:                                                  \
                    OP( SW_ENGINE_TEXTURE( 10 ), SW_ENGINE_SAMPLER( 10 ) ) \
                    break;                                                 \
                case 11u:                                                  \
                    OP( SW_ENGINE_TEXTURE( 11 ), SW_ENGINE_SAMPLER( 11 ) ) \
                    break;                                                 \
                case 12u:                                                  \
                    OP( SW_ENGINE_TEXTURE( 12 ), SW_ENGINE_SAMPLER( 12 ) ) \
                    break;                                                 \
                case 13u:                                                  \
                    OP( SW_ENGINE_TEXTURE( 13 ), SW_ENGINE_SAMPLER( 13 ) ) \
                    break;                                                 \
                case 14u:                                                  \
                    OP( SW_ENGINE_TEXTURE( 14 ), SW_ENGINE_SAMPLER( 14 ) ) \
                    break;                                                 \
                case 15u:                                                  \
                    OP( SW_ENGINE_TEXTURE( 15 ), SW_ENGINE_SAMPLER( 15 ) ) \
                    break;                                                 \
                case 16u:                                                  \
                    OP( SW_ENGINE_TEXTURE( 16 ), SW_ENGINE_SAMPLER( 16 ) ) \
                    break;                                                 \
                default:                                                   \
                    break;                                                 \
            }

/**
 * @brief 에뮬 백엔드(DX11 · GL)의 슬롯 표에 없는 엔진 텍스처 인덱스를 읽었을 때 내는 오류 색(자홍)입니다.
 * @details 표에 없는 인덱스를 슬롯 0 으로 읽지 않는다 — 그러면 그림자 · 원본 색이 엉뚱한 자리에 조용히 나온다. PassCB 의 g_*Index 는 C++ 표
 *          (`shaderslot::emulated::kEngineTextureNames`)에서 선언되므로 이 색은 PassCB 밖 값(루트 상수 · 머티리얼 인덱스)을 `Bindless::sampleIndex` 에
 *          넘긴 셰이더에서만 난다 — 그런 입력은 엔진 텍스처 역할(표)로 걸거나 `loadEngineTexel( index, SW_EMU_SLOT_<역할> )` 로 칸을 직접 고른다.
 */
static const float4 kUnmappedEngineTextureColor = float4( 1, 0, 1, 1 );
static const uint   kUnmappedEngineTextureSlot  = 0xFFFFFFFFu;

// 칸 번호 · 인덱스 이름은 셰이더 컴파일러가 C++ 표에서 내는 매크로다(ShaderCompiler::appendEngineDefines) — 여기에 손으로 맞춘 표가 없다.
// SW_EMU_SLOT_<이름> 은 역할을 이름으로 고르는 자리(PassTexture · 히스토리 읽기)가, SW_ENGINE_TEXTURE_INDEX_<칸> 은 아래 되짚기가 쓴다.
// PassCB 의 엔진 텍스처 필드도 같은 매크로로 선언하므로(binding.hlsli 1 절) 셰이더가 가진 g_*Index 는 모두 이 표에 있다.

/** @brief 엔진 텍스처 인덱스가 걸리는 칸 번호(역할 하나에 한 칸)입니다. PassCB 의 g_*Index 가 아닌 값이면 `kUnmappedEngineTextureSlot` 입니다. */
static uint Bindless::emulatedSlot( uint index )
{
    if ( index == SW_ENGINE_TEXTURE_INDEX_0 )
        return 0u;
    if ( index == SW_ENGINE_TEXTURE_INDEX_1 )
        return 1u;
    if ( index == SW_ENGINE_TEXTURE_INDEX_2 )
        return 2u;
    if ( index == SW_ENGINE_TEXTURE_INDEX_3 )
        return 3u;
    if ( index == SW_ENGINE_TEXTURE_INDEX_4 )
        return 4u;
    if ( index == SW_ENGINE_TEXTURE_INDEX_5 )
        return 5u;
    if ( index == SW_ENGINE_TEXTURE_INDEX_6 )
        return 6u;
    if ( index == SW_ENGINE_TEXTURE_INDEX_7 )
        return 7u;
    if ( index == SW_ENGINE_TEXTURE_INDEX_8 )
        return 8u;
    if ( index == SW_ENGINE_TEXTURE_INDEX_9 )
        return 9u;
    if ( index == SW_ENGINE_TEXTURE_INDEX_10 )
        return 10u;
    if ( index == SW_ENGINE_TEXTURE_INDEX_11 )
        return 11u;
    if ( index == SW_ENGINE_TEXTURE_INDEX_12 )
        return 12u;
    if ( index == SW_ENGINE_TEXTURE_INDEX_13 )
        return 13u;
    if ( index == SW_ENGINE_TEXTURE_INDEX_14 )
        return 14u;
    if ( index == SW_ENGINE_TEXTURE_INDEX_15 )
        return 15u;
    if ( index == SW_ENGINE_TEXTURE_INDEX_16 )
        return 16u;
    return kUnmappedEngineTextureSlot;
}

static float4 Bindless::sampleIndex( uint index, float2 uv )
{
    if ( index == kInvalidIndex )
        return float4( 0, 0, 0, 1 );
            // FrameRenderer 가 같은 C++ 표(commitBindlessTextureBindings)로 역할마다 제 칸에 바인딩한다.
        #define SW_OP( tex, smp ) return tex.Sample( smp, uv );
    SW_ENGINE_TEXTURE_SWITCH( Bindless::emulatedSlot( index ), SW_OP )
        #undef SW_OP
    return kUnmappedEngineTextureColor;
}
/**
 * @brief 씬 깊이의 2x2 이웃을 **텍스처 연산 한 번**으로 읽습니다 (`GatherRed`).
 * @details 에뮬 백엔드는 깊이가 늘 SceneDepth 칸이다 — 인덱스로 되짚을 필요가 없다. 게더는 필터 모드를 보지 않는다.
 */
static float4 PassTexture::gatherDepthRed( float2 uv )
{
    if ( g_SceneDepthIndex == kInvalidIndex )
        return float4( 1, 1, 1, 1 );
    return SW_ENGINE_TEXTURE( SW_EMU_SLOT_SceneDepth ).GatherRed( SW_ENGINE_SAMPLER( SW_EMU_SLOT_SceneDepth ), uv );
}
        #if defined( SW_SEPARATE_SAMPLER_EMULATION )
/** @brief DX11: 텍스처는 역할의 칸(Bindless::sampleIndex 와 같은 표), 샘플러는 정적 세트에서 samplerID 로 고른다. */
static float4 Bindless::sampleIndexWith( uint index, uint samplerID, float2 uv )
{
    if ( index == kInvalidIndex )
        return float4( 0, 0, 0, 1 );
            #define SW_OP( tex, smp ) return Bindless::sampleSlotWith( tex, samplerID, uv );
    SW_ENGINE_TEXTURE_SWITCH( Bindless::emulatedSlot( index ), SW_OP )
            #undef SW_OP
    return kUnmappedEngineTextureColor;
}
        #else
/** @brief 슬롯 텍스처를 샘플러 없이 가장 가까운 텍셀 하나로 읽습니다(점 샘플 · 클램프) — 결합 샘플러가 선형으로 묶인 OpenGL 에서 점 샘플을 얻는 길이다. */
static float4 Bindless::sampleSlotPoint( Texture2D slotTexture, float2 uv )
{
    float2 size;
    slotTexture.GetDimensions( size.x, size.y );
    const int2 texel = clamp( int2( floor( uv * size ) ), int2( 0, 0 ), int2( size ) - int2( 1, 1 ) );
    return slotTexture.Load( int3( texel, 0 ) );
}
/**
 * @brief OpenGL 은 결합 샘플러뿐(ARB_gl_spirv 는 분리 샘플러를 못 쓴다)이라 칸의 샘플러 상태는 엔진이 정한다(엔진 텍스처 칸은 선형 · 클램프).
 * @details 점 샘플(`SW_SAMPLER_POINT_CLAMP`)을 달라는 호출은 샘플러를 거치지 않고 텍셀을 직접 읽는다 — 안 그러면 정수 업스케일 뷰(`SW_PASS_FLAG_POINT_UPSCALE`)의
 *          Present 가 GL 에서만 선형으로 섞는다. 나머지 samplerID 는 슬롯의 샘플러를 그대로 쓴다.
 */
static float4 Bindless::sampleIndexWith( uint index, uint samplerID, float2 uv )
{
    if ( samplerID != SW_SAMPLER_POINT_CLAMP )
        return Bindless::sampleIndex( index, uv );
    if ( index == kInvalidIndex )
        return float4( 0, 0, 0, 1 );
            #define SW_OP( tex, smp ) return Bindless::sampleSlotPoint( tex, uv );
    SW_ENGINE_TEXTURE_SWITCH( Bindless::emulatedSlot( index ), SW_OP )
            #undef SW_OP
    return kUnmappedEngineTextureColor;
}
        #endif
/**
 * @brief 에뮬 백엔드: 비교 샘플러가 없어 2x2 이웃을 게더로 읽어 각각 비교하고 쌍선형으로 섞는다 — DX12 · Vulkan 의 비교 샘플러(LINEAR)와 같은 값이다.
 * @details 한 점만 비교하면 가장자리가 텍셀 계단이고, 하드웨어 비교 백엔드와 그림이 갈린다. 그림자 맵은 ShadowMap 칸이다(Bindless::sampleIndex 의 표).
 */
static float Bindless::sampleShadowComparison( uint index, float2 uv, float depth )
{
    if ( index == kInvalidIndex )
        return 1.0f;
    if ( index != g_ShadowMapIndex )
        return ( depth <= Bindless::sampleIndex( index, uv ).r ) ? 1.0f : 0.0f;
    float2 size;
    SW_ENGINE_TEXTURE( SW_EMU_SLOT_ShadowMap ).GetDimensions( size.x, size.y );
    const float2 texelPosition = uv * size - 0.5f;
    const float2 weight        = frac( texelPosition );
    // 게더는 네 텍셀이 만나는 모서리를 가리킨다. 순서는 w (0,0) · z (1,0) · x (0,1) · y (1,1).
    const float4 stored = SW_ENGINE_TEXTURE( SW_EMU_SLOT_ShadowMap ).GatherRed( SW_ENGINE_SAMPLER( SW_EMU_SLOT_ShadowMap ), ( floor( texelPosition ) + 1.0f ) / size );
    const float4 lit    = step( depth.xxxx, stored ); // depth <= 저장값이면 1 (LESS_EQUAL)
    return lerp( lerp( lit.w, lit.z, weight.x ), lerp( lit.x, lit.y, weight.x ), weight.y );
}

/** @brief 패스가 칸 @p emulatedSlot(`SW_EMU_SLOT_<역할>`)에 건 엔진 텍스처의 밉 0 텍셀을 읽습니다. 칸 밖이면 오류 색(자홍)을 냅니다 — 조용히 0 번으로 읽지 않는다. */
// 에뮬 백엔드(DX11 · GL)의 셰이더 컴파일은 매개변수 속성 `[[maybe_unused]]` 를 읽지 못한다(X3000) — 쓰지 않는 index 는 이름만 두고 속성을 달지 않는다.
static float4 Bindless::loadEngineTexel( uint index, uint emulatedSlot, int2 texel )
{
        #define SW_OP( tex, smp ) return tex.Load( int3( texel, 0 ) );
    SW_ENGINE_TEXTURE_SWITCH( emulatedSlot, SW_OP )
        #undef SW_OP
    return kUnmappedEngineTextureColor;
}
static uint2 Bindless::getEngineTextureSize( uint index, uint emulatedSlot )
{
    uint2 size = uint2( 0, 0 );
        #define SW_OP( tex, smp ) tex.GetDimensions( size.x, size.y );
    SW_ENGINE_TEXTURE_SWITCH( emulatedSlot, SW_OP )
        #undef SW_OP
    return size;
}
/** @brief 칸 @p emulatedSlot 의 2x2 게더입니다(channel 0 R · 1 G · 2 B). 게더는 필터를 보지 않고 주소 모드(클램프)만 본다. */
static float4 Bindless::gatherEngineChannel( uint index, uint emulatedSlot, uint channel, float2 uv )
{
        #define SW_OP( tex, smp ) return channel == 0u ? tex.GatherRed( smp, uv ) : ( channel == 1u ? tex.GatherGreen( smp, uv ) : tex.GatherBlue( smp, uv ) );
    SW_ENGINE_TEXTURE_SWITCH( emulatedSlot, SW_OP )
        #undef SW_OP
    return kUnmappedEngineTextureColor;
}

    #endif // !SW_STAGE_COMPUTE

    #if defined( SW_STAGE_COMPUTE )
        // 컴퓨트 RW 텍스처 고정 슬롯 u5..u7 — 엔진이 bindComputeUAV( index, SW_SLOT_COMPUTE_TEXUAV0 + 서수 ) 로 건다. index = 서수.
        // GL 은 슬롯마다 묶는 텍스처의 포맷을 셰이더가 정한다(common.hlsli 의 SW_DECLARE_RW_TEXTURE2D). 포함하기 전에 SW_RW_SLOT<n>_GL_FORMAT 을 정의하지 않으면 rgba32f 다.
        #if !defined( SW_RW_SLOT0_GL_FORMAT )
            #define SW_RW_SLOT0_GL_FORMAT "rgba32f"
        #endif
        #if !defined( SW_RW_SLOT1_GL_FORMAT )
            #define SW_RW_SLOT1_GL_FORMAT "rgba32f"
        #endif
        #if !defined( SW_RW_SLOT2_GL_FORMAT )
            #define SW_RW_SLOT2_GL_FORMAT "rgba32f"
        #endif
SW_DECLARE_RW_TEXTURE2D( g_SwRWSlot0, SW_SLOT_COMPUTE_TEXUAV0, SW_GL_IMAGE_UNIT0, SW_RW_SLOT0_GL_FORMAT );
SW_DECLARE_RW_TEXTURE2D( g_SwRWSlot1, SW_SLOT_COMPUTE_TEXUAV1, SW_GL_IMAGE_UNIT1, SW_RW_SLOT1_GL_FORMAT );
SW_DECLARE_RW_TEXTURE2D( g_SwRWSlot2, SW_SLOT_COMPUTE_TEXUAV2, SW_GL_IMAGE_UNIT2, SW_RW_SLOT2_GL_FORMAT );
static void Bindless::storeRwTexture2D( uint index, uint2 texelPosition, float4 value )
{
    if ( index == 0 )
        g_SwRWSlot0[texelPosition] = value;
    else if ( index == 1 )
        g_SwRWSlot1[texelPosition] = value;
    else if ( index == 2 )
        g_SwRWSlot2[texelPosition] = value;
}
static float4 Bindless::loadRwTexture2D( uint index, uint2 texelPosition )
{
    if ( index == 0 )
        return g_SwRWSlot0[texelPosition];
    if ( index == 1 )
        return g_SwRWSlot1[texelPosition];
    if ( index == 2 )
        return g_SwRWSlot2[texelPosition];
    return float4( 0, 0, 0, 0 );
}
    #endif // SW_STAGE_COMPUTE

#endif // SW_NATIVE_BINDLESS

// ---- 11-3) 공통 진입 함수 (네 백엔드) -----------------------------------------
#if defined( SW_NATIVE_BINDLESS ) || !defined( SW_STAGE_COMPUTE )
    /**
     * @brief 2D 빛 쿠키 아틀라스(패스 역할 `Light2DCookieAtlas`)를 밉 0 으로 읽습니다. 빛 반복 안에서 부르므로 암시 미분을 쓰지 않는다.
     * @details 미분이 든 샘플을 길이가 정해지지 않은 반복에 두면 FXC(DX11)가 반복을 펼치려다 실패한다(X3511). 아틀라스는 밉이 없는 렌더 타깃이라
     *          밉 0 이 곧 그 값이다. 에뮬 백엔드는 아틀라스를 제 칸에 건다(`Bindless::sampleIndex` 의 표). 아틀라스가 없으면 `Bindless::sampleIndex` 와 같이 검정이다.
     */
    #if SW_ENGINE_TEXTURE_SAMPLER != 1
        #error PassTexture::sampleLight2dCookieAtlas 는 엔진 텍스처 샘플러가 정적 샘플러 1 번(g_SwSampler1)이라고 가정한다
    #endif
static float4 PassTexture::sampleLight2dCookieAtlas( float2 uv )
{
    if ( g_Light2DCookieAtlasIndex == kInvalidIndex )
        return float4( 0, 0, 0, 1 );
    #if defined( SW_NATIVE_BINDLESS )
    const uint slot = Bindless::textureSlot( g_Light2DCookieAtlasIndex );
        #if defined( __spirv__ )
    return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].SampleLevel( SW_SAMPLER_STATE( SW_ENGINE_TEXTURE_SAMPLER ), uv, 0.0f );
        #else
    return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].SampleLevel( g_SwSampler1, uv, 0.0f );
        #endif
    #else
    return SW_ENGINE_TEXTURE( SW_EMU_SLOT_Light2DCookieAtlas ).SampleLevel( SW_ENGINE_SAMPLER( SW_EMU_SLOT_Light2DCookieAtlas ), uv, 0.0f );
    #endif
}
    /**
     * @brief 머티리얼이 준 텍스처 인덱스를 샘플링합니다 (머티리얼 데이터의 uint 슬롯).
     * @details Bindless::sampleIndex 와 나누는 이유: 그쪽은 **엔진이 아는 인덱스**(그림자·G버퍼 등) 전용이다.
     *          DX11/OpenGL 은 bindless 가 없어 t0..t3 에 걸린 엔진 텍스처를 인덱스 값 비교로 되짚는
     *          에뮬 경로라, 머티리얼이 준 임의 인덱스는 풀 수 없다 — 그래서 엔진이 머티리얼 텍스처를
     *          t16..t31 에 서수 순서로 걸고 머티리얼 데이터에는 서수를 넣는다. DX12/Vulkan 은 전역 인덱스 그대로다.
     */
    #if defined( SW_VIEWMODE_MIPMAPS )
/**
 * @brief 보기 모드 Mipmaps — 머티리얼 텍스처를 읽는 밉 단계를 색으로 바꾼다(유니티 Scene 뷰 Mipmaps 와 같은 목적).
 * @details 확대(밉 0 보다 작다 — 텍스처 해상도가 모자란다)는 파랑, 밉 0 은 초록, 1 노랑, 2 주황, 3 이상 빨강(해상도가 남는다)이다. 텍스처가 없으면 흰색.
 *          픽셀 셰이더에서만 부른다(CalculateLevelOfDetail 은 미분이 필요하다).
 */
static float4 ViewMode::computeMipColor( float lod )
{
    if ( lod < -0.5f )
        return float4( 0.2f, 0.4f, 1.0f, 1.0f );
    const float4 arrColor[4] = { float4( 0.2f, 0.9f, 0.2f, 1.0f ), float4( 1.0f, 0.95f, 0.2f, 1.0f ), float4( 1.0f, 0.55f, 0.1f, 1.0f ), float4( 1.0f, 0.15f, 0.1f, 1.0f ) };
    return arrColor[min( uint( lod + 0.5f ), 3u )];
}
static float4 MaterialTexture::sampleMipColor( uint index, float2 uv )
{
        #if defined( SW_NATIVE_BINDLESS )
    if ( index == kInvalidIndex )
        return float4( 1, 1, 1, 1 );
    const uint slot = Bindless::textureSlot( index );
            #if defined( __spirv__ )
    return ViewMode::computeMipColor( g_SwBindlessTex2D[NonUniformResourceIndex( slot )].CalculateLevelOfDetail( SW_SAMPLER_STATE( SW_MATERIAL_TEXTURE_SAMPLER ), uv ) );
            #else
    return ViewMode::computeMipColor( g_SwBindlessTex2D[NonUniformResourceIndex( slot )].CalculateLevelOfDetail( g_SwSampler0, uv ) );
            #endif
        #else
    switch ( index )
    {
            #if defined( SW_SEPARATE_SAMPLER_EMULATION )
        case 0u:
            return ViewMode::computeMipColor( g_SwMaterialTex0.CalculateLevelOfDetail( g_SwSampler0, uv ) );
        case 1u:
            return ViewMode::computeMipColor( g_SwMaterialTex1.CalculateLevelOfDetail( g_SwSampler0, uv ) );
        case 2u:
            return ViewMode::computeMipColor( g_SwMaterialTex2.CalculateLevelOfDetail( g_SwSampler0, uv ) );
        case 3u:
            return ViewMode::computeMipColor( g_SwMaterialTex3.CalculateLevelOfDetail( g_SwSampler0, uv ) );
        case 4u:
            return ViewMode::computeMipColor( g_SwMaterialTex4.CalculateLevelOfDetail( g_SwSampler0, uv ) );
        case 5u:
            return ViewMode::computeMipColor( g_SwMaterialTex5.CalculateLevelOfDetail( g_SwSampler0, uv ) );
        case 6u:
            return ViewMode::computeMipColor( g_SwMaterialTex6.CalculateLevelOfDetail( g_SwSampler0, uv ) );
        case 7u:
            return ViewMode::computeMipColor( g_SwMaterialTex7.CalculateLevelOfDetail( g_SwSampler0, uv ) );
        case 8u:
            return ViewMode::computeMipColor( g_SwMaterialTex8.CalculateLevelOfDetail( g_SwSampler0, uv ) );
        case 9u:
            return ViewMode::computeMipColor( g_SwMaterialTex9.CalculateLevelOfDetail( g_SwSampler0, uv ) );
        case 10u:
            return ViewMode::computeMipColor( g_SwMaterialTex10.CalculateLevelOfDetail( g_SwSampler0, uv ) );
        case 11u:
            return ViewMode::computeMipColor( g_SwMaterialTex11.CalculateLevelOfDetail( g_SwSampler0, uv ) );
        case 12u:
            return ViewMode::computeMipColor( g_SwMaterialTex12.CalculateLevelOfDetail( g_SwSampler0, uv ) );
        case 13u:
            return ViewMode::computeMipColor( g_SwMaterialTex13.CalculateLevelOfDetail( g_SwSampler0, uv ) );
        case 14u:
            return ViewMode::computeMipColor( g_SwMaterialTex14.CalculateLevelOfDetail( g_SwSampler0, uv ) );
        case 15u:
            return ViewMode::computeMipColor( g_SwMaterialTex15.CalculateLevelOfDetail( g_SwSampler0, uv ) );
            #else
        case 0u:
            return ViewMode::computeMipColor( g_SwMaterialTex0.CalculateLevelOfDetail( g_SwMaterialTex0Sampler, uv ) );
        case 1u:
            return ViewMode::computeMipColor( g_SwMaterialTex1.CalculateLevelOfDetail( g_SwMaterialTex1Sampler, uv ) );
        case 2u:
            return ViewMode::computeMipColor( g_SwMaterialTex2.CalculateLevelOfDetail( g_SwMaterialTex2Sampler, uv ) );
        case 3u:
            return ViewMode::computeMipColor( g_SwMaterialTex3.CalculateLevelOfDetail( g_SwMaterialTex3Sampler, uv ) );
        case 4u:
            return ViewMode::computeMipColor( g_SwMaterialTex4.CalculateLevelOfDetail( g_SwMaterialTex4Sampler, uv ) );
        case 5u:
            return ViewMode::computeMipColor( g_SwMaterialTex5.CalculateLevelOfDetail( g_SwMaterialTex5Sampler, uv ) );
        case 6u:
            return ViewMode::computeMipColor( g_SwMaterialTex6.CalculateLevelOfDetail( g_SwMaterialTex6Sampler, uv ) );
        case 7u:
            return ViewMode::computeMipColor( g_SwMaterialTex7.CalculateLevelOfDetail( g_SwMaterialTex7Sampler, uv ) );
        case 8u:
            return ViewMode::computeMipColor( g_SwMaterialTex8.CalculateLevelOfDetail( g_SwMaterialTex8Sampler, uv ) );
        case 9u:
            return ViewMode::computeMipColor( g_SwMaterialTex9.CalculateLevelOfDetail( g_SwMaterialTex9Sampler, uv ) );
        case 10u:
            return ViewMode::computeMipColor( g_SwMaterialTex10.CalculateLevelOfDetail( g_SwMaterialTex10Sampler, uv ) );
        case 11u:
            return ViewMode::computeMipColor( g_SwMaterialTex11.CalculateLevelOfDetail( g_SwMaterialTex11Sampler, uv ) );
        case 12u:
            return ViewMode::computeMipColor( g_SwMaterialTex12.CalculateLevelOfDetail( g_SwMaterialTex12Sampler, uv ) );
        case 13u:
            return ViewMode::computeMipColor( g_SwMaterialTex13.CalculateLevelOfDetail( g_SwMaterialTex13Sampler, uv ) );
        case 14u:
            return ViewMode::computeMipColor( g_SwMaterialTex14.CalculateLevelOfDetail( g_SwMaterialTex14Sampler, uv ) );
        case 15u:
            return ViewMode::computeMipColor( g_SwMaterialTex15.CalculateLevelOfDetail( g_SwMaterialTex15Sampler, uv ) );
            #endif
        default:
            return float4( 1, 1, 1, 1 );
    }
        #endif
}
    #endif

    #if defined( SW_NATIVE_BINDLESS )
static float4 MaterialTexture::sampleIndexTexture( uint index, float2 uv )
{
    // 네이티브 bindless: index 는 배열 전역 인덱스다. 샘플러는 머티리얼 텍스처 샘플러(선형 · 랩)다.
    if ( index == kInvalidIndex )
        return float4( 1, 1, 1, 1 );
    return Bindless::sampleIndexWith( index, SW_MATERIAL_TEXTURE_SAMPLER, uv );
}
    #else
static float4 MaterialTexture::sampleIndexTexture( uint index, float2 uv )
{
    // 에뮬 백엔드: index 는 전역 인덱스가 아니라 **머티리얼 텍스처 서수**(0..15)다.
    // 엔진이 그 서수 순서대로 t16..t31 에 바인딩해 둔다. SM5.0 은 리소스 배열 동적 인덱싱이 안 되므로
    // (그건 SM5.1 = D3D12) 리터럴 분기로 고른다 — 칸 수가 16 이라 분기도 16 개다.
    // DX11 은 정적 세트의 머티리얼 샘플러로, GL 은 슬롯의 결합 샘플러(엔진이 머티리얼 샘플러를 건다)로 읽는다.
    switch ( index )
    {
        #if defined( SW_SEPARATE_SAMPLER_EMULATION )
        case 0u:
            return Bindless::sampleSlotWith( g_SwMaterialTex0, SW_MATERIAL_TEXTURE_SAMPLER, uv );
        case 1u:
            return Bindless::sampleSlotWith( g_SwMaterialTex1, SW_MATERIAL_TEXTURE_SAMPLER, uv );
        case 2u:
            return Bindless::sampleSlotWith( g_SwMaterialTex2, SW_MATERIAL_TEXTURE_SAMPLER, uv );
        case 3u:
            return Bindless::sampleSlotWith( g_SwMaterialTex3, SW_MATERIAL_TEXTURE_SAMPLER, uv );
        case 4u:
            return Bindless::sampleSlotWith( g_SwMaterialTex4, SW_MATERIAL_TEXTURE_SAMPLER, uv );
        case 5u:
            return Bindless::sampleSlotWith( g_SwMaterialTex5, SW_MATERIAL_TEXTURE_SAMPLER, uv );
        case 6u:
            return Bindless::sampleSlotWith( g_SwMaterialTex6, SW_MATERIAL_TEXTURE_SAMPLER, uv );
        case 7u:
            return Bindless::sampleSlotWith( g_SwMaterialTex7, SW_MATERIAL_TEXTURE_SAMPLER, uv );
        case 8u:
            return Bindless::sampleSlotWith( g_SwMaterialTex8, SW_MATERIAL_TEXTURE_SAMPLER, uv );
        case 9u:
            return Bindless::sampleSlotWith( g_SwMaterialTex9, SW_MATERIAL_TEXTURE_SAMPLER, uv );
        case 10u:
            return Bindless::sampleSlotWith( g_SwMaterialTex10, SW_MATERIAL_TEXTURE_SAMPLER, uv );
        case 11u:
            return Bindless::sampleSlotWith( g_SwMaterialTex11, SW_MATERIAL_TEXTURE_SAMPLER, uv );
        case 12u:
            return Bindless::sampleSlotWith( g_SwMaterialTex12, SW_MATERIAL_TEXTURE_SAMPLER, uv );
        case 13u:
            return Bindless::sampleSlotWith( g_SwMaterialTex13, SW_MATERIAL_TEXTURE_SAMPLER, uv );
        case 14u:
            return Bindless::sampleSlotWith( g_SwMaterialTex14, SW_MATERIAL_TEXTURE_SAMPLER, uv );
        case 15u:
            return Bindless::sampleSlotWith( g_SwMaterialTex15, SW_MATERIAL_TEXTURE_SAMPLER, uv );
        #else
        case 0u:
            return g_SwMaterialTex0.Sample( g_SwMaterialTex0Sampler, uv );
        case 1u:
            return g_SwMaterialTex1.Sample( g_SwMaterialTex1Sampler, uv );
        case 2u:
            return g_SwMaterialTex2.Sample( g_SwMaterialTex2Sampler, uv );
        case 3u:
            return g_SwMaterialTex3.Sample( g_SwMaterialTex3Sampler, uv );
        case 4u:
            return g_SwMaterialTex4.Sample( g_SwMaterialTex4Sampler, uv );
        case 5u:
            return g_SwMaterialTex5.Sample( g_SwMaterialTex5Sampler, uv );
        case 6u:
            return g_SwMaterialTex6.Sample( g_SwMaterialTex6Sampler, uv );
        case 7u:
            return g_SwMaterialTex7.Sample( g_SwMaterialTex7Sampler, uv );
        case 8u:
            return g_SwMaterialTex8.Sample( g_SwMaterialTex8Sampler, uv );
        case 9u:
            return g_SwMaterialTex9.Sample( g_SwMaterialTex9Sampler, uv );
        case 10u:
            return g_SwMaterialTex10.Sample( g_SwMaterialTex10Sampler, uv );
        case 11u:
            return g_SwMaterialTex11.Sample( g_SwMaterialTex11Sampler, uv );
        case 12u:
            return g_SwMaterialTex12.Sample( g_SwMaterialTex12Sampler, uv );
        case 13u:
            return g_SwMaterialTex13.Sample( g_SwMaterialTex13Sampler, uv );
        case 14u:
            return g_SwMaterialTex14.Sample( g_SwMaterialTex14Sampler, uv );
        case 15u:
            return g_SwMaterialTex15.Sample( g_SwMaterialTex15Sampler, uv );
        #endif
        default:
            return float4( 1, 1, 1, 1 );
    }
}
    #endif

    // 보기 모드 Mipmaps 는 텍스처 색 대신 밉 단계 색을 낸다 — 케이스마다 정의 하나다(백엔드 정의는 sampleIndexTexture).
    #if defined( SW_VIEWMODE_MIPMAPS )
static float4 MaterialTexture::sampleIndex( uint index, float2 uv )
{
    return MaterialTexture::sampleMipColor( index, uv );
}
    #else
static float4 MaterialTexture::sampleIndex( uint index, float2 uv )
{
    return MaterialTexture::sampleIndexTexture( index, uv );
}
    #endif

    /**
     * @brief 머티리얼 텍스처를 밉 0 으로 읽습니다 — 정점 셰이더용이다(암시 미분이 없다). 인덱스의 뜻은 `MaterialTexture::sampleIndex` 와 같고 텍스처가 없으면 흰색입니다.
     * @details 툰 외곽선 두께 텍스처(MToon outlineWidthMultiplyTexture)가 정점을 밀 때 쓴다. DX11 은 엔진이 머티리얼 슬롯과 정적 샘플러를 정점 셰이더에도 건다.
     */
    #if SW_MATERIAL_TEXTURE_SAMPLER != 0
        #error MaterialTexture::sampleLevelZero 는 머티리얼 샘플러가 정적 샘플러 0 번(g_SwSampler0)이라고 가정한다
    #endif
static float4 MaterialTexture::sampleLevelZero( uint index, float2 uv )
{
    #if defined( SW_NATIVE_BINDLESS )
    if ( index == kInvalidIndex )
        return float4( 1, 1, 1, 1 );
    const uint slot = Bindless::textureSlot( index );
        #if defined( __spirv__ )
    return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].SampleLevel( SW_SAMPLER_STATE( SW_MATERIAL_TEXTURE_SAMPLER ), uv, 0.0f );
        #else
    return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].SampleLevel( g_SwSampler0, uv, 0.0f );
        #endif
    #else
    switch ( index )
    {
        #if defined( SW_SEPARATE_SAMPLER_EMULATION )
        case 0u:
            return g_SwMaterialTex0.SampleLevel( g_SwSampler0, uv, 0.0f );
        case 1u:
            return g_SwMaterialTex1.SampleLevel( g_SwSampler0, uv, 0.0f );
        case 2u:
            return g_SwMaterialTex2.SampleLevel( g_SwSampler0, uv, 0.0f );
        case 3u:
            return g_SwMaterialTex3.SampleLevel( g_SwSampler0, uv, 0.0f );
        case 4u:
            return g_SwMaterialTex4.SampleLevel( g_SwSampler0, uv, 0.0f );
        case 5u:
            return g_SwMaterialTex5.SampleLevel( g_SwSampler0, uv, 0.0f );
        case 6u:
            return g_SwMaterialTex6.SampleLevel( g_SwSampler0, uv, 0.0f );
        case 7u:
            return g_SwMaterialTex7.SampleLevel( g_SwSampler0, uv, 0.0f );
        case 8u:
            return g_SwMaterialTex8.SampleLevel( g_SwSampler0, uv, 0.0f );
        case 9u:
            return g_SwMaterialTex9.SampleLevel( g_SwSampler0, uv, 0.0f );
        case 10u:
            return g_SwMaterialTex10.SampleLevel( g_SwSampler0, uv, 0.0f );
        case 11u:
            return g_SwMaterialTex11.SampleLevel( g_SwSampler0, uv, 0.0f );
        case 12u:
            return g_SwMaterialTex12.SampleLevel( g_SwSampler0, uv, 0.0f );
        case 13u:
            return g_SwMaterialTex13.SampleLevel( g_SwSampler0, uv, 0.0f );
        case 14u:
            return g_SwMaterialTex14.SampleLevel( g_SwSampler0, uv, 0.0f );
        case 15u:
            return g_SwMaterialTex15.SampleLevel( g_SwSampler0, uv, 0.0f );
        #else
        case 0u:
            return g_SwMaterialTex0.SampleLevel( g_SwMaterialTex0Sampler, uv, 0.0f );
        case 1u:
            return g_SwMaterialTex1.SampleLevel( g_SwMaterialTex1Sampler, uv, 0.0f );
        case 2u:
            return g_SwMaterialTex2.SampleLevel( g_SwMaterialTex2Sampler, uv, 0.0f );
        case 3u:
            return g_SwMaterialTex3.SampleLevel( g_SwMaterialTex3Sampler, uv, 0.0f );
        case 4u:
            return g_SwMaterialTex4.SampleLevel( g_SwMaterialTex4Sampler, uv, 0.0f );
        case 5u:
            return g_SwMaterialTex5.SampleLevel( g_SwMaterialTex5Sampler, uv, 0.0f );
        case 6u:
            return g_SwMaterialTex6.SampleLevel( g_SwMaterialTex6Sampler, uv, 0.0f );
        case 7u:
            return g_SwMaterialTex7.SampleLevel( g_SwMaterialTex7Sampler, uv, 0.0f );
        case 8u:
            return g_SwMaterialTex8.SampleLevel( g_SwMaterialTex8Sampler, uv, 0.0f );
        case 9u:
            return g_SwMaterialTex9.SampleLevel( g_SwMaterialTex9Sampler, uv, 0.0f );
        case 10u:
            return g_SwMaterialTex10.SampleLevel( g_SwMaterialTex10Sampler, uv, 0.0f );
        case 11u:
            return g_SwMaterialTex11.SampleLevel( g_SwMaterialTex11Sampler, uv, 0.0f );
        case 12u:
            return g_SwMaterialTex12.SampleLevel( g_SwMaterialTex12Sampler, uv, 0.0f );
        case 13u:
            return g_SwMaterialTex13.SampleLevel( g_SwMaterialTex13Sampler, uv, 0.0f );
        case 14u:
            return g_SwMaterialTex14.SampleLevel( g_SwMaterialTex14Sampler, uv, 0.0f );
        case 15u:
            return g_SwMaterialTex15.SampleLevel( g_SwMaterialTex15Sampler, uv, 0.0f );
        #endif
        default:
            return float4( 1, 1, 1, 1 );
    }
    #endif
}

/** @brief 머티리얼 텍스처의 크기(텍셀)입니다. 텍스처가 없으면 (1, 1) 입니다. 인덱스의 뜻은 `MaterialTexture::sampleIndex` 와 같습니다. */
static float2 MaterialTexture::computeSize( uint index )
{
    uint width  = 1u;
    uint height = 1u;
    #if defined( SW_NATIVE_BINDLESS )
    if ( index != kInvalidIndex )
        g_SwBindlessTex2D[NonUniformResourceIndex( Bindless::textureSlot( index ) )].GetDimensions( width, height );
    #else
    if ( index == 0u )
        g_SwMaterialTex0.GetDimensions( width, height );
    if ( index == 1u )
        g_SwMaterialTex1.GetDimensions( width, height );
    if ( index == 2u )
        g_SwMaterialTex2.GetDimensions( width, height );
    if ( index == 3u )
        g_SwMaterialTex3.GetDimensions( width, height );
    if ( index == 4u )
        g_SwMaterialTex4.GetDimensions( width, height );
    if ( index == 5u )
        g_SwMaterialTex5.GetDimensions( width, height );
    if ( index == 6u )
        g_SwMaterialTex6.GetDimensions( width, height );
    if ( index == 7u )
        g_SwMaterialTex7.GetDimensions( width, height );
    if ( index == 8u )
        g_SwMaterialTex8.GetDimensions( width, height );
    if ( index == 9u )
        g_SwMaterialTex9.GetDimensions( width, height );
    if ( index == 10u )
        g_SwMaterialTex10.GetDimensions( width, height );
    if ( index == 11u )
        g_SwMaterialTex11.GetDimensions( width, height );
    if ( index == 12u )
        g_SwMaterialTex12.GetDimensions( width, height );
    if ( index == 13u )
        g_SwMaterialTex13.GetDimensions( width, height );
    if ( index == 14u )
        g_SwMaterialTex14.GetDimensions( width, height );
    if ( index == 15u )
        g_SwMaterialTex15.GetDimensions( width, height );
    #endif
    return float2( max( width, 1u ), max( height, 1u ) );
}

/**
 * @brief 머티리얼 텍스처를 **점 필터**로 읽습니다(픽셀 아트). UV 를 텍셀 중심에 붙여 선형 샘플러로도 텍셀 하나만 읽게 합니다.
 * @details 샘플러를 바꾸지 않는 이유: GL 은 결합 샘플러라 슬롯의 샘플러를 셰이더가 고를 수 없다 — 네 백엔드가 같은 식으로 같은 그림을 내게
 *          UV 를 붙인다. 확대(텍셀 하나가 화면 픽셀 하나 이상)에서는 붙인 UV 의 미분이 텍셀 하나를 넘지 않아 밉 0 을 읽는다.
 */
static float4 MaterialTexture::samplePoint( uint index, float2 uv )
{
    const float2 size = MaterialTexture::computeSize( index );
    return MaterialTexture::sampleIndex( index, ( floor( uv * size ) + 0.5f ) / size );
}

static float4 PassTexture::sampleShadow( float2 uv )
{
    return Bindless::sampleIndex( g_ShadowMapIndex, uv );
}
static float4 PassTexture::sampleAlbedo( float2 uv )
{
    return Bindless::sampleIndex( g_GBufferAlbedoIndex, uv );
}
/** @brief G버퍼 노멀 칸(R16G16_SNORM 의 옥타헤드럴 좌표 `.xy`)을 단위 노멀로 푼다 — 쓰는 쪽은 12 절 SurfaceOutput::pack 하나다. */
static float3 PassTexture::sampleNormal( float2 uv )
{
    return Octahedral::decode( Bindless::sampleIndex( g_GBufferNormalIndex, uv ).xy );
}
static float4 PassTexture::sampleGbufferMaterial( float2 uv )
{
    return Bindless::sampleIndex( g_GBufferMaterialIndex, uv );
}
/** @brief G버퍼 머티리얼 칸을 읽어 푼다 — 쓰는 쪽은 12 절 SurfaceOutput::encodeGbufferMaterial 하나다(r 금속도, g 거칠기, a "그려졌다" 표시: 클리어 0, 그려지면 1). */
static void PassTexture::loadGbufferMaterial( float2 uv, out float outRoughness, out float outMetallic, out bool outDrawn )
{
    const float4 encoded = PassTexture::sampleGbufferMaterial( uv );
    outMetallic          = encoded.r;
    outRoughness         = encoded.g;
    outDrawn             = encoded.a >= 0.5f;
}
static float4 PassTexture::sampleDepth( float2 uv )
{
    return Bindless::sampleIndex( g_SceneDepthIndex, uv );
}
static float4 PassTexture::sampleSource( float2 uv )
{
    return Bindless::sampleIndex( g_SourceColorIndex, uv );
}
/**
 * @brief 화면과 1:1 인 전체화면 패스 전용 — 텍셀 중심에 정확히 떨어지므로 선형 필터가 **할 일이 없다**.
 * @details 그런데도 값은 공짜가 아니다: 선형 샘플은 텍셀 넷을 읽어 섞고, 깊이 같은 포맷은 그 경로가
 *          더 비싸다. 배율이 1 이 아닌 패스(블룸의 바이리니어 2탭 같은 것)에는 쓰면 안 된다 —
 *          거기서는 섞는 것이 목적이다.
 * @note CLAMP 인 것도 의미가 있다. 화면 가장자리에서 한 텍셀 비낀 샘플이 WRAP 이면 반대편을 읽는다.
 */
static float4 PassTexture::sampleDepthPoint( float2 uv )
{
    return Bindless::sampleIndexWith( g_SceneDepthIndex, SW_SAMPLER_POINT_CLAMP, uv );
}
static float4 PassTexture::sampleSourcePoint( float2 uv )
{
    return Bindless::sampleIndexWith( g_SourceColorIndex, SW_SAMPLER_POINT_CLAMP, uv );
}
/**
 * @brief 깊이의 십자 다섯 칸(중심 + 상하좌우 한 텍셀)을 읽습니다 — 게더가 있으면 **두 번**에 끝낸다.
 * @details 반 텍셀 비낀 두 게더의 2x2 가 십자 다섯 칸을 정확히 덮는다(중심은 둘 다에 들어 있다).
 *          같은 텍셀을 읽으므로 점 샘플 다섯 번과 **값이 같다** — 근사가 아니다.
 * @note `outNeighborDepth` 의 **순서는 약속하지 않는다.** 게더 성분 순서는 백엔드마다 다를 수 있고,
 *       쓰는 쪽(외곽선)이 네 이웃의 **합**만 보므로 순서가 결과를 바꾸지 않는다. 방향이 필요한
 *       계산에는 쓰지 말 것.
 */
static void PassTexture::sampleDepthCross( float2 uv, float2 texel, out float outCenterDepth, out float4 outNeighborDepth )
{
    // **반 텍셀이 아니라 1/4 텍셀이다.** 정확히 텍셀 모서리에 찍으면 어느 2x2 를 잡을지가 반올림에
    // 달려 버려 백엔드·드라이버마다 갈릴 수 있다. 1/4 은 의도한 2x2 안쪽이라 어디서든 같은 넷이다.
    float4 gatherHi  = PassTexture::gatherDepthRed( uv + texel * 0.25f );
    float4 gatherLo  = PassTexture::gatherDepthRed( uv - texel * 0.25f );
    outCenterDepth   = gatherHi.w;
    outNeighborDepth = float4( gatherHi.z, gatherHi.x, gatherLo.x, gatherLo.z );
}
/** @brief 접촉 그림자 마스크(채널 넷 = 접촉 그림자 빛 넷). 패스가 걸지 않았으면(파이프라인에 없음 · 켠 빛 없음 · 반투명) 가림 없음(1)이다. */
static float4 PassTexture::sampleContactShadow( float2 uv )
{
    if ( g_ContactShadowIndex == kInvalidIndex )
        return float4( 1, 1, 1, 1 );
    return Bindless::sampleIndex( g_ContactShadowIndex, uv );
}
/** @brief SSAO 결과. 파이프라인에 SSAO 가 없으면(인덱스 무효) 가림 없음(1)이다 — 0 으로 폴백하면 화면이 검게 된다. */
static float PassTexture::sampleAmbientOcclusion( float2 uv )
{
    if ( g_AmbientOcclusionIndex == kInvalidIndex )
        return 1.0f;
    return Bindless::sampleIndex( g_AmbientOcclusionIndex, uv ).r;
}
/**
 * @brief 화면 속도(이번 UV - 지난 UV, 역할 Velocity)를 가장 가까운 텍셀로 읽습니다. 속도는 섞으면 가장자리에서 두 물체의 평균이 되어 어느 쪽도 아니다(점 샘플).
 * @details 패스가 속도를 걸지 않았으면(`g_VelocityIndex` 무효) 0 — 부르는 쪽(TAA · 모션 블러)은 속도를 필수 입력으로 선언하므로 로드 검증이 그 경우를 막는다.
 */
static float2 PassTexture::sampleVelocityPoint( float2 uv )
{
    if ( g_VelocityIndex == kInvalidIndex )
        return float2( 0.0f, 0.0f );
    return Bindless::sampleIndexWith( g_VelocityIndex, SW_SAMPLER_POINT_CLAMP, uv ).rg;
}
/** @brief 화면 왜곡 오프셋(화면 UV, 역할 Distortion)을 가장 가까운 텍셀로 읽습니다. 걸리지 않았으면 0(왜곡 없음)이다. */
static float2 PassTexture::sampleDistortionPoint( float2 uv )
{
    if ( g_DistortionIndex == kInvalidIndex )
        return float2( 0.0f, 0.0f );
    return Bindless::sampleIndexWith( g_DistortionIndex, SW_SAMPLER_POINT_CLAMP, uv ).rg;
}

#endif // SW_NATIVE_BINDLESS || !SW_STAGE_COMPUTE

#endif // SW_ENGINE_BINDING_TEXTURE_HLSLI
