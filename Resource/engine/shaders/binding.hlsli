/**
 * binding.hlsli — 셰이더가 엔진 리소스(상수 · 버퍼 · 텍스처 · 샘플러)를 읽는 경로를 한곳에 모은 헤더.
 *
 * 셰이더는 `#include "binding.hlsli"` 하나만 한다. 처음 읽는 사람은 아래 0 절(bindless 입문)만 읽어도 셰이더를 쓸 수 있다.
 * 슬롯 번호(t# · s# · space)의 원본은 bindingslots.hlsli 다 — 이 파일은 그 번호로 리소스를 선언하고 읽는 함수를 준다.
 *
 * ==============================================================================
 * 0) bindless 입문
 * ==============================================================================
 * 정신 모델
 *   옛 방식은 드로우마다 텍스처를 슬롯(t0, t1 …)에 바인딩하고 셰이더가 그 슬롯을 읽는다. 드로우가 바뀌면 바인딩도 바뀐다.
 *   bindless 는 텍스처를 전부 **크기 제한이 없는 배열 하나**에 넣어 두고, 셰이더가 **정수 인덱스**로 배열의 원소를 읽는다.
 *   바인딩은 프레임 내내 그대로이고, "어느 텍스처인가" 는 상수(uint 하나)로 넘어온다. 그래서 드로우 사이에 바인딩을
 *   교체하지 않고, 같은 PSO 의 드로우를 멀티 드로우 하나로 낼 수 있다.
 *   이 엔진의 배열은 SM5.1 의 무제한 배열(`Texture2D g_SwBindlessTex2D[]`)이다 — SM6.6 의 힙 직접 인덱싱(ResourceDescriptorHeap)이 아니다.
 *
 * 이름 규약
 *   PassCB 의 `uint g_<Name>Index` ↔ 엔진 리소스 `"<Name>"` (예: `g_SceneDepthIndex` ↔ "SceneDepth").
 *   엔진(C++ ShaderParameterBinder)이 리플렉션으로 필드 이름을 읽어 그 리소스의 인덱스를 채운다. 리소스가 없으면 `kInvalidIndex`(0xFFFFFFFF)다.
 *   머티리얼 텍스처는 머티리얼 구조체의 `uint …Map` 필드다(9 절). 이 이름들은 C++ 가 읽으므로 바꾸면 C++ 도 함께 바꿔야 한다.
 *
 * 최소 예 — 텍스처 하나 샘플
 *   float4 color  = Bindless::sampleIndex( g_SourceColorIndex, uv );                                // 엔진 텍스처(패스 입력)
 *   float4 albedo = MaterialTexture::sampleIndex( SW_MATERIAL( materialIndex )._albedoMap, uv );  // 머티리얼 텍스처
 *
 * 셰이더 작성자는 이 함수만 쓴다 — 네 백엔드에서 뜻이 같고, 무효 인덱스를 스스로 거른다
 *   엔진 텍스처  Bindless::sampleIndex · sampleIndexWith(샘플러 고르기) · sampleShadowComparison
 *   패스 입력    PassTexture::sampleShadow · sampleAlbedo · sampleNormal · sampleDepth · sampleSource · sampleDepthPoint · sampleSourcePoint ·
 *                sampleDepthCross · gatherDepthRed · sampleAmbientOcclusion
 *   머티리얼     MaterialTexture::sampleIndex · samplePoint · computeSize
 *   컴퓨트 RW    Bindless::storeRwTexture2D · loadRwTexture2D
 *   함수는 구조체의 static 멤버로 묶는다 — FXC(DX11)에 네임스페이스가 없어 구조체가 그 자리를 맡는다. 묶음의 선언은 아래 0-1 절이다.
 *   `SW_NATIVE_BINDLESS` · `__spirv__` 분기와 `g_SwBindlessTex2D` · `g_SwSlot#` · `g_SwMaterialTex#` 는 **이 파일 안에서만** 쓴다.
 *   예외 셋: `autoexposure.hlsl` · `depthhistory.hlsl` · `fsr1.hlsl` 이 Load · GetDimensions · Gather 를 배열과 슬롯에 직접 읽는다(진입 함수에 Load · 크기 · 게더가 아직 없다).
 *   진입 함수로 옮기는 일은 네 백엔드 셰이더 컴파일로 확인하며 한다 — 새 패스가 이 분기를 따라 쓰지 않는다.
 *   셰이더 파일(.hlsl)에 이 분기를 들이지 않는다 — 새 읽기 방식이 필요하면 여기에 진입 함수를 더한다.
 *
 * 백엔드 차이
 *   ┌────────┬──────────────────┬─────────────────────────────────────────┬──────────────────────────────────────────┐
 *   │ 백엔드 │ 방식             │ 텍스처가 있는 곳                        │ 인덱스의 뜻                              │
 *   ├────────┼──────────────────┼─────────────────────────────────────────┼──────────────────────────────────────────┤
 *   │ DX12   │ 네이티브 bindless│ t0 space1 무제한 배열(힙 전체 테이블)   │ 배열 원소 번호                           │
 *   │ Vulkan │ 네이티브 bindless│ set 1 binding 0 무제한 배열             │ 배열 원소 번호                           │
 *   │ DX11   │ 슬롯 에뮬        │ 엔진 t0..t3 · t15 · 머티리얼 t16..t31   │ 엔진: PassCB 인덱스와 값 비교로 슬롯 선택│
 *   │ OpenGL │ 슬롯 에뮬        │ 텍스처 유닛 0..3 · 15 · 16..31(결합)    │ 머티리얼: 서수 0..15 (DX11 과 같다)      │
 *   └────────┴──────────────────┴─────────────────────────────────────────┴──────────────────────────────────────────┘
 *   네이티브인지는 common.hlsli 의 `SW_NATIVE_BINDLESS` 가 정한다(ShaderCompiler 가 DX12 · Vulkan 에만 SW_BINDLESS=1 을 준다).
 *   에뮬 백엔드는 엔진이 아는 텍스처(PassCB 의 g_*Index)만 찾을 수 있다 — 모르는 인덱스는 슬롯 0 을 읽는다.
 *
 * 자주 하는 실수
 *   - 무효 인덱스로 배열을 읽는다. `kInvalidIndex` 로 원소를 읽으면 DX12 는 페이지 폴트(DEVICE_HUNG), Vulkan 은 DEVICE_LOST 다.
 *     진입 함수는 무효 인덱스를 먼저 걸러 정해진 값(색 (0,0,0,1) · 그림자 1 · AO 1 · 머티리얼 흰색)을 돌려준다.
 *   - `if ( index != kInvalidIndex )` 분기만 믿는다. 드라이버 컴파일러는 서술자 읽기를 분기 앞으로 끌어올릴 수 있어(부작용이 없다고 본다)
 *     분기 안의 읽기도 무효 인덱스로 실행될 수 있다. 그래서 첨자를 배열 크기로 **마스크**한다(Bindless::textureSlot, 11 절).
 *     배열을 직접 인덱싱하는 함수를 새로 쓰면 반드시 이 마스크를 거친다.
 *   - 픽셀마다 다를 수 있는 인덱스(머티리얼 텍스처)에 `NonUniformResourceIndex` 를 빼먹는다 — 결과가 정의되지 않는다.
 *   - 에뮬 백엔드에 엔진 텍스처를 더하면서 슬롯 대응(11-2 절의 Bindless::sampleIndex · C++ commitBindlessTextureBindings)을 함께 고치지 않는다.
 *
 * ==============================================================================
 * 절 목차
 * ==============================================================================
 *    1) PassCB                 2) 드로우별 루트 상수      3) GPUScene 인스턴스 버퍼   4) 씬 배치 테이블
 *    5) 모프 정점 풀           6) 정점 애니메이션(VAT)    7) 위치 · 노멀 변환        8) 인스턴스 읽기
 *    9) 머티리얼 데이터       10) 샘플러 세트            11) 텍스처 읽기(11-1 네이티브 bindless · 11-2 슬롯 에뮬 · 11-3 공통 진입 함수)
 *   12) 픽셀 출력
 *
 * 그 밖의 약속
 * - 셰이더는 `g_ViewProj` / `g_World` 등 PassCB 필드와 진입 함수를 바로 쓴다(접근 함수 인다이렉션 없음).
 * - 엔진(C++ ShaderParameterBinder)이 ShaderReflection 으로 PassCB 멤버 이름을 읽어 값을 채운다.
 *   따라서 이 파일의 PassCB 를 고치면 C++ 는 자동으로 따라온다 (미러 없음).
 * - GPUScene(언리얼 방식): 드로우별 데이터는 바인딩이 아니라 버퍼에서 읽는다.
 *     VS: `InstanceData instance = InstanceData::load( input._instanceSlot )` → instance._world / instance._materialIndex
 *     PS: `SW_MATERIAL( materialIndex )._color`  — VS 가 materialIndex 를 nointerpolation 으로 넘긴다.
 *   머티리얼 구조체는 셰이더가 SW_MATERIAL_BEGIN/END 로 선언하고, 엔진이 그 셰이더 타입의 머티리얼 데이터를
 *   StructuredBuffer(g_SwMaterials, t5)에 원소로 쌓아 인스턴스에 materialIndex 를 매긴다.
 */

#ifndef SW_ENGINE_BINDING_HLSLI
#define SW_ENGINE_BINDING_HLSLI

#include "common.hlsli"
#include "instancedata.hlsli"
#include "lightgridcommon.hlsli"

static const uint kInvalidIndex = 0xFFFFFFFFu;

// ------------------------------------------------------------------------------
// 0-1) 이 헤더가 내놓는 함수의 묶음 — 선언만 둔다. 정의는 각 절에 있다(리소스 선언과 백엔드 분기 사이에 두어야 해서 밖에 정의한다).
//      정의도 `static` 을 붙인다(FXC 가 선언과 정의의 static 이 다르면 거절한다). 인스턴스 · 배치 원소의 함수는 그 구조체(instancedata · batchdata)에 있다.
// ------------------------------------------------------------------------------
/** @brief 정점 위치 · 노멀을 공간 사이로 옮긴다(7 절). 깊이에서 월드 위치를 되짚는 식은 lighting.hlsli 다. */
struct Transform
{
    static float4 computeWorldPosition( float3 localPosition, float4x4 world );
    static float4 computeClipPosition( float4 worldPosition, float4x4 viewProj );
    static float3 computeWorldNormal( float3 localNormal, float4x4 world );
    static float3 computeWorldPositionFromDepth( float2 uv, float deviceDepth );
};

/** @brief 지금 뷰(PassCB)에서 구하는 값. */
struct View
{
    static float4x4 getViewProjection(); // 그림자 패스(SW_PASS_SHADOW)는 빛의 뷰-투영
    static float3 computeToView( float3 worldPosition );
};

/** @brief 모프 · 스킨 정점 풀(5 절). */
struct MorphPool
{
    static uint computeElement( uint batchIndex, uint vertexId );
    static void loadVertex( uint batchIndex, uint vertexId, float3 restPosition, float3 restNormal, out float3 outPosition, out float3 outNormal );
    static float4 loadTangent( uint batchIndex, uint vertexId, float4 restTangent );
};

/** @brief 단위 벡터 ↔ 옥타헤드럴 좌표 [-1, 1]^2 (6 절 앞). C++ `OctahedralNormal` 과 같은 식이다 — G버퍼 노멀 첨부(R16G16_SNORM)와 VAT 노멀이 쓴다. */
struct Octahedral
{
    static float2 encode( float3 direction );
    static float3 decode( float2 coordinate );
};

/** @brief 정점 애니메이션(VAT) 표(6 절). 인스턴스마다의 원소 번호(표 안 단어 번호)는 InstanceData::computeVertexAnimationElements 다. */
struct VertexAnimation
{
    static float toSnorm( int raw, float maxRaw );
    static float3 loadNormal( uint element );
    static float3 computePosition( uint headerBase, uint firstElement, uint secondElement, float alpha );
};

/** @brief 등장 디더(8-1 절). */
struct Appearance
{
    static float computeDitherThreshold( float2 pixelPosition );
    static void clipDither( float appearance, float2 pixelPosition );
    static float computeLodFadeAppearance( float instanceAppearance, uint fadeCode );
};

/** @brief 머티리얼 데이터 버퍼(9 절). */
struct MaterialBuffer
{
    static uint getDrawCount(); // 이 드로우 그룹의 원소 수(루트 상수). 루트 상수 블록이 없는 셰이더는 0
    static uint clampIndex( uint index );
};

/** @brief bindless 배열과 에뮬 슬롯을 번호로 읽고 쓴다(11-1 · 11-2 절). 슬롯 함수는 DX11 · GL 에만 정의가 있다. */
struct Bindless
{
    static uint textureSlot( uint index );
    static uint rwTextureSlot( uint index );
    static uint emulatedSlot( uint index ); // 에뮬 백엔드(DX11 · GL)만 정의가 있다 — 엔진 텍스처 인덱스가 걸리는 슬롯(없으면 kUnmappedEngineTextureSlot)
    static float4 sampleIndex( uint index, float2 uv );
    static float4 sampleIndexWith( uint index, uint samplerId, float2 uv );
    static float sampleShadowComparison( uint index, float2 uv, float depth );
    static void storeRwTexture2D( uint index, uint2 texelPosition, float4 value );
    static float4 loadRwTexture2D( uint index, uint2 texelPosition );
    static float4 sampleSlotWith( Texture2D slotTexture, uint samplerId, float2 uv );
    static float4 sampleSlotPoint( Texture2D slotTexture, float2 uv );
};

