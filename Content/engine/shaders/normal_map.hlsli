/**
 * @file normal_map.hlsli
 * @brief 탄젠트 공간 노멀 맵을 월드 노멀로 옮기는 공용 함수입니다(툰 · PBR 머티리얼이 함께 쓴다).
 */
#ifndef SW_NORMAL_MAP_HLSLI
#define SW_NORMAL_MAP_HLSLI

/**
 * @brief 노멀 맵을 적용한 월드 노멀입니다. 맵이 없으면 @p normal 그대로입니다.
 * @details 탄젠트를 노멀에 다시 직교화하고 바이탄젠트는 cross( N, T ) × w 다(glTF · MikkTSpace — C++ `Mesh::computeTangents`). 맵은 OpenGL 방향(+Y 위, glTF 규약)이다.
 */
float3 applyNormalMap( uint normalMap, float normalScale, float2 uv, float3 normal, float4 tangent )
{
    if ( normalMap == kInvalidIndex )
        return normal;
    // Z 는 XY 에서 다시 만든다 — BC5 로 압축한 노멀 맵(텍스처 임포트 규칙 Base_NormalMap)은 RG 만 있어 B 가 0 이다(-1 로 읽으면 노멀이 뒤집힌다).
    float3 tangentNormal = float3( MaterialTexture::sampleIndex( normalMap, uv ).xy * 2.0f - 1.0f, 0.0f );
    tangentNormal.z      = sqrt( saturate( 1.0f - dot( tangentNormal.xy, tangentNormal.xy ) ) );
    tangentNormal.xy *= normalScale;
    const float3 orthogonalTangent = tangent.xyz - normal * dot( normal, tangent.xyz );
    const float  tangentLength     = length( orthogonalTangent );
    if ( tangentLength < 1e-5f )
        return normal;
    const float3 unitTangent = orthogonalTangent / tangentLength;
    const float3 bitangent   = cross( normal, unitTangent ) * ( tangent.w < 0.0f ? -1.0f : 1.0f );
    return normalize( unitTangent * tangentNormal.x + bitangent * tangentNormal.y + normal * tangentNormal.z );
}

#endif
