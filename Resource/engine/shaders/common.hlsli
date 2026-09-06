/**
 * Common shader macros, attributes, and definitions for cross-backend HLSL / SPIR-V shaders.
 * - DirectX 11 (FXC, SM5.0)
 * - DirectX 12 (DXC, DXIL, SM6.x)
 * - Vulkan (DXC, SPIR-V, SM6.x)
 * - OpenGL (DXC, SPIR-V, GL_ARB_gl_spirv)
 */

#ifndef SW_ENGINE_COMMON_HLSLI
#define SW_ENGINE_COMMON_HLSLI

#include "bindingslots.hlsli"

// 토큰 붙이기(##)는 인자를 매크로 확장하기 **전에** 붙인다. 그래서 register( SW_CAT( t, slot ) ) 에는 리터럴만 넘길 수
// 있었고, binding.hlsli 는 슬롯 번호를 손으로 다시 적어야 했다(어긋나면 엉뚱한 슬롯을 읽는다). 한 겹 더
// 감싸면 인자가 먼저 확장된다 — SW_CAT( t, SW_SLOT_INSTANCE_SRV ) → t4. 아래 선언 매크로는 전부 이걸 쓴다.
#define SW_CAT_( a, b ) a##b
#define SW_CAT( a, b )  SW_CAT_( a, b )

// ------------------------------------------------------------------------------
// 1) Cross-Backend Vulkan / SPIR-V Attribute Macros
// ------------------------------------------------------------------------------
#if defined( __spirv__ )
#define SW_VK_BINDING( slot, set )	 [[vk::binding( slot, set )]]
#define SW_VK_COMBINED				 [[vk::combinedImageSampler]]

// Paired Combined Texture + Sampler declaration for SPIR-V targets
#define SW_DECLARE_TEXTURE2D_SAMPLER( texName, samplerName, slot, set ) \
	[[vk::combinedImageSampler]] [[vk::binding( slot, set )]] Texture2D texName : register( SW_CAT( t, slot ) ); \
	[[vk::combinedImageSampler]] [[vk::binding( slot, set )]] SamplerState samplerName : register( SW_CAT( s, slot ) )

#define SW_DECLARE_TEXTURE_SAMPLER( texType, texName, samplerName, slot, set ) \
	[[vk::combinedImageSampler]] [[vk::binding( slot, set )]] texType texName : register( SW_CAT( t, slot ) ); \
	[[vk::combinedImageSampler]] [[vk::binding( slot, set )]] SamplerState samplerName : register( SW_CAT( s, slot ) )

// Separate Textures, Samplers, Buffers, and Unbounded Arrays
#define SW_DECLARE_TEXTURE2D( texName, slot ) \
	[[vk::binding( slot, 0 )]] Texture2D texName : register( SW_CAT( t, slot ) )
#define SW_DECLARE_TEXTURE2D_SPACE( texName, slot, spaceSet ) \
	[[vk::binding( slot, spaceSet )]] Texture2D texName : register( SW_CAT( t, slot ), SW_CAT( space, spaceSet ) )
#define SW_DECLARE_TEXTURE2D_ARRAY_UNBOUNDED( texName, slot, spaceSet ) \
	[[vk::binding( slot, spaceSet )]] Texture2D texName[] : register( SW_CAT( t, slot ), SW_CAT( space, spaceSet ) )

#define SW_DECLARE_SAMPLER( samplerName, slot ) \
	[[vk::binding( slot, 0 )]] SamplerState samplerName : register( SW_CAT( s, slot ) )
#define SW_DECLARE_SAMPLER_SPACE( samplerName, slot, spaceSet ) \
	[[vk::binding( slot, spaceSet )]] SamplerState samplerName : register( SW_CAT( s, slot ), SW_CAT( space, spaceSet ) )

#if defined( VULKAN )
// Vulkan: 버퍼는 슬롯마다 세트 하나 — t# → set SW_VK_SET_STORAGE#, u# → set SW_VK_SET_UAV# (binding 0).
// C++ bindStructuredBuffer / bindComputeShaderResource / bindComputeUAV 가 같은 규칙(shaderslot::vk)으로 건다.
// 그래서 셰이더는 백엔드 분기 없이 SW_DECLARE_STRUCTURED_BUFFER( T, name, 0 ) 만 쓰면 된다.
#define SW_DECLARE_STRUCTURED_BUFFER( elemType, bufName, slot ) \
	[[vk::binding( 0, SW_CAT( SW_VK_SET_STORAGE, slot ) )]] StructuredBuffer<elemType> bufName : register( SW_CAT( t, slot ) )
#define SW_DECLARE_RW_STRUCTURED_BUFFER( elemType, bufName, slot ) \
	[[vk::binding( 0, SW_CAT( SW_VK_SET_UAV, slot ) )]] RWStructuredBuffer<elemType> bufName : register( SW_CAT( u, slot ) )