/** @brief 패스가 이름으로 건 엔진 텍스처(PassCB 의 g_*Index)를 읽는다(11-3 절). */
struct PassTexture
{
    static float4 sampleShadow( float2 uv );
    static float4 sampleAlbedo( float2 uv );
    static float3 sampleNormal( float2 uv ); // G버퍼 노멀 칸(옥타헤드럴 좌표)을 푼 단위 노멀
    static float4 sampleGbufferMaterial( float2 uv );
    static float4 sampleGbufferMaterialSlot( float2 uv );
    static void loadGbufferMaterial( float2 uv, out float outRoughness, out float outMetallic, out bool outDrawn );
    static float4 sampleDepth( float2 uv );
    static float4 sampleSource( float2 uv );
    static float4 sampleDepthPoint( float2 uv );
    static float4 sampleSourcePoint( float2 uv );
    static void sampleDepthCross( float2 uv, float2 texel, out float outCenterDepth, out float4 outNeighborDepth );
    static float4 gatherDepthRed( float2 uv );
    static float sampleAmbientOcclusion( float2 uv );
    static float4 sampleLight2dCookieAtlas( float2 uv );
};

/** @brief 머티리얼이 준 텍스처 번호를 읽는다(11-3 절). 에뮬 백엔드의 번호는 머티리얼 슬롯 서수다. */
struct MaterialTexture
{
    static float4 sampleIndex( uint index, float2 uv );
    static float4 sampleIndexTexture( uint index, float2 uv ); // 텍스처를 읽는다(보기 모드 Mipmaps 가 아닐 때의 sampleIndex)
    static float4 samplePoint( uint index, float2 uv );
    static float4 sampleLevelZero( uint index, float2 uv );
    static float2 computeSize( uint index );
    static float4 sampleMipColor( uint index, float2 uv );
};

/** @brief 보기 모드가 바꾸는 색(12 절 앞). */
struct ViewMode
{
    static float4 computeMipColor( float lod );
    static float3 computeLightComplexityColor( uint visitCount );
    static float4 computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic );
    static float3 overrideLightingAlbedo( float3 albedo ); // LightingOnly 만 표면 색을 회색으로 바꾼다
    static void applyToSurface( inout float4 inoutLitColor, inout float4 inoutAlbedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic );
};

// ------------------------------------------------------------------------------
// 1) PassCB — b0. 셰이더가 실제 쓰는 필드만. 엔진이 이름으로 채운다. 패스(배치)마다 한 번 걸리는 진짜 상수버퍼.
// ------------------------------------------------------------------------------
SW_DECLARE_CBUFFER( PassCB, SW_SLOT_PASS_CB )
{
    float4x4 g_LightViewProj;
    float4x4 g_ViewProj;
    // 뷰-투영의 역행렬 — 디퍼드 조명이 **깊이에서 월드 위치를 복원**하는 데 쓴다(G버퍼에 위치를
    // 따로 저장하지 않는다: 첨부 하나를 통째로 아끼고, 언리얼도 깊이에서 복원한다). 점광은 위치가
    // 있어야 거리 감쇠를 계산할 수 있으므로 이게 없으면 디퍼드에 점광을 넣을 수 없다.
    float4x4 g_InvViewProj;
    // 지난번 이 뷰를 그린 뷰-투영 — 모션 블러가 깊이로 복원한 월드 위치를 다시 투영해 화면 속도를 구한다(motionblur.hlsl). 처음 · 컷 프레임은 g_ViewProj 와 같다.
    float4x4 g_PrevViewProj;
    float4x4 g_World;
    float4   g_KeyLightDirIntensity;
    float4   g_KeyLightColor;
    float4   g_ShadowParams;
    // 방향광 캐스케이드 — x 수, y 칸 배율(1 = 아틀라스 전체, 0.5 = 사분면), z 경계 섞기 폭(캐스케이드 깊이 구간의 비율). 칸 c 의 원점은 사분면 c 다(lighting.hlsli).
    float4 g_ShadowCascadeLayout;
    // 캐스케이드마다 (배율, 이동 x, 이동 y, 깊이 끝) — 기준 NDC(g_LightViewProj) xy 를 그 캐스케이드 NDC 로 옮긴다. 배열이 아닌 까닭은 C++ 가 이름으로 채우기 때문이다(GL 리플렉션은 배열 이름에 [0] 을 붙인다).
    float4 g_ShadowCascade0;
    float4 g_ShadowCascade1;
    float4 g_ShadowCascade2;
    float4 g_ShadowCascade3;
    float4 g_BloomParams;
    float4 g_CameraGrade; // 카메라별 색 보정 — x 노출 배율(2^EV), y 채도, z 대비, w 비네트 세기(CameraPostProcessValues, postgrade.hlsli)
    float4 g_CameraFrame; // 카메라별 화면 틀 — x 레터박스로 남길 가로/세로 비율(0 = 없음), y 지금 뷰의 가로/세로 비율, z 색각 보정 방식(colorvision.hlsli, 렌더 텍스처 뷰는 0)
    float4 g_CameraLens;  // 카메라별 피사계 심도 — x 초점 거리(m), y 착란원 반지름 배율(px, 반지름 = y · |1 - x / 깊이|), z 반지름 상한(px), w 켬 1 · 끔 0(depthoffield.hlsl)
    float4 g_OutlineColor;
    float4 g_OutlineParams;
    float4 g_SourceTexel; // xy = 1 / 원본(SourceColor 역할 입력) 크기, zw = 그 크기. 원본이 없는 패스는 프레임 크기 — 반해상도 첨부를 읽는 패스가 비켜 읽는 거리
    // 하늘 그러데이션(스카이 패스 sky.hlsl, C++ SkyGradient) — 천정(rgb, w 지평선 지수) · 지평선(rgb) · 땅(rgb, w 세기). 씬에 하늘이 없으면 스카이 패스가 돌지 않는다
    float4 g_SkyZenith;
    float4 g_SkyHorizon;
    float4 g_SkyGround;
    // 대기 산란 하늘(스카이 패스 sky.hlsl, C++ SkyAtmosphere) — (레일리 산란 계수 rgb 1/km, 대기 모드 1 · 그러데이션 0) · (미 산란 계수 1/km, 미 규모 높이 km, 미 이방성, 해 복사휘도 배율) ·
    // (행성 반지름 km, 대기 두께 km, 눈 고도 km, 해 원반 각 반지름 rad). 해 방향 · 색은 키 라이트 상수(g_KeyLightDirIntensity · g_KeyLightColor)에서 읽는다.
    float4 g_SkyAtmosphereRayleigh;
    float4 g_SkyAtmosphereMie;
    float4 g_SkyAtmosphereGeometry;
    // 구름 층(sky.hlsl, C++ SkyClouds) — (덮는 비율, 층 고도 km, 노이즈 배율 1/km, 최대 불투명도) · (바람 방향 xz 단위 벡터, 바람 속도 km/s, 경계 부드러움) · (구름 색 rgb, 그리면 1).
    float4 g_SkyCloudShape;
    float4 g_SkyCloudWind;
    float4 g_SkyCloudColor;
    // 높이 · 거리 지수 안개(안개 패스 fog.hlsl, C++ FogSettings) — (안개 색 rgb, 밀도) · (높이 감쇠, 기준 높이, 시작 거리, 최대 불투명도) · (방향광 산란 색 × 세기 rgb, 지수) ·
    // (볼륨 안개 켬 1, 볼륨 거리, 산란 배율, 이방성). 씬에 안개가 없으면 안개 패스가 돌지 않는다. 볼륨 켬은 주 시점에서 컴퓨트가 돈 프레임만 1 이다(FrameRenderer::executeFogPass).
    float4 g_FogColorDensity;
    float4 g_FogHeightParams;
    float4 g_FogInscatter;
    float4 g_FogVolumeParams;
    uint   g_ShadowMapIndex;
    uint   g_GBufferAlbedoIndex;
    uint   g_GBufferNormalIndex;
    uint   g_GBufferMaterialIndex; // G버퍼 머티리얼 칸(금속도 · 거칠기) — 디퍼드 조명이 읽는다(PassTexture::loadGbufferMaterial)
    uint   g_SceneDepthIndex;
    uint   g_SourceColorIndex;
    uint   g_AmbientOcclusionIndex;   // SSAO 결과(AOColor)가 걸려 있으면 유효, 아니면 kInvalidIndex (AO = 1 로 폴백)
    uint   g_BloomBrightIndex;        // 반해상도 블룸 밝은 부분(BloomDownsample 출력)이 걸려 있으면 유효, 아니면 kInvalidIndex (원본에서 직접 구한다)
    uint   g_Light2DCookieAtlasIndex; // 2D 빛 쿠키 아틀라스(Light2DCookieAtlas)가 걸려 있으면 유효, 아니면 kInvalidIndex — 쿠키를 쓴 2D 빛이 있는 프레임에만 걸린다
    uint   g_AutoExposureIndex;       // 자동 노출의 이번 EV(1x1 R32, autoexposure.hlsl)가 걸려 있으면 유효, 아니면 kInvalidIndex(EV 0) — 톤맵하는 패스에만 걸린다(postgrade.hlsli)
    uint   g_SceneColorCopyIndex;     // 반투명 앞에 떠 둔 장면 색 사본(SceneColorCopy 패스 출력)이 걸려 있으면 유효, 아니면 kInvalidIndex — 반투명 패스가 선언했을 때만 걸린다(water.hlsl 의 굴절)
    uint   g_SceneDepthCopyIndex;     // 반투명 앞에 떠 둔 장면 깊이 사본(SceneDepthCopy 패스 출력, R32 실수)이 걸려 있으면 유효, 아니면 kInvalidIndex — 반투명 패스가 선언했을 때만 걸린다(water.hlsl 의 화면 공간 두께)
    uint   g_Flags;
    uint   g_SwInstancesIndex;          // 인스턴스 구조버퍼가 걸려 있으면 유효, kInvalidIndex 면 g_World 폴백 (풀스크린·픽스처)
    uint   g_SwInstanceCount;           // 인스턴스 버퍼 원소 수 — 범위 밖 인덱스를 막는다 (DX12 루트 SRV 는 경계 검사가 없다)
    uint   g_SwVisibleInstanceIDsIndex; // 컬링이 만든 가시 ID 목록이 걸려 있으면 유효, 아니면 kInvalidIndex
    uint   g_SwVisibleSlotCount;        // 그 목록의 칸 수(가시 구간의 합 — LOD 그룹이 없으면 인스턴스 수) — 범위 밖 슬롯을 막는다
    uint   g_SwMorphVerticesIndex;      // GPU 가 변형한 정점 풀이 걸려 있으면 유효, 아니면 kInvalidIndex
    uint   g_SwMorphVertexCount;        // 그 풀의 원소 수 — 범위 밖 인덱스를 막는다
    uint   g_SwLightsIndex;             // 씬 라이트 버퍼가 걸려 있으면 유효, 아니면 kInvalidIndex
    uint   g_SwLightCount;              // 그 버퍼의 빛 원소 수 (0 이면 빛 없음 — 앰비언트만 받는다)
    uint   g_SwLightGridIndex;          // 라이트 그리드 버퍼(g_SwLightGrid, t12)가 걸려 있으면 유효, 아니면 kInvalidIndex
    uint   g_SwLightGridBase;           // 지금 뷰의 라이트 그리드 첫 단어(g_SwLightGrid 안). kInvalidIndex 면 그리드 없이 목록 전체를 돈다(gv_lightCulling 0)
    uint   g_SwLightProbesIndex;        // 라이트 프로브 SH9 버퍼(g_SwLightProbes, t13)가 걸려 있으면 유효, 아니면 kInvalidIndex — 프로브 볼륨 원소는 이것이 유효할 때만 읽는다
    uint   g_SwBatchesIndex;            // 씬 배치 표가 걸려 있으면 유효, 아니면 kInvalidIndex (풀스크린·픽스처)
    uint   g_SwBatchCount;              // 그 표의 원소 수 — 범위 밖 배치 번호를 막는다
    uint   g_SwVertexAnimationIndex;    // 정점 애니메이션(VAT) 표가 걸려 있으면 유효, 아니면 kInvalidIndex
    uint   g_SwVertexAnimationCount;    // 그 표의 단어(uint32) 수 — 범위 밖 번호를 막는다
    float  g_SwVertexAnimationTime;     // VAT 시계(초) — 게임 스레드의 군중 시계와 같은 값이라 인스턴스 위상이 CPU 시각과 맞는다
    float  g_SwMaterialTime;            // 머티리얼 애니메이션 시계(초, 툰 UV 애니메이션) — VAT 시계와 같은 값이지만 VAT 표가 없어도 늘 걸린다
};

