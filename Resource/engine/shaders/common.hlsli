/**
 * Common shader macros, attributes, and definitions for cross-backend HLSL / SPIR-V shaders.
 * - DirectX 11 (FXC, SM5.0)
 * - DirectX 12 (DXC, DXIL, SM6.x)
 * - Vulkan (DXC, SPIR-V, SM6.x)
 * - OpenGL (DXC, SPIR-V, GL_ARB_gl_spirv)
 */

#ifndef SW_ENGINE_COMMON_HLSLI
#define SW_ENGINE_COMMON_HLSLI

// ------------------------------------------------------------------------------
// 1) Cross-Backend Vulkan / SPIR-V Attribute Macros
// ------------------------------------------------------------------------------
#if defined( __spirv__ )
#define SW_VK_BINDING( slot, set )	 [[vk::binding( slot, set )]]
#define SW_VK_COMBINED				 [[vk::combinedImageSampler]]

// Paired Combined Texture + Sampler declaration for SPIR-V targets
#define SW_DECLARE_TEXTURE2D_SAMPLER( texName, samplerName, slot, set ) \
	[[vk::combinedImageSampler]] [[vk::binding( slot, set )]] Texture2D texName : register( t##slot ); \
	[[vk::combinedImageSampler]] [[vk::binding( slot, set )]] SamplerState samplerName : register( s##slot )

#define SW_DECLARE_TEXTURE_SAMPLER( texType, texName, samplerName, slot, set ) \
	[[vk::combinedImageSampler]] [[vk::binding( slot, set )]] texType texName : register( t##slot ); \
	[[vk::combinedImageSampler]] [[vk::binding( slot, set )]] SamplerState samplerName : register( s##slot )

// Separate Textures, Samplers, Buffers, and Unbounded Arrays
#define SW_DECLARE_TEXTURE2D( texName, slot ) \
	[[vk::binding( slot, 0 )]] Texture2D texName : register( t##slot )
#define SW_DECLARE_TEXTURE2D_SPACE( texName, slot, spaceSet ) \
	[[vk::binding( slot, spaceSet )]] Texture2D texName : register( t##slot, space##spaceSet )
#define SW_DECLARE_TEXTURE2D_ARRAY_UNBOUNDED( texName, slot, spaceSet ) \
	[[vk::binding( slot, spaceSet )]] Texture2D texName[] : register( t##slot, space##spaceSet )

#define SW_DECLARE_SAMPLER( samplerName, slot ) \
	[[vk::binding( slot, 0 )]] SamplerState samplerName : register( s##slot )
#define SW_DECLARE_SAMPLER_SPACE( samplerName, slot, spaceSet ) \
	[[vk::binding( slot, spaceSet )]] SamplerState samplerName : register( s##slot, space##spaceSet )

#define SW_DECLARE_STRUCTURED_BUFFER( elemType, bufName, slot ) \
	[[vk::binding( slot, 0 )]] StructuredBuffer<elemType> bufName : register( t##slot )
#define SW_DECLARE_STRUCTURED_BUFFER_SPACE( elemType, bufName, slot, spaceSet ) \
	[[vk::binding( slot, spaceSet )]] StructuredBuffer<elemType> bufName : register( t##slot, space##spaceSet )

#define SW_DECLARE_RW_STRUCTURED_BUFFER( elemType, bufName, slot ) \
	[[vk::binding( slot, 0 )]] RWStructuredBuffer<elemType> bufName : register( u##slot )
#define SW_DECLARE_RW_STRUCTURED_BUFFER_SPACE( elemType, bufName, slot, spaceSet ) \
	[[vk::binding( slot, spaceSet )]] RWStructuredBuffer<elemType> bufName : register( u##slot, space##spaceSet )

#define SW_DECLARE_BYTE_ADDRESS_BUFFER( bufName, slot ) \
	[[vk::binding( slot, 0 )]] ByteAddressBuffer bufName : register( t##slot )
#define SW_DECLARE_BYTE_ADDRESS_BUFFER_SPACE( bufName, slot, spaceSet ) \
	[[vk::binding( slot, spaceSet )]] ByteAddressBuffer bufName : register( t##slot, space##spaceSet )

#define SW_DECLARE_RW_BYTE_ADDRESS_BUFFER( bufName, slot ) \
	[[vk::binding( slot, 0 )]] RWByteAddressBuffer bufName : register( u##slot )
#define SW_DECLARE_RW_BYTE_ADDRESS_BUFFER_SPACE( bufName, slot, spaceSet ) \
	[[vk::binding( slot, spaceSet )]] RWByteAddressBuffer bufName : register( u##slot, space##spaceSet )
#define SW_DECLARE_RW_BYTE_ADDRESS_BUFFER_ARRAY_UNBOUNDED( bufName, slot, spaceSet ) \
	[[vk::binding( slot, spaceSet )]] RWByteAddressBuffer bufName[] : register( u##slot, space##spaceSet )

// Vulkan 은 디스크립터 **세트** 단위로 바인딩하므로 상수 버퍼 슬롯마다 세트가 하나씩 필요하다.
// 아래 번호는 C++ VulkanRHIDevice::kPassCbSetIndex / kMaterialCbSetIndex 와 같은 값이어야 한다.
#define SW_VK_CB_SET_0 0
#define SW_VK_CB_SET_1 10
#define SW_DECLARE_CBUFFER( name, slot ) \
	[[vk::binding( 0, SW_VK_CB_SET_##slot )]] cbuffer name : register( b##slot )
#define SW_DECLARE_CBUFFER_SPACE( name, slot, spaceSet ) \
	[[vk::binding( slot, spaceSet )]] cbuffer name : register( b##slot, space##spaceSet )
#else
#define SW_VK_BINDING( slot, set )
#define SW_VK_COMBINED

// Paired Texture + Sampler declaration for Direct3D (FXC / DXC)
#define SW_DECLARE_TEXTURE2D_SAMPLER( texName, samplerName, slot, set ) \
	Texture2D texName : register( t##slot ); \
	SamplerState samplerName : register( s##slot )

#define SW_DECLARE_TEXTURE_SAMPLER( texType, texName, samplerName, slot, set ) \
	texType texName : register( t##slot ); \
	SamplerState samplerName : register( s##slot )

// Separate Textures, Samplers, Buffers, and Unbounded Arrays
#define SW_DECLARE_TEXTURE2D( texName, slot ) \
	Texture2D texName : register( t##slot )
#define SW_DECLARE_TEXTURE2D_SPACE( texName, slot, spaceSet ) \
	Texture2D texName : register( t##slot, space##spaceSet )
#define SW_DECLARE_TEXTURE2D_ARRAY_UNBOUNDED( texName, slot, spaceSet ) \
	Texture2D texName[] : register( t##slot, space##spaceSet )

#define SW_DECLARE_SAMPLER( samplerName, slot ) \
	SamplerState samplerName : register( s##slot )
#define SW_DECLARE_SAMPLER_SPACE( samplerName, slot, spaceSet ) \
	SamplerState samplerName : register( s##slot, space##spaceSet )

#define SW_DECLARE_STRUCTURED_BUFFER( elemType, bufName, slot ) \
	StructuredBuffer<elemType> bufName : register( t##slot )
#define SW_DECLARE_STRUCTURED_BUFFER_SPACE( elemType, bufName, slot, spaceSet ) \
	StructuredBuffer<elemType> bufName : register( t##slot, space##spaceSet )

#define SW_DECLARE_RW_STRUCTURED_BUFFER( elemType, bufName, slot ) \
	RWStructuredBuffer<elemType> bufName : register( u##slot )
#define SW_DECLARE_RW_STRUCTURED_BUFFER_SPACE( elemType, bufName, slot, spaceSet ) \
	RWStructuredBuffer<elemType> bufName : register( u##slot, space##spaceSet )

#define SW_DECLARE_BYTE_ADDRESS_BUFFER( bufName, slot ) \
	ByteAddressBuffer bufName : register( t##slot )
#define SW_DECLARE_BYTE_ADDRESS_BUFFER_SPACE( bufName, slot, spaceSet ) \
	ByteAddressBuffer bufName : register( t##slot, space##spaceSet )

#define SW_DECLARE_RW_BYTE_ADDRESS_BUFFER( bufName, slot ) \
	RWByteAddressBuffer bufName : register( u##slot )
#define SW_DECLARE_RW_BYTE_ADDRESS_BUFFER_SPACE( bufName, slot, spaceSet ) \
	RWByteAddressBuffer bufName : register( u##slot, space##spaceSet )
#define SW_DECLARE_RW_BYTE_ADDRESS_BUFFER_ARRAY_UNBOUNDED( bufName, slot, spaceSet ) \
	RWByteAddressBuffer bufName[] : register( u##slot, space##spaceSet )

#define SW_DECLARE_CBUFFER( name, slot ) \
	cbuffer name : register( b##slot )
#define SW_DECLARE_CBUFFER_SPACE( name, slot, spaceSet ) \
	cbuffer name : register( b##slot, space##spaceSet )
#endif

// ------------------------------------------------------------------------------
// 2) Common Math Constants
// ------------------------------------------------------------------------------
static const float kPi	   = 3.14159265358979323846f;
static const float kTwoPi  = 6.28318530717958647692f;
static const float kHalfPi = 1.57079632679489661923f;
static const float kInvPi  = 0.31830988618379067154f;

#endif // SW_ENGINE_COMMON_HLSLI
