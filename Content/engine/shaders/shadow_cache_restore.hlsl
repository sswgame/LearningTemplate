#include "binding.hlsli"
#include "fullscreen.hlsli"

/**
 * shadow_cache_restore.hlsl — 그림자 캐시 복원(RenderPassType::ShadowCacheRestore).
 *
 * 그림자 패스가 캐시한 점광 · 스폿 타일의 칸을 뷰포트로 잡고 이 풀스크린 삼각형을 그린다. 픽셀마다 캐시 층(정적 메시만 그린 깊이 — C++ ShadowCache,
 * 이 드로우 동안 ShadowMap 역할 이름으로 걸린다)의 같은 텍셀을 픽셀 깊이로 쓴다. 캐시 층과 아틀라스는 형식 · 크기가 같아 SV_Position(렌더 타깃 픽셀, 뷰포트 원점 포함)이 곧 텍셀이다.
 * 아틀라스는 패스 시작에 1 로 지워졌으므로 깊이 테스트(LessEqual)는 늘 통과한다. 그 뒤 움직이는 메시만 그 위에 그린다.
 * 깊이 복사(CopyResource)로 칸만 옮길 수는 없다 — D3D12 는 깊이 · 스텐실 자원의 부분 복사를 받지 않는다.
 */
float PSMain( PSInput input ) : SV_Depth
{
    return Bindless::loadEngineTexel( SW_ENGINE_ROLE( ShadowMap ), int2( input._position.xy ) ).r;
}
