/**
 * binding.hlsli — 셰이더가 엔진 리소스(상수 · 버퍼 · 텍스처 · 샘플러)를 읽는 경로를 한곳에 모은 헤더.
 *
 * 셰이더는 `#include "binding.hlsli"` 하나만 한다. 처음 읽는 사람은 아래 0 절(bindless 입문)만 읽어도 셰이더를 쓸 수 있다.
 * 슬롯 번호(t# · s# · space)의 원본은 binding_slots.hlsli 다 — 이 파일은 그 번호로 리소스를 선언하고 읽는 함수를 준다.
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
 *   `SW_NATIVE_BINDLESS` · `__spirv__` 분기와 `g_SwBindlessTex2D` · `g_SwEngineTex#` · `g_SwMaterialTex#` 는 **이 파일 안에서만** 쓴다.
 *   셰이더 파일(.hlsl)에 이 분기를 들이지 않는다 — 새 읽기 방식이 필요하면 여기에 진입 함수를 더한다.
 *
 * 백엔드 차이
 *   ┌────────┬──────────────────┬─────────────────────────────────────────┬──────────────────────────────────────────┐
 *   │ 백엔드 │ 방식             │ 텍스처가 있는 곳                        │ 인덱스의 뜻                              │
 *   ├────────┼──────────────────┼─────────────────────────────────────────┼──────────────────────────────────────────┤
 *   │ DX12   │ 네이티브 bindless│ t0 space1 무제한 배열(힙 전체 테이블)   │ 배열 원소 번호                           │
 *   │ Vulkan │ 네이티브 bindless│ set 1 binding 0 무제한 배열             │ 배열 원소 번호                           │
 *   │ DX11   │ 슬롯 에뮬        │ 엔진 t32..t43 · 머티리얼 t16..t31       │ 엔진: PassCB 인덱스와 값 비교로 슬롯 선택│
 *   │ OpenGL │ 슬롯 에뮬        │ 텍스처 유닛 0..11 · 16..31(결합)        │ 머티리얼: 서수 0..15 (DX11 과 같다)      │
 *   └────────┴──────────────────┴─────────────────────────────────────────┴──────────────────────────────────────────┘
 *   네이티브인지는 common.hlsli 의 `SW_NATIVE_BINDLESS` 가 정한다(ShaderCompiler 가 DX12 · Vulkan 에만 SW_BINDLESS=1 을 준다).
 *   에뮬 백엔드는 엔진이 아는 텍스처(PassCB 의 g_*Index — C++ 표에서 나온다)만 찾을 수 있다 — 모르는 인덱스는 자홍(kUnmappedEngineTextureColor)이다.
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
 *     VS: `InstanceData instance = InstanceData::load( instanceSlot )` → instance._world / instance._materialIndex
 *     PS: `SW_MATERIAL( materialIndex )._color`  — VS 가 materialIndex 를 nointerpolation 으로 넘긴다.
 *   머티리얼 구조체는 셰이더가 SW_MATERIAL_BEGIN/END 로 선언하고, 엔진이 그 셰이더 타입의 머티리얼 데이터를
 *   StructuredBuffer(g_SwMaterials, t5)에 원소로 쌓아 인스턴스에 materialIndex 를 매긴다.
 */

#ifndef SW_ENGINE_BINDING_HLSLI
#define SW_ENGINE_BINDING_HLSLI

#include "common.hlsli"
#include "instance_data.hlsli"
#include "octahedral.hlsli"
#include "light_grid_common.hlsli"

static const uint kInvalidIndex = 0xFFFFFFFFu;