// ------------------------------------------------------------------------------
// 2) 드로우별 루트 상수 — **배치마다 달라지는 값만** 여기 둔다.
//    PassCB 는 패스당 한 번 올리는 버퍼라, 배치마다 바뀌는 값을 거기 넣으면 한 패스의 드로우들이 서로를
//    덮어써 전부 마지막 값을 읽는다(GPU 는 제출 뒤에 읽는다). 언리얼이 FMeshDrawCommand 의 느슨한
//    파라미터를 드로우별로 싣는 자리와 같다. DX12 루트 상수 / Vulkan 푸시 상수 / DX11·GL 은 b2 에뮬.
//    컴퓨트 셰이더는 자기 루트 상수 블록을 직접 선언하므로(예: computetexturewrite.hlsl) 여기서는 빼 둔다 —
//    한 셰이더에 블록이 둘이면 재정의다. 자기 블록을 쓰는 그래픽스 셰이더(canvas.hlsl)는 include 앞에 SW_OWN_ROOT_CONSTANTS 를 정의한다.
// ------------------------------------------------------------------------------
#if !defined( SW_STAGE_COMPUTE ) && !defined( SW_OWN_ROOT_CONSTANTS )
SW_ROOT_CONSTANTS_BEGIN
{
    // 이 드로우 그룹의 머티리얼 데이터 버퍼(g_SwMaterials) 원소 수 — SW_MATERIAL 이 클램프한다. 그룹(같은 PSO·머티리얼 버퍼) 안에서
    // 같다. 배치마다 다른 값(인스턴스 시작·모프 풀 시작·정점 풀 시작)은 루트 상수가 아니라 배치 표(g_SwBatches)에 있고, 정점은
    // 인스턴스 슬롯 스트림(VertexInput._instanceSlot)으로 자기 인스턴스를, 인스턴스로 자기 배치를 찾는다 — 그래서 같은 PSO 의
    // 배치들이 루트 상수를 바꾸지 않고 멀티 드로우 하나로 나간다.
    uint g_SwMaterialCount;
};
SW_ROOT_CONSTANTS_END
static uint MaterialBuffer::getDrawCount()
{
    return SW_ROOT( g_SwMaterialCount );
}
#else
// 컴퓨트 · 자기 블록을 쓰는 셰이더에는 이 블록이 없다 — 그래픽스 전용 헬퍼(SW_MATERIAL)가 컴파일만 되게 0 으로 둔다.
static uint MaterialBuffer::getDrawCount()
{
    return 0u;
}
#endif

// 뷰-투영 — 그림자 패스는 빛의 뷰-투영, 나머지는 카메라다. 클립 위치를 만드는 쪽은 패스를 가르지 않고 이것을 부른다.
#if defined( SW_PASS_SHADOW )
static float4x4 View::getViewProjection()
{
    return g_LightViewProj;
}
#else
static float4x4 View::getViewProjection()
{
    return g_ViewProj;
}
#endif

// ------------------------------------------------------------------------------
// 3) GPUScene 인스턴스 버퍼 (per-instance world/material/스프라이트 프레임 · 색). 원소 정의는 instancedata.hlsli 하나다(컴퓨트도 그것을 쓴다).
//    네 백엔드 공통 — 엔진이 리플렉션 슬롯 t4 에 인스턴스 버퍼를 바인딩한다 (레지스트리 이름 "SwInstances"). 읽는 함수는 8 절.
// ------------------------------------------------------------------------------
SW_DECLARE_STRUCTURED_BUFFER( InstanceData, g_SwInstances, SW_SLOT_INSTANCE_SRV );

// ------------------------------------------------------------------------------
// 4) 씬 배치 테이블 — C++ GPUBatchInfo 와 레이아웃 일치(4바이트 열 칸, 40바이트, batchdata.hlsli).
//    배치마다 다른 값은 전부 여기 있다. 정점은 인스턴스(InstanceData::load)의 meshBatchIndex 로 자기 배치를 찾는다.
// ------------------------------------------------------------------------------
// 원소 정의는 batchdata.hlsli 하나다(컬링 컴퓨트도 같은 파일을 쓴다).
#include "batchdata.hlsli"

SW_DECLARE_STRUCTURED_BUFFER( BatchData, g_SwBatches, SW_SLOT_BATCH_SRV );

/**
 * @brief 배치 표의 원소. 표가 안 걸렸거나(풀스크린·픽스처) 범위 밖이면 "인스턴스 0 부터 · 모프 없음 · 정점 풀 없음" 이다.
 * @note 분기는 있지만 early-return 은 없다 — GL 드라이버가 early-return 을 잘못 컴파일한 사연은 MorphPool::computeElement 참고.
 */
static BatchData BatchData::load( uint batchIndex )
{
    BatchData batch;
    batch._instanceBase        = 0u;
    batch._instanceCount       = 0u;
    batch._sortMode            = 0u;
    batch._morphVertexBase     = kInvalidIndex;
    batch._firstVertex         = 0u;
    batch._vertexAnimationBase = kInvalidIndex;
    batch._sectionVertexOffset = 0u;
    batch._visibleBase         = 0u;
    batch._lodLevelCount       = 0u;
    batch._lodScreenSize       = 0.0f;
    const bool bValid          = ( g_SwBatchesIndex != kInvalidIndex ) && ( batchIndex < g_SwBatchCount );
    if ( bValid )
        batch = g_SwBatches[batchIndex];
    return batch;
}

// ------------------------------------------------------------------------------
// 5) 모프 정점 풀 — GPU 가 변형한 정점. C++ `GPUMorphVertex` 와 바이트 배치 일치(float4 셋).
//    메시마다 버퍼를 따로 두지 않고 **풀 하나에 구간을 나눠 쓴다** — 언리얼 GPU Skin Cache 가 캐시
//    버퍼 하나를 할당해 나눠 쓰는 것과 같다. 그래야 드로우 사이에 바인딩이 바뀌지 않는다(이 엔진의 규약).
// ------------------------------------------------------------------------------
// **구조체가 아니라 평면 float4 배열이다.** 정점 하나가 원소 셋 — [3i] 위치, [3i+1] 노멀(w 는 안 쓴다), [3i+2] 탄젠트(xyz, w 부호).
// 주의: `struct { float4 pos; float4 nrm; }` 로 두면 레이아웃이 네 백엔드에서 같아도(ArrayStride 32 · 오프셋 0/16)
// OpenGL 만 **같은 원소의 두 멤버를 다른 원소에서 읽는다**(셰이더 모양에 따라 달라진다 — 드라이버의 SPIR-V 경로가
// 구조체 멤버 로드를 다루는 방식). 평면 배열은 멤버가 없으니 그 자리가 아예 없다.
// 색은 담지 않는다 — 정점 셰이더가 색·UV 는 **입력 스트림에서** 읽고 풀에서는 위치와 노멀만 가져간다.
SW_DECLARE_STRUCTURED_BUFFER( float4, g_SwMorphVertices, SW_SLOT_MORPH_VERTEX_SRV );

/**
 * @brief 이 정점의 풀 원소 번호 — 모프 대상이 아니면 `kInvalidIndex`.
 * @details 폴백이 조건 셋인 이유: (1) 이 배치가 모프 대상이 아니거나, (2) 풀이 안 걸렸거나,
 *          (3) 예산이 모자라 이 메시가 풀에 못 들어갔을 수 있다. 셋 다 "레스트 포즈로 그린다" 로
 *          끝나야 한다 — 언리얼도 스킨 캐시가 차면 일반 경로로 되돌아간다.
 */
// **분기 없는 한 식이어야 한다.** 주의: `if (base == INVALID || …) return INVALID;` 같은 early-return 을 DXC 는
// SPIR-V 의 `OpSwitch(0){ default: … }` 구조로 내는데, OpenGL 드라이버가 그 모양을 잘못 컴파일해 **같은 인보케이션에서
// 같은 UBO 멤버를 두 번 읽어 다른 값**(0 과 -1)을 낸다 — 결과는 정점마다 한 칸 앞 원소를 읽는 것(DX12·DX11·Vulkan 은
// 같은 소스로 멀쩡하다). 회귀는 RenderPassGPUTest.MorphPoolIdentityMatchesRest 가 픽셀로 잡는다.
static uint MorphPool::computeElement( uint batchIndex, uint vertexId )
{
    const BatchData batch = BatchData::load( batchIndex );
    // **API 차이 — 이 메시의 로컬 정점 번호.** 간접 드로우의 startVertex 를 SV_VertexID 가 포함하는지가 백엔드마다 다르다:
    // Vulkan(VertexIndex)·OpenGL(gl_VertexID)은 포함하고, D3D11·D3D12 는 드로우 안의 0 기반 번호다.
    // RHIDeviceTest.SceneDrawVertexIdStartsAtZeroOnlyOnD3D 가 네 백엔드에서 이 기대를 실측한다 — 넷 다 포함한다고 보고
    // 빼기만 하면 DX 에서 정점 풀 첫 메시만 모프된다(RenderPassGPUTest.MorphPoolIdentityMatchesRest 가 DX 에서만 진다).
    // 셰이더 파일에 백엔드 분기는 없다 — 이 헤더가 흡수한다.
#if defined( DX11 ) || defined( DX12 )
    const uint local = vertexId;
#else
    const uint local = vertexId - batch._firstVertex; // 풀 밖 메시는 firstVertex 가 0 이라 그대로다
#endif
    const uint base    = batch._morphVertexBase;
    const uint element = base + local;
    // 잘못된 번호(밑돎으로 커진 local)는 element 가 풀 크기를 넘어 여기서 걸러진다.
    const bool bValid = ( base != kInvalidIndex ) && ( g_SwMorphVerticesIndex != kInvalidIndex ) && ( element < g_SwMorphVertexCount );
    return bValid ? element : kInvalidIndex;
}

/**
 * @brief 이 정점의 위치·노멀 — 모프 대상이면 GPU 가 변형한 값을, 아니면 입력 스트림 값을 돌려준다.
 * @details 위치와 노멀을 **함께** 돌려주는 이유는 둘이 같이 변하기 때문이다. 노멀은 정점 속성이므로 위치만 바꾸면
 *          **레스트 포즈의 노멀**이 남아 변형된 표면이 원래 모양대로 칠해진다.
 */
static void MorphPool::loadVertex( uint batchIndex, uint vertexId, float3 restPosition, float3 restNormal, out float3 outPosition, out float3 outNormal )
{
    outPosition        = restPosition;
    outNormal          = restNormal;
    const uint element = MorphPool::computeElement( batchIndex, vertexId );
    if ( element == kInvalidIndex )
        return;
    outPosition = g_SwMorphVertices[element * SW_MORPH_FLOAT4_PER_VERTEX].xyz;
    outNormal   = g_SwMorphVertices[element * SW_MORPH_FLOAT4_PER_VERTEX + 1u].xyz;
}

/**
 * @brief 이 정점의 탄젠트(xyz 로컬 방향, w 바이탄젠트 부호) — 모프 · 스킨 대상이면 컴퓨트가 돌린 값을, 아니면 입력 스트림 값을 돌려준다.
 * @details 부호는 변형으로 바뀌지 않아 입력 스트림 값을 쓴다. 노멀 맵을 읽는 셰이더만 부른다 — 부르지 않는 패스는 이 로드가 빠진다.
 *          early-return 이 없다 — GL 드라이버 사연은 MorphPool::computeElement 참고.
 */
static float4 MorphPool::loadTangent( uint batchIndex, uint vertexId, float4 restTangent )
{
    const uint element = MorphPool::computeElement( batchIndex, vertexId );
    return ( element == kInvalidIndex ) ? restTangent : float4( g_SwMorphVertices[element * SW_MORPH_FLOAT4_PER_VERTEX + 2u].xyz, restTangent.w );
}

// ------------------------------------------------------------------------------
// 6) 정점 애니메이션(VAT) — 클립 하나를 미리 스키닝해 베이크한 프레임 × 정점 테이블(C++ `MeshVertexAnimation`, `GPUVertexAnimationPool`).
//    먼 군중은 CPU 포즈 · GPU 스키닝 없이 이 테이블을 **인스턴스마다 다른 시각**(g_SwVertexAnimationTime + instance._vertexAnimationPhase)으로 읽는다.
//    표는 32 비트 단어 배열이다. 메시마다 머리 단어 SW_VERTEX_ANIMATION_HEADER_WORDS 개(프레임 수 · 프레임율 · 정점 수 · 반복 · 위치 경계 중심 · 반 크기) 뒤에
//    원소가 프레임 우선 순서로 이어진다. 원소 하나 = 단어 둘(8 B): 단어0 = 위치 x · y(snorm16 둘), 단어1 = 위치 z(snorm16) · 옥타헤드럴 법선 u · v(snorm8 둘).
//    텍스처가 아니라 구조버퍼인 것은 모프 풀과 같은 이유다(정점 셰이더가 SV_VertexID 로 읽는다 — 네 백엔드가 같은 경로).
// ------------------------------------------------------------------------------
SW_DECLARE_STRUCTURED_BUFFER( uint, g_SwVertexAnimation, SW_SLOT_VERTEX_ANIMATION_SRV );
// 헤더 · 원소 단어 수는 SW_VERTEX_ANIMATION_HEADER_WORDS · SW_VERTEX_ANIMATION_WORDS_PER_ELEMENT(bindingslots.hlsli — C++ MeshVertexAnimationElement 와 같은 배치).

