#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"

#include "EngineTest/LoaderFuzzTargets.h"

#include "TestFramework/TestFramework.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>

using namespace sw;

// ------------------------------------------------------------------------------
// LoaderFuzzTest — 로더 퍼징(시드 고정 변이)
//
// 씨앗(저장소의 실제 파일 · 만든 바이트)을 변이해 로더에 먹이고, **죽지 않고 · 단언하지 않는지** 본다. 결과(성공 · 실패)는 보지 않는다 —
// 망가진 입력을 거절하는 것이 맞는 동작이다. 변이는 시드 고정이라 CI 에서도 같은 입력이 돈다(실패하면 같은 수로 다시 난다).
//   SW_FUZZ_ITERATIONS=<n>   대상마다 변이 수(기본 120) — 사냥은 수만으로 돌린다
//   SW_FUZZ_SEED=<n>         변이 시드(기본 0x5eed) — 다른 입력을 보고 싶을 때
//   SW_FUZZ_TARGET=<이름>    한 대상만(`LoaderFuzzTargets.cpp` 의 표 이름)
//   SW_FUZZ_TRACE=1          입력마다 대상 · 씨앗 · 회차를 찍고 입력을 임시 폴더 `sw_fuzz_last_<대상>.bin` 에 남긴다 — 죽은 입력을 건질 때
// 커버리지 안내 퍼징(libFuzzer)은 리눅스 `Test/FuzzTest/LoaderFuzzer` 가 같은 표를 돈다 — `LoaderFuzzTargets.h` 머리말.
// ------------------------------------------------------------------------------

namespace
{
    /** @brief xorshift64* — 플랫폼 · 표준 라이브러리와 무관하게 같은 수열을 낸다(`std::mt19937` 분포는 구현마다 다를 수 있다). */
    class FuzzRandom
    {
    public:
        explicit FuzzRandom( uint64 seed )
            : _state{ seed == 0 ? sw::HashUtil::kGoldenRatio64 : seed }
        {
        }
        uint64 next()
        {
            _state ^= _state >> 12;
            _state ^= _state << 25;
            _state ^= _state >> 27;
            return _state * 0x2545F4914F6CDD1Dull;
        }
        size_t below( size_t bound ) { return bound == 0 ? 0 : static_cast<size_t>( next() % bound ); }

    private:
        uint64 _state;
    };

    /** @brief 크기 · 개수 · 오프셋 칸을 노리는 값들이다(부호 경계 · 2 의 거듭제곱 경계 · 0). */
    constexpr uint32      kArrInterestingInt[]        = { 0u, 1u, 2u, 0x7Fu, 0x80u, 0xFFu, 0x100u, 0x7FFFu, 0x8000u, 0xFFFFu, 0x10000u, 0x7FFFFFFFu, 0x80000000u,
                                                          0xFFFFFFFFu, 0xFFFFFFFEu, 0x10000000u, 4096u, 3u };
    constexpr const utf8* kArrInterestingNumberText[] = { "-1", "0", "4294967296", "99999999999999999999", "-2147483649", "1e308", "-1e308", "nan",
                                                          "inf", "0x7fffffff", "65536", "1.#INF" };
    constexpr utf8        kArrStructuralChar[]        = { '<', '>', '"', '=', '/', '{', '}', '[', ']', ',', ':', '&', ';', '\0', '\n' };

