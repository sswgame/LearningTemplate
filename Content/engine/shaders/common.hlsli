/**
 * Common shader macros, attributes, and definitions for cross-backend HLSL / SPIR-V shaders.
 * - DirectX 11 (FXC, SM5.0)
 * - DirectX 12 (DXC, DXIL, SM6.x)
 * - Vulkan (DXC, SPIR-V, SM6.x)
 * - OpenGL (DXC, SPIR-V, GL_ARB_gl_spirv)
 *
 * 선언은 네 백엔드에서 같다 (binding_slots.hlsli 의 표). 백엔드별로 다른 것은 SPIR-V 의 binding 번호뿐인데,
 * Vulkan 은 DXC 시프트(-fvk-*-shift)가, OpenGL 은 아래 매크로의 [[vk::binding]] 이 정한다.
 *
 *   SW_DECLARE_CBUFFER( MaterialCB, SW_SLOT_MATERIAL_CB ) { float4 color; };   // 픽스처·비 GPUScene 드로우용
 *   SW_DECLARE_STRUCTURED_BUFFER( InstanceData, g_Instances, 0 );              // 컴퓨트 읽기 t0
 *   SW_DECLARE_RW_STRUCTURED_BUFFER( SceneDrawArgs, g_IndirectArgs, 0 ); // 컴퓨트 쓰기 u0
 * 씬 메시 머티리얼은 binding.hlsli 의 SW_MATERIAL_BEGIN/END + SW_MATERIAL( index ) 를 쓴다 (GPUScene 버퍼).
 */

#ifndef SW_ENGINE_COMMON_HLSLI
#define SW_ENGINE_COMMON_HLSLI

#include "binding_slots.hlsli"

// 토큰 붙이기(##)는 인자를 매크로 확장하기 **전에** 붙인다. 한 겹으로는 register( SW_CAT( t, slot ) ) 에 리터럴만 넘길 수
// 있어 슬롯 번호를 손으로 다시 적어야 한다(어긋나면 엉뚱한 슬롯을 읽는다). 한 겹 더
// 감싸면 인자가 먼저 확장된다 — SW_CAT( t, SW_SLOT_INSTANCE_SRV ) → t4. 아래 선언 매크로는 전부 이걸 쓴다.
#define SW_CAT_( a, b ) a##b
#define SW_CAT( a, b )  SW_CAT_( a, b )

// 네이티브 bindless 텍스처 백엔드 — 컴파일러(ShaderCompiler.cpp)가 DX12/Vulkan 에 SW_BINDLESS=1 을 정의한다.
#if defined( SW_BINDLESS ) && ( defined( DX12 ) || defined( VULKAN ) )
    #define SW_NATIVE_BINDLESS 1
#endif

// 분리 샘플러 에뮬 — 고정 슬롯 텍스처를 정적 샘플러 세트(s9..s15)로 읽는 백엔드다. DX11 은 샘플러 슬롯이 16 개뿐이라서, WebGPU 는 WGSL 이 텍스처 · 샘플러를
// 나눠 받아서 이 모양이다. GL 은 결합 샘플러뿐이라 슬롯마다 짝 샘플러를 둔다(ARB_gl_spirv). WebGPU 는 SPIR-V 를 거치지만(__spirv__) GL 의 결합 갈래를 타지 않는다.
#if defined( DX11 ) || defined( WEBGPU )
    #define SW_SEPARATE_SAMPLER_EMULATION 1
#endif
// GL 결합 샘플러 갈래 — SPIR-V 중 GL 만이다(WebGPU 는 분리 샘플러, Vulkan 은 네이티브 bindless 라 슬롯 텍스처를 선언하지 않는다).
#if defined( __spirv__ ) && !defined( SW_SEPARATE_SAMPLER_EMULATION )
    #define SW_COMBINED_SAMPLER_SLOTS 1
#endif

