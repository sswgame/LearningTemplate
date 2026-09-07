/**
 * Common shader macros, attributes, and definitions for cross-backend HLSL / SPIR-V shaders.
 * - DirectX 11 (FXC, SM5.0)
 * - DirectX 12 (DXC, DXIL, SM6.x)
 * - Vulkan (DXC, SPIR-V, SM6.x)
 * - OpenGL (DXC, SPIR-V, GL_ARB_gl_spirv)
 *
 * 선언은 네 백엔드에서 같다 (bindingslots.hlsli 의 표). 백엔드별로 다른 것은 SPIR-V 의 binding 번호뿐인데,
 * Vulkan 은 DXC 시프트(-fvk-*-shift)가, OpenGL 은 아래 매크로의 [[vk::binding]] 이 정한다.
 *
 *   SW_DECLARE_CBUFFER( MaterialCB, SW_SLOT_MATERIAL_CB ) { float4 color; };   // 픽스처·비 GPUScene 드로우용
 *   SW_DECLARE_STRUCTURED_BUFFER( GpuInstance, g_Instances, 0 );              // 컴퓨트 읽기 t0
 *   SW_DECLARE_RW_STRUCTURED_BUFFER( DrawIndirectCommand, g_IndirectArgs, 0 ); // 컴퓨트 쓰기 u0
 * 씬 메시 머티리얼은 binding.hlsli 의 SW_MATERIAL_BEGIN/END + SW_MATERIAL( index ) 를 쓴다 (GPUScene 버퍼).
 */

#ifndef SW_ENGINE_COMMON_HLSLI
#define SW_ENGINE_COMMON_HLSLI

#include "bindingslots.hlsli"

// 토큰 붙이기(##)는 인자를 매크로 확장하기 **전에** 붙인다. 그래서 register( SW_CAT( t, slot ) ) 에는 리터럴만 넘길 수
// 있었고, binding.hlsli 는 슬롯 번호를 손으로 다시 적어야 했다(어긋나면 엉뚱한 슬롯을 읽는다). 한 겹 더
// 감싸면 인자가 먼저 확장된다 — SW_CAT( t, SW_SLOT_INSTANCE_SRV ) → t4. 아래 선언 매크로는 전부 이걸 쓴다.
#define SW_CAT_( a, b ) a##b
#define SW_CAT( a, b )  SW_CAT_( a, b )

// 네이티브 bindless 텍스처 백엔드 — 컴파일러(ShaderCompiler.cpp)가 DX12/Vulkan 에 SW_BINDLESS=1 을 정의한다.
#if defined( SW_BINDLESS ) && ( defined( DX12 ) || defined( VULKAN ) )
#define SW_NATIVE_BINDLESS 1
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

#if defined( __spirv__ )
#define SW_VK_COMBINED [[vk::combinedImageSampler]]
#else
#define SW_VK_COMBINED
#endif

// ------------------------------------------------------------------------------
// 2) 상수버퍼 — 네 백엔드 공통 `cbuffer name : register(b#)`. 필드는 맨이름으로 쓴다.
// ------------------------------------------------------------------------------
#define SW_DECLARE_CBUFFER( name, slot ) \
	SW_GL_BINDING( slot ) cbuffer name : register( SW_CAT( b, slot ) )

// ------------------------------------------------------------------------------
// 3) 구조버퍼 / 바이트주소버퍼 — 읽기 t#, 쓰기 u#.
//    GL: u 는 명시 binding 을 두지 않고 DXC -fvk-u-shift(= SW_GL_UAV_BINDING0) 에 맡긴다. 예전엔 u0 도 binding 0 이라
//    gpucull 의 g_Instances(t0) 와 g_IndirectArgs(u0) 가 같은 SSBO 자리를 다퉜다(검증 에러 없음, 컬링 결과가 조용히 깨짐).
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
#if defined( OPENGL )
#define SW_DECLARE_RW_TEXTURE2D( texName, slot, glUnit ) \
	[[vk::binding( glUnit, 0 )]] RWTexture2D<float4> texName : register( SW_CAT( u, slot ) )
#else
#define SW_DECLARE_RW_TEXTURE2D( texName, slot, glUnit ) \
	RWTexture2D<float4> texName : register( SW_CAT( u, slot ) )
#endif

