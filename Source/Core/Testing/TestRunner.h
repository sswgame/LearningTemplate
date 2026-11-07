/**
 * @file TestRunner.h
 * @brief 등록된 케이스(`SW_TEST_CASE`)를 거르고 섞어 돌리는 실행기입니다. `App --test` 가 헤드리스로 선 엔진 위에서 부른다(`HeadlessSubsystem`).
 * @details 기본은 한 프로세스가 모두 돈다 — 실행 파일 · 정책 표 · 샤드 비용표가 없다. 케이스가 늘면 `--test-shard=i/n` 으로 이름 해시에 따라 나눠
 *          프로세스 여럿이 돈다(CTest `SW_UNIT_TEST_SHARDS`, GoogleTest 샤딩과 같다). 결과는 표준 출력(사람)과 JSON 보고(`--test-report`)로 낸다.
 *          보고에는 거른 케이스 수 · 돈 케이스 수 · 씨앗 · 케이스별 시간 · 실패 글 · 갱신한 스냅숏이 든다 — 구성마다 실제로 몇 개가 돌았는지가 보인다.
 *          보고 형식은 판 번호(`kReportSchemaVersion`)를 단다. 케이스마다 이름 · 정의한 파일과 줄 · 회차 · 결과 · 시간 · 격리 여부 · 실패 글(`파일(줄): error:` 꼴)이다.
 *          CTest 는 실행마다 빌드 폴더의 고정 경로(`Testing/Reports/<항목>.json`)에 보고를 남긴다(`Test/CMakeLists.txt`) — 통과한 실행도 시간 추세가 남는다.
 *          격리 케이스(`._bIsolated`)는 이 실행 파일을 자식으로 다시 띄워(`--test-isolated-case`) 돌린다 — 크래시 · 교착 · 강제 종료가 그 케이스의 실패로 남고 나머지는 이어진다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class CrashHandler;
} // namespace sw

namespace sw::test
{
    /** @struct TestRunOptions @brief 실행기의 명령줄 값입니다. */
    struct TestRunOptions
    {
        string        _filter{ "*" };             ///< 케이스 이름 와일드카드(쉼표로 여럿, 앞에 `-` 면 뺀다)
        string        _sourceFolder{};            ///< 비지 않으면 정의한 파일 경로에 이 폴더(`/` 구분, 쉼표로 여럿, 예: `Source/Editor/`)가 든 케이스만 돈다 — 에디터 이미지의 케이스를 에디터 안에서, 게임이 싣는 키트 케이스를 그 게임으로(`--test-source`)
        string        _reportPath{};              ///< JSON 보고를 쓸 경로(비면 실패한 실행만 남겨 둔 쓰기 폴더 `<뿌리>/<프로세스>/report.json` 에 쓴다 — 모두 통과한 실행은 폴더째 지운다)
        string        _scratchRoot{};             ///< 케이스별 쓰기 폴더의 뿌리(비면 `Saved/Test`)
        string        _childRole{};               ///< 비지 않으면 이 자식 역할(`SW_TEST_CHILD_ROLE`) 하나만 돈다 — 거르개 · 샤드 · 반복은 보지 않는다(`runChildRole` 이 띄운 자식)
        string        _childArgument{};           ///< 자식 역할이 `getChildRoleArgument` 로 받는 값
        string        _isolatedCase{};            ///< 비지 않으면 이 케이스(`._bIsolated` 인 보통 케이스) 하나만 이 프로세스에서 돈다 — 부모 실행기가 격리 케이스마다 띄운 자식이다. 거르개 · 샤드 · 반복은 보지 않는다
        uint32        _seed{ 0 };                 ///< 순서 섞기 씨앗(0 이면 시각에서 고르고 찍는다)
        uint32        _repeatCount{ 1 };          ///< 같은 순서를 몇 번 돌지(간헐 실패를 찾는다). 회차마다 씨앗을 하나씩 올린다
        uint32        _shardIndex{ 0 };           ///< 이 프로세스가 도는 몫(0 부터)
        uint32        _shardCount{ 1 };           ///< 몫 수 — 케이스는 이름 해시로 나뉜다(섞기 · 거르개와 무관하게 늘 같은 몫)
        bool          _bUpdateSnapshots{ false }; ///< NOLINT(sw-bool-flag): 참조(`bool&`)로 넘긴다
        bool          _bListOnly{ false };        ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool          _bRunDisabled{ false };     ///< `_disabledReason` 이 있는 케이스도 돈다(고친 뒤 확인) NOLINT(sw-bool-flag): 참조(`bool&`)로 넘긴다
        CrashHandler* _pCrashHandler{ nullptr };  ///< 도는 케이스 이름을 문맥(`TestCase`)에 적을 크래시 처리기 — 크래시 보고에 어느 케이스였는지 남는다. nullptr 이면 적지 않는다
    };

    /**
     * @class TestRunner
     * @brief 케이스 실행기입니다(전부 static).
     */
    class SW_API TestRunner
    {
    public:
        /** @brief JSON 보고 형식의 판입니다. 칸을 바꾸면 올린다(읽는 도구가 판으로 가른다). 2: 판 번호 · 케이스의 파일 · 줄 · 격리 칸을 더했다. */
        static constexpr uint32 kReportSchemaVersion = 2;

        /** @brief @p name(`스위트.케이스`)이 거르개에 맞는지입니다. */
        [[nodiscard]] static bool matchesFilter( string_view filter, string_view name );
        /** @brief 태그까지 봅니다 — 패턴 `[태그]` 는 @p tags(공백으로 가른 낱말) 가운데 하나와 같으면 맞는다(`-[태그]` 는 빼기). */
        [[nodiscard]] static bool matchesFilter( string_view filter, string_view name, string_view tags );
        /** @brief `i/n`(0 ≤ i < n)을 읽습니다. 틀리면 false 입니다. */
        [[nodiscard]] static bool parseShard( string_view text, uint32& outIndex, uint32& outCount );
        /** @brief @p name 이 @p shardCount 몫 가운데 몇 번째인지입니다(이름의 FNV-1a — 실행 · 빌드마다 같다). */
        [[nodiscard]] static uint32 getShardOf( string_view name, uint32 shardCount );
        /** @brief 지금 등록된 케이스 이름입니다(이름 순, 자식 역할은 빼고). */
        static vector<string> collectCaseNames();
        /**
         * @brief 파일 경로에 @p listFolder 중 하나가 든 케이스를 뺀 이름입니다(이름 순, 자식 역할은 빼고).
         * @details 에디터 · 편집기 플러그인 케이스는 `App -test` 에 등록되지 않는다 — 그쪽으로 자식을 띄우는 곳이 돌리지 못할 이름을 고르지 않게 거른다. 구분자는 `/` 로 적는다.
         */
        static vector<string> collectCaseNamesOutside( std::initializer_list<string_view> listFolder );
        /**
         * @brief 거른 케이스를 섞어 돌립니다. 모두 통과하면 true 입니다.
         * @details 거르개에 맞는 케이스가 없으면 실패다(빈 실행을 통과로 보지 않는다). 케이스 하나는 실패 단언 · 단언 0 개 · 적지 않은 Warning · Error 로그 ·
         *          적었는데 나오지 않은 로그 가운데 하나라도 있으면 실패다. 비활성 케이스는 돌지 않고 까닭과 함께 센다. 시한을 넘긴 케이스는 실행을 끝낸다(`kTestTimeoutExitCode`).
         * @param pOutListFailure 비지 않으면 실패 줄(`<케이스>: <파일>(<줄>): error: …`)을 받는다 — 표준 출력을 남기지 않는 실행(에디터 자체 테스트)이 로그에 사유를 적는다.
         */
        [[nodiscard]] static bool run( const TestRunOptions& options, vector<string>* pOutListFailure = nullptr );
    };
} // namespace sw::test
