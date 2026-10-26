#include "pch.h"

#include "Core/Serialization/BitStream.h"

#include "EngineTest/LoaderFuzzTargets.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Rule/BrZone.h"

// 퍼징 대상(키트 BattleRoyale) — 받은 자기장 상태(넷 코덱 `BrZone::readState`). 표는 `LoaderFuzzTargets.h` 머리말.

using namespace sw;

namespace
{
    struct KitFuzzTargetsBattleRoyaleInternal
    {
        static void runBrZone( const uint8* pData, size_t size )
        {
            BitReader reader( pData, static_cast<int32>( size ) );
            BrZone    zone;
            (void)zone.readState( reader ); // 퍼즈 대상 — 거절도 정상이다
        }

        static void seedBrZone( vector<vector<uint8>>& outListSeed )
        {
            const BrZone zone;
            BitWriter    writer;
            if ( zone.writeState( writer ) )
                outListSeed.push_back( writer.releaseBytes() );
        }
    };

    const test::LoaderFuzzTargetRegistrar s_brZoneFuzzTarget{
        test::LoaderFuzzTarget{ "BrZone", &KitFuzzTargetsBattleRoyaleInternal::runBrZone, &KitFuzzTargetsBattleRoyaleInternal::seedBrZone, false }
    };
} // namespace
