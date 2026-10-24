/**
 * materialshader.hlsli — 머티리얼 셰이더 뼈대의 1단: 훅이 주고받는 형식과 훅 선택 검사.
 *
 * 엔진이 정점 · 픽셀 진입점(VSMain / PSMain)을 가지고(materialshadermain.hlsli), 머티리얼 셰이더 파일은 **훅만 정의한다**.
 * 위치를 만드는 코드는 뼈대 한 곳뿐이다 — 훅이 클립 위치를 직접 만들 수 없으므로 깊이 프리패스 · 그림자 · 본 패스의 깊이가 같은 비트로 남는다.
 *
 * 셰이더 파일의 순서(순서가 계약이다)
 *   1. `#include "lighting.hlsli"`
 *   2. `SW_MATERIAL_BEGIN { … } SW_MATERIAL_END`        머티리얼 구조체(.material 의 프로퍼티와 이름 · 순서가 같다)
 *   3. 선택 매크로                                       아래 "훅 선택" 표
 *   4. 사용자 보간 칸 구조체 `MaterialVaryings`          SW_MATERIAL_CUSTOM_VARYINGS 일 때만
 *   5. `#include "materialshader.hlsli"`                 이 파일 — 훅의 입출력 형식
 *   6. 훅 정의
 *   7. `#include "materialshadermain.hlsli"`            진입점 뼈대
 *
 * 훅 다섯 — 있으면 쓰고 없으면 기본이라는 폴백은 없다. 훅마다 정의하거나 기본을 명시해 고른다.
 *   표면      `MaterialSurface computeMaterialSurface( PixelContext context )`         기본 없음 — 머티리얼마다 정의한다
 *   조명 모델 `float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )`
 *             rgb 는 조명을 받은 색, a 는 알파다(알파를 쓰는지는 뼈대가 MATERIAL_BLEND_TRANSLUCENT 로 정한다).
 *             SW_MATERIAL_CUSTOM_LIGHTING(훅 정의) · SW_MATERIAL_DEFAULT_LIGHTING(PBR) · SW_MATERIAL_UNLIT(표면 색 그대로) 중 하나.
 *   월드 위치 오프셋 `float3 computeWorldPositionOffset( VertexContext context )`        MATERIAL_VERTEX_DEFORM 퍼뮤테이션에서만 부른다
 *   사용자 보간 칸  `MaterialVaryings computeMaterialVaryings( VertexContext context )`
 *             SW_MATERIAL_CUSTOM_VARYINGS(구조체 + 훅 정의) · SW_MATERIAL_DEFAULT_VARYINGS(칸 없음) 중 하나. 시맨틱은 TEXCOORD8 ~ TEXCOORD14.
 *   외곽선    `OutlineOffset computeMaterialOutline( VertexContext context )`  메시 외곽선 패스(SW_PASS_MESH_OUTLINE)에서만 부른다.
 * 선택이 없거나 겹치면 이 파일의 `#error` 가 멈춘다. 훅 정의가 없는데 선택했으면 컴파일러가 미선언 식별자로 멈춘다.
 *
 * 머티리얼은 **한 단계에서만 읽는다**(GL 은 구조버퍼를 정점 · 픽셀 두 단계가 읽으면 링크를 거절한다 — binding.hlsli 9 절).
 * 읽는 단계는 형식이 정한다. 기본은 픽셀이고, `SW_MATERIAL_READ_IN_VERTEX` 를 정의하면 정점이다. 안 읽는 단계의 문맥에는 `material` 멤버가 없어서
 * 거기서 머티리얼을 읽으려는 코드는 컴파일이 안 된다. 정점에서 읽는 셰이더는 픽셀이 쓸 값을 사용자 보간 칸으로 넘긴다.
 *
 * 셰이더 안의 기능 스위치(외부 define 이 아니다 — 쿠킹 퍼뮤테이션 수가 늘지 않는다)
 *   SW_MATERIAL_READ_IN_VERTEX   머티리얼을 정점에서 읽는다
 *   SW_MATERIAL_USES_TANGENT     정점 탄젠트를 월드로 옮겨 `PixelContext._tangent` 로 넘긴다(노멀 맵)
 *   SW_MATERIAL_NONE             머티리얼이 없는 사용자(gbuffer · shadowdepth — 머티리얼 없는 배치의 패스 셰이더). 머티리얼 구조체 · 읽기 · 위치 오프셋 훅이 없고,
 *                                알파 컷오프(MATERIAL_ALPHA_CUTOFF)는 쓸 수 없다. 깊이 패스에서는 이 셰이더가 같은 자리를 못 만드는 배치를 뼈대가 빼 준다
 *                                (깊이 프리패스의 MATERIAL_VERTEX_DEFORM · 그림자의 MATERIAL_SHADOW_CAST_OFF).
 *
 * 깊이 패스(SW_PASS_SHADOW · SW_PASS_DEPTH_PREPASS)에서 알파 컷오프가 아니면 정점 → 픽셀 칸은 위치(와 등장 디더)뿐이다 — 그림자 맵은 패스마다 모든 메시를 그리므로
 * 쓰이지 않는 색 · 노멀 · 사용자 칸을 정점 단계가 내보내지 않는다.
 *
 * 훅 안에서 이른 반환(중간 `return`)을 쓰지 않는다. 뼈대는 출구가 하나이고 훅이 인라인되어도 그래야 한다 — Vfx V5 에서 드라이버가 이른 반환 모양을 틀리게
 * 컴파일했을 가능성이 있다(docs/plans/Vfx.md V4 남은 확인). 형식으로 막지 못하는 틈이라 리뷰가 본다.
 */

