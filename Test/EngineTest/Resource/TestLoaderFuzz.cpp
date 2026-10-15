#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"

#include "Engine/Graphics/RHI/RHIRenderResource.h"

#include "EngineTest/LoaderFuzzTargets.h"

#include "TestFramework/Harness/TestChildProcess.h"
#include "TestFramework/TestFramework.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <random>

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
            const size_t size           = inoutBytes.size();
            const size_t operation      = random.below( bText ? 10 : 7 );
            const bool   bNeedsSeedByte = size == 0 && operation != 5 && operation != 9;
            if ( bNeedsSeedByte )
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
                    inoutBytes.insert( inoutBytes.begin() + static_cast<ptrdiff_t>( start ), reinterpret_cast<const uint8*>( replacement.data() ), reinterpret_cast<const uint8*>( replacement.data() ) + replacement.size() );
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

    /**
     * @brief 텍스트 씨앗의 CRLF 를 LF 로 바꿉니다.
     * @details Windows 체크아웃(core.autocrlf)은 저장소 텍스트를 CRLF 로, 리눅스는 LF 로 받는다. 씨앗 바이트가 다르면 같은 시드라도
     *          변이 위치 · 결과가 달라 리눅스 CI 가 Windows 에서 본 적 없는 입력을 먹는다. 줄끝을 맞춰 두 플랫폼이 같은 입력을 돌게 한다.
     */
    void normalizeLineEnding( vector<uint8>& inoutBytes )
    {
        size_t writeIndex = 0;
        for ( size_t readIndex = 0; readIndex < inoutBytes.size(); ++readIndex )
        {
            const bool bCrLfPair = inoutBytes[readIndex] == '\r' && readIndex + 1 < inoutBytes.size() && inoutBytes[readIndex + 1] == '\n';
            if ( bCrLfPair )
                continue;
            inoutBytes[writeIndex++] = inoutBytes[readIndex];
        }
        inoutBytes.resize( writeIndex );
    }

    uint64 readEnvironmentNumber( const utf8* pName, uint64 fallback )
    {
        const utf8* pValue = std::getenv( pName );
        return pValue != nullptr && pValue[0] != '\0' ? std::strtoull( pValue, nullptr, 0 ) : fallback;
    }

    /** @brief libFuzzer 가 먼저 먹이는 빈 입력 다음의 경계 입력입니다(1 바이트 · 공백 · 널 · 잘린 문서). */
    constexpr string_view kArrBoundaryInput[] = {
        { "\0", 1 },
        " ",
        " \r\n\t ",
        "<",
        "{",
        "\xFF",
        "<?xml version=\"1.0\"?>",
        "<Material",
        "<Material>",
        "<a><b>",
        "{\"a\":"
    };

    /** @brief 아무 장치도 붙지 않는 렌더 리소스입니다 — 레지스트리 등록 · 해제만 한다. */
    class ExitProbeRenderResource final : public RHIRenderResource
    {
    public:
        void releaseRHI( IRHIDevice* ) override {}
        void forgetRHI( IRHIDevice* ) override {}
    };

    /**
     * @brief 정적 소멸 끝 무렵에 렌더 리소스를 소멸시킵니다.
     * @details 네임스페이스 범위라 `main` 앞에서 서고, 그 뒤 처음 쓰인 함수 지역 static 보다 늦게 소멸합니다. 리눅스에서는 엔진 공유 라이브러리의
     *          정적 소멸과 한 목록에서 돌므로, 퍼저 하네스(정적 `TestHostRuntime`)가 정적 소멸 중에 엔진을 내리는 순서를 그대로 만듭니다.
     */
    struct ExitProbe
    {
        ExitProbeRenderResource* _pResource{ nullptr };
        ~ExitProbe() { sw_delete( _pResource ); }
    };
    ExitProbe s_exitProbe;
} // namespace