    /** @brief 바이트열 하나를 1~4 번 변이합니다. 텍스트면 숫자 · 구두점을 노리는 변이를 섞습니다. 크기는 1 MiB 를 넘기지 않습니다. */
    void mutateBytes( vector<uint8>& inoutBytes, FuzzRandom& random, bool bText, const vector<vector<uint8>>& listSeed )
    {
        constexpr size_t kMaxSize       = 1u << 20;
        const size_t     operationCount = 1 + random.below( 4 );
        for ( size_t operationIndex = 0; operationIndex < operationCount; ++operationIndex )
        {
            const size_t size      = inoutBytes.size();
            const size_t operation = random.below( bText ? 10 : 7 );
            if ( size == 0 && operation != 5 && operation != 9 )
            {
                inoutBytes.push_back( static_cast<uint8>( random.next() ) );
                continue;
            }
            switch ( operation )
            {
                case 0: // 비트 하나
                {
                    inoutBytes[random.below( size )] ^= static_cast<uint8>( 1u << random.below( 8 ) );
                    break;
                }
                case 1: // 경계 바이트
                {
                    inoutBytes[random.below( size )] = static_cast<uint8>( kArrInterestingInt[random.below( std::size( kArrInterestingInt ) )] );
                    break;
                }
                case 2: // 4 바이트 경계 정수(리틀 엔디언) — 머리의 크기 · 개수 · 오프셋 칸을 노린다
                {
                    const uint32 value  = kArrInterestingInt[random.below( std::size( kArrInterestingInt ) )];
                    const size_t offset = random.below( size );
                    for ( size_t byteIndex = 0; byteIndex < 4 && offset + byteIndex < size; ++byteIndex )
                    {
                        inoutBytes[offset + byteIndex] = static_cast<uint8>( value >> ( 8 * byteIndex ) );
                    }
                    break;
                }
                case 3: // 자르기
                {
                    inoutBytes.resize( random.below( size ) );
                    break;
                }
                case 4: // 구간 지우기
                {
                    const size_t start = random.below( size );
                    const size_t count = 1 + random.below( std::min<size_t>( 64, size - start ) );
                    inoutBytes.erase( inoutBytes.begin() + static_cast<ptrdiff_t>( start ), inoutBytes.begin() + static_cast<ptrdiff_t>( start + count ) );
                    break;
                }
                case 5: // 다른 씨앗의 구간을 끼우기
                {
                    const vector<uint8>& donor = listSeed[random.below( listSeed.size() )];
                    if ( donor.empty() || size + 256 > kMaxSize )
                        break;
                    const size_t start = random.below( donor.size() );
                    const size_t count = 1 + random.below( std::min<size_t>( 256, donor.size() - start ) );
                    const size_t at    = random.below( size + 1 );
                    inoutBytes.insert( inoutBytes.begin() + static_cast<ptrdiff_t>( at ), donor.begin() + static_cast<ptrdiff_t>( start ),
                                       donor.begin() + static_cast<ptrdiff_t>( start + count ) );
                    break;
                }
                case 6: // 구간 되풀이(목록 · 원소가 늘어난 입력)
                {
                    const size_t start = random.below( size );
                    const size_t count = 1 + random.below( std::min<size_t>( 512, size - start ) );
                    if ( size + count > kMaxSize )
                        break;
                    const vector<uint8> chunk( inoutBytes.begin() + static_cast<ptrdiff_t>( start ), inoutBytes.begin() + static_cast<ptrdiff_t>( start + count ) );
                    inoutBytes.insert( inoutBytes.begin() + static_cast<ptrdiff_t>( start ), chunk.begin(), chunk.end() );
                    break;
                }
                case 7: // (텍스트) 숫자 하나를 경계 수로
                {
                    size_t start = random.below( size );
                    while ( start < size && ( inoutBytes[start] < '0' || inoutBytes[start] > '9' ) )
                    {
                        ++start;
                    }
                    if ( start >= size )
                        break;
                    size_t end = start;
                    while ( end < size && ( ( inoutBytes[end] >= '0' && inoutBytes[end] <= '9' ) || inoutBytes[end] == '.' ) )
                    {
                        ++end;
                    }
                    const string_view replacement = kArrInterestingNumberText[random.below( std::size( kArrInterestingNumberText ) )];
                    inoutBytes.erase( inoutBytes.begin() + static_cast<ptrdiff_t>( start ), inoutBytes.begin() + static_cast<ptrdiff_t>( end ) );
                    inoutBytes.insert( inoutBytes.begin() + static_cast<ptrdiff_t>( start ), replacement.begin(), replacement.end() );
                    break;
                }
                case 8: // (텍스트) 구조 글자 하나 지우기
                {
                    const size_t start = random.below( size );
                    for ( size_t index = start; index < size; ++index )
                    {
                        if ( std::find( std::begin( kArrStructuralChar ), std::end( kArrStructuralChar ), static_cast<utf8>( inoutBytes[index] ) ) !=
                             std::end( kArrStructuralChar ) )
                        {
                            inoutBytes.erase( inoutBytes.begin() + static_cast<ptrdiff_t>( index ) );
                            break;
                        }
                    }
                    break;
                }
                default: // (텍스트) 구조 글자 하나 끼우기
                {
                    inoutBytes.insert( inoutBytes.begin() + static_cast<ptrdiff_t>( random.below( size + 1 ) ),
                                       static_cast<uint8>( kArrStructuralChar[random.below( std::size( kArrStructuralChar ) )] ) );
                    break;
                }
            }
        }
    }

