/**
 * material_shader.hlsli — 머티리얼 셰이더 뼈대의 1단: 훅이 주고받는 형식과 훅 선택 검사.
 *
 * 엔진이 정점 · 픽셀 진입점(VSMain / PSMain)을 가지고(material_shader_main.hlsli), 머티리얼 셰이더 파일은 **훅만 정의한다**.
 * 위치를 만드는 코드는 뼈대 한 곳뿐이다 — 훅이 클립 위치를 직접 만들 수 없으므로 깊이 프리패스 · 그림자 · 본 패스의 깊이가 같은 비트로 남는다.
 *
 * 셰이더 파일의 순서(순서가 계약이다)
 *   1. `#include "lighting.hlsli"`
 *   2. `SW_MATERIAL_BEGIN { … } SW_MATERIAL_END`        머티리얼 구조체(.material 의 프로퍼티와 이름 · 순서가 같다)
 *   3. 선택 매크로                                       아래 "훅 선택" 표
 *   4. 사용자 보간 칸 구조체 `MaterialVaryings`          SW_MATERIAL_CUSTOM_VARYINGS 일 때만
 *   5. `#include "material_shader.hlsli"`                 이 파일 — 훅의 입출력 형식
 *   6. 훅 정의
 *   7. `#include "material_shader_main.hlsli"`            진입점 뼈대
 *
 * 훅 다섯 — 있으면 쓰고 없으면 기본이라는 폴백은 없다. 훅마다 정의하거나 기본을 명시해 고른다.
 *   표면      `MaterialSurface computeMaterialSurface( PixelContext context )`         기본 없음 — 머티리얼마다 정의한다
 *   조명 모델 `float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )`
 *             rgb 는 조명을 받은 색, a 는 알파다(알파를 쓰는지는 뼈대가 MATERIAL_BLEND_TRANSLUCENT 로 정한다).
 *             SW_MATERIAL_CUSTOM_LIGHTING(훅 정의) · SW_MATERIAL_DEFAULT_LIGHTING(PBR) · SW_MATERIAL_UNLIT(표면 색 그대로) 중 하나.
 *             SW_MATERIAL_RIM_LIGHT 는 기본 PBR 에 포워드 림 라이트를 더한다(선택이 아니라 덧붙임이다).
 *             SW_MATERIAL_EMISSIVE 는 기본 PBR 에 `float3 computeMaterialEmissive( PixelContext context )` 훅의 빛을 더한다(덧붙임 — G버퍼에 칸이 없어 머티리얼이 MATERIAL_FORWARD_ONLY 를 함께 적는다).
 *   월드 위치 오프셋 `float3 computeWorldPositionOffset( VertexContext context )`        MATERIAL_VERTEX_DEFORM 퍼뮤테이션에서만 부른다
 *   사용자 보간 칸  `MaterialVaryings computeMaterialVaryings( VertexContext context )`
 *             SW_MATERIAL_CUSTOM_VARYINGS(구조체 + 훅 정의) · SW_MATERIAL_DEFAULT_VARYINGS(칸 없음) 중 하나. 시맨틱은 TEXCOORD8 ~ TEXCOORD14.
 *   외곽선    `OutlineOffset computeMaterialOutline( VertexContext context )`  메시 외곽선 패스(SW_PASS_MESH_OUTLINE)에서만 부른다.
 *   왜곡      `float2 computeMaterialDistortion( PixelContext context, MaterialSurface surface )`  화면 UV 오프셋. 왜곡 누적 패스(SW_PASS_DISTORTION)에서만 부른다 —
 *             그 패스는 머티리얼 define MATERIAL_DISTORTION 이 있는 배치만 그리므로 그 define 을 적는 셰이더만 SW_MATERIAL_CUSTOM_DISTORTION 으로 정의한다(본보기 distortion.hlsl).
 * 선택이 없거나 겹치면 이 파일의 `#error` 가 멈춘다. 훅 정의가 없는데 선택했으면 컴파일러가 미선언 식별자로 멈춘다.
 *
 * 머티리얼은 **한 단계에서만 읽는다**(GL 은 구조버퍼를 정점 · 픽셀 두 단계가 읽으면 링크를 거절한다 — binding.hlsli 9 절).
 * 읽는 단계는 형식이 정한다. 기본은 픽셀이고, `SW_MATERIAL_READ_IN_VERTEX` 를 정의하면 정점이다. 안 읽는 단계의 문맥에는 `material` 멤버가 없어서
 * 거기서 머티리얼을 읽으려는 코드는 컴파일이 안 된다. 정점에서 읽는 셰이더는 픽셀이 쓸 값을 사용자 보간 칸으로 넘긴다.
 *
 * 셰이더 안의 기능 스위치(외부 define 이 아니다 — 쿠킹 퍼뮤테이션 수가 늘지 않는다)
 *   SW_MATERIAL_READ_IN_VERTEX   머티리얼을 정점에서 읽는다
 *   SW_MATERIAL_USES_TANGENT     정점 탄젠트를 월드로 옮겨 `PixelContext._tangent` 로 넘긴다(노멀 맵)
 *   SW_MATERIAL_NONE             머티리얼이 없는 사용자(gbuffer · shadow_depth — 머티리얼 없는 배치의 패스 셰이더). 머티리얼 구조체 · 읽기 · 위치 오프셋 훅이 없고,
 *                                알파 컷오프(MATERIAL_ALPHA_CUTOFF)는 쓸 수 없다. 깊이 패스에서는 이 셰이더가 같은 자리를 못 만드는 배치를 뼈대가 빼 준다
 *                                (깊이 프리패스의 MATERIAL_VERTEX_DEFORM · 그림자의 MATERIAL_SHADOW_CAST_OFF).
 *
 * 깊이 패스(SW_PASS_SHADOW · SW_PASS_DEPTH_PREPASS)에서 알파 컷오프가 아니면 정점 → 픽셀 칸은 위치(와 등장 디더)뿐이다 — 그림자 맵은 패스마다 모든 메시를 그리므로
 * 쓰이지 않는 색 · 노멀 · 사용자 칸을 정점 단계가 내보내지 않는다.
 *
 * 훅 안에서 이른 반환(중간 `return`)을 쓰지 않는다. 뼈대는 출구가 하나이고 훅이 인라인되어도 그래야 한다 — VFX V5 에서 드라이버가 이른 반환 모양을 틀리게
 * 컴파일했을 가능성이 있다. 형식으로 막지 못하는 틈이라 리뷰가 본다.
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

// 그림자를 끈 머티리얼(풀 · 꽃 카드)의 그림자는 패스 셰이더가 그린다 — 뼈대가 배치를 한 점으로 모아 버리므로(`excludeFromDepthPass`) 오릴 표면이 없다.
// 그 머티리얼이 알파 컷오프도 가지면(잎 카드) 컷오프를 지운다 — 남기면 머티리얼 없는 셰이더가 표면을 읽으려다 컴파일이 안 된다(아래 검사).
#if defined( SW_MATERIAL_NONE ) && defined( SW_PASS_SHADOW ) && defined( MATERIAL_SHADOW_CAST_OFF )
    #undef MATERIAL_ALPHA_CUTOFF
#endif

#if defined( SW_MATERIAL_DEPTH_PASS ) && !defined( MATERIAL_ALPHA_CUTOFF )
    #define SW_MATERIAL_DEPTH_ONLY 1 // 깊이만 쓰고 픽셀이 읽을 것이 없다 — 보간 칸이 위치뿐이다
#endif

// 위치 스트림만 읽는 변형(언리얼의 위치 전용 정점 스트림) — 깊이만 쓰고 정점 변형 훅도 없으면 위치 말고 읽을 속성이 없다. DX 는 선언한 정점 입력을 모두
// 입력 레이아웃에 넣으므로 `VertexInput` 을 받으면 쓰지 않아도 속성 스트림(슬롯 2)을 읽는다 — 그래서 입력 구조를 바꾼다. 정점 변형 훅(지형 지오모프 ·
// 바람 · 파도)은 깊이 패스에서도 색 · UV · 탄젠트를 읽으므로 전체 입력을 받는다(그림자가 본 패스와 같은 자리여야 한다).
// 변위(MATERIAL_DISPLACEMENT)는 깊이 패스에서도 노멀 · UV 로 표면을 옮기므로 전체 입력을 받는다.
#if defined( SW_MATERIAL_DEPTH_ONLY ) && !defined( MATERIAL_VERTEX_DEFORM ) && !defined( MATERIAL_DISPLACEMENT )
    #define SW_MATERIAL_POSITION_ONLY 1
#endif

// 변위 머티리얼(material_displacement.hlsli)이 받지 않는 조합 — 정점 출력이 보간 칸이 아니라 제어점이라 아래 셋은 그 자리에 칸이 없다.
#if defined( MATERIAL_DISPLACEMENT ) && defined( SW_MATERIAL_NONE )
    #error "MATERIAL_DISPLACEMENT is drawn by the material shader in every pass (FrameRendererUtil::drawsDepthWithMaterialShader) - a material-less shader cannot displace"
#endif
#if defined( MATERIAL_DISPLACEMENT ) && defined( SW_MATERIAL_CUSTOM_VARYINGS )
    #error "MATERIAL_DISPLACEMENT does not carry custom varyings through the hull and domain stages - use SW_MATERIAL_DEFAULT_VARYINGS"
#endif
#if defined( MATERIAL_DISPLACEMENT ) && defined( SW_PASS_MESH_OUTLINE )
    #error "MATERIAL_DISPLACEMENT has no outline offset in the domain stage - do not enable MATERIAL_OUTLINE on a displaced material"
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
#if defined( SW_PASS_DISTORTION ) && !defined( SW_MATERIAL_CUSTOM_DISTORTION )
    #error "the distortion pass draws MATERIAL_DISTORTION materials only: define SW_MATERIAL_CUSTOM_DISTORTION and computeMaterialDistortion in a shader whose material lists MATERIAL_DISTORTION"
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

#if defined( SW_MATERIAL_POSITION_ONLY )
typedef VertexPositionInput MaterialVertexInput; // 위치 스트림만 — 1 절
#else
typedef VertexInput MaterialVertexInput;
#endif

// ------------------------------------------------------------------------------
// 3) 문맥 — 훅이 읽는 값. 정점 문맥에는 입력 스트림 · 인스턴스 · 변형이 끝난 위치와 노멀이 있다.
// ------------------------------------------------------------------------------
struct VertexContext
{
    VertexInput  _input; ///< 입력 스트림 그대로(색 · UV · 탄젠트)
    InstanceData _instance;
    uint         _vertexID;
    float3       _localPosition; ///< 모프 · 스킨 · VAT 가 적용된 로컬 위치
    float3       _localNormal;
    float3       _worldPosition; ///< 뼈대가 만든 월드 위치(월드 위치 오프셋을 더하기 전)
    float3       _worldNormal;
    float4       _clipPosition; ///< 월드 위치 오프셋 · 외곽선이 반영된 최종 클립 위치. **사용자 보간 칸 훅에서만 채워진다**(다른 훅에서는 0). 읽기만 한다 — 위치는 뼈대가 만든다
#if defined( SW_MATERIAL_READ_IN_VERTEX )
    MaterialData _material; ///< 머티리얼을 정점에서 읽는 셰이더에만 있다
#endif

    // 정점 진입점의 단계(material_shader_main.hlsli). 패스 · 기능 스위치마다 다른 정의를 파일 수준 `#if` 가 고른다 — 진입점 본문에는 `#if` 가 없다.
    void load( MaterialVertexInput input, uint instanceSlot, uint vertexID, out float4 outWorldPosition ); // 입력 · 인스턴스 · 변형 정점으로 문맥을 채운다(두 VSMain 이 함께 쓴다)
    void readMaterial();                                                                                   // 정점에서 읽는 셰이더만 읽는다
    float4 applyWorldPositionOffset( float4 worldPosition );                                               // 위치 오프셋 훅(MATERIAL_VERTEX_DEFORM)
    float4 computeClipPosition( inout float4 inoutWorldPosition );                                         // 오프셋 · 외곽선을 더하고 클립 위치를 만든다
    static float4 excludeFromDepthPass( float4 clipPosition );                                             // 이 셰이더가 같은 자리를 못 만드는 깊이 패스 배치를 뺀다
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
    #if SW_WRITES_VELOCITY
    float4 _velocityClip : TEXCOORD5;     // 이번 클립 위치(화면 속도의 이번 자리 — SV_POSITION 은 픽셀 단계에서 화면 좌표라 따로 넘긴다)
    float4 _velocityPrevClip : TEXCOORD6; // 지난 클립 위치(지난 월드 행렬 · 스킨 결과의 지난 벌 · g_PrevViewProj)
    #endif
#endif
    SW_APPEARANCE_VARYING   // 등장 디더 변형만 등장 비율을 넘긴다(binding.hlsli 8-1)
        SW_LODDEBUG_VARYING // 보기 모드 LODLevel · LODFade 만 가시 항목을 넘긴다(binding.hlsli 보기 모드 절)

        // 정점 진입점이 칸을 채우는 단계와 픽셀 진입점의 본문(material_shader_main.hlsli). 패스 · 기능 스위치에 맞는 정의만 있다.
        void
        storeVaryings( VertexContext context, float4 vertexWorldPosition, float4 vertexClipPosition );
    void storeTangent( VertexContext context );
    void storeMaterialIndex( VertexContext context );
    void storeUserVaryings( VertexContext context, float4 vertexClipPosition );
    void storeVelocity( VertexContext context, uint instanceSlot, float4 restWorldPosition, float4 vertexWorldPosition, float4 vertexClipPosition ); // 화면 속도의 이번 · 지난 클립 위치
    void storeSurfaceVelocity();                                                                                                                     // 픽셀 단계 — 보간한 두 위치로 g_SurfaceVelocity 를 정한다
    SurfaceOutput shadePixel( bool bFrontFace );                                                                                                     // 본 패스 · G버퍼
    void shadeDepthPixel( bool bFrontFace );                                                                                                         // 깊이 패스
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
    void readMaterial( MaterialInterpolants input );                 // 픽셀에서 읽는 셰이더만 읽는다
    static void addRimLight( float3 normal, inout float3 inoutLit ); // SW_MATERIAL_RIM_LIGHT 의 단계
    void addEmissive( inout float3 inoutLit );                       // SW_MATERIAL_EMISSIVE 의 단계
    float4 computeSelectedLighting( MaterialSurface surface );       // 조명 훅의 선택(훅 정의 · 기본 PBR · 조명 없음)을 푼다
    SurfaceOutput storeSurface( MaterialSurface surface );           // 패스가 원하는 출력(포워드 색 · G버퍼 셋)으로 낸다
};

#endif // SW_ENGINE_MATERIALSHADER_HLSLI
