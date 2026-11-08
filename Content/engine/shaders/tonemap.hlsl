#include "binding.hlsli"
#include "fullscreen.hlsli"
#include "post_grade.hlsli"

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float3 source = PassTexture::sampleSource( input._uv ).rgb;
    // 표면 값을 보이는 보기 모드(Normals · Depth · Overdraw)는 색을 바꾸지 않는다 — 뷰마다라 퍼뮤테이션이 아니라 패스 플래그다.
    // 고르기는 early-return 이 아니라 값 선택이다(post_chain 과 같은 이유).
    const bool bSkipTonemap = ( g_Flags & SW_PASS_FLAG_SKIP_TONEMAP ) != 0u;
    // 뷰의 후처리 값(렌즈 · 노출 · 그레이딩 · LUT · 그레인 · 채도 · 대비 · 비네트 · 레터박스)은 후처리를 끈 뷰(CCTV)에는 걸지 않는다 — 톤맵만 한다.
    const bool   bSkipPost = ( g_Flags & SW_PASS_FLAG_SKIP_POST ) != 0u;
    const float3 lensed    = bSkipPost ? source : CameraGrade::applyLens( input._uv, g_SourceTexel.xy, source );
    const float3 exposed   = bSkipPost ? source : CameraGrade::applyColorGrading( CameraGrade::applyExposure( lensed ) );
    const float3 mapped    = exposed / ( exposed + 1.0f );
    const float3 graded    = bSkipPost ? mapped : CameraGrade::apply( input._uv, CameraGrade::applyFilmGrain( input._position.xy, CameraGrade::applyLut( mapped ) ) );
    return float4( bSkipTonemap ? source : graded, 1.0f );
}