// ------------------------------------------------------------------------------
// 1) SPIR-V 바인딩 속성 — OpenGL 만 명시한다 (GL 은 set 을 버리고 binding 이 곧 UBO/SSBO/텍스처 유닛 번호다).
//    Vulkan 은 DXC 시프트에 맡긴다: b# → SW_VK_B_SHIFT+#, t# → SW_VK_T_SHIFT+#, u# → SW_VK_U_SHIFT+# (모두 set 0).
// ------------------------------------------------------------------------------
#if defined( OPENGL )
    #define SW_GL_BINDING( slot ) [[vk::binding( slot, 0 )]]
#else
    #define SW_GL_BINDING( slot )
#endif

// ------------------------------------------------------------------------------
// 2) 상수버퍼 — 네 백엔드 공통 `cbuffer name : register(b#)`. 필드는 맨이름으로 쓴다.
// ------------------------------------------------------------------------------
#define SW_DECLARE_CBUFFER( name, slot ) \
    SW_GL_BINDING( slot ) cbuffer name : register( SW_CAT( b, slot ) )

// ------------------------------------------------------------------------------
// 3) 구조버퍼 / 바이트주소버퍼 — 읽기 t#, 쓰기 u#.
//    GL: u 는 명시 binding 을 두지 않고 DXC -fvk-u-shift(= SW_GL_UAV_BINDING0) 에 맡긴다. u0 를 binding 0 에 두면
//    t0 과 u0 이 같은 SSBO 자리를 다툰다(검증 에러 없음, 컬링 결과가 조용히 깨짐).
// ------------------------------------------------------------------------------
#define SW_DECLARE_STRUCTURED_BUFFER( elemType, bufName, slot ) \
    SW_GL_BINDING( slot ) StructuredBuffer<elemType> bufName : register( SW_CAT( t, slot ) )
#define SW_DECLARE_RW_STRUCTURED_BUFFER( elemType, bufName, slot ) \
    RWStructuredBuffer<elemType> bufName : register( SW_CAT( u, slot ) )
#define SW_DECLARE_BYTE_ADDRESS_BUFFER( bufName, slot ) \
    SW_GL_BINDING( slot ) ByteAddressBuffer bufName : register( SW_CAT( t, slot ) )
#define SW_DECLARE_RW_BYTE_ADDRESS_BUFFER( bufName, slot ) \
    RWByteAddressBuffer bufName : register( SW_CAT( u, slot ) )

// 컴퓨트 RW 텍스처 슬롯(DX11/GL 에뮬 전용 — binding.hlsli 가 쓴다). GL 은 이미지 유닛이 SSBO 와 다른 이름공간이라
// 명시 binding(SW_GL_IMAGE_UNIT0 + 서수)을 적는다 — -fvk-u-shift 가 붙으면 유닛 20 같은 값이 나와 GL 한계(8)를 넘는다.
// GL 의 이미지 유닛은 묶은 텍스처 포맷과 셰이더의 `layout(포맷)` 이 **같아야** 읽는다 — 다르면 imageLoad 가 0 을 준다(R32F 깊이 히스토리를 rgba32f 로
// 선언한 vfx_simulate · occlusion_tiles 가 GL 에서만 깊이를 못 읽던 결함). 그래서 슬롯 선언이 포맷을 받고, 셰이더가 binding.hlsli 를 포함하기 전에
// `SW_RW_SLOT0_GL_FORMAT "r32f"` 처럼 슬롯별로 정한다(기본 rgba32f). GL 밖은 이 인자를 쓰지 않는다.
#if defined( OPENGL )
    #define SW_DECLARE_RW_TEXTURE2D( texName, slot, glUnit, glFormat ) \
        [[vk::binding( glUnit, 0 )]] [[vk::image_format( glFormat )]] RWTexture2D<float4> texName : register( SW_CAT( u, slot ) )