// ------------------------------------------------------------------------------
// 0-1) 이 헤더가 내놓는 함수의 묶음 — 선언만 둔다. 정의는 각 절에 있다(리소스 선언과 백엔드 분기 사이에 두어야 해서 밖에 정의한다).
//      정의도 `static` 을 붙인다(FXC 가 선언과 정의의 static 이 다르면 거절한다). 인스턴스 · 배치 원소의 함수는 그 구조체(instance_data · batch_data)에 있다.
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
    static uint2 loadSkinResultOffset( uint skinSlot );
    static uint computeElement( uint batchIndex, uint skinSlot, uint vertexID );
    static void loadVertex( uint batchIndex, uint skinSlot, uint vertexID, float3 restPosition, float3 restNormal, out float3 outPosition, out float3 outNormal );
    static float4 loadTangent( uint batchIndex, uint skinSlot, uint vertexID, float4 restTangent );
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
    static float computeLODFadeAppearance( float instanceAppearance, uint fadeCode );
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
    static float4 sampleIndexWith( uint index, uint samplerID, float2 uv );
    static float sampleShadowComparison( uint index, float2 uv, float depth );
    static void storeRwTexture2D( uint index, uint2 texelPosition, float4 value );
    static float4 loadRwTexture2D( uint index, uint2 texelPosition );
    // 엔진 텍스처 한 장을 직접 읽는 패스(풀스크린 입력 · 히스토리)용 진입 함수다. 네이티브는 index 로, 에뮬은 패스가 거는 역할의 칸(emulatedSlot = SW_EMU_SLOT_<역할>)으로 읽는다 — 분기는 11 절 안에서 끝난다.
    static float4 loadEngineTexel( uint index, uint emulatedSlot, int2 texel );                  // 밉 0 의 텍셀 하나
    static uint2 getEngineTextureSize( uint index, uint emulatedSlot );                          // 밉 0 의 크기
    static float4 gatherEngineChannel( uint index, uint emulatedSlot, uint channel, float2 uv ); // 점 · 클램프 샘플러로 2x2 게더(channel 0 R · 1 G · 2 B)
    static float4 sampleSlotWith( Texture2D slotTexture, uint samplerID, float2 uv );
    static float4 sampleSlotPoint( Texture2D slotTexture, float2 uv );
};

