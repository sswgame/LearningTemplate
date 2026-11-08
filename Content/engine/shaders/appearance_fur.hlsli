/**
 * appearance_fur.hlsli — 외형 머티리얼(appearance_lit.hlsl)의 털 껍질(MATERIAL_FUR_SHELL).
 *
 * 껍질 렌더링: 같은 메시를 껍질 수만큼 겹쳐 그리고(씬의 메시 컴포넌트 여럿 — 같은 머티리얼이라 배치 하나), 껍질마다 노멀 방향으로 밀어 낸 뒤 가닥 자리만 남기고 오린다
 * (alphaMode Mask — 깊이 · 그림자도 같은 자리를 오린다). 지느러미(fin)는 두지 않았다 — 실루엣이 가늘게 비지만 기하 셰이더나 메시 가공 없이 데이터만으로 된다.
 * 가장 값싼 길을 고른 까닭: 털 카드는 아티스트 메시가, 가닥(언리얼 Groom)은 시뮬레이션 · 전용 렌더가 든다. 반사는 머리결 엽(Kajiya · Kay, shading_model.hlsli)이다.
 * 인스턴스 값(MeshComponent `_customData0`): `.y` 껍질 비율(0 바탕 피부 · 1 털 끝), `.z` 밀어 낼 거리(m). 바탕 껍질(.y = 0)은 오리지 않는다.
 * 머티리얼 칸(털에서의 뜻): `_patternScale` 가닥 수 / UV 1, `_patternWidth` 가닥 뿌리 반지름(칸 비), `_sheenColor` 털 반사 색, `_strandShift` 반사를 뿌리 쪽으로 옮기는 정도.
 */

#ifndef SW_ENGINE_APPEARANCEFUR_HLSLI
#define SW_ENGINE_APPEARANCEFUR_HLSLI

struct AppearanceFur
{
    static void apply( inout AppearanceSurface surface, PixelContext context );
};

#if defined( MATERIAL_FUR_SHELL )
static void AppearanceFur::apply( inout AppearanceSurface surface, PixelContext context )
{
    const float  shell        = saturate( context._user._instanceData0.y );
    const float2 position     = context._uv * surface._patternScale;
    const float2 cell         = floor( position );
    const float  strandHash   = AppearanceNoise::hash( cell );
    const float2 center       = float2( 0.3f, 0.3f ) + 0.4f * float2( strandHash, AppearanceNoise::hash( cell + float2( 13.0f, 29.0f ) ) );
    const float  strandLength = lerp( 0.55f, 1.0f, AppearanceNoise::hash( cell + float2( 71.0f, 3.0f ) ) );
    // 가닥은 끝으로 갈수록 가늘다. 끝을 넘은 껍질에는 가닥이 없다.
    const float radius = surface._patternWidth * saturate( 1.0f - shell / strandLength );
    const float gap    = length( position - cell - center );
    const float edge   = max( fwidth( gap ), 1e-4f );
    const float strand = 1.0f - smoothstep( radius - edge, radius + edge, gap );
    surface._albedo.a  = ( shell <= 0.0f ) ? surface._albedo.a : surface._albedo.a * strand;
    // 뿌리 쪽은 둘레 털에 가려 어둡다(값싼 자기 그림자).
    surface._albedo.rgb *= lerp( 0.35f, 1.0f, shell );
    surface._strandColor = surface._sheenColor * shell;
}
#else
static void AppearanceFur::apply( inout AppearanceSurface surface, PixelContext context )
{
}
#endif

#endif // SW_ENGINE_APPEARANCEFUR_HLSLI