#elif defined( WEBGPU )
    // WGSL 의 저장 텍스처는 형식을 적어야 한다(GL 과 같은 까닭) — binding 은 DXC 의 u 시프트가 정한다. 읽고 쓰는 저장 텍스처는 r32 형식만 된다(WebGPU 명세, 기억).
    #define SW_DECLARE_RW_TEXTURE2D( texName, slot, glUnit, glFormat ) \
        [[vk::image_format( glFormat )]] RWTexture2D<float4> texName : register( SW_CAT( u, slot ) )
#else
    #define SW_DECLARE_RW_TEXTURE2D( texName, slot, glUnit, glFormat ) \
        RWTexture2D<float4> texName : register( SW_CAT( u, slot ) )
#endif

// ------------------------------------------------------------------------------
// 3-1) 루트/푸시 상수 — setComputeRootConstants 가 채우는 16 dword. 블록 이름은 SwRootConstants 로 고정(계약 검증 키).
//      DX12 b0 space2 (32비트 루트 상수), Vulkan 푸시 상수, DX11/GL 은 b SW_SLOT_ROOT_CB_EMUL 상수버퍼 에뮬.
//      용법(중괄호는 셰이더가 쓴다 — 매크로는 블록의 머리와 꼬리만 낸다):
//          SW_ROOT_CONSTANTS_BEGIN
//          {
//              uint g_Target;
//          };
//          SW_ROOT_CONSTANTS_END
//          ...  SW_ROOT( g_Target )
//      Vulkan 은 [[vk::push_constant]] 가 구조체 전역에만 붙어 필드가 전역 이름이 아니다 — 그래서 SW_ROOT() 로 읽고, 꼬리가 그 전역을 선언한다.
//      매크로 본문에 짝 없는 중괄호를 두지 않는다 — clang-format 이 `#define` 을 여러 줄로 가르고 쓰는 쪽도 블록으로 읽지 못한다.
// ------------------------------------------------------------------------------
#if defined( OPENGL )
    #define SW_ROOT_CONSTANTS_BEGIN [[vk::binding( SW_SLOT_ROOT_CB_EMUL, 0 )]] cbuffer SwRootConstants : register( SW_CAT( b, SW_SLOT_ROOT_CB_EMUL ) )
    #define SW_ROOT_CONSTANTS_END
    #define SW_ROOT( field ) field
#elif defined( WEBGPU )
    // WebGPU 는 푸시 상수가 없다 — DX11 과 같은 b2 상수 버퍼 에뮬이고 binding 은 DXC 의 b 시프트가 정한다(shaderslot::wgpu).
    #define SW_ROOT_CONSTANTS_BEGIN cbuffer SwRootConstants : register( SW_CAT( b, SW_SLOT_ROOT_CB_EMUL ) )
    #define SW_ROOT_CONSTANTS_END
    #define SW_ROOT( field ) field
#elif defined( __spirv__ )
    #define SW_ROOT_CONSTANTS_BEGIN struct RootConstantsData
    #define SW_ROOT_CONSTANTS_END   [[vk::push_constant]] ConstantBuffer<RootConstantsData> SwRootConstants;
    #define SW_ROOT( field )        SwRootConstants.field
#elif defined( DX12 )
    #define SW_ROOT_CONSTANTS_BEGIN cbuffer SwRootConstants : register( SW_CAT( b, SW_SLOT_ROOT_CB ), SW_CAT( space, SW_SPACE_ROOT_CB ) )
    #define SW_ROOT_CONSTANTS_END
    #define SW_ROOT( field ) field
#else
    #define SW_ROOT_CONSTANTS_BEGIN cbuffer SwRootConstants : register( SW_CAT( b, SW_SLOT_ROOT_CB_EMUL ) )
    #define SW_ROOT_CONSTANTS_END
    #define SW_ROOT( field ) field
#endif