/**
 * @brief 옥타헤드럴 좌표 [-1, 1]^2 를 단위 벡터로 푼다. C++ `OctahedralNormal::decodeOctant` 와 같은 식이다(0 은 양수 부호).
 */
static float3 Octahedral::decode( float2 coordinate )
{
    float3       normal = float3( coordinate, 1.0f - abs( coordinate.x ) - abs( coordinate.y ) );
    const float2 signs  = float2( normal.x >= 0.0f ? 1.0f : -1.0f, normal.y >= 0.0f ? 1.0f : -1.0f );
    const float2 folded = ( 1.0f - abs( normal.yx ) ) * signs;
    normal.xy           = ( normal.z < 0.0f ) ? folded : normal.xy;
    return normalize( normal );
}

/**
 * @brief 방향(길이는 상관없다)을 옥타헤드럴 좌표 [-1, 1]^2 로 접는다. C++ `OctahedralNormal::fromNormal` 과 같은 식이다
 *        (`sum = |x| + |y| + |z|`, z < 0 이면 접기, 0 은 양수 부호). 영벡터는 (0, 0) 이라 +z 로 풀린다.
 */
static float2 Octahedral::encode( float3 direction )
{
    const float  sum    = abs( direction.x ) + abs( direction.y ) + abs( direction.z );
    const float2 plane  = sum > 0.0f ? direction.xy / sum : float2( 0.0f, 0.0f );
    const float2 signs  = float2( plane.x >= 0.0f ? 1.0f : -1.0f, plane.y >= 0.0f ? 1.0f : -1.0f );
    const float2 folded = ( 1.0f - abs( plane.yx ) ) * signs;
    return ( direction.z < 0.0f ) ? folded : plane;
}

/** @brief 부호 있는 정수의 최대값(snorm16 = 32767 · snorm8 = 127)으로 나눈 [-1, 1] 실수다(최솟값은 -1 로 자른다, C++ snorm::toFloat 과 같다). */
static float VertexAnimation::toSnorm( int raw, float maxRaw )
{
    return max( (float)raw / maxRaw, -1.0f );
}

/** @brief 원소의 법선(snorm8 옥타헤드럴 둘, 단어1 의 위쪽 16 비트)을 단위 노멀로 푼다. C++ `MeshVertexAnimation::unpackNormal` 과 같은 식이다. */
static float3 VertexAnimation::loadNormal( uint element )
{
    const uint  word1   = g_SwVertexAnimation[element + 1u];
    const float octantU = VertexAnimation::toSnorm( (int)( word1 << 8u ) >> 24, 127.0f );
    const float octantV = VertexAnimation::toSnorm( (int)word1 >> 24, 127.0f );
    return Octahedral::decode( float2( octantU, octantV ) );
}

/**
 * @brief 이 정점의 VAT 두 프레임 원소 번호와 사이 비율. VAT 가 없으면 `outValid` 가 거짓이다.
 * @details 반복이면 프레임 수로 감고(끝 → 처음 보간), 아니면 마지막 프레임에 멈춘다(C++ `MeshVertexAnimation::samplePosition` 과 같은 식).
 *          early-return 이 없다 — GL 드라이버 사연은 MorphPool::computeElement 참고.
 */
void InstanceData::computeVertexAnimationElements( uint vertexId, out bool outValid, out uint outFirstElement, out uint outSecondElement, out float outAlpha )
{
    const BatchData batch = BatchData::load( _meshBatchIndex );
#if defined( DX11 ) || defined( DX12 )
    const uint local = vertexId;
#else
    const uint local = vertexId - batch._firstVertex;
#endif
    // VAT 표는 메시 전체의 정점 순서다 — 구간 배치는 배치 안 번호에 구간의 첫 정점을 더해 메시 안 번호로 돌린다(모프 풀은 시작에 이미 더해 두었다).
    const uint  meshLocal     = local + batch._sectionVertexOffset;
    const uint  base          = batch._vertexAnimationBase;
    const bool  bTable        = ( base != kInvalidIndex ) && ( g_SwVertexAnimationIndex != kInvalidIndex ) && ( base + SW_VERTEX_ANIMATION_HEADER_WORDS <= g_SwVertexAnimationCount );
    const uint  headerFrames  = bTable ? g_SwVertexAnimation[base] : 0u;
    const float headerRate    = bTable ? asfloat( g_SwVertexAnimation[base + 1u] ) : 0.0f;
    const uint  vertexCount   = bTable ? g_SwVertexAnimation[base + 2u] : 0u;
    const bool  bLoop         = bTable && ( g_SwVertexAnimation[base + 3u] != 0u );
    const uint  frameCount    = max( headerFrames, 1u );
    const float frameTotal    = (float)frameCount;
    const float framePosition = max( g_SwVertexAnimationTime + _vertexAnimationPhase, 0.0f ) * headerRate;
    const float wrapped       = bLoop ? ( framePosition - floor( framePosition / frameTotal ) * frameTotal ) : min( framePosition, frameTotal - 1.0f );
    const uint  frame         = min( (uint)wrapped, frameCount - 1u );
    const uint  nextFrame     = bLoop ? ( ( frame + 1u ) % frameCount ) : min( frame + 1u, frameCount - 1u );
    outFirstElement           = base + SW_VERTEX_ANIMATION_HEADER_WORDS + ( frame * vertexCount + meshLocal ) * SW_VERTEX_ANIMATION_WORDS_PER_ELEMENT;
    outSecondElement          = base + SW_VERTEX_ANIMATION_HEADER_WORDS + ( nextFrame * vertexCount + meshLocal ) * SW_VERTEX_ANIMATION_WORDS_PER_ELEMENT;
    outAlpha                  = wrapped - (float)frame;
    outValid                  = bTable && ( headerFrames > 0u ) && ( meshLocal < vertexCount ) && ( outFirstElement + 1u < g_SwVertexAnimationCount ) &&
               ( outSecondElement + 1u < g_SwVertexAnimationCount );
}

/**
 * @brief VAT 위치 — 깊이 프리패스와 기본 패스가 비트까지 같아야 해 이 함수 하나로 만든다(precise). 정규화한 두 원소를 보간한 뒤 머리의 경계(중심 + 반 크기)로 푼다.
 * @param headerBase 이 메시의 머리 단어 번호(BatchData::_vertexAnimationBase).
 */
static float3 VertexAnimation::computePosition( uint headerBase, uint firstElement, uint secondElement, float alpha )
{
    const uint     firstWord0       = g_SwVertexAnimation[firstElement];
    const uint     firstWord1       = g_SwVertexAnimation[firstElement + 1u];
    const uint     secondWord0      = g_SwVertexAnimation[secondElement];
    const uint     secondWord1      = g_SwVertexAnimation[secondElement + 1u];
    const float3   firstNormalized  = float3( VertexAnimation::toSnorm( (int)( firstWord0 << 16u ) >> 16, 32767.0f ), VertexAnimation::toSnorm( (int)firstWord0 >> 16, 32767.0f ),
                                              VertexAnimation::toSnorm( (int)( firstWord1 << 16u ) >> 16, 32767.0f ) );
    const float3   secondNormalized = float3( VertexAnimation::toSnorm( (int)( secondWord0 << 16u ) >> 16, 32767.0f ), VertexAnimation::toSnorm( (int)secondWord0 >> 16, 32767.0f ),
                                              VertexAnimation::toSnorm( (int)( secondWord1 << 16u ) >> 16, 32767.0f ) );
    const float3   center           = float3( asfloat( g_SwVertexAnimation[headerBase + 4u] ), asfloat( g_SwVertexAnimation[headerBase + 5u] ), asfloat( g_SwVertexAnimation[headerBase + 6u] ) );
    const float3   halfExtent       = float3( asfloat( g_SwVertexAnimation[headerBase + 7u] ), asfloat( g_SwVertexAnimation[headerBase + 8u] ), asfloat( g_SwVertexAnimation[headerBase + 9u] ) );
    precise float3 position         = center + halfExtent * lerp( firstNormalized, secondNormalized, alpha );
    return position;
}

/**
 * @brief 이 정점의 위치 · 노멀 — VAT 가 걸린 배치면 표에서, 아니면 모프 · 스킨 결과(MorphPool::loadVertex)에서, 그것도 아니면 입력 스트림에서.
 * @details 정점을 읽는 **모든** 패스(본 · G버퍼 · 그림자 · 깊이 프리패스)가 머티리얼 셰이더 뼈대(materialshadermain.hlsli)를 거쳐 이것 하나로 읽는다 —
 *          위치만 읽는 짧은 형태를 따로 두면 패스마다 위치가 비트 단위로 갈라질 수 있어 없앴다.
 */
void InstanceData::loadAnimatedVertex( uint vertexId, float3 restPosition, float3 restNormal, out float3 outPosition, out float3 outNormal )
{
    MorphPool::loadVertex( _meshBatchIndex, vertexId, restPosition, restNormal, outPosition, outNormal );
    bool  bAnimated;
    uint  firstElement;
    uint  secondElement;
    float alpha;
    computeVertexAnimationElements( vertexId, bAnimated, firstElement, secondElement, alpha );
    if ( bAnimated )
    {
        const float3 firstNormal  = VertexAnimation::loadNormal( firstElement );
        const float3 secondNormal = VertexAnimation::loadNormal( secondElement );
        outPosition               = VertexAnimation::computePosition( BatchData::load( _meshBatchIndex )._vertexAnimationBase, firstElement, secondElement, alpha );
        outNormal                 = normalize( lerp( firstNormal, secondNormal, alpha ) );
    }
}

// ------------------------------------------------------------------------------
// 7) 위치 · 노멀 변환 — 정점 위치를 만드는 모든 지오메트리 패스가 같은 식을 쓰게 한곳에 둔다.
// ------------------------------------------------------------------------------
/**
 * @brief 로컬 위치를 월드로 옮긴다. 정점 위치를 만드는 **모든** 지오메트리 패스가 이것과 Transform::computeClipPosition 를 쓴다.
 * @details 깊이 프리패스와 기본 패스가 같은 정점에서 **비트까지 같은 깊이**를 내야 기본 패스의 LessEqual 이 통과한다(언리얼은 depth-only 패스와
 *          base pass 가 같은 정점 팩토리 코드를 쓴다). 셰이더마다 같은 식을 따로 적으면 컴파일러가 곱셈 · 덧셈을 다르게 묶을 수 있어(FMA)
 *          `precise` 로 묶음을 고정하고 한 함수로 모은다.
 */
static float4 Transform::computeWorldPosition( float3 localPosition, float4x4 world )
{
    precise float4 worldPosition = mul( float4( localPosition, 1.0f ), world );
    return worldPosition;
}

/** @brief 월드 위치를 클립 공간으로 옮긴다(카메라는 g_ViewProj, 그림자는 g_LightViewProj). Transform::computeWorldPosition 와 같은 이유로 precise 다. */
static float4 Transform::computeClipPosition( float4 worldPosition, float4x4 viewProj )
{
    precise float4 clipPosition = mul( worldPosition, viewProj );
    return clipPosition;
}

/**
 * @brief 로컬 노멀을 월드로 옮겨 정규화한다. 노멀을 내보내는 **모든** 지오메트리 패스(forwardlit · gbuffer · gbuffernormal)가 이것을 쓴다.
 * @details 노멀은 위치처럼 월드 행렬로 옮기면 안 된다. 행 벡터 규약(`p' = p · M`)에서 접선은 `t · M` 으로 가므로, 수직을 지키는 노멀은
 *          `n · (M⁻¹)ᵀ` 다. 균등 스케일 · 회전뿐이면 두 식의 방향이 같아 드러나지 않지만, 비균등 스케일(스케일한 부모 아래 회전한 자식도)에서는
 *          노멀이 늘어난 축 **쪽으로** 기울어 조명이 틀린다(`mul( float4( n, 0 ), world )` 가 그렇다).
 *          역행렬은 필요 없다. 3x3 의 `(M⁻¹)ᵀ = cof(M) / det(M)` 이고 여인수 행렬의 행은 월드 행 셋의 외적 셋(r1×r2, r2×r0, r0×r1)이다.
 *          정규화하므로 행렬식은 **부호만** 남긴다 — 거울 변환(det < 0)에서 노멀이 안쪽을 보지 않게 한다. 한 축이 0 인 납작한 물체도
 *          보이는 면의 노멀이 남는다(월드 행렬을 곱하면 그 면의 노멀이 0 이 되어 NaN 이다). 언리얼의 로컬 정점 팩토리도 노멀을
 *          역스케일 · 행렬식 부호로 옮긴다. 회귀는 RenderPassGPUTest.NormalsStayPerpendicularUnderNonUniformScale 가 G버퍼 노멀로 잡는다.
 */
