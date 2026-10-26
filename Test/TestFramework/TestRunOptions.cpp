#include "pch.h"

#include "TestFramework/TestRunOptions.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Process/ProcessEnvironment.h"
#include "Core/Time/MonotonicClock.h"

#include "sw/config/CookContract.gen.h"

namespace test
{
    namespace
    {
        struct TestRunOptionsInternal
        {
            /** @brief 씨앗을 주지 않았을 때 고르는 범위 — 사람이 다시 칠 수 있게 다섯 자리 안. */
            static constexpr int64 kGeneratedSeedRange = 100000;

            /** @brief CLI 인자 값의 따옴표를 제거합니다. */
            static sw::string trimArgValue( std::string_view value )
            {
                while ( value.empty() == false && ( value.front() == '"' || value.front() == '\'' ) )
                {
                    value.remove_prefix( 1 );
                }
                while ( value.empty() == false && ( value.back() == '"' || value.back() == '\'' ) )
                {
                    value.remove_suffix( 1 );
                }
                return sw::string( value );
            }

            /** @brief `exclude` · `only` · `all` 을 읽습니다. 모르는 값이면 false. */
            [[nodiscard]] static bool parseKindMode( std::string_view text, SuiteKindMode& outMode )
            {
                if ( text == "exclude" )
                    outMode = SuiteKindMode::Exclude;
                else if ( text == "only" )
                    outMode = SuiteKindMode::Only;
                else if ( text == "all" )
                    outMode = SuiteKindMode::All;
                else
                    return false;
                return true;
            }

            /** @brief 쿠킹 표 한 줄의 백엔드와 그 명령줄 이름입니다. */
            struct BackendName
            {
                sw::RHIBackend   _backend;
                std::string_view _name;
            };
#define SW_TEST_BACKEND_NAME_ROW( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) BackendName{ sw::RHIBackend::Backend, CommandLineName },
            /** @brief 백엔드 이름 표 — 쿠킹 표(`SW_RHI_BACKEND_TABLE`)의 줄마다 하나입니다. 이름을 여기 따로 적지 않습니다. */
            static constexpr BackendName kArrBackendName[] = { SW_RHI_BACKEND_TABLE( SW_TEST_BACKEND_NAME_ROW ) };
#undef SW_TEST_BACKEND_NAME_ROW

            static const utf8* getKindModeName( SuiteKindMode mode )
            {
                switch ( mode )
                {
                    case SuiteKindMode::All:
                        return "all";
                    case SuiteKindMode::Exclude:
                        return "exclude";
                    case SuiteKindMode::Only:
                        return "only";
                }
                return "all";
            }
        };
    } // namespace
} // namespace test

namespace test
{
    TestRunOptions::TestRunOptions()
        : _filter{}
        , _filterText{}
        , _listEngineArgument{}
        , _mapCaseCost{}
        , _caseCostText{}
        , _listRequiredBackend{}
        , _requiredBackendText{}
        , _arrKindMode{ SuiteKindMode::All, SuiteKindMode::Exclude }
        , _repeatCount{ 1 }
        , _randomSeed{ 0 }
        , _shardIndex{ 0 }
        , _shardCount{ 1 }
        , _bShuffle{ false }
        , _bListOnly{ false }
        , _bAllowEmptySuite{ false }
        , _bEnforceBudgets{ false }
        , _bInvalidArgument{ false }
        , _bHelpRequested{ false }
    {
    }