// ------------------------------------------------------------------------------
// 3-2) 컴퓨트 진입점 스레드 그룹 — `[numthreads( x, y, z )]` 대신 쓴다. 맨 속성은 clang-format 이 다음 줄의 진입점 함수와
//      한 줄로 붙이고, 매크로는 Content/.clang-format 의 StatementMacros 가 한 문장으로 읽어 제 줄에 둔다.
// ------------------------------------------------------------------------------
#define SW_NUMTHREADS( x, y, z ) [numthreads( x, y, z )]

// 반복 · 분기 힌트 — 맨 `[unroll]` 은 clang-format 이 다음 문장과 붙이므로 속성 매크로로 쓴다(Content/.clang-format 의 AttributeMacros).
#define SW_UNROLL [unroll]
#define SW_LOOP   [loop]
#define SW_BRANCH [branch]

// ------------------------------------------------------------------------------
// 4) 텍스처 / 샘플러 — 에뮬 백엔드(DX11/GL)의 고정 슬롯 (binding.hlsli 가 쓴다). 네이티브 bindless 텍스처 배열은
//    binding.hlsli 가 SW_SPACE_BINDLESS_TEX / SW_VK_TEXTURE_SET 으로 직접 선언한다.
// ------------------------------------------------------------------------------
#if defined( SW_COMBINED_SAMPLER_SLOTS )
    #define SW_DECLARE_TEXTURE2D_SAMPLER( texName, samplerName, slot )                                                \
        [[vk::combinedImageSampler]] [[vk::binding( slot, 0 )]] Texture2D    texName : register( SW_CAT( t, slot ) ); \
        [[vk::combinedImageSampler]] [[vk::binding( slot, 0 )]] SamplerState samplerName : register( SW_CAT( s, slot ) )
    #define SW_DECLARE_TEXTURE2D( texName, slot ) \
        [[vk::binding( slot, 0 )]] Texture2D texName : register( SW_CAT( t, slot ) )
    #define SW_DECLARE_SAMPLER( samplerName, slot ) \
        [[vk::binding( slot, 0 )]] SamplerState samplerName : register( SW_CAT( s, slot ) )
#else
    #define SW_DECLARE_TEXTURE2D_SAMPLER( texName, samplerName, slot ) \
        Texture2D    texName : register( SW_CAT( t, slot ) );          \
        SamplerState samplerName : register( SW_CAT( s, slot ) )
    #define SW_DECLARE_TEXTURE2D( texName, slot ) \
        Texture2D texName : register( SW_CAT( t, slot ) )
    #define SW_DECLARE_SAMPLER( samplerName, slot ) \
        SamplerState samplerName : register( SW_CAT( s, slot ) )
#endif

// 엔진 텍스처 칸 ordinal(리터럴 0..SW_ENGINE_TEXTURE_COUNT-1)의 이름 · 샘플러 · 선언(에뮬 — binding.hlsli 11-2). HLSL 레지스터는 SW_SLOT_ENGINE_TEX<n>(DX11),
// GL 텍스처 유닛은 레지스터 번호 그대로다(binding_slots.hlsli 2). DX11 은 짝 샘플러 없이 정적 세트의 엔진 텍스처 샘플러로 읽는다.
#define SW_ENGINE_TEXTURE( ordinal ) SW_CAT( g_SwEngineTex, ordinal )
#if defined( SW_COMBINED_SAMPLER_SLOTS )
    #define SW_ENGINE_SAMPLER( ordinal ) SW_CAT( SW_ENGINE_TEXTURE( ordinal ), Sampler )
    #define SW_DECLARE_ENGINE_TEXTURE( ordinal )                                                                                                                                                          \
        [[vk::combinedImageSampler]] [[vk::binding( SW_CAT( SW_SLOT_ENGINE_TEX, ordinal ), 0 )]] Texture2D SW_ENGINE_TEXTURE( ordinal ) : register( SW_CAT( t, SW_CAT( SW_SLOT_ENGINE_TEX, ordinal ) ) ); \
        [[vk::combinedImageSampler]] [[vk::binding( SW_CAT( SW_SLOT_ENGINE_TEX, ordinal ), 0 )]] SamplerState SW_ENGINE_SAMPLER( ordinal ) : register( SW_CAT( s, ordinal ) )