static float3 Transform::computeWorldNormal( float3 localNormal, float4x4 world )
{
    const float3 row0        = world[0].xyz;
    const float3 row1        = world[1].xyz;
    const float3 row2        = world[2].xyz;
    const float3 cofactor0   = cross( row1, row2 );
    const float3 cofactor1   = cross( row2, row0 );
    const float3 cofactor2   = cross( row0, row1 );
    const float  handedness  = ( dot( row0, cofactor0 ) < 0.0f ) ? -1.0f : 1.0f;
    const float3 worldNormal = localNormal.x * cofactor0 + localNormal.y * cofactor1 + localNormal.z * cofactor2;
    return normalize( worldNormal * handedness );
}

// ------------------------------------------------------------------------------
// 8) 인스턴스 읽기 — 인스턴스 슬롯을 실제 인스턴스 번호로 바꾸고(GPU 컬링의 가시 목록) 3 절의 버퍼에서 읽는다.
// ------------------------------------------------------------------------------
// GPU 컬링이 압축해 넣은 가시 인스턴스 번호 목록. 컬링이 꺼져 있거나 못 만들면 바인딩되지 않는다.
SW_DECLARE_STRUCTURED_BUFFER( uint, g_SwVisibleInstanceIDs, SW_SLOT_VISIBLE_INSTANCE_SRV );

/**
 * @brief 인스턴스 슬롯(전역 자리)을 **실제 인스턴스 번호**로 바꾼다.
 * @details GPU 컬링이 켜져 있으면 드로우가 그리는 것은 "배치의 n 번째 인스턴스"가 아니라 "배치에서
 *          살아남은 n 번째 인스턴스"다. 그 대응이 g_SwVisibleInstanceIDs 에 들어 있다.
 *          목록이 없으면(컬링 없음) 슬롯이 곧 인스턴스 번호다.
 */
static uint InstanceData::resolveEntry( uint instanceSlot )
{
    // 슬롯은 입력 어셈블러가 인스턴스 슬롯 스트림에서 준 전역 자리다(간접 인자의 startInstance + 인스턴스 서수).
    const uint slot = instanceSlot;
    if ( g_SwVisibleInstanceIDsIndex != kInvalidIndex && slot < g_SwVisibleSlotCount )
        return g_SwVisibleInstanceIDs[slot];
    return slot;
}

static uint InstanceData::resolveId( uint instanceSlot )
{
    return InstanceData::resolveEntry( instanceSlot ) & kVisibleEntryIdMask;
}

/**
 * @brief 이 드로우의 인스턴스 데이터 — 씬 메시는 전부 인스턴스 버퍼에서 읽는다(경로 하나).
 * @details 인스턴스 버퍼가 안 걸린 드로우(풀스크린·픽스처)나 범위 밖 인덱스는 PassCB 의 g_World 와 머티리얼 원소 0 으로 만든다.
 *          범위 검사는 백엔드마다 다른 OOB 결과(DX11/GL 0, Vulkan robustBufferAccess 0, DX12 루트 SRV 는 정의되지 않음)를
 *          하나로 맞추기 위한 것이다.
 */
static InstanceData InstanceData::load( uint instanceSlot )
{
    const uint entry   = InstanceData::resolveEntry( instanceSlot );
    const uint element = entry & kVisibleEntryIdMask;
    if ( g_SwInstancesIndex != kInvalidIndex && element < g_SwInstanceCount )
    {
        InstanceData instance = g_SwInstances[element];
        // LOD 크로스페이드 중인 항목은 인스턴스의 등장 비율 대신 상보 디더 값을 받는다(8-1 절). 코드가 0 이면 그대로다.
        instance._appearance = Appearance::computeLodFadeAppearance( instance._appearance, entry >> kVisibleEntryFadeShift );
        return instance;
    }
    InstanceData instance;
    instance._world                = g_World;
    instance._boundsCenter         = float3( 0, 0, 0 );
    instance._boundsRadius         = 0;
    instance._meshBatchIndex       = 0;
    instance._materialIndex        = 0;
    instance._blendMode            = 0;
    instance._spinSeed             = 0;
    instance._uvStart              = 0u;          // (0, 0)
    instance._uvEnd                = 0xFFFFFFFFu; // (1, 1) — 텍스처 전체
    instance._tint                 = 0xFFFFFFFFu; // 흰색 불투명
    instance._pixelSnap            = 0.0f;        // 스냅 없음
    instance._vertexAnimationPhase = 0.0f;
    instance._highlight            = 0u;   // 강조 없음
    instance._appearance           = 1.0f; // 다 보인다
    instance._reserved1            = 0u;
    return instance;
}

static float4x4 InstanceData::loadWorld( uint instanceSlot )
{
    return InstanceData::load( instanceSlot )._world;
}

// ------------------------------------------------------------------------------
// 8-1) 등장 디더 — 로드가 끝난 메시 · 텔레포트 · 사라지기가 한 프레임에 튀지 않고 서서히 나타나게 한다(언리얼 Dithered LOD Transition 과 같은 기법).
//
//      등장 중인 인스턴스(appearance < 1)만 디더 변형(`SW_APPEARANCE_DITHER`)으로 그린다 — 다 보이는 인스턴스는 그 변형이 아니라서 픽셀 버리기가 없고
//      조기 깊이 검사를 잃지 않는다(배치 키가 퍼뮤테이션 해시라 등장 중인 것만 따로 배치된다, GPUSceneBuilder). 문턱은 화면 픽셀의 4x4 Bayer 행렬이라
//      깊이 프리패스 · 그림자 · 기본 패스가 **같은 픽셀**을 버린다 — 프리패스가 쓴 깊이와 기본 패스가 남긴 픽셀이 어긋나 구멍이 나지 않는다.
//      TAA 가 꺼진 품질 단계에서도 그대로 디더다: 등장 시간이 짧고(MeshComponent `_appearSeconds`), 알파 페이드는 불투명 정렬 · 깊이 쓰기를 깨기 때문이다.
//
//      쓰는 셰이더: VS 출력 구조체에 `SW_APPEARANCE_VARYING`, VS 끝에 `SW_APPEARANCE_WRITE( output, instance )`, PS 처음에 `SW_APPEARANCE_CLIP( input )`.
//      변형이 아니면 셋 다 비어 바이트코드가 그대로다.
// ------------------------------------------------------------------------------

/** @brief 화면 픽셀 @p pixelPosition(SV_POSITION.xy)의 디더 문턱(0 ~ 1)입니다. 4x4 Bayer 행렬 — 16 단계가 화면에 고르게 흩어진다. */
static float Appearance::computeDitherThreshold( float2 pixelPosition )
{
    static const float arrBayer4x4[16] = { 0.0f, 8.0f, 2.0f, 10.0f, 12.0f, 4.0f, 14.0f, 6.0f, 3.0f, 11.0f, 1.0f, 9.0f, 15.0f, 7.0f, 13.0f, 5.0f };
    const uint2        cell            = uint2( pixelPosition ) & 3u;
    return ( arrBayer4x4[cell.y * 4u + cell.x] + 0.5f ) / 16.0f;
}

/**
 * @brief 등장 비율 @p appearance 보다 문턱이 높은 픽셀을 버린다(0 이면 모두, 1 이면 하나도 버리지 않는다).
 * @details **음수는 상보 패턴이다.** `[-1, 0)` 이면 문턱이 `appearance + 1` 보다 낮은 픽셀을 버린다 — 같은 값 p 에서 `appearance = p` 와 `appearance = p - 1` 은 겹치지도
 *          비지도 않고 화면을 나눈다. LOD 크로스페이드가 새 단계에 p, 옛 단계에 p - 1 을 준다(`Appearance::computeLodFadeAppearance`).
 */
static void Appearance::clipDither( float appearance, float2 pixelPosition )
{
    const float threshold = Appearance::computeDitherThreshold( pixelPosition );
    clip( appearance >= 0.0f ? appearance - threshold : threshold - ( appearance + 1.0f ) );
}

/**
 * @brief 가시 목록 항목의 크로스페이드 코드(위쪽 5비트: 직전 단계 비트 | 진행 L = 1 ~ 15)를 이 인스턴스의 등장 비율로 바꿉니다. 코드 0 이면 인스턴스 값 그대로입니다.
 * @details 새 단계는 `L / 16`, 직전 단계는 `L / 16 - 1`(상보). 4x4 Bayer 문턱이 16 단계라 L 이 곧 남는 픽셀 수(16 칸 중)다.
 */
static float Appearance::computeLodFadeAppearance( float instanceAppearance, uint fadeCode )
{
    const float progress = float( fadeCode & 15u ) / 16.0f;
    const float faded    = ( fadeCode & 16u ) != 0u ? progress - 1.0f : progress;
    return fadeCode == 0u ? instanceAppearance : faded;
}

// 매크로로 남긴 이유: `SW_APPEARANCE_VARYING` 은 구조체 **필드 선언**이라 함수가 될 수 없고, 나머지 둘은 그 필드가 있을 때만 존재하는 구조체 멤버를 읽는다(퍼뮤테이션이 필드 유무를 가른다).
#if defined( SW_APPEARANCE_DITHER )
    #define SW_APPEARANCE_VARYING                   nointerpolation float _appearance : TEXCOORD15;
    #define SW_APPEARANCE_WRITE( output, instance ) ( output )._appearance = ( instance )._appearance;
    #define SW_APPEARANCE_CLIP( input )             Appearance::clipDither( ( input )._appearance, ( input )._position.xy );
#else
    #define SW_APPEARANCE_VARYING
    #define SW_APPEARANCE_WRITE( output, instance )
    #define SW_APPEARANCE_CLIP( input )
#endif

// ------------------------------------------------------------------------------
// 9) 머티리얼 데이터 — 셰이더가 구조체를 선언하면 엔진이 그 타입의 머티리얼들을 StructuredBuffer 원소로 쌓는다.
//      SW_MATERIAL_BEGIN { float4 _color; uint _albedoMap; } SW_MATERIAL_END
//      ... SW_MATERIAL( instance._materialIndex )._color
//      리플렉션 이름 g_SwMaterials(t5) ↔ 레지스트리 "SwMaterials" (배치마다 그 셰이더 타입의 버퍼를 등록한다).
//      원소 레이아웃은 네 백엔드가 같다 — SPIR-V 도 DX 패킹(-fvk-use-dx-layout, ShaderCompiler.cpp)으로 쿠킹하고
//      ShaderBindingValidatorTest.ReflectionNamesAreUniformAcrossBackends 가 쿠킹된 바이너리로 확인한다.
//      인덱스는 g_SwMaterialCount 로 클램프한다 — 잘못된 인덱스가 백엔드마다 다른 OOB 결과를 내지 않도록.
//      **텍스처 인덱스 필드는 uint 이고 이름이 Map 으로 끝난다**(albedoMap · normalMap · splatMap). 리플렉션은 그 필드도 그냥 uint 라
//      이름이 표식이다(MaterialUtil::isTextureIndexMember). 머티리얼 없는 배치의 폴백 원소는 그 필드를 kInvalidIndex 로 채운다 — 0 은
//      Vulkan 의 첫 실제 텍스처 · DX11 · GL 의 서수 0 이다. 텍스처가 아닌 uint(pointFilter)는 Map 으로 끝나는 이름을 쓰지 않는다.
//      **머티리얼은 픽셀 단계에서만 읽는다.** 정점 · 픽셀 두 단계가 g_SwMaterials 를 함께 읽으면 GL(ARB_gl_spirv) 드라이버가 구조 버퍼의
//      이름 없는 멤버를 단계마다 다른 SPIR-V id 로 이름 짓고("_struct14_member0" · "_struct19_member0") 링크를 거절한다 — 그 배치는 패스
//      셰이더로 물러나 그려진다. 정점에서 필요한 값(sprite2d 의 uvRect)은 픽셀에서 적용한다(아핀이면 결과가 같다).
//      RenderPassGPUTest.SpriteDrawsWithTheSpriteShader 가 GL 에서 확인한다.
// ------------------------------------------------------------------------------
static uint MaterialBuffer::clampIndex( uint index )
{
    const uint materialCount = MaterialBuffer::getDrawCount();
    return ( materialCount == 0 ) ? 0 : min( index, materialCount - 1 );
}
#define SW_MATERIAL_BEGIN struct MaterialData
#define SW_MATERIAL_END \
    ;                   \
    SW_DECLARE_STRUCTURED_BUFFER( MaterialData, g_SwMaterials, SW_SLOT_MATERIAL_BUFFER );
// 매크로로 남긴 이유: `MaterialData` 는 셰이더마다 선언이 다른 구조체라 함수의 반환형을 적을 수 없다(HLSL 에 템플릿이 없다). 원소 참조를 그대로 돌려주는 자리가 매크로뿐이다.
#define SW_MATERIAL( index ) g_SwMaterials[MaterialBuffer::clampIndex( index )]

// ------------------------------------------------------------------------------
// 10) 샘플러 세트 — 네이티브 bindless 백엔드(DX12 · Vulkan)만 여기서 선언한다. 에뮬 백엔드(DX11 · OpenGL)는 슬롯 결합 샘플러(g_SwSlot#Sampler, s#)라
//     여기서 s0 를 또 선언하면 같은 레지스터를 두 샘플러가 나눠 갖는다(FXC 는 안 쓰면 조용히 버려 드러나지 않았다). DX11 의 정적 세트는 11-2 절.
//     DX12 는 루트 시그니처 정적 샘플러 s0..s7, Vulkan 은 set SW_VK_TEXTURE_SET 의 immutable sampler 배열(binding 1) + 비교 샘플러(binding 2).
//     번호는 bindingslots.hlsli 4 절의 SW_SAMPLER_* 다 — 언리얼의 정적 샘플러 세트에 해당한다.
// ------------------------------------------------------------------------------
#if defined( SW_NATIVE_BINDLESS )
    #if defined( __spirv__ )
