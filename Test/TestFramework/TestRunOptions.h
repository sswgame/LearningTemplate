/**
 * @file Test/TestFramework/TestRunOptions.h
 * @brief 테스트 실행 파일의 명령줄 — 필터 · 샤드 · 반복 · 섞기 · 씨앗 · 스위트 종류 · 예산을 읽고, 실패한 실행을 다시 돌릴 명령을 만든다
 */
#pragma once
#include "Engine/Config/RHIBackendType.h"

#include "TestFramework/TestCase.h"
#include "TestFramework/TestFilter.h"

namespace test
{
    /**
     * @brief 이 빌드가 그 백엔드를 링크했는가. 배포본은 `SW_RHI_TARGET_*` 로 고른 **하나만** 링크한다 — 나머지를 만들려 하면 엔진이
     *        "이 빌드에 없습니다" Error 를 남기고(호스트 스위트는 예상 밖 Error 로 진다) 널을 돌려준다. 개발 구성은 넷 다 모듈로 짓는다
     *        (이 호스트에 런타임이 없는 것은 `RHIAvailability` 가 따로 거른다). 빌드에 없는 백엔드는 요구 백엔드(`--required_backends=`)여도
     *        요구하지 않는다 — 구성 차이는 선언된 차이다.
     */
    constexpr bool isBackendInThisBuild( sw::RHIBackend backend )
    {
#if defined( SW_SHIPPING )
    #if defined( SW_RHI_TARGET_DX11 )
        return backend == sw::RHIBackend::DirectX11;
    #elif defined( SW_RHI_TARGET_VULKAN )
        return backend == sw::RHIBackend::Vulkan;
    #elif defined( SW_RHI_TARGET_OPENGL )
        return backend == sw::RHIBackend::OpenGL;
    #else
        return backend == sw::RHIBackend::DirectX12;
    #endif
#else
        (void)backend;
        return true;
#endif
    }
} // namespace test

namespace test
{
    /** @brief 한 실행의 선택 · 순서 · 판정 옵션. 기본값이 개발자가 실행 파일을 직접 돌릴 때의 동작이다. */
    struct TestRunOptions
    {
        TestRunOptions();

        /**
         * @brief 테스트 전용 인자를 가져가고 나머지(엔진 명령줄)를 돌려줍니다. 첫 원소는 실행 파일 경로다.
         * @details 엔진 인자는 `_listEngineArgument` 에도 남는다 — 재현 명령이 그대로 붙인다(`-gv_rhiBackend=Vulkan` 같은 것).
         */
        sw::vector<utf8*> parse( int32 argc, utf8* argv[] );
        /** @brief 테스트 실행 파일이 받는 인자를 표준 출력에 찍습니다. */
        static void printHelp();

        /**
         * @brief 이 실행을 다시 만드는 테스트 인자입니다(엔진 인자 포함) — @p filter 가 비지 않으면 그것으로 고른다.
         * @details 섞은 실행은 순서가 실패의 일부일 수 있어 원래 필터 · 샤드 · 씨앗을 그대로 쓴다. 섞지 않았으면 진 케이스만 고르는 것이 빠르다.
         */
        sw::string makeReproductionArguments( const sw::string& filter, bool bSeedUsed ) const;

        /** @brief glob 필터를 정합니다(gtest 모양 — "A.*:B.*" 또는 "A.*,B.*" 로 여럿, 첫 "-" 뒤는 빼는 패턴, `TestFilter::setPattern`). */
        void setFilter( const sw::string& filter );
        /** @brief 느린 케이스의 예상 비용을 읽습니다(`Suite.Case=<초>` 를 쉼표로). 형식이 틀리면 실행이 진다. */
        void setCaseCosts( const sw::string& text );
        /** @brief 종류 @p kind 의 스위트를 어떻게 다루는지. */
        SuiteKindMode getKindMode( SuiteKind kind ) const { return _arrKindMode[static_cast<size_t>( kind )]; }
        /**
         * @brief 요구 백엔드를 읽습니다(쿠킹 표의 명령줄 이름 `dx11` · `dx12` · `vk` · `gl` 을 쉼표로). 모르는 이름이면 실행이 진다.
         * @details 값의 기준은 `Test/TestPolicy.json` 의 `requiredBackends` 이고 CMake 가 hostgpu 항목에 넘긴다(C-41 3단계).
         */
        void setRequiredBackends( const sw::string& text );
        /** @brief 이 실행이 그 백엔드를 요구하는가 — 요구한 백엔드가 서지 못하면 케이스는 건너뛰지 않고 진다. */
        bool isBackendRequired( sw::RHIBackend backend ) const;
        /**
         * @brief 이 실행이 그 백엔드를 검증 대상으로 두는가 — `--required_backends=` 가 없으면 전부, 있으면 그 목록만.
         * @details 소프트웨어 렌더러 묶음은 WARP · lavapipe 가 서는 백엔드만 요구한다. 목록 밖 백엔드(Windows 러너의 Vulkan · GL 처럼 소프트웨어
         *          드라이버가 없어 반쯤 서다 마는 것)를 세워 보면 환경 탓 경고가 쏟아지고 검증할 것도 없다 — 이 실행은 세우지 않는다.
         */
        bool isBackendSelected( sw::RHIBackend backend ) const;
        /** @brief 쿠킹 표의 명령줄 이름(`dx12` 등)입니다. 메시지에 씁니다. */
        static const utf8* getBackendName( sw::RHIBackend backend );

        TestFilter                   _filter;
        sw::string                   _filterText;                                           ///< 받은 그대로의 `--test_filter`(재현 명령용)
        sw::vector<sw::string>       _listEngineArgument;                                   ///< 테스트 인자가 아닌 것(엔진 명령줄)
        sw::map<sw::string, float64> _mapCaseCost;                                          ///< `--test_costs=A.B=40,C.D=12` — 샤드를 비용으로 고르게 나눌 때(`Test/TestPolicy.json` 의 `caseCosts`)
        sw::string                   _caseCostText;                                         ///< 받은 그대로의 `--test_costs`(재현 명령용)
        sw::vector<sw::RHIBackend>   _listRequiredBackend;                                  ///< `--required_backends=` — 서지 못하면 건너뛰는 대신 지는 백엔드
        sw::string                   _requiredBackendText;                                  ///< 받은 그대로의 `--required_backends`(재현 명령용)
        SuiteKindMode                _arrKindMode[static_cast<size_t>( SuiteKind::Count )]; ///< 기본: 호스트 All, 부하 Exclude
        uint32                       _repeatCount;
        uint32                       _randomSeed; ///< `--test_seed` · `--test_shuffle=` · `--gtest_random_seed` — 0 이면 아직 정하지 않음
        uint32                       _shardIndex; ///< 이 실행의 샤드(0 부터) — `--test_shard=INDEX/COUNT`
        uint32                       _shardCount; ///< 샤드 수(1 = 나누지 않는다)
        bool                         _bShuffle;
        bool                         _bListOnly;
        bool                         _bAllowEmptySuite;
        bool                         _bEnforceBudgets; ///< `SW_TEST_TIME_BUDGET` 을 넘은 케이스를 실패로 친다(부하 검증 실행)
        bool                         _bInvalidArgument;
        bool                         _bHelpRequested;
    };
} // namespace test