/**
 * @brief [LoaderFuzzTest] 모든 로더가 변이한 입력에 죽지도 단언하지도 않는다(씨앗 그대로도 포함)
 * @details 대상 표는 `LoaderFuzzTargets.cpp` 다. 씨앗을 하나도 못 모은 대상은 테스트가 진다(대상이 아무것도 안 먹은 것이다).
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
        const bool bSkipTarget = pOnlyTarget != nullptr && pOnlyTarget[0] != '\0' && string_view( pOnlyTarget ) != target._pName;
        if ( bSkipTarget )
            continue;
        ++targetCount;

        vector<vector<uint8>> listSeed;
        target._pfnCollectSeed( listSeed );
        SW_EXPECT_TRUE_MSG( listSeed.empty() == false, ( string( "no seed for fuzz target " ) + target._pName ).c_str() );
        if ( listSeed.empty() )
            continue;
        if ( target._bText )
        {
            for ( vector<uint8>& seed : listSeed )
            {
                normalizeLineEnding( seed );
            }
        }

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
                // 죽은 입력을 그대로 남긴다 — 프로세스가 죽어도 파일은 남는다(회귀 테스트의 재료).
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

/**
 * @brief [LoaderFuzzTest] 모든 로더가 빈 입력(널 포인터 · 길이 0 포함)과 1 바이트 · 공백 · 잘린 문서에 죽지도 단언하지도 않는다
 * @details libFuzzer 는 말뭉치보다 먼저 빈 입력을 한 번 먹인다. 시드 고정 변이는 씨앗에서 출발해 이 입력들을 거의 만들지 않으므로 따로 돈다.
 */
SW_TEST_CASE( LoaderFuzzTest, EveryLoaderSurvivesBoundaryInput )
{
    test::ScopedLogSuppressor suppressor;
    test::ScopedAssertCapture asserts;
    for ( const test::LoaderFuzzTarget& target : test::getLoaderFuzzTargets() )
    {
        const uint32 assertCountBefore = asserts.getCount();
        const uint8  emptyByte         = 0;
        target._pfnRun( nullptr, 0 );
        target._pfnRun( &emptyByte, 0 );
        for ( const string_view input : kArrBoundaryInput )
        {
            target._pfnRun( reinterpret_cast<const uint8*>( input.data() ), input.size() );
        }
        SW_EXPECT_TRUE_MSG( asserts.getCount() == assertCountBefore, ( string( "fuzz target " ) + target._pName + " asserted on a boundary input" ).c_str() );
    }
}

/**
 * @brief [LoaderFuzzTest] 자식 역할 — 모든 대상에 경계 입력을 먹이고, 레지스트리보다 늦게 소멸할 렌더 리소스를 하나 남긴다
 * @details `RenderResourceOutlivesStaticDestruction` 이 띄운다. `main` 이 엔진을 내린 뒤 정적 소멸 끝에서 `s_exitProbe` 가 리소스를 지운다.
 */
SW_TEST_CASE( LoaderFuzzTest, ChildLeavesRenderResourceForStaticDestruction )
{
    if ( std::getenv( "SW_FUZZ_EXIT_PROBE_CHILD" ) == nullptr )
        SW_TEST_SKIP( "exit probe child only — RenderResourceOutlivesStaticDestruction launches it" );
    test::ScopedLogSuppressor suppressor;
    for ( const test::LoaderFuzzTarget& target : test::getLoaderFuzzTargets() )
    {
        target._pfnRun( nullptr, 0 );
    }
    s_exitProbe._pResource = sw_new ExitProbeRenderResource();
}

/**
 * @brief [LoaderFuzzTest] 대상이 처음 세운 렌더 리소스 레지스트리가 프로세스 끝의 정적 소멸 뒤에도 쓸 수 있다
 * @details 퍼저 하네스는 정적 `TestHostRuntime` 이라 libFuzzer 의 `exit` 에서 정적 소멸 중에 엔진을 내린다. 그 전에 레지스트리가 소멸하면
 *          `releaseRegistryStorage` 가 지워진 목록을 다시 놓았다(CI 퍼징 `Material`, ASan heap-use-after-free). 자식이 0 으로 끝나야 한다.
 *          리눅스(ASan)에서만 그 순서가 드러난다 — Windows 는 DLL 마다 정적 소멸이 따로라 고치기 전에도 통과한다.
 */
SW_TEST_CASE( LoaderFuzzTest, RenderResourceOutlivesStaticDestruction )
{
    const test::ChildEnvironmentVariable arrEnvironment[] = {
        { "SW_FUZZ_EXIT_PROBE_CHILD", "1" }
    };
    const test::ProcessRunResult child = test::runThisExecutableAsChild( "LoaderFuzzTest.ChildLeavesRenderResourceForStaticDestruction", arrEnvironment, 120 );
    SW_ASSERT_TRUE( child._bLaunched );
    SW_ASSERT_TRUE_MSG( child._bTimedOut == false, child.getOutputTail().c_str() );
    SW_EXPECT_TRUE_MSG( child._exitCode == 0, child.getOutputTail().c_str() );
}