// Vulkan: set 1 binding 1 의 immutable sampler 배열 — 배열 그대로 인덱싱한다.
[[vk::binding( SW_VK_SAMPLER_BINDING, SW_VK_TEXTURE_SET )]] SamplerState                  g_SwSamplers[SW_STATIC_SAMPLER_ARRAY_COUNT] : register( s0 );
[[vk::binding( SW_VK_SHADOW_SAMPLER_BINDING, SW_VK_TEXTURE_SET )]] SamplerComparisonState g_SwSamplerShadowCmp : register( SW_CAT( s, SW_SAMPLER_SHADOW_CMP ) );
        // 매크로로 남긴 이유: 백엔드마다 선언 모양이 다르다(Vulkan 은 배열 인덱싱, DX12 는 개별 변수 + 리터럴 분기). 함수로 감싸 `SamplerState` 를 돌려주는 길은
        // 컴파일 · GL 결합 샘플러 경로를 확인할 수 없어 보류했고, 호출처는 `Bindless::sampleSlotWith` 와 fsr1 몇 곳뿐이다.
        #define SW_SAMPLER_STATE( samplerId ) g_SwSamplers[samplerId]
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
/** @brief 인덱스의 텍스처를 샘플러 세트의 samplerId(SW_SAMPLER_*, 비교 샘플러 제외)로 샘플링합니다. */
static float4 Bindless::sampleIndexWith( uint index, uint samplerId, float2 uv )
{
    if ( index == kInvalidIndex )
        return float4( 0, 0, 0, 1 );
    const uint slot = Bindless::textureSlot( index );
    #if defined( __spirv__ )
    return g_SwBindlessTex2D[NonUniformResourceIndex( slot )].Sample( SW_SAMPLER_STATE( samplerId ), uv );
    #else
    switch ( samplerId )
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
 * @note 샘플러 3 번은 `SW_SAMPLER_POINT_CLAMP` 다 (bindingslots.hlsli 4 의 표).
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
        // 인덱스는 registerBindlessTextureUav 가 준다. 컴퓨트에서만 선언한다 — 그래픽스 스테이지에 UAV 배열을 두면
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

#else // !SW_NATIVE_BINDLESS

    // ---- 11-2) 슬롯 에뮬 (DX11 · OpenGL) ------------------------------------------
    #if !defined( SW_STAGE_COMPUTE )
// 에뮬 백엔드의 샘플 슬롯은 그래픽스 스테이지 전용이다 — GL 은 텍스처 유닛과 이미지 유닛이 SPIR-V 에서 같은 binding 번호를
// 쓰므로 컴퓨트에서 둘을 함께 선언하면 DXC 가 RW 텍스처를 결합 샘플러와 합쳐 버린다(엔진도 컴퓨트에는 t0..t3 을 걸지 않는다).
// DX11 / OpenGL : 엔진이 t0..t3 에 SRV 바인딩. 값 비교로 어느 논리 텍스처인지 판별.
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwSlot0, g_SwSlot0Sampler, SW_SLOT_ENGINE_TEX0 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwSlot1, g_SwSlot1Sampler, SW_SLOT_ENGINE_TEX1 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwSlot2, g_SwSlot2Sampler, SW_SLOT_ENGINE_TEX2 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwSlot3, g_SwSlot3Sampler, SW_SLOT_ENGINE_TEX3 );
        // G버퍼 머티리얼 칸 t15 — 조명 패스의 다섯째 엔진 텍스처라 t0..t3 밖이다(bindingslots.hlsli SW_SLOT_ENGINE_GBUFFER_MATERIAL). DX11 은 s15 가 정적 세트라 짝 샘플러가 없다.
        #if defined( DX11 )
SW_DECLARE_TEXTURE2D( g_SwSlotGBufferMaterial, SW_SLOT_ENGINE_GBUFFER_MATERIAL );
        #else
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwSlotGBufferMaterial, g_SwSlotGBufferMaterialSampler, SW_SLOT_ENGINE_GBUFFER_MATERIAL );
        #endif

        // 머티리얼 텍스처 고정 슬롯 t16..t31(16 칸) — 번호는 bindingslots.hlsli 가 정한다 (C++ shaderslot::kMaterialTexture0 와 같은 파일).
        // DX11 은 샘플러 슬롯이 16 개뿐이라 머티리얼 슬롯에 짝 샘플러를 두지 않는다 — 정적 세트의 머티리얼 샘플러(SW_MATERIAL_TEXTURE_SAMPLER)로 읽는다.
        // GL 은 결합 샘플러뿐이라 슬롯마다 짝을 두고, 엔진이 그 유닛에 머티리얼 샘플러를 건다(OpenGLRHIDevice 초기화).
        #if defined( DX11 )
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

        #if defined( DX11 )