#define SW_DECLARE_BYTE_ADDRESS_BUFFER( bufName, slot ) \
	[[vk::binding( 0, SW_CAT( SW_VK_SET_STORAGE, slot ) )]] ByteAddressBuffer bufName : register( SW_CAT( t, slot ) )
#define SW_DECLARE_RW_BYTE_ADDRESS_BUFFER( bufName, slot ) \
	[[vk::binding( 0, SW_CAT( SW_VK_SET_UAV, slot ) )]] RWByteAddressBuffer bufName : register( SW_CAT( u, slot ) )
#else
// OpenGL: t# → SSBO binding #, u# → SSBO binding SW_GL_UAV_BINDING0 + # — u 는 명시 binding 을 두지 않고
// DXC -fvk-u-shift(= SW_GL_UAV_BINDING0) 에 맡긴다. 예전엔 u0 도 binding 0 이라 gpucull 의 g_Instances(t0) 와
// g_IndirectArgs(u0) 가 같은 SSBO 자리를 다퉜다(검증 에러 없음, 컬링 결과가 조용히 깨짐).
#define SW_DECLARE_STRUCTURED_BUFFER( elemType, bufName, slot ) \
	[[vk::binding( slot, 0 )]] StructuredBuffer<elemType> bufName : register( SW_CAT( t, slot ) )
#define SW_DECLARE_RW_STRUCTURED_BUFFER( elemType, bufName, slot ) \
	RWStructuredBuffer<elemType> bufName : register( SW_CAT( u, slot ) )
#define SW_DECLARE_BYTE_ADDRESS_BUFFER( bufName, slot ) \
	[[vk::binding( slot, 0 )]] ByteAddressBuffer bufName : register( SW_CAT( t, slot ) )
#define SW_DECLARE_RW_BYTE_ADDRESS_BUFFER( bufName, slot ) \
	RWByteAddressBuffer bufName : register( SW_CAT( u, slot ) )
#endif
#define SW_DECLARE_STRUCTURED_BUFFER_SPACE( elemType, bufName, slot, spaceSet ) \
	[[vk::binding( slot, spaceSet )]] StructuredBuffer<elemType> bufName : register( SW_CAT( t, slot ), SW_CAT( space, spaceSet ) )
#define SW_DECLARE_RW_STRUCTURED_BUFFER_SPACE( elemType, bufName, slot, spaceSet ) \
	[[vk::binding( slot, spaceSet )]] RWStructuredBuffer<elemType> bufName : register( SW_CAT( u, slot ), SW_CAT( space, spaceSet ) )
#define SW_DECLARE_BYTE_ADDRESS_BUFFER_SPACE( bufName, slot, spaceSet ) \
	[[vk::binding( slot, spaceSet )]] ByteAddressBuffer bufName : register( SW_CAT( t, slot ), SW_CAT( space, spaceSet ) )
#define SW_DECLARE_RW_BYTE_ADDRESS_BUFFER_SPACE( bufName, slot, spaceSet ) \
	[[vk::binding( slot, spaceSet )]] RWByteAddressBuffer bufName : register( SW_CAT( u, slot ), SW_CAT( space, spaceSet ) )
#define SW_DECLARE_RW_BYTE_ADDRESS_BUFFER_ARRAY_UNBOUNDED( bufName, slot, spaceSet ) \
	[[vk::binding( slot, spaceSet )]] RWByteAddressBuffer bufName[] : register( SW_CAT( u, slot ), SW_CAT( space, spaceSet ) )

// Vulkan 은 디스크립터 **세트** 단위로 바인딩하므로 상수 버퍼 슬롯마다 세트가 하나씩 필요하다.
// b# → 세트 번호는 bindingslots.hlsli 가 정한다 — C++ VulkanRHIDevice 도 같은 파일을 읽으므로 어긋날 수 없다.
#define SW_VK_CB_SET_0 SW_VK_SET_PASS_CB
#define SW_VK_CB_SET_1 SW_VK_SET_MATERIAL_CB
#if defined( OPENGL )
// **GL 은 디스크립터 세트가 없다** (GL_ARB_gl_spirv 는 set 을 무시한다). Vulkan 처럼 b0/b1 을 세트로
// 가르면 GL 에서는 둘 다 UBO binding 0 이 되어 **겹친다** — MaterialCB 의 color 가 PassCB 의
// g_LightViewProj 첫 행을 읽어 큐브가 검게 나왔다(검증 에러 없음). GL 은 슬롯을 binding 번호로
// 가른다: b0=binding 0, b1=binding 1. 엔진도 같은 번호에 건다(OpenGLRHICommandContext::bindConstantBuffer).
// 구운 .spv 의 OpDecorate(Binding) 이 정본이다.
#define SW_DECLARE_CBUFFER( name, slot ) \
	[[vk::binding( slot, 0 )]] cbuffer name : register( SW_CAT( b, slot ) )