    sw::vector<utf8*> TestRunOptions::parse( int32 argc, utf8* argv[] )
    {
        sw::vector<utf8*> listApplicationArg;
        listApplicationArg.reserve( static_cast<size_t>( argc ) );
        if ( argc > 0 )
            listApplicationArg.push_back( argv[0] );

        // 샤드 — gtest 의 환경 변수도 받는다(아래 `--test_shard=` 가 있으면 그쪽이 이긴다).
        const auto setShard = [this]( std::string_view indexText, std::string_view countText )
        {
            int32      index         = -1;
            int32      count         = 0;
            const bool bParsed       = sw::StringUtil::parseInt( indexText, index ) && sw::StringUtil::parseInt( countText, count );
            const bool bInvalidShard = bParsed == false || count < 1 || index < 0 || index >= count;
            if ( bInvalidShard )
            {
                _bInvalidArgument = true;
                std::fprintf( stdout, "Invalid test shard '%s/%s' (expected <index>/<count> with 0 <= index < count)\n", sw::string( indexText ).c_str(),
                              sw::string( countText ).c_str() );
                return;
            }
            _shardIndex = static_cast<uint32>( index );
            _shardCount = static_cast<uint32>( count );
        };
        sw::string  pShardIndexEnvText;
        const utf8* pShardIndexEnv = sw::ProcessEnvironment::tryRead( "GTEST_SHARD_INDEX", pShardIndexEnvText ) ? pShardIndexEnvText.c_str() : nullptr;
        sw::string  pShardCountEnvText;
        const utf8* pShardCountEnv = sw::ProcessEnvironment::tryRead( "GTEST_TOTAL_SHARDS", pShardCountEnvText ) ? pShardCountEnvText.c_str() : nullptr;
        if ( pShardIndexEnv != nullptr && pShardCountEnv != nullptr )
            setShard( pShardIndexEnv, pShardCountEnv );

        const auto parseKind = [this]( std::string_view arg, std::string_view prefix, SuiteKind kind )
        {
            if ( sw::StringUtil::startsWith( arg, prefix ) == false )
                return false;
            const std::string_view value = arg.substr( prefix.size() );
            if ( TestRunOptionsInternal::parseKindMode( value, _arrKindMode[static_cast<size_t>( kind )] ) == false )
            {
                // 오타를 "전부" 로 읽으면 CI 가 GPU 스위트를 돌린다 — 실행을 실패로 끝낸다.
                _bInvalidArgument = true;
                std::fprintf( stdout, "Unknown %.*s value '%s' (expected exclude, only or all)\n", static_cast<int32>( prefix.size() - 1 ), prefix.data(),
                              sw::string( value ).c_str() );
            }
            return true;
        };

        for ( int32 argIndex = 1; argIndex < argc; ++argIndex )
        {
            const std::string_view arg = argv[argIndex] != nullptr ? argv[argIndex] : "";
            // 도움말 — 엔진 명령줄로 넘기면 모르는 인자로 무시되고 전체 테스트가 돈다. 하네스가 먼저 가져가 기동 전에 끝낸다.
            const bool bHelpArgument = arg == "--help" || arg == "-h" || arg == "-?" || arg == "/?" || arg == "--gtest_help";
            if ( bHelpArgument )
            {
                _bHelpRequested = true;
                continue;
            }
            if ( arg == "--test_list" || arg == "--gtest_list_tests" )
            {
                _bListOnly = true;
                continue;
            }

            constexpr std::string_view kFilterPrefixA = "--test_filter=";
            constexpr std::string_view kFilterPrefixB = "--gtest_filter=";
            const bool                 bFilterA       = sw::StringUtil::startsWith( arg, kFilterPrefixA );
            if ( bFilterA || sw::StringUtil::startsWith( arg, kFilterPrefixB ) )
            {
                setFilter( TestRunOptionsInternal::trimArgValue( arg.substr( bFilterA ? kFilterPrefixA.size() : kFilterPrefixB.size() ) ) );
                continue;
            }
            if ( arg == "--test_filter" || arg == "--gtest_filter" )
            {
                if ( argIndex + 1 < argc && argv[argIndex + 1] != nullptr )
                    setFilter( TestRunOptionsInternal::trimArgValue( argv[++argIndex] ) );
                continue;
            }

            if ( arg == "--allow_empty_suite" )
            {
                _bAllowEmptySuite = true;
                continue;
            }
            if ( arg == "--enforce_budgets" )
            {
                _bEnforceBudgets = true;
                continue;
            }

            // 되풀이 · 섞기 · 씨앗 — gtest 의 이름(`--gtest_repeat` · `--gtest_shuffle` · `--gtest_random_seed`)도 받는다.
            constexpr std::string_view kRepeatPrefixA = "--test_repeat=";
            constexpr std::string_view kRepeatPrefixB = "--gtest_repeat=";
            if ( sw::StringUtil::startsWith( arg, kRepeatPrefixA ) || sw::StringUtil::startsWith( arg, kRepeatPrefixB ) )
            {
                const std::string_view value       = arg.substr( arg.find( '=' ) + 1 );
                int32                  count       = 0;
                const bool             bValidCount = sw::StringUtil::parseInt( value, count ) && count >= 1;
                if ( bValidCount == false )
                {
                    _bInvalidArgument = true;
                    std::fprintf( stdout, "Invalid --test_repeat value '%s' (expected a count of 1 or more)\n", sw::string( value ).c_str() );
                }
                else
                    _repeatCount = static_cast<uint32>( count );
                continue;
            }

            if ( arg == "--test_shuffle" || arg == "--gtest_shuffle" )
            {
                _bShuffle = true;
                continue;
            }
            constexpr std::string_view kShufflePrefix   = "--test_shuffle=";
            constexpr std::string_view kSeedPrefix      = "--test_seed=";
            constexpr std::string_view kGtestSeedPrefix = "--gtest_random_seed=";
            const bool                 bSeedArgument    = sw::StringUtil::startsWith( arg, kSeedPrefix ) || sw::StringUtil::startsWith( arg, kGtestSeedPrefix );
            if ( sw::StringUtil::startsWith( arg, kShufflePrefix ) || bSeedArgument )
            {
                _bShuffle                         = _bShuffle || bSeedArgument == false;
                const std::string_view value      = arg.substr( arg.find( '=' ) + 1 );
                uint64                 seed       = 0;
                const bool             bValidSeed = sw::StringUtil::parseUint64( value, seed ) && seed <= sw::MathUtil::kMaxUInt32;
                if ( bValidSeed == false )
                {
                    _bInvalidArgument = true;
                    std::fprintf( stdout, "Invalid test seed '%s' (expected a number from 0 to 4294967295)\n", sw::string( value ).c_str() );
                }
                else
                    _randomSeed = static_cast<uint32>( seed );
                continue;
            }

            constexpr std::string_view kShardPrefix = "--test_shard=";
            if ( sw::StringUtil::startsWith( arg, kShardPrefix ) )
            {
                const std::string_view value = arg.substr( kShardPrefix.size() );
                const size_t           slash = value.find( '/' );
                if ( slash == std::string_view::npos )
                    setShard( value, "" ); // 형식 오류로 보고된다
                else
                    setShard( value.substr( 0, slash ), value.substr( slash + 1 ) );
                continue;
            }

            constexpr std::string_view kCostPrefix = "--test_costs=";
            if ( sw::StringUtil::startsWith( arg, kCostPrefix ) )
            {
                setCaseCosts( sw::string( arg.substr( kCostPrefix.size() ) ) );
                continue;
            }

            if ( parseKind( arg, "--host_suites=", SuiteKind::Host ) || parseKind( arg, "--load_suites=", SuiteKind::Load ) )
                continue;

            constexpr std::string_view kRequiredBackendPrefix = "--required_backends=";
            if ( sw::StringUtil::startsWith( arg, kRequiredBackendPrefix ) )
            {
                setRequiredBackends( TestRunOptionsInternal::trimArgValue( arg.substr( kRequiredBackendPrefix.size() ) ) );
                continue;
            }

            listApplicationArg.push_back( argv[argIndex] );
            _listEngineArgument.push_back( sw::string( arg ) );
        }

        // 씨앗을 주지 않았으면 여기서 고른다 — 섞기와 `test::getRandomSeed` 가 같은 값을 쓰고, 진 실행은 그 값을 재현 명령에 찍는다.
        if ( _randomSeed == 0 )
            _randomSeed = static_cast<uint32>( sw::MonotonicClock::nowNanoseconds() % TestRunOptionsInternal::kGeneratedSeedRange ) + 1;

        return listApplicationArg;
    }

