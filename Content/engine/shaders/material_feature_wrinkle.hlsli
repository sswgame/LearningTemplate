/**
 * @file material_feature_wrinkle.hlsli
 * @brief 머티리얼 기능: 표정 주름 맵. 영역 마스크 두 장(RGBA × 2 = 8 영역)과 주름 가중치(C++ `WrinkleMap` 이 표정에서 구한 `_wrinkleWeightsA` · `_wrinkleWeightsB`)로
 *        주름 노멀(과 선택으로 알베도 · 캐비티)을 기본 값 위에 섞습니다.
 * @details 머티리얼 라이브러리와 따로 둔 기능 파일이다 — 피부 머티리얼이 표면 훅(`computeMaterialSurface`) 안에서 부른다. 머티리얼 구조체에 다음 칸을 둔다(이름이 C++ 과의 계약이다):
 *          `float4 _wrinkleWeightsA; float4 _wrinkleWeightsB; uint _wrinkleNormalMap; uint _wrinkleMaskA; uint _wrinkleMaskB; uint _wrinkleAlbedoMap; uint _wrinkleCavityMap;`
 *          알베도 · 캐비티 맵이 없으면 `kInvalidIndex` 로 둔다(노멀만 섞는다). 노멀은 탄젠트 공간에서 섞은 뒤 `normal_map.hlsli` 와 같은 규약으로 쓴다.
 *          언리얼 MetaHuman 의 애니메이션 주름 맵(마스크 × 커브 가중치 → 노멀 블렌드)과 같은 계산이다.
 */
#ifndef SW_ENGINE_MATERIALFEATUREWRINKLE_HLSLI
#define SW_ENGINE_MATERIALFEATUREWRINKLE_HLSLI

struct WrinkleFeature
{
    /** @brief 이 픽셀의 주름 세기(0..1)입니다. 영역 마스크 채널마다 가중치를 곱해 더하고 자른다. */
    static float computeAmount( uint maskA, uint maskB, float4 weightsA, float4 weightsB, float2 uv )
    {
        const float4 sampleA = MaterialTexture::sampleIndex( maskA, uv );
        const float4 sampleB = maskB == kInvalidIndex ? float4( 0.0f, 0.0f, 0.0f, 0.0f ) : MaterialTexture::sampleIndex( maskB, uv );
        return saturate( dot( sampleA, weightsA ) + dot( sampleB, weightsB ) );
    }

    /** @brief 기본 탄젠트 노멀 @p baseNormal 과 주름 노멀 맵을 @p amount 만큼 섞은 탄젠트 노멀입니다(정규화). */
    static float3 blendNormal( float3 baseNormal, uint wrinkleNormalMap, float amount, float2 uv )
    {
        float3 wrinkle = float3( MaterialTexture::sampleIndex( wrinkleNormalMap, uv ).xy * 2.0f - 1.0f, 0.0f );
        wrinkle.z      = sqrt( saturate( 1.0f - dot( wrinkle.xy, wrinkle.xy ) ) );
        return normalize( lerp( baseNormal, wrinkle, amount ) );
    }

    /** @brief 알베도를 주름 알베도로 섞습니다. 맵이 없으면 그대로입니다. */
    static float3 blendAlbedo( float3 baseAlbedo, uint wrinkleAlbedoMap, float amount, float2 uv )
    {
        return wrinkleAlbedoMap == kInvalidIndex ? baseAlbedo : lerp( baseAlbedo, MaterialTexture::sampleIndex( wrinkleAlbedoMap, uv ).rgb, amount );
    }

    /** @brief 주름 골의 캐비티(오클루전 곱)입니다. 맵이 없으면 1 입니다. */
    static float computeCavity( uint wrinkleCavityMap, float amount, float2 uv )
    {
        return wrinkleCavityMap == kInvalidIndex ? 1.0f : lerp( 1.0f, MaterialTexture::sampleIndex( wrinkleCavityMap, uv ).r, amount );
    }
};

#endif