    /** @brief FNV-1a 64 — 대상 이름에서 변이 수열을 고른다. */
    uint64 hashTargetName( string_view name )
    {
        uint64 hash = sw::HashUtil::kFnvOffset64;
        for ( const utf8 character : name )
        {
            hash ^= static_cast<uint8>( character );
            hash *= sw::HashUtil::kFnvPrime64;
        }
        return hash;
    }

    uint64 readEnvironmentNumber( const utf8* pName, uint64 fallback )
    {
        const utf8* pValue = std::getenv( pName );
        return pValue != nullptr && pValue[0] != '\0' ? std::strtoull( pValue, nullptr, 0 ) : fallback;
    }
} // namespace

/**
 * @brief [LoaderFuzzTest] 모든 로더가 변이한 입력에 죽지도 단언하지도 않는다(씨앗 그대로도 포함)
 * @details 대상 표는 `LoaderFuzzTargets.cpp` 다. 씨앗을 하나도 못 모은 대상은 시험이 진다(대상이 아무것도 안 먹은 것이다).
 *          단언은 `ScopedAssertCapture` 로 세고, 걸리면 대상 이름과 회차를 알린다 — 바깥 데이터로 걸리는 단언은 결함이다(입력 검증이 먼저 막아야 한다).
 */
SW_TEST_CASE( LoaderFuzzTest, EveryLoaderSurvivesMutatedInput )
{
    const uint64              iterationCount = readEnvironmentNumber( "SW_FUZZ_ITERATIONS", 120 );
    const uint64              baseSeed       = readEnvironmentNumber( "SW_FUZZ_SEED", 0x5EED );
    const utf8*               pOnlyTarget    = std::getenv( "SW_FUZZ_TARGET" );
    const bool                bTrace         = readEnvironmentNumber( "SW_FUZZ_TRACE", 0 ) != 0;
    uint32                    targetCount    = 0;
    test::ScopedLogSuppressor suppressor;

    for ( const test::LoaderFuzzTarget& target : test::getLoaderFuzzTargets() )
    {
        if ( pOnlyTarget != nullptr && pOnlyTarget[0] != '\0' && string_view( pOnlyTarget ) != target._pName )
            continue;
        ++targetCount;

        vector<vector<uint8>> listSeed;
        target._pfnCollectSeed( listSeed );
        SW_EXPECT_TRUE_MSG( listSeed.empty() == false, ( string( "no seed for fuzz target " ) + target._pName ).c_str() );
        if ( listSeed.empty() )
            continue;

        test::ScopedAssertCapture asserts;
        // 수열은 대상 이름에서 나온다 — `SW_FUZZ_TARGET` 으로 하나만 돌려도 같은 입력이 나온다(재현).
        FuzzRandom random( baseSeed ^ hashTargetName( target._pName ) );
        uint32     assertCountBefore = asserts.getCount();
        for ( uint64 iteration = 0; iteration < iterationCount + listSeed.size(); ++iteration )
        {
            // 처음 몇 번은 씨앗을 그대로 먹인다 — 멀쩡한 입력도 단언 없이 지나야 한다.
            const size_t  seedIndex = iteration < listSeed.size() ? static_cast<size_t>( iteration ) : random.below( listSeed.size() );
            vector<uint8> input     = listSeed[seedIndex];
            if ( iteration >= listSeed.size() )
                mutateBytes( input, random, target._bText, listSeed );
            if ( bTrace )
            {
                std::printf( "[Fuzz] %s seed %zu iteration %" PRIu64 " size %zu\n", target._pName, seedIndex, static_cast<uint64>( iteration ),
                             input.size() );
                std::fflush( stdout );
                // 죽은 입력을 그대로 남긴다 — 프로세스가 죽어도 파일은 남는다(회귀 시험의 재료).
                (void)FileUtil::writeFile( test::makeFuzzTracePath( target._pName ), input.data(), input.size() );
            }
            target._pfnRun( input.data(), input.size() );
            if ( asserts.getCount() != assertCountBefore )
            {
                string message = string( "fuzz target " ) + target._pName + " asserted on iteration " + to_string( iteration ) + " (seed " +
                                 to_string( seedIndex ) + ", SW_FUZZ_SEED " + to_string( baseSeed ) + ")";
                SW_EXPECT_TRUE_MSG( false, message.c_str() );
                assertCountBefore = asserts.getCount();
            }
        }
    }
    SW_EXPECT_TRUE_MSG( targetCount > 0, "SW_FUZZ_TARGET names no target" );
}