    void TestRunOptions::setFilter( const sw::string& filter )
    {
        _filterText = filter;
        _filter.setPattern( filter );
    }

    void TestRunOptions::setCaseCosts( const sw::string& text )
    {
        _caseCostText = text;
        _mapCaseCost.clear();
        if ( text.empty() )
            return;
        // 빈 항목(`a,,b` · 끝 쉼표)도 항목이다 — 잘못된 항목으로 알린다.
        for ( const std::string_view entry : sw::StringUtil::split( text, "," ) )
        {
            const size_t equals     = entry.rfind( '=' );
            float64      cost       = 0.0;
            const bool   bValidCost = equals != std::string_view::npos && sw::StringUtil::parseDouble( entry.substr( equals + 1 ), cost ) && cost > 0.0;
            if ( bValidCost == false )
            {
                _bInvalidArgument = true;
                std::fprintf( stdout, "Invalid --test_costs entry '%s' (expected Suite.Case=<seconds>)\n", sw::string( entry ).c_str() );
            }
            else
                _mapCaseCost[sw::string( entry.substr( 0, equals ) )] = cost;
        }
    }

    void TestRunOptions::setRequiredBackends( const sw::string& text )
    {
        _requiredBackendText = text;
        _listRequiredBackend.clear();
        if ( text.empty() )
            return;
        for ( const std::string_view name : sw::StringUtil::split( text, "," ) )
        {
            bool bKnown = false;
            for ( const TestRunOptionsInternal::BackendName& row : TestRunOptionsInternal::kArrBackendName )
            {
                if ( row._name != name )
                    continue;
                _listRequiredBackend.push_back( row._backend );
                bKnown = true;
            }
            if ( bKnown == false )
            {
                // 모르는 이름을 버리면 그 백엔드가 서지 못해도 건너뛰기로 끝난다 — 실행을 실패로 끝낸다.
                _bInvalidArgument = true;
                std::fprintf( stdout, "Unknown --required_backends entry '%s' (expected the cook table's command line names: dx11, dx12, vk, gl)\n",
                              sw::string( name ).c_str() );
            }
        }
    }