#ifndef SW_ENGINE_MATERIALSHADER_HLSLI
#define SW_ENGINE_MATERIALSHADER_HLSLI

#include "lighting.hlsli"

// ------------------------------------------------------------------------------
// 1) 패스 종류와 훅 선택 검사
// ------------------------------------------------------------------------------
#if defined( SW_PASS_SHADOW ) || defined( SW_PASS_DEPTH_PREPASS )
    #define SW_MATERIAL_DEPTH_PASS 1
#endif

#if defined( SW_PASS_SHADOW ) && defined( SW_PASS_DEPTH_PREPASS )
    #error "SW_PASS_SHADOW and SW_PASS_DEPTH_PREPASS are exclusive pass kinds"
#endif

#if defined( SW_MATERIAL_DEPTH_PASS ) && !defined( MATERIAL_ALPHA_CUTOFF )
    #define SW_MATERIAL_DEPTH_ONLY 1 // 깊이만 쓰고 픽셀이 읽을 것이 없다 — 보간 칸이 위치뿐이다
#endif

#if defined( SW_MATERIAL_NONE )
    #if defined( SW_MATERIAL_READ_IN_VERTEX )
        #error "SW_MATERIAL_NONE has no material to read: SW_MATERIAL_READ_IN_VERTEX is invalid"
    #endif
    #if defined( MATERIAL_ALPHA_CUTOFF )
        #error "SW_MATERIAL_NONE has no material alpha: MATERIAL_ALPHA_CUTOFF must be drawn by the material shader (FrameRendererUtil::drawsDepthWithMaterialShader)"
    #endif
    #if !defined( SW_PASS_GBUFFER ) && !defined( SW_MATERIAL_DEPTH_PASS )
        #error "a material-less shader draws only the G-buffer or a depth pass (SW_PASS_GBUFFER, SW_PASS_SHADOW, SW_PASS_DEPTH_PREPASS)"
    #endif
#endif

// 조건식 안의 `defined` 산술은 컴파일러마다 다르므로(FXC) 조합을 풀어 쓴다.
#if !defined( SW_MATERIAL_DEFAULT_LIGHTING ) && !defined( SW_MATERIAL_UNLIT ) && !defined( SW_MATERIAL_CUSTOM_LIGHTING )
    #error "pick one lighting hook: SW_MATERIAL_CUSTOM_LIGHTING (define computeMaterialLighting), SW_MATERIAL_DEFAULT_LIGHTING or SW_MATERIAL_UNLIT"
#endif
#if ( defined( SW_MATERIAL_DEFAULT_LIGHTING ) && defined( SW_MATERIAL_UNLIT ) ) || ( defined( SW_MATERIAL_DEFAULT_LIGHTING ) && defined( SW_MATERIAL_CUSTOM_LIGHTING ) ) || \
    ( defined( SW_MATERIAL_UNLIT ) && defined( SW_MATERIAL_CUSTOM_LIGHTING ) )
    #error "pick exactly one lighting hook: SW_MATERIAL_CUSTOM_LIGHTING, SW_MATERIAL_DEFAULT_LIGHTING and SW_MATERIAL_UNLIT are exclusive"