/** @brief 패스가 이름으로 건 엔진 텍스처(PassCB 의 g_*Index)를 읽는다(11-3 절). */
struct PassTexture
{
    static float4 sampleShadow( float2 uv );
    static float4 sampleAlbedo( float2 uv );
    static float3 sampleNormal( float2 uv ); // G버퍼 노멀 칸(옥타헤드럴 좌표)을 푼 단위 노멀
    static float4 sampleGbufferMaterial( float2 uv );
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
    static float3 computeLODLevelColor( uint entry );
    static float3 computeLODFadeColor( uint entry );
    static float3 computeShadingPathColor(); // 보기 모드 ShadingPath — 이 변형이 그리는 셰이딩 경로의 색
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
    // 지난번 이 뷰를 그린 뷰-투영 — 모션 블러가 깊이로 복원한 월드 위치를 다시 투영해 화면 속도를 구한다(motion_blur.hlsl). 처음 · 컷 프레임은 g_ViewProj 와 같다.
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
    float4 g_CameraGrade; // 카메라별 색 보정 — x 노출 배율(2^EV), y 채도, z 대비, w 비네트 세기(PostProcessValues, post_grade.hlsli)
    float4 g_CameraFrame; // 카메라별 화면 틀 — x 레터박스로 남길 가로/세로 비율(0 = 없음), y 지금 뷰의 가로/세로 비율, z 색각 보정 방식(color_vision.hlsli, 렌더 텍스처 뷰는 0)
    // 컬러 그레이딩(post_grade.hlsli, C++ PostGradeConstants) — 화이트 밸런스 3x3 행(xyz), 범위(그림자 · 중간 · 밝은 부분)마다 채도 · 대비 · 감마 · 게인 · 오프셋 rgb(전역을 미리 합쳤다),
    // (그림자 끝 휘도, 밝은 부분 시작 휘도, 켬 1 · 끔 0, 0), (LUT 한 변 N — 0 이면 없음, LUT 세기, 0, 0). 톤맵하는 패스만 읽는다
    float4 g_GradeWhiteBalance0;
    float4 g_GradeWhiteBalance1;
    float4 g_GradeWhiteBalance2;
    float4 g_GradeShadowsSaturation;
    float4 g_GradeShadowsContrast;
    float4 g_GradeShadowsGamma;
    float4 g_GradeShadowsGain;
    float4 g_GradeShadowsOffset;
    float4 g_GradeMidtonesSaturation;
    float4 g_GradeMidtonesContrast;
    float4 g_GradeMidtonesGamma;
    float4 g_GradeMidtonesGain;
    float4 g_GradeMidtonesOffset;
    float4 g_GradeHighlightsSaturation;
    float4 g_GradeHighlightsContrast;
    float4 g_GradeHighlightsGamma;
    float4 g_GradeHighlightsGain;
    float4 g_GradeHighlightsOffset;
    float4 g_GradeParams;
    float4 g_GradeLut;
    // 렌즈 · 필름(post_grade.hlsli, C++ PostProcessValues) — (색수차 세기, 색수차 시작 반경, 렌즈 왜곡, 선명화) · (필름 그레인 세기, 그레인 크기 px, 0, 0). 톤맵하는 패스만 읽는다
    float4 g_LensParams;
    float4 g_GrainParams;
    float4 g_CameraLens; // 카메라별 피사계 심도 — x 초점 거리(m), y 착란원 반지름 배율(px, 반지름 = y · |1 - x / 깊이|), z 반지름 상한(px), w 켬 1 · 끔 0(depth_of_field.hlsl)
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
    // (볼륨 안개 켬 1, 볼륨 거리, 산란 배율, 이방성). 씬에 안개가 없으면 안개 패스가 돌지 않는다. 볼륨 켬은 주 시점에서 컴퓨트가 돈 프레임만 1 이다(FramePassExecutor::executeFogPass).
    float4 g_FogColorDensity;
    float4 g_FogHeightParams;
    float4 g_FogInscatter;
    float4 g_FogVolumeParams;
    float4 g_FogDebugParams; // 안개 진단 보기(C++ FrameFog::makeDebugParams) — x 보기(0 없음 · 1 비교 · 2 산란 단면 · 3 투과율 단면), y 단면 조각, z 비교 경계 uv.x. 안개 패스만 읽는다
    // 엔진 텍스처 인덱스 g_<이름>Index — 이름은 C++ 표 shaderslot::emulated::kEngineTextureNames 하나가 정하고 컴파일러가 SW_ENGINE_TEXTURE_INDEX_<칸> 으로 넣는다.
    // 손으로 적지 않는다: 표에 없는 엔진 텍스처 인덱스가 생기면 에뮬 백엔드(DX11 · GL)가 칸을 몰라 자홍을 낸다(뜻은 표의 주석). 걸려 있지 않은 역할은 kInvalidIndex 다.
    // 아래 14 줄은 SW_ENGINE_TEXTURE_COUNT 와 같은 수다.
#if !defined( SW_ENGINE_TEXTURE_INDEX_0 ) || !defined( SW_ENGINE_TEXTURE_INDEX_13 ) || defined( SW_ENGINE_TEXTURE_INDEX_14 ) || SW_ENGINE_TEXTURE_COUNT != 14
    #error "SW_ENGINE_TEXTURE_INDEX_* missing or count changed - ShaderCompiler::appendEngineDefines injects them from shaderslot::emulated::kEngineTextureNames; match the 14 fields below"
#endif
    uint  SW_ENGINE_TEXTURE_INDEX_0;
    uint  SW_ENGINE_TEXTURE_INDEX_1;
    uint  SW_ENGINE_TEXTURE_INDEX_2;
    uint  SW_ENGINE_TEXTURE_INDEX_3;
    uint  SW_ENGINE_TEXTURE_INDEX_4;
    uint  SW_ENGINE_TEXTURE_INDEX_5;
    uint  SW_ENGINE_TEXTURE_INDEX_6;
    uint  SW_ENGINE_TEXTURE_INDEX_7;
    uint  SW_ENGINE_TEXTURE_INDEX_8;
    uint  SW_ENGINE_TEXTURE_INDEX_9;
    uint  SW_ENGINE_TEXTURE_INDEX_10;
    uint  SW_ENGINE_TEXTURE_INDEX_11;
    uint  SW_ENGINE_TEXTURE_INDEX_12;
    uint  SW_ENGINE_TEXTURE_INDEX_13;
    uint  g_Flags;
    uint  g_SwInstancesIndex;          // 인스턴스 구조버퍼가 걸려 있으면 유효, kInvalidIndex 면 g_World 폴백 (풀스크린·픽스처)
    uint  g_SwInstanceCount;           // 인스턴스 버퍼 원소 수 — 범위 밖 인덱스를 막는다 (DX12 루트 SRV 는 경계 검사가 없다)
    uint  g_SwVisibleInstanceIDsIndex; // 컬링이 만든 가시 ID 목록이 걸려 있으면 유효, 아니면 kInvalidIndex
    uint  g_SwVisibleSlotCount;        // 그 목록의 칸 수(가시 구간의 합 — LOD 그룹이 없으면 인스턴스 수) — 범위 밖 슬롯을 막는다
    uint  g_SwMorphVerticesIndex;      // GPU 가 변형한 정점 풀이 걸려 있으면 유효, 아니면 kInvalidIndex
    uint  g_SwMorphVertexCount;        // 그 풀의 원소 수 — 범위 밖 인덱스를 막는다
    uint  g_SwSkinResultOffsetsIndex;  // 스킨 칸 → 결과 시작 표(g_SwSkinResultOffsets, t14)가 걸려 있으면 유효, 아니면 kInvalidIndex
    uint  g_SwSkinResultOffsetCount;   // 그 표의 칸 수 — 범위 밖 칸을 막는다
    uint  g_SwLightsIndex;             // 씬 라이트 버퍼가 걸려 있으면 유효, 아니면 kInvalidIndex
    uint  g_SwLightCount;              // 그 버퍼의 빛 원소 수 (0 이면 빛 없음 — 앰비언트만 받는다)
    uint  g_SwLightGridIndex;          // 라이트 그리드 버퍼(g_SwLightGrid, t12)가 걸려 있으면 유효, 아니면 kInvalidIndex
    uint  g_SwLightGridBase;           // 지금 뷰의 라이트 그리드 첫 단어(g_SwLightGrid 안). kInvalidIndex 면 그리드 없이 목록 전체를 돈다(gv_lightCulling 0)
    uint  g_SwLightProbesIndex;        // 라이트 프로브 SH9 버퍼(g_SwLightProbes, t13)가 걸려 있으면 유효, 아니면 kInvalidIndex — 프로브 볼륨 원소는 이것이 유효할 때만 읽는다
    uint  g_SwBatchesIndex;            // 씬 배치 표가 걸려 있으면 유효, 아니면 kInvalidIndex (풀스크린·픽스처)
    uint  g_SwBatchCount;              // 그 표의 원소 수 — 범위 밖 배치 번호를 막는다
    uint  g_SwVertexAnimationIndex;    // 정점 애니메이션(VAT) 표가 걸려 있으면 유효, 아니면 kInvalidIndex
    uint  g_SwVertexAnimationCount;    // 그 표의 단어(uint32) 수 — 범위 밖 번호를 막는다
    float g_SwVertexAnimationTime;     // VAT 시계(초) — 게임 스레드의 군중 시계와 같은 값이라 인스턴스 위상이 CPU 시각과 맞는다
    float g_SwMaterialTime;            // 머티리얼 애니메이션 시계(초, 툰 UV 애니메이션) — VAT 시계와 같은 값이지만 VAT 표가 없어도 늘 걸린다
};