    bool TestRunOptions::isBackendRequired( sw::RHIBackend backend ) const
    {
        for ( const sw::RHIBackend required : _listRequiredBackend )
        {
            if ( required == backend )
                return true;
        }
        return false;
    }

    bool TestRunOptions::isBackendSelected( sw::RHIBackend backend ) const
    {
        return _listRequiredBackend.empty() || isBackendRequired( backend );
    }

    const utf8* TestRunOptions::getBackendName( sw::RHIBackend backend )
    {
        for ( const TestRunOptionsInternal::BackendName& row : TestRunOptionsInternal::kArrBackendName )
        {
            if ( row._backend == backend )
                return row._name.data(); // 표의 이름은 문자열 리터럴이라 널로 끝난다
        }
        return "?";
    }

    void TestRunOptions::printHelp()
    {
        std::fprintf( stdout,
                      "Usage: <TestExecutable> [options] [engine arguments]\n"
                      "  --help, -h, -?, /?, --gtest_help  Print this help and exit (the engine is not started)\n"
                      "  --test_list, --gtest_list_tests   List the cases the filter selects and exit\n"
                      "  --test_filter=PATTERN             Run matching cases (gtest form: A.*:B.* or A.*,B.*; after the first '-' patterns exclude)\n"
                      "  --gtest_filter=PATTERN            Same as --test_filter\n"
                      "  --test_repeat=N                   Run the selected cases N times and report each failing case's rate (--gtest_repeat=N)\n"
                      "  --test_shuffle[=SEED]             Shuffle suites and cases per iteration (--gtest_shuffle)\n"
                      "  --test_seed=SEED                  Seed for shuffling and test::getRandomSeed() (--gtest_random_seed=SEED)\n"
                      "  --test_shard=INDEX/COUNT          Run one shard (GTEST_SHARD_INDEX / GTEST_TOTAL_SHARDS also work)\n"
                      "  --test_costs=A.B=SEC,...          Expected seconds of slow cases - shards are balanced by cost\n"
                      "  --host_suites=exclude|only|all    Exclude or select suites that need a GPU, window or DXC (default all)\n"
                      "  --load_suites=exclude|only|all    Exclude or select load and time budget suites (default exclude)\n"
                      "  --enforce_budgets                 Fail cases that exceed their SW_TEST_TIME_BUDGET\n"
                      "  --required_backends=dx11,dx12,... Fail (instead of skip) when one of these RHI backends cannot stand up\n"
                      "  --allow_empty_suite               Do not fail when a selected suite has no runnable case\n"
                      "Other arguments go to the engine command line (-gv_<name>=<value> and so on).\n" );
        std::fflush( stdout );
    }

    sw::string TestRunOptions::makeReproductionArguments( const sw::string& filter, bool bSeedUsed ) const
    {
        sw::string arguments;
        const auto append = [&arguments]( const sw::string& text )
        {
            if ( arguments.empty() == false )
                arguments += ' ';
            arguments += text;
        };

        // 섞은 실행은 순서까지 다시 만들어야 하므로 원래 고른 집합과 씨앗을 그대로 쓴다.
        const sw::string& selection = _bShuffle ? _filterText : filter;
        if ( selection.empty() == false )
            append( "\"--test_filter=" + selection + "\"" );
        for ( size_t kindIndex = 0; kindIndex < static_cast<size_t>( SuiteKind::Count ); ++kindIndex )
        {
            const SuiteKindMode defaultMode = kindIndex == static_cast<size_t>( SuiteKind::Load ) ? SuiteKindMode::Exclude : SuiteKindMode::All;
            if ( _arrKindMode[kindIndex] == defaultMode )
                continue;
            append( sw::string( kindIndex == static_cast<size_t>( SuiteKind::Host ) ? "--host_suites=" : "--load_suites=" ) +
                    TestRunOptionsInternal::getKindModeName( _arrKindMode[kindIndex] ) );
        }
        if ( _bShuffle && _shardCount > 1 )
        {
            append( "--test_shard=" + sw::to_string( _shardIndex ) + "/" + sw::to_string( _shardCount ) );
            if ( _caseCostText.empty() == false )
                append( "--test_costs=" + _caseCostText );
        }
        if ( _bShuffle )
            append( "--test_shuffle=" + sw::to_string( _randomSeed ) );
        else if ( bSeedUsed )
            append( "--test_seed=" + sw::to_string( _randomSeed ) );
        if ( _repeatCount > 1 )
            append( "--test_repeat=" + sw::to_string( _repeatCount ) );
        if ( _bEnforceBudgets )
            append( "--enforce_budgets" );
        if ( _requiredBackendText.empty() == false )
            append( "--required_backends=" + _requiredBackendText );
        for ( const sw::string& engineArgument : _listEngineArgument )
        {
            append( engineArgument );
        }
        return arguments;
    }
} // namespace test