#else
    #define SW_ENGINE_SAMPLER( ordinal ) SW_CAT( g_SwSampler, SW_ENGINE_TEXTURE_SAMPLER )
    #define SW_DECLARE_ENGINE_TEXTURE( ordinal ) \
        Texture2D SW_ENGINE_TEXTURE( ordinal ) : register( SW_CAT( t, SW_CAT( SW_SLOT_ENGINE_TEX, ordinal ) ) )
#endif

// ------------------------------------------------------------------------------
// 5) Common Math Constants
// ------------------------------------------------------------------------------
static const float kPi     = 3.14159265358979323846f;
static const float kTwoPi  = 6.28318530717958647692f;
static const float kHalfPi = 1.57079632679489661923f;
static const float kInvPi  = 0.31830988618379067154f;

// 선형 Rec.709 휘도 가중치 — 노출 측정 · 그레이딩 · 가닥 흔들림이 같은 값을 쓴다.
static const float3 kLuminanceWeights = float3( 0.2126f, 0.7152f, 0.0722f );

/** @brief 여러 셰이더가 함께 쓰는 순수 함수(상태 · 리소스 없음). 같은 식을 셰이더마다 두지 않는다. */
struct CommonMath
{
    static float computeLuminance( float3 linearColor );
    static float computeInterleavedGradientNoise( float2 pixel );
};

/** @brief 선형 색의 휘도(Rec.709)입니다. */
static float CommonMath::computeLuminance( float3 linearColor )
{
    return dot( linearColor, kLuminanceWeights );
}

/**
 * @brief 픽셀 좌표의 문턱 · 회전(0..1) — 인터리브 그래디언트 노이즈(Jimenez 2014). 프레임마다 같아 골든이 정해진다.
 * @details 픽셀 칸(floor)으로 잰다 — SV_Position(칸 가운데 .5)이든 UV × 크기든 같은 칸은 같은 값이다(GTAO · SSGI · 접촉 그림자 · 머리카락 디더가 같은 무늬).
 */
static float CommonMath::computeInterleavedGradientNoise( float2 pixel )
{
    return frac( 52.9829189f * frac( dot( floor( pixel ), float2( 0.06711056f, 0.00583715f ) ) ) );
}

// ------------------------------------------------------------------------------
// 6) 정점 입력 — **정본은 C++ 의 메시 정점 표(`constant::render::getMeshVertexAttributes`)다.** location 은 표의 칸 번호와 같다.
// ------------------------------------------------------------------------------
// DX 는 시맨틱 이름으로 묶지만 Vulkan·OpenGL 은 location 으로 묶는다. location 을 `SW_VK_LOCATION` 으로 **명시**한다 — 선언 순서로 매기게 두면
// 중간 속성을 빼는 순간 그 뒤가 한 칸씩 당겨져 색을 읽으려던 셰이더가 노멀을 읽는다(DX 에서는 멀쩡하고 그 둘에서만 조용히 틀린다).
// DXC 는 한 진입점의 입력이 모두 명시이거나 모두 암시여야 하므로 정점 입력은 전부 이 매크로를 단다. 리플렉션 계약 검사(ShaderBindingValidator 5번 규칙)가
// 어긋남을 바이너리에서 잡는다. 형식(Packed 의 snorm8 노멀 · 탄젠트)은 입력 어셈블러가 float 로 풀어 주므로 셰이더는 형식을 모른다.
#if defined( __spirv__ )
    #define SW_VK_LOCATION( index ) [[vk::location( index )]]
#else
    #define SW_VK_LOCATION( index )
