#include "pch.h"

#include "GameFramework/Runtime/World/Environment/WeatherShelter.h"

#include "Core/Foundation/Macros.h"

#include "Engine/Character/Hit/CharacterHit.h"

namespace sw
{
    namespace
    {
        struct WeatherShelterInternal
        {
            static constexpr uint32 kAllLayers = 0xFFFFFFFFu;
            /** @brief 열린 하늘을 재는 방향입니다 — 위와 고도 45 도 네 방위(단위 벡터). */
            static constexpr float32 kDiagonal        = 0.70710678f;
            static constexpr float32 kArrSkyRay[5][3] = {
                {      0.0f,      1.0f,       0.0f},
                { kDiagonal, kDiagonal,       0.0f},
                {-kDiagonal, kDiagonal,       0.0f},
                {      0.0f, kDiagonal,  kDiagonal},
                {      0.0f, kDiagonal, -kDiagonal},
            };
        };
    } // namespace

    bool WeatherShelter::isSheltered( const GameObjectManager& manager, const float3& position, float32 distance, uint64 ignoreObjectID )
    {
        CharacterRayHit roof;
        return CharacterRayHit::raycast3D( manager, position, float3{ 0.0f, 1.0f, 0.0f }, distance, WeatherShelterInternal::kAllLayers, ignoreObjectID, roof );
    }

    float32 WeatherShelter::computeSkyOpenness( const GameObjectManager& manager, const float3& position, float32 distance )
    {
        uint32 openCount = 0;
        for ( const float32( &ray )[3] : WeatherShelterInternal::kArrSkyRay )
        {
            CharacterRayHit hit;
            if ( CharacterRayHit::raycast3D( manager, position, float3{ ray[0], ray[1], ray[2] }, distance, WeatherShelterInternal::kAllLayers, 0, hit ) == false )
                ++openCount;
        }
        return static_cast<float32>( openCount ) / static_cast<float32>( SW_COUNT_OF( WeatherShelterInternal::kArrSkyRay ) );
    }
} // namespace sw