// DX11 정적 샘플러 세트 s9..s15 (bindingslots.hlsli 4) — 엔진이 디바이스 초기화 때 건다(D3D11RHIDevice::bindStaticSamplers).
// DX12 와 같은 표(SW_SAMPLER_*)라 셰이더가 고른 samplerId 가 DX11 에서도 존중된다.
SamplerState g_SwSampler0 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER0 ) );
SamplerState g_SwSampler1 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER1 ) );
SamplerState g_SwSampler2 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER2 ) );
SamplerState g_SwSampler3 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER3 ) );
SamplerState g_SwSampler4 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER4 ) );
SamplerState g_SwSampler5 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER5 ) );
SamplerState g_SwSampler6 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER6 ) );
/** @brief 슬롯 텍스처를 정적 샘플러 세트의 samplerId 로 샘플링합니다 — SM5.0 은 샘플러 배열 동적 인덱싱이 없어 리터럴 분기. */
static float4 Bindless::sampleSlotWith( Texture2D slotTexture, uint samplerId, float2 uv )
{
    switch ( samplerId )
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

/** @brief G버퍼 머티리얼 칸의 슬롯(t15)을 엔진 텍스처 샘플러로 읽습니다 — DX11 은 짝 샘플러가 없어 정적 세트에서 고른다. */
static float4 PassTexture::sampleGbufferMaterialSlot( float2 uv )
{
        #if defined( DX11 )
    return Bindless::sampleSlotWith( g_SwSlotGBufferMaterial, SW_ENGINE_TEXTURE_SAMPLER, uv );
        #else
    return g_SwSlotGBufferMaterial.Sample( g_SwSlotGBufferMaterialSampler, uv );
        #endif
}

/**
 * @brief 에뮬 백엔드(DX11 · GL)의 슬롯 표에 없는 엔진 텍스처 인덱스를 읽었을 때 내는 오류 색(자홍)입니다.
 * @details 표에 없는 인덱스를 슬롯 0 으로 읽지 않는다 — 그러면 그림자 · 원본 색이 엉뚱한 자리에 조용히 나온다. 이 색이 보이면
 *          그 인덱스를 C++ 표(`shaderslot::emulated::kEngineTextureBindings` — ShaderBindingSlots.h)에 더해야 한다. 셰이더에는 멈출 길이 없어 눈에 띄는 값으로 낸다.
 */
static const float4 kUnmappedEngineTextureColor = float4( 1, 0, 1, 1 );
static const uint   kUnmappedEngineTextureSlot  = 0xFFFFFFFFu;

        // 슬롯 번호는 셰이더 컴파일러가 C++ 표에서 내는 `SW_EMU_SLOT_<이름>` 매크로다(ShaderCompiler::compileHlsl) — 여기에 손으로 맞춘 번호가 없다.
        // 매크로가 없으면 셰이더를 컴파일하는 경로가 표를 넣지 않은 것이라 조용히 0 으로 읽지 않고 멈춘다.
        #if !defined( SW_EMU_SLOT_ShadowMap ) || !defined( SW_EMU_SLOT_SourceColor ) || !defined( SW_EMU_SLOT_GBufferAlbedo ) || !defined( SW_EMU_SLOT_AmbientOcclusion ) ||      \
            !defined( SW_EMU_SLOT_Light2DCookieAtlas ) || !defined( SW_EMU_SLOT_GBufferNormal ) || !defined( SW_EMU_SLOT_BloomBright ) || !defined( SW_EMU_SLOT_AutoExposure ) || \
            !defined( SW_EMU_SLOT_SceneDepth ) || !defined( SW_EMU_SLOT_GBufferMaterial ) || !defined( SW_EMU_SLOT_SceneColorCopy ) || !defined( SW_EMU_SLOT_SceneDepthCopy )
            #error "SW_EMU_SLOT_* is missing - ShaderCompiler::compileHlsl injects it from shaderslot::emulated::kEngineTextureBindings"
        #endif

/** @brief 엔진 텍스처 인덱스가 걸리는 슬롯 번호(t0..t3 · G버퍼 머티리얼)입니다. 표에 없는 인덱스면 `kUnmappedEngineTextureSlot` 입니다. */
static uint Bindless::emulatedSlot( uint index )
{
    if ( index == g_ShadowMapIndex )
        return SW_EMU_SLOT_ShadowMap;
    if ( index == g_SourceColorIndex )
        return SW_EMU_SLOT_SourceColor;
    if ( index == g_GBufferAlbedoIndex )
        return SW_EMU_SLOT_GBufferAlbedo;
    if ( index == g_AmbientOcclusionIndex )
        return SW_EMU_SLOT_AmbientOcclusion;
    if ( index == g_Light2DCookieAtlasIndex )
        return SW_EMU_SLOT_Light2DCookieAtlas;
    if ( index == g_GBufferNormalIndex )
        return SW_EMU_SLOT_GBufferNormal;
    if ( index == g_BloomBrightIndex )
        return SW_EMU_SLOT_BloomBright;
    if ( index == g_AutoExposureIndex )
        return SW_EMU_SLOT_AutoExposure;
    if ( index == g_SceneColorCopyIndex )
        return SW_EMU_SLOT_SceneColorCopy;
    if ( index == g_SceneDepthCopyIndex )
        return SW_EMU_SLOT_SceneDepthCopy;
    if ( index == g_SceneDepthIndex )
        return SW_EMU_SLOT_SceneDepth;
    if ( index == g_GBufferMaterialIndex )
        return SW_EMU_SLOT_GBufferMaterial;
    return kUnmappedEngineTextureSlot;
}

static float4 Bindless::sampleIndex( uint index, float2 uv )
{
    if ( index == kInvalidIndex )
        return float4( 0, 0, 0, 1 );
    // FrameRenderer 가 같은 C++ 표(commitBindlessTextureBindings)로 t0..t3 에, G버퍼 머티리얼을 t15 에 바인딩한다.
    const uint slot = Bindless::emulatedSlot( index );
    if ( slot == SW_SLOT_ENGINE_TEX0 )
        return g_SwSlot0.Sample( g_SwSlot0Sampler, uv );
    if ( slot == SW_SLOT_ENGINE_TEX1 )
        return g_SwSlot1.Sample( g_SwSlot1Sampler, uv );
    if ( slot == SW_SLOT_ENGINE_TEX2 )
        return g_SwSlot2.Sample( g_SwSlot2Sampler, uv );
    if ( slot == SW_SLOT_ENGINE_TEX3 )
        return g_SwSlot3.Sample( g_SwSlot3Sampler, uv );
    if ( slot == SW_SLOT_ENGINE_GBUFFER_MATERIAL )
        return PassTexture::sampleGbufferMaterialSlot( uv );
    return kUnmappedEngineTextureColor;
}
/**
 * @brief 씬 깊이의 2x2 이웃을 **텍스처 연산 한 번**으로 읽습니다 (`GatherRed`).
 * @details 에뮬 백엔드는 깊이가 늘 슬롯 3 이다(위 표와 같은 순서) — 인덱스로 되짚을 필요가 없다.
 *          샘플러는 슬롯에 걸린 것을 그대로 쓴다. 게더는 필터 모드를 보지 않는다.
 */
static float4 PassTexture::gatherDepthRed( float2 uv )
{
    if ( g_SceneDepthIndex == kInvalidIndex )
        return float4( 1, 1, 1, 1 );
    return g_SwSlot3.GatherRed( g_SwSlot3Sampler, uv );
}
        #if defined( DX11 )
/** @brief DX11: 텍스처는 슬롯 멀티플렉싱(Bindless::sampleIndex 와 같은 표), 샘플러는 정적 세트에서 samplerId 로 고른다. */
static float4 Bindless::sampleIndexWith( uint index, uint samplerId, float2 uv )
{
    if ( index == kInvalidIndex )
        return float4( 0, 0, 0, 1 );
    const uint slot = Bindless::emulatedSlot( index );
    if ( slot == SW_SLOT_ENGINE_TEX0 )
        return Bindless::sampleSlotWith( g_SwSlot0, samplerId, uv );
    if ( slot == SW_SLOT_ENGINE_TEX1 )
        return Bindless::sampleSlotWith( g_SwSlot1, samplerId, uv );
    if ( slot == SW_SLOT_ENGINE_TEX2 )
        return Bindless::sampleSlotWith( g_SwSlot2, samplerId, uv );
    if ( slot == SW_SLOT_ENGINE_TEX3 )
        return Bindless::sampleSlotWith( g_SwSlot3, samplerId, uv );
    if ( slot == SW_SLOT_ENGINE_GBUFFER_MATERIAL )
        return Bindless::sampleSlotWith( g_SwSlotGBufferMaterial, samplerId, uv );
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
 * @brief OpenGL 은 결합 샘플러뿐(ARB_gl_spirv 는 분리 샘플러를 못 쓴다)이라 슬롯의 샘플러 상태는 엔진이 정한다(t0..t3 는 선형 · 클램프).
 * @details 점 샘플(`SW_SAMPLER_POINT_CLAMP`)을 달라는 호출은 샘플러를 거치지 않고 텍셀을 직접 읽는다 — 안 그러면 정수 업스케일 뷰(`SW_PASS_FLAG_POINT_UPSCALE`)의
 *          Present 가 GL 에서만 선형으로 섞는다. 나머지 samplerId 는 슬롯의 샘플러를 그대로 쓴다.
 */
static float4 Bindless::sampleIndexWith( uint index, uint samplerId, float2 uv )
{
    if ( samplerId != SW_SAMPLER_POINT_CLAMP )
        return Bindless::sampleIndex( index, uv );
    if ( index == kInvalidIndex )
        return float4( 0, 0, 0, 1 );
    const uint slot = Bindless::emulatedSlot( index );
    if ( slot == SW_SLOT_ENGINE_TEX0 )
        return Bindless::sampleSlotPoint( g_SwSlot0, uv );
    if ( slot == SW_SLOT_ENGINE_TEX1 )
        return Bindless::sampleSlotPoint( g_SwSlot1, uv );
    if ( slot == SW_SLOT_ENGINE_TEX2 )
        return Bindless::sampleSlotPoint( g_SwSlot2, uv );
    if ( slot == SW_SLOT_ENGINE_TEX3 )
        return Bindless::sampleSlotPoint( g_SwSlot3, uv );
    if ( slot == SW_SLOT_ENGINE_GBUFFER_MATERIAL )
        return Bindless::sampleSlotPoint( g_SwSlotGBufferMaterial, uv );
    return kUnmappedEngineTextureColor;
}
        #endif
/**
 * @brief 에뮬 백엔드: 비교 샘플러가 없어 2x2 이웃을 게더로 읽어 각각 비교하고 쌍선형으로 섞는다 — DX12 · Vulkan 의 비교 샘플러(LINEAR)와 같은 값이다.
 * @details 한 점만 비교하면 가장자리가 텍셀 계단이고, 하드웨어 비교 백엔드와 그림이 갈린다. 그림자 맵은 슬롯 0 이다(Bindless::sampleIndex 의 표).
 */
static float Bindless::sampleShadowComparison( uint index, float2 uv, float depth )
{
    if ( index == kInvalidIndex )
        return 1.0f;
    if ( index != g_ShadowMapIndex )
        return ( depth <= Bindless::sampleIndex( index, uv ).r ) ? 1.0f : 0.0f;
    float2 size;
    g_SwSlot0.GetDimensions( size.x, size.y );
    const float2 texelPosition = uv * size - 0.5f;
    const float2 weight        = frac( texelPosition );
    // 게더는 네 텍셀이 만나는 모서리를 가리킨다. 순서는 w (0,0) · z (1,0) · x (0,1) · y (1,1).
    const float4 stored = g_SwSlot0.GatherRed( g_SwSlot0Sampler, ( floor( texelPosition ) + 1.0f ) / size );
    const float4 lit    = step( depth.xxxx, stored ); // depth <= 저장값이면 1 (LESS_EQUAL)
    return lerp( lerp( lit.w, lit.z, weight.x ), lerp( lit.x, lit.y, weight.x ), weight.y );
}

    #endif // !SW_STAGE_COMPUTE

    #if defined( SW_STAGE_COMPUTE )
        // 컴퓨트 RW 텍스처 고정 슬롯 u4..u7 — 엔진이 bindComputeUav( index, SW_SLOT_COMPUTE_TEXUAV0 + 서수 ) 로 건다. index = 서수.
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
        #if !defined( SW_RW_SLOT3_GL_FORMAT )
            #define SW_RW_SLOT3_GL_FORMAT "rgba32f"
        #endif
SW_DECLARE_RW_TEXTURE2D( g_SwRWSlot0, SW_SLOT_COMPUTE_TEXUAV0, SW_GL_IMAGE_UNIT0, SW_RW_SLOT0_GL_FORMAT );
SW_DECLARE_RW_TEXTURE2D( g_SwRWSlot1, SW_SLOT_COMPUTE_TEXUAV1, SW_GL_IMAGE_UNIT1, SW_RW_SLOT1_GL_FORMAT );
SW_DECLARE_RW_TEXTURE2D( g_SwRWSlot2, SW_SLOT_COMPUTE_TEXUAV2, SW_GL_IMAGE_UNIT2, SW_RW_SLOT2_GL_FORMAT );
SW_DECLARE_RW_TEXTURE2D( g_SwRWSlot3, SW_SLOT_COMPUTE_TEXUAV3, SW_GL_IMAGE_UNIT3, SW_RW_SLOT3_GL_FORMAT );
static void Bindless::storeRwTexture2D( uint index, uint2 texelPosition, float4 value )
{
    if ( index == 0 )
        g_SwRWSlot0[texelPosition] = value;
    else if ( index == 1 )
        g_SwRWSlot1[texelPosition] = value;
    else if ( index == 2 )
        g_SwRWSlot2[texelPosition] = value;
    else if ( index == 3 )
        g_SwRWSlot3[texelPosition] = value;
}
static float4 Bindless::loadRwTexture2D( uint index, uint2 texelPosition )
{
    if ( index == 0 )
        return g_SwRWSlot0[texelPosition];
    if ( index == 1 )
        return g_SwRWSlot1[texelPosition];
    if ( index == 2 )
        return g_SwRWSlot2[texelPosition];
    if ( index == 3 )
        return g_SwRWSlot3[texelPosition];
    return float4( 0, 0, 0, 0 );
}
    #endif // SW_STAGE_COMPUTE

#endif // SW_NATIVE_BINDLESS

// ---- 11-3) 공통 진입 함수 (네 백엔드) -----------------------------------------
#if defined( SW_NATIVE_BINDLESS ) || !defined( SW_STAGE_COMPUTE )
    /**
     * @brief 2D 빛 쿠키 아틀라스(패스 역할 `Light2DCookieAtlas`)를 밉 0 으로 읽습니다. 빛 반복 안에서 부르므로 암시 미분을 쓰지 않는다.
     * @details 미분이 든 샘플을 길이가 정해지지 않은 반복에 두면 FXC(DX11)가 반복을 펼치려다 실패한다(X3511). 아틀라스는 밉이 없는 렌더 타깃이라
     *          밉 0 이 곧 그 값이다. 에뮬 백엔드는 아틀라스를 슬롯 1 에 건다(`Bindless::sampleIndex` 의 표). 아틀라스가 없으면 `Bindless::sampleIndex` 와 같이 검정이다.
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
    return g_SwSlot1.SampleLevel( g_SwSlot1Sampler, uv, 0.0f );
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
            #if defined( DX11 )
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
        #if defined( DX11 )
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
        #if defined( DX11 )
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
/** @brief SSAO 결과. 파이프라인에 SSAO 가 없으면(인덱스 무효) 가림 없음(1)이다 — 0 으로 폴백하면 화면이 검게 된다. */
static float PassTexture::sampleAmbientOcclusion( float2 uv )
{
    if ( g_AmbientOcclusionIndex == kInvalidIndex )
        return 1.0f;
    return Bindless::sampleIndex( g_AmbientOcclusionIndex, uv ).r;
}

#endif // SW_NATIVE_BINDLESS || !SW_STAGE_COMPUTE

// ------------------------------------------------------------------------------
// 12) 픽셀 출력 — **같은 머티리얼 셰이더가 포워드와 G버퍼 양쪽에 쓰인다.**
//     머티리얼이 셰이더 경로를 정하므로(usesMaterialShader) G버퍼 패스도 머티리얼의 .hlsl 로 그린다.
//     그 셰이더가 SV_TARGET 하나만 내면 **G버퍼의 노멀 타깃이 클리어 값 그대로 남아**, 디퍼드 조명이 모든 픽셀을
//     같은 노멀로 계산한다(오류도 경고도 없이).
//     언리얼이 같은 머티리얼을 패스별 셰이더 **타입**으로 감싸는 것에 해당한다. 여기서는 패스가 define 을
//     추가하고(SW_PASS_GBUFFER), 출력 서명이 그 define 을 따라간다.
// ------------------------------------------------------------------------------
// 양쪽 다 **구조체**다. 포워드 쪽을 `float4` 로 두면 `SurfaceOutput PSMain(...)` 에 반환
// 시맨틱이 사라져 DXC 가 "Semantic must be defined for all outputs" 로 거절한다 — 그러면 머티리얼
// 셰이더가 통째로 컴파일되지 않아 화면이 빈다(실제로 한 번 그랬다).
struct SurfaceOutput
{
#if defined( SW_PASS_GBUFFER )
    float4 _albedo : SV_TARGET0;
    float2 _normal : SV_TARGET1;   // R16G16_SNORM — Octahedral::encode(월드 노멀)
    float4 _material : SV_TARGET2; // SurfaceOutput::encodeGbufferMaterial
#else
    float4 _color : SV_TARGET0;
#endif

    static SurfaceOutput store( float4 litColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic );
    static SurfaceOutput pack( float4 litColor, float4 albedo, float3 worldNormal, float roughness, float metallic ); // 패스의 출력 서명대로 담는다
    static float4 encodeGbufferMaterial( float roughness, float metallic );
};

// ------------------------------------------------------------------------------
// 라이트 목록과 라이트 그리드 읽기 — 조명 식(lighting.hlsli 의 커서)과 보기 모드 Light Complexity(G버퍼 · 포워드 표면)가 같은 함수를 쓴다.
// 라이트 원소 구조체와 버퍼(g_SwLights)는 lighting.hlsli 다. 여기는 개수와 그리드(g_SwLightGrid, t12)만 안다.
// ------------------------------------------------------------------------------
// 뷰마다의 라이트 그리드(32 비트 단어, 배치는 bindingslots.hlsli 10 절). CPU 생산자와 GPU 생산자가 같은 슬롯에 건다 — 읽는 함수는 LightGrid::loadWord 하나다.
SW_DECLARE_STRUCTURED_BUFFER( uint, g_SwLightGrid, SW_SLOT_LIGHT_GRID_SRV );

/** @brief 이번 패스에 바인딩된 라이트 수입니다. 라이트 버퍼가 바인딩되지 않았으면 0 이고, 그 씬은 앰비언트만 받습니다(키 라이트로 대신하지 않습니다). */
static uint LightGrid::computeLightCount()
{
    return ( g_SwLightsIndex == kInvalidIndex ) ? 0u : g_SwLightCount;
}

/**
 * @brief 지금 뷰의 라이트 그리드 단어 하나를 읽습니다. @p wordIndex 는 그리드 첫 단어 기준입니다(bindingslots.hlsli 10절의 배치).
 * @details 이 함수만 그리드의 위치를 안다(g_SwLightGrid, t12 — CPU · GPU 생산자가 같은 슬롯에 건다).
 */
static uint LightGrid::loadWord( uint wordIndex )
{
    return g_SwLightGrid[g_SwLightGridBase + wordIndex];
}

/** @brief 지금 뷰에 라이트 그리드가 있는지 — 버퍼가 걸려 있고 이 뷰의 그리드 첫 단어가 있어야 한다. 없으면 커서는 목록 전체를 돈다. */
static bool LightGrid::hasGrid()
{
    return g_SwLightGridIndex != kInvalidIndex && g_SwLightGridBase != kInvalidIndex;
}

/**
 * @brief 이 월드 위치가 든 클러스터 번호입니다. C++ `LightGrid::computeClusterIndex` 와 같은 식입니다(LightGridTest 가 C++ 쪽으로 확인한다).
 * @details 클러스터는 지금 뷰의 NDC 를 X · Y 칸으로, 깊이를 조각으로 나눈 것이다. 원근은 클립 w 의 로그 조각, 직교는 NDC z 의 선형 조각이다.
 *          NDC 로 정의하므로 화면 크기 · 해상도 배율과 무관하다. 화면 밖 위치는 가장자리 칸으로 자른다.
 */
static uint LightGrid::computeCluster( float3 worldPosition )
{
    const uint   dimX          = LightGrid::loadWord( SW_LIGHT_GRID_WORD_DIM_X );
    const uint   dimY          = LightGrid::loadWord( SW_LIGHT_GRID_WORD_DIM_Y );
    const uint   dimZ          = LightGrid::loadWord( SW_LIGHT_GRID_WORD_DIM_Z );
    const bool   bPerspective  = LightGrid::loadWord( SW_LIGHT_GRID_WORD_PERSPECTIVE ) != 0u;
    const float  nearDepth     = asfloat( LightGrid::loadWord( SW_LIGHT_GRID_WORD_NEAR_DEPTH ) );
    const float  sliceScale    = asfloat( LightGrid::loadWord( SW_LIGHT_GRID_WORD_SLICE_SCALE ) );
    const float4 clip          = mul( float4( worldPosition, 1.0f ), g_ViewProj );
    const float3 ndc           = clip.xyz / clip.w;
    const uint   tileX         = LightGrid::computeTile( ndc.x, dimX );
    const uint   tileY         = LightGrid::computeTile( ndc.y, dimY );
    const float  slicePosition = LightGrid::computeSlicePosition( bPerspective ? clip.w : ndc.z, bPerspective, nearDepth, sliceScale );
    const uint   slice         = min( (uint)max( slicePosition, 0.0f ), dimZ - 1u );
    return ( slice * dimY + tileY ) * dimX + tileX;
}

/**
 * @brief 이 월드 위치를 비추는 라이트 수입니다(전역 구간 포함). 그리드가 없으면 목록 전체의 수입니다. 보기 모드 Light Complexity 가 색으로 보입니다.
 * @details 커서가 도는 항목 수와 같다 — 조명 식이 실제로 도는 비용이다(언리얼 Light Complexity, 유니티 Forward+ 디버그 보기).
 */
static uint LightGrid::computeVisitCount( float3 worldPosition )
{
    uint visitCount = LightGrid::computeLightCount();
    if ( visitCount > 0u && LightGrid::hasGrid() )
    {
        const uint clusterWord = LightGrid::loadWord( SW_LIGHT_GRID_WORD_CLUSTER_BASE ) + LightGrid::computeCluster( worldPosition ) * SW_LIGHT_GRID_WORDS_PER_CLUSTER;
        visitCount             = LightGrid::loadWord( SW_LIGHT_GRID_WORD_GLOBAL_COUNT ) + LightGrid::loadWord( clusterWord + 1u );
    }
    return visitCount;
}

// 보기 모드 Unlit · Normals · Depth · Overdraw — 조명 · 그림자 · 림 · 반사를 컴파일 아웃한다. 조명을 하는 셰이더는 **모두** 이 매크로로 가르고,
// 내보낼 색은 SurfaceOutput::store 가 보기 모드대로 바꾼다(ViewMode::computeColor). C++ 정본은 RenderView.h 의 보기 모드 표(kArrRenderViewModeInfo)다 —
// 이름이 어긋나면 컴파일은 되고 그림만 안 바뀐다(RenderPassGPUTest.ViewModesProduceDistinctPictures).
#if defined( SW_VIEWMODE_UNLIT ) || defined( SW_VIEWMODE_NORMALS ) || defined( SW_VIEWMODE_DEPTH ) || defined( SW_VIEWMODE_OVERDRAW ) ||               \
    defined( SW_VIEWMODE_ROUGHNESS ) || defined( SW_VIEWMODE_METALLIC ) || defined( SW_VIEWMODE_MIPMAPS ) || defined( SW_VIEWMODE_LIGHTCOMPLEXITY ) || \
    defined( SW_VIEWMODE_WIREOVERLAY )
    #define SW_VIEWMODE_SKIPS_LIGHTING 1
#else
    #define SW_VIEWMODE_SKIPS_LIGHTING 0
#endif

// 보기 모드 LightingOnly — 조명은 그대로 계산하고 표면 색만 이 회색으로 바꾼다(언리얼 Lighting Only). 포워드는 Lighting::shadeLights 가,
// 디퍼드는 G버퍼 알베도 칸(SurfaceOutput::store)이 바꾼다 — 디퍼드 조명 패스는 보기 모드 변형이 없으므로 G버퍼에서 바꿔야 한다.
#define SW_VIEWMODE_LIGHTINGONLY_ALBEDO float3( 0.5f, 0.5f, 0.5f )

#if defined( SW_VIEWMODE_LIGHTINGONLY )
static float3 ViewMode::overrideLightingAlbedo( float3 albedo )
{
    return SW_VIEWMODE_LIGHTINGONLY_ALBEDO;
}
#else
static float3 ViewMode::overrideLightingAlbedo( float3 albedo )
{
    return albedo;
}
#endif

static const float  kViewModeDepthRange         = 100.0f;                        // Depth 보기: 이 뷰 공간 깊이(m)에서 흰색
static const float  kViewModeOverdrawStep       = 0.08f;                         // Overdraw 보기: 한 번 그릴 때 더하는 밝기(12 겹이면 흰색)
static const float  kViewModeLightComplexityMax = 16.0f;                         // Light Complexity 보기: 이 라이트 수부터 빨강
static const float3 kViewModeWireOverlayColor   = float3( 0.02f, 0.02f, 0.02f ); // ShadedWireframe 의 선 색(셰이딩한 면 위에 겹친다)

/** @brief 라이트 수를 색 띠로 바꿉니다 — 0 은 검정, 1 부터 파랑 · 초록 · 노랑 · 빨강(kViewModeLightComplexityMax 이상)이다. */
static float3 ViewMode::computeLightComplexityColor( uint visitCount )
{
    const float  ratio  = saturate( (float)visitCount / kViewModeLightComplexityMax );
    const float3 cold   = lerp( float3( 0.0f, 0.1f, 0.8f ), float3( 0.0f, 0.9f, 0.2f ), saturate( ratio * 3.0f ) );
    const float3 warm   = lerp( float3( 1.0f, 0.9f, 0.0f ), float3( 1.0f, 0.0f, 0.0f ), saturate( ratio * 3.0f - 2.0f ) );
    const float3 middle = lerp( cold, float3( 1.0f, 0.9f, 0.0f ), saturate( ratio * 3.0f - 1.0f ) );
    return ( visitCount == 0u ) ? float3( 0.0f, 0.0f, 0.0f ) : ( ratio > 2.0f / 3.0f ? warm : middle );
}

/**
 * @brief 보기 모드가 바꾼 표면 색이다. Lit 에서는 부르지 않는다(SW_VIEWMODE_SKIPS_LIGHTING 이 0).
 * @details Overdraw 는 알파 0 을 낸다 — PSO 가 프리멀티플라이 블렌드(색 One / InvSrcAlpha)라 알파 0 이 더하기가 된다.
 *          Depth 는 클립 w(원근 카메라의 뷰 공간 깊이)다. 직교 카메라는 w 가 1 이라 한 색이다.
 */
#if defined( SW_VIEWMODE_ROUGHNESS )
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    return float4( saturate( roughness ).xxx, 1.0f );
}
#elif defined( SW_VIEWMODE_METALLIC )
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    return float4( saturate( metallic ).xxx, 1.0f );
}
#elif defined( SW_VIEWMODE_NORMALS )
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    return float4( normalize( worldNormal ) * 0.5f + 0.5f, 1.0f );
}
#elif defined( SW_VIEWMODE_DEPTH )
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    const float viewDepth = Transform::computeClipPosition( float4( worldPosition, 1.0f ), g_ViewProj ).w;
    return float4( saturate( viewDepth / kViewModeDepthRange ).xxx, 1.0f );
}
#elif defined( SW_VIEWMODE_OVERDRAW )
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    return float4( kViewModeOverdrawStep, kViewModeOverdrawStep * 0.5f, kViewModeOverdrawStep * 0.15f, 0.0f );
}
#elif defined( SW_VIEWMODE_WIREOVERLAY )
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    // ShadedWireframe 의 두 번째 드로우 — PSO 가 삼각형을 선으로 그리므로 단색이면 외곽선이다.
    return float4( kViewModeWireOverlayColor, 1.0f );
}
#elif defined( SW_VIEWMODE_LIGHTCOMPLEXITY )
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    // 조명 식이 이 위치에서 도는 라이트 수 — 커서(LightCursor::begin)와 같은 그리드를 읽는다. 표면에서 세므로 디퍼드도 G버퍼가 색을 넘긴다.
    return float4( ViewMode::computeLightComplexityColor( LightGrid::computeVisitCount( worldPosition ) ), 1.0f );
}
#else
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    // Unlit — 셰이더가 조명 없이 만든 색(알베도) 그대로.
    return float4( albedo.rgb, shadedColor.a );
}
#endif

