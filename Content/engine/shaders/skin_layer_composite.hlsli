/**
 * @file skin_layer_composite.hlsli
 * @brief 피부 층(화장 · 문신 · 흉터 · 바디 페인트) 굽기의 공용 함수입니다. 캐릭터마다 층 목록이 바뀔 때만 UV 공간 텍스처 한 장에 차례로 그린다 — 피부 머티리얼은 구운 한 장만 읽는다.
 * @details C++ 기준 구현은 `SkinLayerComposite` · `SkinLayerPlacement` · `SkinLayerSmearState`(GameFramework Appearance/SkinLayer.h)이고 식이 같다(테스트가 그쪽을 본다).
 *          층 하나 = 마스크 × 색 × 불투명도 × (1 − 번짐 묽음), 번짐 이동만큼 옮겨 읽는다. 배치 층은 피부 UV → 층 UV 로 바꾸고 영역 경계 밖은 덮지 않는다.
 *          굽기 패스는 `layer_bake.hlsl`(층마다 전체 화면 한 번 — `FramePassExecutor::drawTextureLayerBakes`), 열쇠 · 캐시는 `SkinLayerBakeBinding` 이다.
 */
#ifndef SW_ENGINE_SKINLAYERCOMPOSITE_HLSLI
#define SW_ENGINE_SKINLAYERCOMPOSITE_HLSLI

static const uint kSkinLayerBlendNormal   = 0;
static const uint kSkinLayerBlendMultiply = 1;
static const uint kSkinLayerBlendOverlay  = 2;

struct SkinLayerComposite
{
    static float applyOverlay( float below, float layer ) { return below < 0.5f ? 2.0f * below * layer : 1.0f - 2.0f * ( 1.0f - below ) * ( 1.0f - layer ); }

    /** @brief 섞기 한 번 — lerp( 아래, 섞은 색, 덮임 ). */
    static float3 blend( float3 below, float3 layer, float coverage, uint mode )
    {
        float3 blended = layer;
        if ( mode == kSkinLayerBlendMultiply )
            blended = below * layer;
        else if ( mode == kSkinLayerBlendOverlay )
            blended = float3( applyOverlay( below.x, layer.x ), applyOverlay( below.y, layer.y ), applyOverlay( below.z, layer.z ) );
        return lerp( below, blended, saturate( coverage ) );
    }

    /** @brief 피부 UV → 층 UV(배치 층). R(−θ) · (uv − 이동) ÷ 크기 + 0.5, 대칭 짝은 u 를 뒤집고 결과 u 도 뒤집는다. */
    static float2 toLayerUV( float2 skinUV, float2 offset, float2 scale, float rotationDegrees, bool bMirrorCopy )
    {
        const float2 source  = bMirrorCopy ? float2( 1.0f - skinUV.x, skinUV.y ) : skinUV;
        const float2 local   = source - offset;
        const float  radians = -rotationDegrees * 0.0174532925f;
        const float  cosine  = cos( radians );
        const float  sine    = sin( radians );
        const float2 rotated = float2( local.x * cosine - local.y * sine, local.x * sine + local.y * cosine );
        const float2 layerUV = rotated / max( scale, float2( 0.01f, 0.01f ) ) + 0.5f;
        return bMirrorCopy ? float2( 1.0f - layerUV.x, layerUV.y ) : layerUV;
    }

    /** @brief 층 UV 가 [0,1]² 안이고 피부 UV 가 영역 경계 안이면 1, 아니면 0 입니다. */
    static float computeInside( float2 layerUV, float2 skinUV, float2 regionMin, float2 regionMax )
    {
        const bool bInsideLayer  = all( layerUV >= 0.0f ) && all( layerUV <= 1.0f );
        const bool bInsideRegion = all( skinUV >= regionMin ) && all( skinUV <= regionMax );
        return bInsideLayer && bInsideRegion ? 1.0f : 0.0f;
    }
};

#endif