// ------------------------------------------------------------------------------
// 3-1) 루트/푸시 상수 — setComputeRootConstants 가 채우는 16 dword. 블록 이름은 SwRootConstants 로 고정(계약 검증 키).
//      DX12 b0 space2 (32비트 루트 상수), Vulkan 푸시 상수, DX11/GL 은 b SW_SLOT_ROOT_CB_EMUL 상수버퍼 에뮬.
//      용법: SW_ROOT_CONSTANTS_BEGIN  uint g_Target; uint g_Width;  SW_ROOT_CONSTANTS_END  ...  SW_ROOT( g_Target )
//      Vulkan 은 [[vk::push_constant]] 가 구조체 전역에만 붙어 필드가 전역 이름이 아니다 — 그래서 SW_ROOT() 로 읽는다.
// ------------------------------------------------------------------------------
#if defined( OPENGL )
#define SW_ROOT_CONSTANTS_BEGIN [[vk::binding( SW_SLOT_ROOT_CB_EMUL, 0 )]] cbuffer SwRootConstants : register( SW_CAT( b, SW_SLOT_ROOT_CB_EMUL ) ) {
#define SW_ROOT_CONSTANTS_END   };
#define SW_ROOT( field ) field
#elif defined( __spirv__ )
#define SW_ROOT_CONSTANTS_BEGIN struct SwRootConstants_t {
#define SW_ROOT_CONSTANTS_END   }; [[vk::push_constant]] ConstantBuffer<SwRootConstants_t> SwRootConstants;
#define SW_ROOT( field ) SwRootConstants.field
#elif defined( DX12 )
#define SW_ROOT_CONSTANTS_BEGIN cbuffer SwRootConstants : register( SW_CAT( b, SW_SLOT_ROOT_CB ), SW_CAT( space, SW_SPACE_ROOT_CB ) ) {
#define SW_ROOT_CONSTANTS_END   };
#define SW_ROOT( field ) field
#else
#define SW_ROOT_CONSTANTS_BEGIN cbuffer SwRootConstants : register( SW_CAT( b, SW_SLOT_ROOT_CB_EMUL ) ) {
#define SW_ROOT_CONSTANTS_END   };
#define SW_ROOT( field ) field
#endif

// ------------------------------------------------------------------------------
// 4) 텍스처 / 샘플러 — 에뮬 백엔드(DX11/GL)의 고정 슬롯 (binding.hlsli 가 쓴다). 네이티브 bindless 텍스처 배열은
//    binding.hlsli 가 SW_SPACE_BINDLESS_TEX / SW_VK_TEXTURE_SET 으로 직접 선언한다.
// ------------------------------------------------------------------------------
#if defined( __spirv__ )
#define SW_DECLARE_TEXTURE2D_SAMPLER( texName, samplerName, slot ) \
	[[vk::combinedImageSampler]] [[vk::binding( slot, 0 )]] Texture2D texName : register( SW_CAT( t, slot ) ); \
	[[vk::combinedImageSampler]] [[vk::binding( slot, 0 )]] SamplerState samplerName : register( SW_CAT( s, slot ) )
#define SW_DECLARE_TEXTURE2D( texName, slot ) \
	[[vk::binding( slot, 0 )]] Texture2D texName : register( SW_CAT( t, slot ) )
#define SW_DECLARE_SAMPLER( samplerName, slot ) \
	[[vk::binding( slot, 0 )]] SamplerState samplerName : register( SW_CAT( s, slot ) )
#else
#define SW_DECLARE_TEXTURE2D_SAMPLER( texName, samplerName, slot ) \
	Texture2D texName : register( SW_CAT( t, slot ) ); \
	SamplerState samplerName : register( SW_CAT( s, slot ) )
#define SW_DECLARE_TEXTURE2D( texName, slot ) \
	Texture2D texName : register( SW_CAT( t, slot ) )
#define SW_DECLARE_SAMPLER( samplerName, slot ) \
	SamplerState samplerName : register( SW_CAT( s, slot ) )
#endif

// ------------------------------------------------------------------------------
// 5) Common Math Constants
// ------------------------------------------------------------------------------
static const float kPi	   = 3.14159265358979323846f;
static const float kTwoPi  = 6.28318530717958647692f;
static const float kHalfPi = 1.57079632679489661923f;
static const float kInvPi  = 0.31830988618379067154f;

#endif // SW_ENGINE_COMMON_HLSLI