// ------------------------------------------------------------------------------
// 2) 드로우별 루트 상수 — **배치마다 달라지는 값만** 여기 둔다.
//    PassCB 는 패스당 한 번 올리는 버퍼라, 배치마다 바뀌는 값을 거기 넣으면 한 패스의 드로우들이 서로를
//    덮어써 전부 마지막 값을 읽는다(GPU 는 제출 뒤에 읽는다). 언리얼이 FMeshDrawCommand 의 느슨한
//    파라미터를 드로우별로 싣는 자리와 같다. DX12 루트 상수 / Vulkan 푸시 상수 / DX11·GL 은 b2 에뮬.
//    컴퓨트 셰이더는 자기 루트 상수 블록을 직접 선언하므로(예: compute_texture_write.hlsl) 여기서는 빼 둔다 —
//    한 셰이더에 블록이 둘이면 재정의다. 자기 블록을 쓰는 그래픽스 셰이더(canvas.hlsl)는 include 앞에 SW_OWN_ROOT_CONSTANTS 를 정의한다.
// ------------------------------------------------------------------------------
#if !defined( SW_STAGE_COMPUTE ) && !defined( SW_OWN_ROOT_CONSTANTS )
SW_ROOT_CONSTANTS_BEGIN
{
    // 이 드로우 그룹의 머티리얼 데이터 버퍼(g_SwMaterials) 원소 수 — SW_MATERIAL 이 클램프한다. 그룹(같은 PSO·머티리얼 버퍼) 안에서
    // 같다. 배치마다 다른 값(인스턴스 시작·모프 풀 시작·정점 풀 시작)은 루트 상수가 아니라 배치 표(g_SwBatches)에 있고, 정점은
    // 인스턴스 슬롯 스트림(정점 셰이더 인자 SW_INSTANCESLOT)으로 자기 인스턴스를, 인스턴스로 자기 배치를 찾는다 — 그래서 같은 PSO 의
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
// 3) GPUScene 인스턴스 버퍼 (per-instance world/material/스프라이트 프레임 · 색). 원소 정의는 instance_data.hlsli 하나다(컴퓨트도 그것을 쓴다).
//    네 백엔드 공통 — 엔진이 리플렉션 슬롯 t4 에 인스턴스 버퍼를 바인딩한다 (레지스트리 이름 "SwInstances"). 읽는 함수는 8 절.
// ------------------------------------------------------------------------------
SW_DECLARE_STRUCTURED_BUFFER( InstanceData, g_SwInstances, SW_SLOT_INSTANCE_SRV );

// ------------------------------------------------------------------------------
// 4) 씬 배치 테이블 — C++ GPUBatchInfo 와 레이아웃 일치(4바이트 아홉 칸, 36바이트, batch_data.hlsli).
//    배치마다 다른 값은 전부 여기 있다. 정점은 인스턴스(InstanceData::load)의 meshBatchIndex 로 자기 배치를 찾는다.
// ------------------------------------------------------------------------------
// 원소 정의는 batch_data.hlsli 하나다(컬링 컴퓨트도 같은 파일을 쓴다).
#include "batch_data.hlsli"

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
    batch._baseVertex          = 0u;
    batch._vertexAnimationBase = kInvalidIndex;
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
// **구조체가 아니라 평면 uint 배열이다.** 정점 하나가 단어 SW_MORPH_RESULT_WORDS_PER_VERTEX 개 — 위치 float 비트 셋, 노멀 · 탄젠트 옥타헤드럴(Octahedral::pack) 하나씩.
// 탄젠트 부호(w)는 변형으로 바뀌지 않아 싣지 않는다(입력 스트림 값). 예전 float4 셋(48 B)에서 20 B 다 — 언리얼 스킨 캐시도 법선 · 탄젠트를 묶어 쓴다.
// 주의: `struct { float4 pos; float4 nrm; }` 로 두면 레이아웃이 네 백엔드에서 같아도(ArrayStride 32 · 오프셋 0/16)
// OpenGL 만 **같은 원소의 두 멤버를 다른 원소에서 읽는다**(셰이더 모양에 따라 달라진다 — 드라이버의 SPIR-V 경로가
// 구조체 멤버 로드를 다루는 방식). 평면 배열은 멤버가 없으니 그 자리가 아예 없다.
// 색은 담지 않는다 — 정점 셰이더가 색·UV 는 **입력 스트림에서** 읽고 풀에서는 위치와 노멀만 가져간다.
SW_DECLARE_STRUCTURED_BUFFER( uint, g_SwMorphVertices, SW_SLOT_MORPH_VERTEX_SRV );
// 스킨 칸 → (결과 시작, 변형한 LOD 단계의 첫 정점) 표(binding_slots.hlsli SW_SLOT_SKIN_RESULT_OFFSET_SRV, C++ GPUSkinResultOffset). 스킨 결과는 메시가 아니라 칸(유닛 · 군중 묶음)마다 하나라
// 같은 메시의 캐릭터들이 한 배치로 그려지고, 칸마다 그리는 단계 하나만 변형된다.
SW_DECLARE_STRUCTURED_BUFFER( uint2, g_SwSkinResultOffsets, SW_SLOT_SKIN_RESULT_OFFSET_SRV );

/** @brief 스킨 칸의 (결과 시작, 단계 첫 정점) — 칸이 없거나(@p skinSlot 이 kInvalidIndex) 표가 안 걸렸으면 시작이 kInvalidIndex. 분기는 있지만 early-return 은 없다. */
static uint2 MorphPool::loadSkinResultOffset( uint skinSlot )
{
    uint2      base   = uint2( kInvalidIndex, 0u );
    const bool bValid = ( skinSlot != kInvalidIndex ) && ( g_SwSkinResultOffsetsIndex != kInvalidIndex ) && ( skinSlot < g_SwSkinResultOffsetCount );
    if ( bValid )
        base = g_SwSkinResultOffsets[skinSlot];
    return base;
}

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
static uint MorphPool::computeElement( uint batchIndex, uint skinSlot, uint vertexID )
{
    const BatchData batch = BatchData::load( batchIndex );
    // **API 차이 — 이 메시의 로컬 정점 번호.** 씬 드로우는 인덱스 드로우이고 인덱스는 메시 안 번호다. 간접 인자의 baseVertex 를
    // SV_VertexID 가 포함하는지가 백엔드마다 다르다: Vulkan(VertexIndex)·OpenGL(gl_VertexID)은 인덱스 + baseVertex 이고,
    // D3D11·D3D12 는 인덱스 값 그대로다(D3D 의 SV_VertexID 는 BaseVertexLocation 을 더하지 않는다 — SM 6.8 이 SV_StartVertexLocation 을 따로 둔 까닭). 네 백엔드의 스킨 · 모프 골든 시나리오가 이 기대를 본다 —
    // 넷 다 포함한다고 보고 빼기만 하면 DX 에서 정점 풀 첫 메시만 모프된다.
    // 구간 · LOD 배치도 로컬 번호는 메시 안 번호다(인덱스 범위만 다르다). 셰이더 파일에 백엔드 분기는 없다 — 이 헤더가 흡수한다.
#if defined( DX11 ) || defined( DX12 )
    const uint local = vertexID;
#else
    const uint local = vertexID - batch._baseVertex; // 풀 밖 메시는 baseVertex 가 0 이라 그대로다
#endif
    // 결과 시작은 메시 종류마다 하나다 — 스킨 메시는 인스턴스의 칸(유닛마다 결과, 변형한 단계의 첫 정점부터), 모프 메시는 배치(메시마다 결과). 폴백이 아니다.
    const uint2 skinResultOffset = MorphPool::loadSkinResultOffset( skinSlot );
    const bool  bSkin            = skinSlot != kInvalidIndex;
    const uint  base             = bSkin ? skinResultOffset.x : batch._morphVertexBase;
    const uint  element          = base + ( local - ( bSkin ? skinResultOffset.y : 0u ) ); // 칸의 인스턴스는 모두 변형한 그 단계를 그린다(구간 컴포넌트는 주 메시의 단계를 따른다 — MeshComponent::setLODIndex)
    // 잘못된 번호(밑돎으로 커진 local)는 element 가 풀 크기를 넘어 여기서 걸러진다.
    const bool bValid = ( base != kInvalidIndex ) && ( g_SwMorphVerticesIndex != kInvalidIndex ) && ( element < g_SwMorphVertexCount );
    return bValid ? element : kInvalidIndex;
}

/**
 * @brief 이 정점의 위치·노멀 — 모프 대상이면 GPU 가 변형한 값을, 아니면 입력 스트림 값을 돌려준다.
 * @details 위치와 노멀을 **함께** 돌려주는 이유는 둘이 같이 변하기 때문이다. 노멀은 정점 속성이므로 위치만 바꾸면
 *          **레스트 포즈의 노멀**이 남아 변형된 표면이 원래 모양대로 칠해진다.
 */
static void MorphPool::loadVertex( uint batchIndex, uint skinSlot, uint vertexID, float3 restPosition, float3 restNormal, out float3 outPosition, out float3 outNormal )
{
    outPosition        = restPosition;
    outNormal          = restNormal;
    const uint element = MorphPool::computeElement( batchIndex, skinSlot, vertexID );
    if ( element != kInvalidIndex )
    {
        const uint word = element * SW_MORPH_RESULT_WORDS_PER_VERTEX;
        outPosition     = asfloat( uint3( g_SwMorphVertices[word], g_SwMorphVertices[word + 1u], g_SwMorphVertices[word + 2u] ) );
        outNormal       = Octahedral::unpack( g_SwMorphVertices[word + 3u] );
    }
    // 칸이 있는데 결과가 없다(풀 상한 초과 · 버퍼 실패 — 풀이 오류를 남겼다) — 레스트 포즈로 대신 그리지 않는다. NaN 위치의 삼각형은 래스터에서 버려진다.
    const bool bRefused = ( skinSlot != kInvalidIndex ) && ( element == kInvalidIndex );
    outPosition         = bRefused ? asfloat( 0x7FC00000u ).xxx : outPosition;
}

/**
 * @brief 이 정점의 탄젠트(xyz 로컬 방향, w 바이탄젠트 부호) — 모프 · 스킨 대상이면 컴퓨트가 돌린 값을, 아니면 입력 스트림 값을 돌려준다.
 * @details 부호는 변형으로 바뀌지 않아 입력 스트림 값을 쓴다. 노멀 맵을 읽는 셰이더만 부른다 — 부르지 않는 패스는 이 로드가 빠진다.
 *          early-return 이 없다 — GL 드라이버 사연은 MorphPool::computeElement 참고.
 */
static float4 MorphPool::loadTangent( uint batchIndex, uint skinSlot, uint vertexID, float4 restTangent )
{
    const uint element = MorphPool::computeElement( batchIndex, skinSlot, vertexID );
    return ( element == kInvalidIndex ) ? restTangent : float4( Octahedral::unpack( g_SwMorphVertices[element * SW_MORPH_RESULT_WORDS_PER_VERTEX + 4u] ), restTangent.w );
}

// ------------------------------------------------------------------------------
// 6) 정점 애니메이션(VAT) — 클립 하나를 미리 스키닝해 베이크한 프레임 × 정점 테이블(C++ `MeshVertexAnimation`, `GPUVertexAnimationPool`).
//    먼 군중은 CPU 포즈 · GPU 스키닝 없이 이 테이블을 **인스턴스마다 다른 시각**(g_SwVertexAnimationTime + instance._vertexAnimationPhase)으로 읽는다.
//    표는 32 비트 단어 배열이다. 메시마다 머리 단어 SW_VERTEX_ANIMATION_HEADER_WORDS 개(프레임 수 · 프레임율 · 정점 수 · 반복 · 위치 경계 중심 · 반 크기) 뒤에
//    원소가 프레임 우선 순서로 이어진다. 원소 하나 = 단어 둘(8 B): 단어0 = 위치 x · y(snorm16 둘), 단어1 = 위치 z(snorm16) · 옥타헤드럴 법선 u · v(snorm8 둘).
//    텍스처가 아니라 구조버퍼인 것은 모프 풀과 같은 이유다(정점 셰이더가 SV_VertexID 로 읽는다 — 네 백엔드가 같은 경로).
// ------------------------------------------------------------------------------
SW_DECLARE_STRUCTURED_BUFFER( uint, g_SwVertexAnimation, SW_SLOT_VERTEX_ANIMATION_SRV );
// 헤더 · 원소 단어 수는 SW_VERTEX_ANIMATION_HEADER_WORDS · SW_VERTEX_ANIMATION_WORDS_PER_ELEMENT(binding_slots.hlsli — C++ MeshVertexAnimationElement 와 같은 배치).

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
void InstanceData::computeVertexAnimationElements( uint vertexID, out bool outValid, out uint outFirstElement, out uint outSecondElement, out float outAlpha )
{
    const BatchData batch = BatchData::load( _meshBatchIndex );
    // VAT 표는 메시 전체의 정점 순서다 — 로컬 번호가 곧 메시 안 번호다(MorphPool::computeElement 와 같은 API 차이).
#if defined( DX11 ) || defined( DX12 )
    const uint meshLocal = vertexID;
#else
    const uint meshLocal = vertexID - batch._baseVertex;
#endif
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
 * @details 정점을 읽는 **모든** 패스(본 · G버퍼 · 그림자 · 깊이 프리패스)가 머티리얼 셰이더 뼈대(material_shader_main.hlsli)를 거쳐 이것 하나로 읽는다 —
 *          위치만 읽는 짧은 형태를 따로 두면 패스마다 위치가 비트 단위로 갈라질 수 있어 없앴다.
 */
void InstanceData::loadAnimatedVertex( uint vertexID, float3 restPosition, float3 restNormal, out float3 outPosition, out float3 outNormal )
{
    MorphPool::loadVertex( _meshBatchIndex, _skinSlot, vertexID, restPosition, restNormal, outPosition, outNormal );
    bool  bAnimated;
    uint  firstElement;
    uint  secondElement;
    float alpha;
    computeVertexAnimationElements( vertexID, bAnimated, firstElement, secondElement, alpha );
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
 * @brief 로컬 노멀을 월드로 옮겨 정규화한다. 노멀을 내보내는 **모든** 지오메트리 패스(forward_lit · gbuffer · gbuffernormal)가 이것을 쓴다.
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

static uint InstanceData::resolveID( uint instanceSlot )
{
    return InstanceData::resolveEntry( instanceSlot ) & kVisibleEntryIDMask;
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
    const uint element = entry & kVisibleEntryIDMask;
    if ( g_SwInstancesIndex != kInvalidIndex && element < g_SwInstanceCount )
    {
        InstanceData instance = g_SwInstances[element];
        // LOD 크로스페이드 중인 항목은 인스턴스의 등장 비율 대신 상보 디더 값을 받는다(8-1 절). 코드가 0 이면 그대로다.
        instance._appearance = Appearance::computeLODFadeAppearance( instance._appearance, entry >> kVisibleEntryFadeShift );
        // 그리는 배치는 LOD 그룹 머리 + 단계다 — 정점 풀 · 모프(스킨) 시작은 단계 배치 행이 든다. 단계가 0 이면 그대로다.
        instance._meshBatchIndex += ( entry >> kVisibleEntryLevelShift ) & kVisibleEntryLevelMask;
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
    instance._highlight            = 0u;            // 강조 없음
    instance._appearance           = 1.0f;          // 다 보인다
    instance._skinSlot             = kInvalidIndex; // 스킨 결과 없음
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
//      쓰는 셰이더: VS 출력 구조체에 `SW_APPEARANCE_VARYING`, VS 끝에 `SW_APPEARANCE_WRITE( output, instance )`, PS 의 출력을 다 만든 **뒤**(반환 직전)에
//      `SW_APPEARANCE_CLIP( input )`. Vulkan 1.2 타깃의 `clip` 은 OpKill 이라 버린 픽셀이 쿼드 이웃의 미분(ddx · Sample 의 밉)에서 빠진다 — 텍스처 읽기 앞에서
//      버리면 이웃의 밉이 틀린다. 1.3 의 demote(도움 호출로 남기기)를 요구하지 않으려고 순서로 막는다(constant::render::kVulkanRequiredMinor).
//      순서는 커밋 훅의 린트(Scripts/lint/ShaderKillOrder.py)가 본다 — 버리기 뒤에 미분을 쓰는 읽기 · 호출이 있는 함수는 커밋되지 않는다.
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
 *          비지도 않고 화면을 나눈다. LOD 크로스페이드가 새 단계에 p, 옛 단계에 p - 1 을 준다(`Appearance::computeLODFadeAppearance`).
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
static float Appearance::computeLODFadeAppearance( float instanceAppearance, uint fadeCode )
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
//      셰이더로 물러나 그려진다. 정점에서 필요한 값(sprite_2d 의 uvRect)은 픽셀에서 적용한다(아핀이면 결과가 같다).
//      RenderPassGPUTest.SpriteDrawsWithTheSpriteShader 가 GL 에서 확인한다.
// ------------------------------------------------------------------------------
static uint MaterialBuffer::clampIndex( uint index )
{
    const uint materialCount = MaterialBuffer::getDrawCount();
    return ( materialCount == 0 ) ? 0 : min( index, materialCount - 1 );
}
// 원소 크기는 16 바이트의 배수, 3 · 4 성분 벡터는 16 바이트 경계에서 시작해야 한다(DX 의 촘촘한 배치 = GL std430 — ShaderBindingValidator 가 막는다).
// 모자라면 끝에 `uint _swPad#;` 을 둔다(머티리얼 속성이 아니다 — Material::fillPropertiesFromSchema 가 건너뛴다).
#define SW_MATERIAL_BEGIN struct MaterialData
#define SW_MATERIAL_END \
    ;                   \
    SW_DECLARE_STRUCTURED_BUFFER( MaterialData, g_SwMaterials, SW_SLOT_MATERIAL_BUFFER );
// 매크로로 남긴 이유: `MaterialData` 는 셰이더마다 선언이 다른 구조체라 함수의 반환형을 적을 수 없다(HLSL 에 템플릿이 없다). 원소 참조를 그대로 돌려주는 자리가 매크로뿐이다.
#define SW_MATERIAL( index ) g_SwMaterials[MaterialBuffer::clampIndex( index )]

#include "binding_texture.hlsli" // 10 · 11 절
#include "binding_output.hlsli"  // 12 절

#endif // SW_ENGINE_BINDING_HLSLI
