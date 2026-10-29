/**
 * @file TestRunner.h
 * @brief 등록된 케이스(`SW_TEST_CASE`)를 거르고 섞어 돌리는 실행기입니다. `App --test` 가 헤드리스로 선 엔진 위에서 부른다(`HeadlessSubsystem`).
 * @details 한 프로세스가 모두 돈다 — 실행 파일 · 정책 표 · 샤드 비용표가 없다. 결과는 표준 출력(사람)과 JSON 보고(`--test-report`)로 낸다.
 *          보고에는 거른 케이스 수 · 돈 케이스 수 · 씨앗 · 케이스별 시간 · 실패 글 · 갱신한 스냅숏이 든다 — 구성마다 실제로 몇 개가 돌았는지가 보인다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw::test
{
    /** @struct TestRunOptions @brief 실행기의 명령줄 값입니다. */
    struct TestRunOptions
    {
        string _filter{ "*" };    ///< 케이스 이름 와일드카드(쉼표로 여럿, 앞에 `-` 면 뺀다)
        string _reportPath{};     ///< JSON 보고를 쓸 경로(비면 이 실행의 폴더 `<뿌리>/<프로세스>/report.json`)
        string _scratchRoot{};    ///< 케이스별 쓰기 폴더의 뿌리(비면 `Saved/Test`)
        uint32 _seed{ 0 };        ///< 순서 섞기 씨앗(0 이면 시각에서 고르고 찍는다)
        uint32 _repeatCount{ 1 }; ///< 같은 순서를 몇 번 돌지(간헐 실패를 찾는다). 회차마다 씨앗을 하나씩 올린다
        bool   _bUpdateSnapshots{ false };
        bool   _bListOnly{ false };
    };

    /**
     * @class TestRunner
     * @brief 케이스 실행기입니다(전부 static).
     */
    class SW_API TestRunner
    {
    public:
        /** @brief @p name(`스위트.케이스`)이 거르개에 맞는지입니다. */
        [[nodiscard]] static bool matchesFilter( string_view filter, string_view name );
        /** @brief 지금 등록된 케이스 이름입니다(이름 순). */
        static vector<string> collectCaseNames();
        /**
         * @brief 거른 케이스를 섞어 돌립니다. 모두 통과하면 true 입니다.
         * @details 거르개에 맞는 케이스가 없으면 실패다(빈 실행을 통과로 보지 않는다). 케이스 하나는 실패 단언 · 단언 0 개 · 적지 않은 Warning · Error 로그 ·
         *          적었는데 나오지 않은 로그 가운데 하나라도 있으면 실패다.
         */
        [[nodiscard]] static bool run( const TestRunOptions& options );
    };
} // namespace sw::test