#else
#define SW_DECLARE_CBUFFER( name, slot ) \
	[[vk::binding( 0, SW_CAT( SW_VK_CB_SET_, slot ) )]] cbuffer name : register( SW_CAT( b, slot ) )
#endif
#define SW_DECLARE_CBUFFER_SPACE( name, slot, spaceSet ) \
	[[vk::binding( slot, spaceSet )]] cbuffer name : register( SW_CAT( b, slot ), SW_CAT( space, spaceSet ) )
#else
#define SW_VK_BINDING( slot, set )
#define SW_VK_COMBINED

// Paired Texture + Sampler declaration for Direct3D (FXC / DXC)
#define SW_DECLARE_TEXTURE2D_SAMPLER( texName, samplerName, slot, set ) \
	Texture2D texName : register( SW_CAT( t, slot ) ); \
	SamplerState samplerName : register( SW_CAT( s, slot ) )

#define SW_DECLARE_TEXTURE_SAMPLER( texType, texName, samplerName, slot, set ) \
	texType texName : register( SW_CAT( t, slot ) ); \
	SamplerState samplerName : register( SW_CAT( s, slot ) )

// Separate Textures, Samplers, Buffers, and Unbounded Arrays
#define SW_DECLARE_TEXTURE2D( texName, slot ) \
	Texture2D texName : register( SW_CAT( t, slot ) )
#define SW_DECLARE_TEXTURE2D_SPACE( texName, slot, spaceSet ) \
	Texture2D texName : register( SW_CAT( t, slot ), SW_CAT( space, spaceSet ) )
#define SW_DECLARE_TEXTURE2D_ARRAY_UNBOUNDED( texName, slot, spaceSet ) \
	Texture2D texName[] : register( SW_CAT( t, slot ), SW_CAT( space, spaceSet ) )

#define SW_DECLARE_SAMPLER( samplerName, slot ) \
	SamplerState samplerName : register( SW_CAT( s, slot ) )
#define SW_DECLARE_SAMPLER_SPACE( samplerName, slot, spaceSet ) \
	SamplerState samplerName : register( SW_CAT( s, slot ), SW_CAT( space, spaceSet ) )

#define SW_DECLARE_STRUCTURED_BUFFER( elemType, bufName, slot ) \
	StructuredBuffer<elemType> bufName : register( SW_CAT( t, slot ) )
#define SW_DECLARE_STRUCTURED_BUFFER_SPACE( elemType, bufName, slot, spaceSet ) \
	StructuredBuffer<elemType> bufName : register( SW_CAT( t, slot ), SW_CAT( space, spaceSet ) )

#define SW_DECLARE_RW_STRUCTURED_BUFFER( elemType, bufName, slot ) \
	RWStructuredBuffer<elemType> bufName : register( SW_CAT( u, slot ) )
#define SW_DECLARE_RW_STRUCTURED_BUFFER_SPACE( elemType, bufName, slot, spaceSet ) \
	RWStructuredBuffer<elemType> bufName : register( SW_CAT( u, slot ), SW_CAT( space, spaceSet ) )

#define SW_DECLARE_BYTE_ADDRESS_BUFFER( bufName, slot ) \
	ByteAddressBuffer bufName : register( SW_CAT( t, slot ) )
#define SW_DECLARE_BYTE_ADDRESS_BUFFER_SPACE( bufName, slot, spaceSet ) \
	ByteAddressBuffer bufName : register( SW_CAT( t, slot ), SW_CAT( space, spaceSet ) )

#define SW_DECLARE_RW_BYTE_ADDRESS_BUFFER( bufName, slot ) \
	RWByteAddressBuffer bufName : register( SW_CAT( u, slot ) )
#define SW_DECLARE_RW_BYTE_ADDRESS_BUFFER_SPACE( bufName, slot, spaceSet ) \
	RWByteAddressBuffer bufName : register( SW_CAT( u, slot ), SW_CAT( space, spaceSet ) )
#define SW_DECLARE_RW_BYTE_ADDRESS_BUFFER_ARRAY_UNBOUNDED( bufName, slot, spaceSet ) \
	RWByteAddressBuffer bufName[] : register( SW_CAT( u, slot ), SW_CAT( space, spaceSet ) )

#define SW_DECLARE_CBUFFER( name, slot ) \
	cbuffer name : register( SW_CAT( b, slot ) )
#define SW_DECLARE_CBUFFER_SPACE( name, slot, spaceSet ) \
	cbuffer name : register( SW_CAT( b, slot ), SW_CAT( space, spaceSet ) )
#endif

// ------------------------------------------------------------------------------
// 2) Common Math Constants
// ------------------------------------------------------------------------------
static const float kPi	   = 3.14159265358979323846f;
static const float kTwoPi  = 6.28318530717958647692f;
static const float kHalfPi = 1.57079632679489661923f;
static const float kInvPi  = 0.31830988618379067154f;

#endif // SW_ENGINE_COMMON_HLSLI
