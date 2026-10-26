#include "pch.h"

#include "EngineTest/LoaderFuzzTargets.h"

#include "GameFramework/Kits/Genre/Casual/KartRacing/Rule/KartGhost.h"

// 퍼징 대상(키트 KartRacing) — 받은 고스트 바이트(세이브 · 서버 순위표). 표는 `LoaderFuzzTargets.h` 머리말.

using namespace sw;

namespace
{
    struct KitFuzzTargetsKartRacingInternal
    {
        static constexpr int32   kSeedFrameCount = 8;
        static constexpr float32 kSeedStep       = 1.0f / 60.0f;

        static void runKartGhost( const uint8* pData, size_t size )
        {
            KartGhost ghost;
            (void)ghost.deserialize( pData, static_cast<int32>( size ) ); // 퍼즈 대상 — 거절도 정상이다
        }

        static void seedKartGhost( vector<vector<uint8>>& outListSeed )
        {
            KartGhost ghost;
            ghost.begin( float3{ 1.0f, 0.0f, -2.0f }, 0.5f, kSeedStep );
            for ( int32 frameIndex = 0; frameIndex < kSeedFrameCount; ++frameIndex )
            {
                ghost.record( ArcadeVehicleInput{}, frameIndex % 3 == 0 );
            }
            ghost.setFinishTime( kSeedStep * kSeedFrameCount );
            vector<uint8> bytes;
            if ( ghost.serialize( bytes ) )
                outListSeed.push_back( std::move( bytes ) );
        }
    };

    const test::LoaderFuzzTargetRegistrar s_kartGhostFuzzTarget{
        test::LoaderFuzzTarget{ "KartGhost", &KitFuzzTargetsKartRacingInternal::runKartGhost, &KitFuzzTargetsKartRacingInternal::seedKartGhost, false }
    };
} // namespace
