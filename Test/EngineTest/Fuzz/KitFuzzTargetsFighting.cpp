#include "pch.h"

#include "EngineTest/LoaderFuzzTargets.h"

#include "GameFramework/Base/Actor/Combat/FrameData.h"
#include "GameFramework/Kits/Genre/Action/Fighting/FighterCatalog.h"
#include "GameFramework/Kits/Genre/Action/Fighting/FightingMatch.h"

// 퍼징 대상(키트 Fighting) — 롤백 상태 바이트(`FightingMatch::loadState`, 고정 배치형 + 캐릭터 정의 검증). 표는 `LoaderFuzzTargets.h` 머리말.

using namespace sw;

namespace
{
    struct KitFuzzTargetsFightingInternal
    {
        static constexpr int32 kSeedFrameCount = 12;

        static constexpr const utf8* kMoveXML    = R"(<MoveCatalog><Move id="jab" startup="10" active="2" recovery="10" damage="5" hitstun="19" blockstun="12" hitstop="6" height="High">)"
                                                   R"(<Hitbox x="0.8" y="1.5" w="0.8" h="0.4"/></Move></MoveCatalog>)";
        static constexpr const utf8* kFighterXML = R"(<FighterCatalog><Fighter id="tester" name="Tester" health="100" sidestepFrames="20" sidestepAngle="0.6981317")"
                                                   R"( sidestep="u" sidestepDown="d,n" jump="u/f"><Move id="jab" command="1"/></Fighter></FighterCatalog>)";

        /** @brief 카탈로그 둘을 들고 대전을 세웁니다(대전이 정의를 빌려 쓰므로 함께 산다). */
        struct Arena
        {
            MoveCatalog    _moveCatalog;
            FighterCatalog _fighterCatalog;
            FightingMatch  _match;
            bool           _bReady{ false };

            Arena()
                : _moveCatalog{}
                , _fighterCatalog{}
                , _match{}
                , _bReady{ false }
            {
                const bool        bLoaded  = _moveCatalog.loadFromXMLText( kMoveXML, "FuzzFighting" ) && _fighterCatalog.loadFromXMLText( kFighterXML, _moveCatalog, "FuzzFighting" );
                const FighterDef* pFighter = bLoaded ? _fighterCatalog.find( "tester" ) : nullptr;
                _bReady                    = pFighter != nullptr && _match.initialize( *pFighter, *pFighter );
            }
        };

        static void runFightingRollback( const uint8* pData, size_t size )
        {
            Arena arena;
            if ( arena._bReady == false )
                return;
            const vector<uint8> bytes( pData, pData + ( pData != nullptr ? size : 0 ) );
            (void)arena._match.loadState( bytes ); // 퍼즈 대상 — 거절도 정상이다
        }

        static void seedFightingRollback( vector<vector<uint8>>& outListSeed )
        {
            Arena arena;
            if ( arena._bReady == false )
                return;
            vector<uint8> bytes;
            arena._match.saveState( bytes );
            outListSeed.push_back( bytes );
            const vector<uint8> listInput{ 0x10, 0x01 }; // 플레이어마다 1 바이트(방향 · 버튼) — 기술이 재생 중인 상태
            for ( int32 frameIndex = 0; frameIndex < kSeedFrameCount; ++frameIndex )
            {
                arena._match.advanceFrame( listInput );
            }
            arena._match.saveState( bytes );
            outListSeed.push_back( std::move( bytes ) );
        }
    };

    const test::LoaderFuzzTargetRegistrar s_fightingRollbackFuzzTarget{
        test::LoaderFuzzTarget{ "FightingRollback", &KitFuzzTargetsFightingInternal::runFightingRollback, &KitFuzzTargetsFightingInternal::seedFightingRollback, false }
    };
} // namespace