/** @brief 보기 모드가 표면의 색을 바꾼다 — 색을 덮는 모드(SW_VIEWMODE_SKIPS_LIGHTING)는 최종 색과 알베도를 그 모드의 색으로, LightingOnly 는 알베도만 회색으로. */
#if SW_VIEWMODE_SKIPS_LIGHTING
static void ViewMode::applyToSurface( inout float4 inoutLitColor, inout float4 inoutAlbedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    inoutLitColor = ViewMode::computeColor( inoutLitColor, inoutAlbedo, worldNormal, worldPosition, roughness, metallic );
    inoutAlbedo   = float4( inoutLitColor.rgb, inoutAlbedo.a );
}
#else
static void ViewMode::applyToSurface( inout float4 inoutLitColor, inout float4 inoutAlbedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    inoutAlbedo = float4( ViewMode::overrideLightingAlbedo( inoutAlbedo.rgb ), inoutAlbedo.a );
}
#endif

// G버퍼 알베도의 알파는 **셰이딩 모델**이다 — 1 은 조명, 0 은 조명 없이 알베도 그대로(deferredlighting 이 읽는다).
// 디퍼드 조명 패스는 머티리얼 PSO 가 아니라 뷰 모드를 모르므로, 뷰 모드를 받는 G버퍼 패스가 픽셀마다 적어 넘긴다(언리얼 G버퍼의 ShadingModelID 와 같은 자리).
#define SW_GBUFFER_SHADING_LIT   1.0f
#define SW_GBUFFER_SHADING_UNLIT 0.0f
#if SW_VIEWMODE_SKIPS_LIGHTING
    #define SW_GBUFFER_SHADING SW_GBUFFER_SHADING_UNLIT
#else
    #define SW_GBUFFER_SHADING SW_GBUFFER_SHADING_LIT
#endif

/**
 * @brief G버퍼 머티리얼 칸(첨부 GBufferMaterial, R8G8B8A8_UNORM)에 적을 값 — r 금속도, g 거칠기(지각 거칠기, 제곱 전), b 는 쓰지 않는다(0), a 는 "그려졌다" 표시(1).
 * @details 읽는 쪽은 11-3 절 PassTexture::loadGbufferMaterial 하나다. 셰이딩 모델은 알베도 칸의 알파(SW_GBUFFER_SHADING)에 그대로 둔다.
 *          클리어 값 0 과 갈라지는 알파가 "이 픽셀을 G버퍼 패스가 그렸다" 를 싣는다(노멀 칸은 옥타헤드럴 두 채널이라 알파가 없다).
 */
static float4 SurfaceOutput::encodeGbufferMaterial( float roughness, float metallic )
{
    return float4( saturate( metallic ), saturate( roughness ), 0.0f, 1.0f );
}

/**
 * @brief 표면을 **패스가 원하는 모양**으로 내보낸다.
 * @param roughness · metallic 보기 모드 Roughness · Metallic 이 보이는 값이다. 기본값을 두지 않는다 — 셰이더마다 자기 값(머티리얼 칸이나 그 셰이더의 정의)을 넘긴다.
 * @details 포워드는 셰이딩한 색 하나, G버퍼는 알베도(알파 = 셰이딩 모델) · 월드 노멀 · 머티리얼(금속도 · 거칠기) 셋. 노멀 인코딩은 한 군데뿐이어야
 *          한다 — 쓰는 쪽(여기)과 읽는 쪽(deferredlighting)이 어긋나면 조명이 조용히 틀린다.
 *          보기 모드(Unlit · Normals · Depth · Overdraw)의 색도 여기서 바꾼다. G버퍼는 그 색을 알베도 칸에 셰이딩 모델 Unlit 으로 적어
 *          디퍼드 조명 패스가 그대로 낸다 — 조명 패스는 보기 모드 변형이 없다.
 */
static SurfaceOutput SurfaceOutput::store( float4 litColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    ViewMode::applyToSurface( litColor, albedo, worldNormal, worldPosition, roughness, metallic );
    return SurfaceOutput::pack( litColor, albedo, worldNormal, roughness, metallic );
}

#if defined( SW_PASS_GBUFFER )
static SurfaceOutput SurfaceOutput::pack( float4 litColor, float4 albedo, float3 worldNormal, float roughness, float metallic )
{
    SurfaceOutput output;
    output._albedo   = float4( albedo.rgb, SW_GBUFFER_SHADING );
    output._normal   = Octahedral::encode( normalize( worldNormal ) );
    output._material = SurfaceOutput::encodeGbufferMaterial( roughness, metallic );
    return output;
}
#else
static SurfaceOutput SurfaceOutput::pack( float4 litColor, float4 albedo, float3 worldNormal, float roughness, float metallic )
{
    SurfaceOutput output;
    output._color = litColor;
    return output;
}
#endif

#endif // SW_ENGINE_BINDING_HLSLI
