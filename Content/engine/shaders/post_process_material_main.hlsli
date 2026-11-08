/**
 * post_process_material_main.hlsli — 후처리 머티리얼 뼈대의 2단: 풀스크린 픽셀 진입점. 셰이더 파일의 맨 끝에서 include 한다(훅 정의 뒤).
 *
 * 블렌드는 프리멀티플라이(색 One / InvSrcAlpha — `FramePSOManager` 의 확장 프로그램 상태)다. 무게 w = 블렌더블 무게(`g_PostProcessMaterial.x`) × 훅의 덮임이고,
 * 결과 = 아래 색 × (1 - w) + 머티리얼 색 × w 다 — 무게 1 이면 바꾸고, 0.5 면 반만 섞는다(언리얼 블렌더블 무게와 같은 뜻).
 */

#ifndef SW_ENGINE_POSTPROCESSMATERIALMAIN_HLSLI
#define SW_ENGINE_POSTPROCESSMATERIALMAIN_HLSLI

float4 PSMain( PSInput input ) : SV_TARGET
{
    const PixelContext context = PixelContext::make( input );
    const float4       result  = computePostProcessMaterial( context );
    const float        weight  = saturate( g_PostProcessMaterial.x * result.a );
    return float4( result.rgb * weight, weight );
}

#endif // SW_ENGINE_POSTPROCESSMATERIALMAIN_HLSLI