#endif
#define SW_VERTEX_LOCATION_POSITION      0
#define SW_VERTEX_LOCATION_NORMAL        1
#define SW_VERTEX_LOCATION_TEXCOORD      2
#define SW_VERTEX_LOCATION_COLOR         3
#define SW_VERTEX_LOCATION_TANGENT       4
#define SW_VERTEX_LOCATION_INSTANCE_SLOT 5
// 위치는 슬롯 0(위치 스트림), 나머지는 슬롯 2(속성 스트림)에서 온다. 위치만 쓰는 셰이더(깊이 · 그림자)는 `VertexPositionInput` 을 받으면 속성 스트림을
// 읽지 않는다 — DX 는 선언한 입력을 모두 레이아웃에 넣으므로 `VertexInput` 을 받으면 쓰지 않아도 슬롯 2 를 읽는다.
struct VertexInput
{
    SW_VK_LOCATION( SW_VERTEX_LOCATION_POSITION )
    float3 _position : POSITION;
    SW_VK_LOCATION( SW_VERTEX_LOCATION_NORMAL )
    float3 _normal : NORMAL;
    SW_VK_LOCATION( SW_VERTEX_LOCATION_TEXCOORD )
    float2 _uv : TEXCOORD0;
    SW_VK_LOCATION( SW_VERTEX_LOCATION_COLOR )
    float4 _color : COLOR;
    // xyz 탄젠트 · w 바이탄젠트 부호(±1) — 바이탄젠트 = cross( normal, tangent.xyz ) * tangent.w (glTF · MikkTSpace 규약, C++ RHIVertex::_arrTangent).
    SW_VK_LOCATION( SW_VERTEX_LOCATION_TANGENT )
    float4 _tangent : TANGENT;
};
struct VertexPositionInput
{
    SW_VK_LOCATION( SW_VERTEX_LOCATION_POSITION )
    float3 _position : POSITION;
};
/** @brief 위치만 받은 정점을 전체 정점 모양으로 — 나머지 칸은 쓰지 않는 셰이더(깊이 전용)만 이렇게 받는다. 노멀은 +Y, 색은 흰색, 탄젠트는 +X 다. */
VertexInput toVertexInput( VertexPositionInput positionInput )
{
    VertexInput input;
    input._position = positionInput._position;
    input._normal   = float3( 0.0f, 1.0f, 0.0f );
    input._uv       = float2( 0.0f, 0.0f );
    input._color    = float4( 1.0f, 1.0f, 1.0f, 1.0f );
    input._tangent  = float4( 1.0f, 0.0f, 0.0f, 1.0f );
    return input;
}
VertexInput toVertexInput( VertexInput input )
{
    return input;
}
// 슬롯 1(인스턴스 스텝)의 uint 하나 — 이 드로우 인스턴스의 **전역 자리**(가시 목록 슬롯)는 구조체에 넣지 않고, 읽는 셰이더만
// `VSMain( VertexInput input, SW_INSTANCE_SLOT_INPUT( instanceSlot ), … )` 처럼 인자로 받는다. 구조체에 두면 읽지 않는 셰이더도 선언하게 되고,
// DX 는 선언만 한 입력도 시그니처에 남겨 PSO 가 슬롯 1 을 읽는 것으로 판정한다 — 스트림을 걸지 않는 메시 드로우(확장 패스 데모 등)가 버려진다.
// SV_InstanceID 에 배치 시작을 루트 상수로 더하던 것을 대신한다 — 그래서 같은 PSO 의 배치들이 멀티 드로우 하나로 나간다(간접 인자의 startInstance 가
// 배치 시작이고 입력 어셈블러가 네 API 모두에서 그 원소를 준다).
#define SW_INSTANCE_SLOT_INPUT( name ) SW_VK_LOCATION( SW_VERTEX_LOCATION_INSTANCE_SLOT ) uint name : SW_INSTANCESLOT

#endif // SW_ENGINE_COMMON_HLSLI
