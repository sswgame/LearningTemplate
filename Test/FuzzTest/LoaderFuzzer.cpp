#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "EngineTest/LoaderFuzzTargets.h"

#include "TestFramework/TestHostRuntime.h"

#include <cstdio>
#include <cstdlib>

// libFuzzer 진입점 — 대상은 환경 변수 SW_FUZZ_TARGET(LoaderFuzzTargets.cpp 의 표 이름). SW_FUZZ_LIST=1 이면 대상 이름을 줄마다 찍고 끝나고,
// SW_FUZZ_DUMP_SEEDS=<폴더> 면 그 대상의 씨앗을 파일로 쓰고 끝난다(fuzz.yml 이 말뭉치 첫 판으로 쓴다). 엔진은 시험 하네스와 같은 기동으로 한 번만 세운다.
// 엔진 단언(`SW_ASSERT`)이 멈추면 libFuzzer 는 그것을 크래시로 본다 — 원하는 판정이다(죽지 않고 · 단언하지 않고). 종료 정리는 libFuzzer 의 exit 에 맡긴다.

namespace
{
    /** @brief libFuzzer 가 넘기는 argv 입니다(진입점 모양이 정해져 있다 — `char***`). */
    using FuzzerArgumentList = utf8**;

    struct LoaderFuzzerInternal
    {
        static const test::LoaderFuzzTarget*& getTarget()
        {
            static const test::LoaderFuzzTarget* s_pTarget = nullptr;
            return s_pTarget;
        }

        static bool isSet( const utf8* pName )
        {
            const utf8* pValue = std::getenv( pName );
            return pValue != nullptr && pValue[0] == '1';
        }

        static void dumpSeeds( const test::LoaderFuzzTarget& target, const utf8* pFolder )
        {
            sw::vector<sw::vector<uint8>> listSeed;
            target._pfnCollectSeed( listSeed );
            for ( size_t index = 0; index < listSeed.size(); ++index )
            {
                sw::string name( "seed_" );
                name += sw::to_string( static_cast<uint64>( index ) );
                if ( sw::FileUtil::writeFile( sw::FileUtil::joinPath( pFolder, name ), listSeed[index].data(), listSeed[index].size() ) == false )
                    std::fprintf( stderr, "LoaderFuzzer: could not write seed %zu\n", index );
            }
        }
    };
} // namespace

extern "C" int32 LLVMFuzzerInitialize( int32* pArgc, FuzzerArgumentList* pArgumentList )
{
    (void)pArgc;
    if ( LoaderFuzzerInternal::isSet( "SW_FUZZ_LIST" ) )
    {
        for ( const test::LoaderFuzzTarget& target : test::getLoaderFuzzTargets() )
            std::fprintf( stdout, "%s\n", target._pName );
        std::exit( 0 );
    }
    static test::TestHostRuntime s_runtime;
    utf8*                        arrArgument[] = { ( *pArgumentList )[0] };
    if ( s_runtime.start( 1, arrArgument ) == false )
    {
        std::fprintf( stderr, "LoaderFuzzer: engine harness did not start\n" );
        std::exit( 2 );
    }
    const utf8*                   pTargetName = std::getenv( "SW_FUZZ_TARGET" );
    const test::LoaderFuzzTarget* pTarget     = pTargetName != nullptr ? test::findLoaderFuzzTarget( pTargetName ) : nullptr;
    if ( pTarget == nullptr )
    {
        std::fprintf( stderr, "LoaderFuzzer: set SW_FUZZ_TARGET to one of the names SW_FUZZ_LIST=1 prints\n" );
        std::exit( 2 );
    }
    LoaderFuzzerInternal::getTarget() = pTarget;
    const utf8* pSeedFolder           = std::getenv( "SW_FUZZ_DUMP_SEEDS" );
    if ( pSeedFolder != nullptr )
    {
        LoaderFuzzerInternal::dumpSeeds( *pTarget, pSeedFolder );
        std::exit( 0 );
    }
    return 0;
}

extern "C" int32 LLVMFuzzerTestOneInput( const uint8* pData, size_t size )
{
    LoaderFuzzerInternal::getTarget()->_pfnRun( pData, size );
    return 0;
}