#endif

#if !defined( SW_MATERIAL_DEFAULT_VARYINGS ) && !defined( SW_MATERIAL_CUSTOM_VARYINGS )
    #error "pick one varyings hook: SW_MATERIAL_CUSTOM_VARYINGS (define MaterialVaryings and computeMaterialVaryings) or SW_MATERIAL_DEFAULT_VARYINGS"
#endif
#if defined( SW_MATERIAL_DEFAULT_VARYINGS ) && defined( SW_MATERIAL_CUSTOM_VARYINGS )
    #error "pick exactly one varyings hook: SW_MATERIAL_CUSTOM_VARYINGS and SW_MATERIAL_DEFAULT_VARYINGS are exclusive"
#endif

// ------------------------------------------------------------------------------
// 2) 사용자 보간 칸 — 기본이면 이 파일이 빈 `MaterialVaryings` 를 둔다(FXC · DXC DXIL · DXC SPIR-V 가 빈 구조체 · 중첩 구조체 보간 칸을 받는다, 확인함).
//    사용자 보간 칸 셰이더(SW_MATERIAL_CUSTOM_VARYINGS)는 같은 이름의 구조체를 직접 둔다 — 타입 별칭 매크로가 없다.
// ------------------------------------------------------------------------------
#if defined( SW_MATERIAL_DEFAULT_VARYINGS )
struct MaterialVaryings
{
};
#endif

// ------------------------------------------------------------------------------
// 3) 문맥 — 훅이 읽는 값. 정점 문맥에는 입력 스트림 · 인스턴스 · 변형이 끝난 위치와 노멀이 있다.
// ------------------------------------------------------------------------------
struct VertexContext
{
    VertexInput  _input; ///< 입력 스트림 그대로(색 · UV · 탄젠트)
    InstanceData _instance;
    uint         _vertexId;
    float3       _localPosition; ///< 모프 · 스킨 · VAT 가 적용된 로컬 위치
    float3       _localNormal;
    float3       _worldPosition; ///< 뼈대가 만든 월드 위치(월드 위치 오프셋을 더하기 전)
    float3       _worldNormal;
    float4       _clipPosition; ///< 월드 위치 오프셋 · 외곽선이 반영된 최종 클립 위치. **사용자 보간 칸 훅에서만 채워진다**(다른 훅에서는 0). 읽기만 한다 — 위치는 뼈대가 만든다
#if defined( SW_MATERIAL_READ_IN_VERTEX )
    MaterialData _material; ///< 머티리얼을 정점에서 읽는 셰이더에만 있다
#endif

    // 정점 진입점의 단계(materialshadermain.hlsli). 패스 · 기능 스위치마다 다른 정의를 파일 수준 `#if` 가 고른다 — 진입점 본문에는 `#if` 가 없다.
    void readMaterial();                                           // 정점에서 읽는 셰이더만 읽는다
    float4 applyWorldPositionOffset( float4 worldPosition );       // 위치 오프셋 훅(MATERIAL_VERTEX_DEFORM)
    float4 computeClipPosition( inout float4 inoutWorldPosition ); // 오프셋 · 외곽선을 더하고 클립 위치를 만든다
    static float4 excludeFromDepthPass( float4 clipPosition );     // 이 셰이더가 같은 자리를 못 만드는 깊이 패스 배치를 뺀다
};

#if defined( SW_PASS_MESH_OUTLINE )
/** @brief 외곽선 훅의 결과 — 월드 위치에 더할 오프셋과, 클립 위치를 만든 뒤 xy 에 더할 오프셋(화면 두께). 훅은 위치를 직접 만들지 못하고 오프셋만 준다. */
struct OutlineOffset
{
    float3 _worldOffset;
    float2 _clipOffset;
};
#endif

