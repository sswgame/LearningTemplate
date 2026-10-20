/**
 * @file TestHostRuntime.h
 * @brief 테스트 하네스의 엔진 기동 · 종료입니다 — 테스트 실행 파일의 `main` 과 퍼저(`Test/FuzzTest/LoaderFuzzer`)가 같은 기동을 쓴다.
 * @details 부트스트랩(이름 풀 · 로거 · 크래시 핸들러 · 명령줄) → 서비스 → `Engine` 과 같은 서브시스템 클래스. 하네스는 창 · RHI ·
 *          렌더러를 세우지 않는다. `stop` 은 기동이 중간에 졌어도 세운 만큼 역순으로 내린다.
 * @code
 *     test::TestHostRuntime runtime;
 *     if ( runtime.start( argc, argv ) )
 *         result = test::TestRegistry::getInstance().runAllTests();
 *     runtime.stop();
 * @endcode
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    struct EngineDefaultAssets;
    struct GameConfig;
} // namespace sw

namespace test
{
    /**
     * @brief 하네스가 데이터(enginedefaultassets.xml)에서 읽은 엔진 기본 에셋 표입니다 — 테스트가 셰이더 · 파이프라인 · 글꼴 경로를 얻는 창구다.
     * @details 표는 기본 생성할 수 없다(`EngineDefaultAssets::ConstructKey`). 경로를 리터럴로 다시 적지 말고 여기서 읽는다.
     */
    [[nodiscard]] const sw::EngineDefaultAssets& getEngineDefaultAssets();

    /**
     * @brief 팩 루트가 @p packRoot 인 게임 프리셋입니다 — 프리셋 JSON 을 읽어 만든다(`GameConfig::_packRoot` 는 `DataPath` 라 데이터 읽기만 값을 넣는다).
     * @details 읽지 못하면 테스트를 실패로 적고 읽지 않은 프리셋을 돌려준다.
     */
    [[nodiscard]] sw::GameConfig makeGameConfig( sw::string_view packRoot );

    /** @class TestHostRuntime @brief 하네스 하나 — 프로세스에 하나만 세운다(엔진 서비스 표가 전역이다). */
    class TestHostRuntime
    {
    public:
        TestHostRuntime();
        ~TestHostRuntime();

        TestHostRuntime( const TestHostRuntime& )            = delete;
        TestHostRuntime& operator=( const TestHostRuntime& ) = delete;

        /**
         * @brief 엔진을 세웁니다(명령줄의 테스트 플래그는 `TestRegistry` 가 먼저 가져간다). 모두 섰으면 true. 지면 `stop` 으로 내린다.
         * @details 도움말을 요청했으면(`TestRegistry::isHelpRequested`) 도움말만 찍고 엔진을 세우지 않은 채 false 다.
         */
        [[nodiscard]] bool start( int32 argc, utf8* argv[] );
        /** @brief 세운 만큼 역순으로 내립니다. 두 번 불러도 된다. */
        void stop();

    private:
        struct State;
        sw::unique_ptr<State> _pState;
    };
} // namespace test