/** @brief 정점 → 픽셀 보간 칸. 모든 패스가 같은 칸을 쓴다(훅이 패스마다 다른 문맥을 받지 않게). 위치는 뼈대 한 곳이 만든다. */
struct MaterialInterpolants
{
    float4 _position : SV_POSITION;
#if !defined( SW_MATERIAL_DEPTH_ONLY )
    float4 _color : COLOR;
    float2 _uv : TEXCOORD0;
    float3 _normal : TEXCOORD1;
    float3 _worldPosition : TEXCOORD2;
    #if defined( SW_MATERIAL_USES_TANGENT )
    float4 _tangent : TEXCOORD3; // xyz 월드 탄젠트 · w 바이탄젠트 부호(정점 탄젠트 그대로)
    #endif
    #if !defined( SW_MATERIAL_READ_IN_VERTEX ) && !defined( SW_MATERIAL_NONE )
    nointerpolation uint _materialIndex : TEXCOORD4; // GPUScene 인스턴스가 준 머티리얼 원소
    #endif
    MaterialVaryings _user; // 사용자 보간 칸(시맨틱은 사용자 구조체가 정한다)
#endif
    SW_APPEARANCE_VARYING // 등장 디더 변형만 등장 비율을 넘긴다(binding.hlsli 8-1)

        // 정점 진입점이 칸을 채우는 단계와 픽셀 진입점의 본문(materialshadermain.hlsli). 패스 · 기능 스위치에 맞는 정의만 있다.
        void
        storeVaryings( VertexContext context, float4 vertexWorldPosition, float4 vertexClipPosition );
    void storeTangent( VertexContext context );
    void storeMaterialIndex( VertexContext context );
    void storeUserVaryings( VertexContext context, float4 vertexClipPosition );
    SurfaceOutput shadePixel( bool bFrontFace ); // 본 패스 · G버퍼
    void shadeDepthPixel( bool bFrontFace );     // 깊이 패스
};

/**
 * @brief 표면 훅의 결과 — 알베도 · 노멀 · 거칠기 · 금속도(보기 모드 Roughness · Metallic 과 G버퍼 머티리얼 칸이 읽는다).
 * @details 알파 컷오프 머티리얼(MATERIAL_ALPHA_CUTOFF)은 `alphaCutoff` 도 채운다 — 깊이 패스와 본 패스가 같은 값으로 오린다(뼈대가 clip 한다).
 */
struct MaterialSurface
{
    float4 _albedo;
    float3 _normal;
    float  _roughness;
    float  _metallic;
#if defined( MATERIAL_ALPHA_CUTOFF )
    float _alphaCutoff;
#endif

    void setAlphaCutoff( float cutoff ); // 알파 컷오프 머티리얼만 컷오프를 싣는다(아니면 아무것도 하지 않는다)
    void applyAlphaCutoff();             // 알파 컷오프 머티리얼만 오린다(아니면 아무것도 하지 않는다)
};

#if defined( MATERIAL_ALPHA_CUTOFF )
/** @brief 알파 컷오프 머티리얼 — 표면 훅이 컷오프를 싣는다. */
void MaterialSurface::setAlphaCutoff( float cutoff )
{
    _alphaCutoff = cutoff;
}
#else
void MaterialSurface::setAlphaCutoff( float cutoff )
{
}
#endif

/** @brief 픽셀 문맥 — 보간된 값과 머티리얼. 양면 머티리얼(MATERIAL_TWO_SIDED)의 뒷면은 `normal` 이 이미 뒤집혀 있다. */
struct PixelContext
{
    float4 _position; ///< SV_POSITION(화면 픽셀 좌표 · 깊이)
    float4 _color;    ///< 정점 색
    float2 _uv;
    float3 _normal; ///< 정규화한 월드 노멀
    float3 _worldPosition;
#if defined( SW_MATERIAL_USES_TANGENT )
    float4 _tangent;
#endif
    MaterialVaryings _user;
#if !defined( SW_MATERIAL_READ_IN_VERTEX ) && !defined( SW_MATERIAL_NONE )
    MaterialData _material; ///< 머티리얼을 픽셀에서 읽는 셰이더에만 있다
#endif

    static PixelContext make( MaterialInterpolants input, bool bFrontFace );
    static float3 faceNormal( float3 normal, bool bFrontFace ); // 양면 머티리얼의 뒷면은 노멀을 뒤집는다
    void copyTangent( MaterialInterpolants input );
    void readMaterial( MaterialInterpolants input );           // 픽셀에서 읽는 셰이더만 읽는다
    float4 computeSelectedLighting( MaterialSurface surface ); // 조명 훅의 선택(훅 정의 · 기본 PBR · 조명 없음)을 푼다
    SurfaceOutput storeSurface( MaterialSurface surface );     // 패스가 원하는 출력(포워드 색 · G버퍼 셋)으로 낸다
};

#endif // SW_ENGINE_MATERIALSHADER_HLSLI
