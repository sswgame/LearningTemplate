/**
 * @file AutomationLogPolicy.h
 * @brief 자동화 실행과 호스트 테스트 스위트의 로그 게이트입니다. 남은 `[Warning]` · `[Error]` 줄 가운데 처리하지 않은 줄을 가려냅니다.
 * @details 언리얼 Automation 의 `AddExpectedError`(기대한 줄 선언)와 "경고를 실패로" 설정을 합친 자리다. 줄 하나는 아래 순서로 판정한다(`classify`).
 *          1. 실행기 줄(`[Scenario]`)과 방어 구간 줄(`[Expected Defensive Test]`)은 처음부터 보지 않는다.
 *          2. 시나리오의 `<ExpectLog>` 가 선언한 줄은 통과다(`count="0"` 은 선언이 아니다). id 가 있는 줄은 `id="…"` 로만, id 없는 줄은 `contains="…"` 가 **본문 전체와 같을 때만** 선언된다 — 부분 문자열로 다른 줄까지 가리지 못한다.
 *          3. 로그 id(`SW_LOG_WARNING_ID` · `SW_LOG_ERROR_ID`)가 없는 줄은 허용 목록으로 가릴 수 없다 — 소스에 id 를 붙여야 한다(`MissingID`).
 *          4. id 가 있으면 허용 목록 `Resource/engine/automation/allowedlogs.json` 에서 (id, 지금 범위)가 맞는 항목을 찾는다. 없으면 `NotAllowed` 다.
 *          허용 목록의 항목은 id · 범위(`scenario` 나 `suite` 중 하나) · 사유 · 처리 계획 문서가 모두 있어야 읽힌다. 하나라도 비었거나 `TODO` 이면 읽기 오류로 멈춘다 —
 *          범위 없는 전역 허용과 와일드카드 id 는 형식으로 만들 수 없다. 목록에 있으나 더 이상 나오지 않는 항목은 `Scripts` 의 `scenario-logs --check` 가 실패로 센다.
 *          `AutomationRunner`(프로세스 안, 시나리오 구간), `AppScenarioTest`(프로세스 출력 전체, 기동 · 종료 포함), 테스트 케이스의 `CaseLogJudge` 가 같은 목록을 쓴다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Log/LogTypes.h"

namespace sw
{
    class JSONValue;

    /** @brief 허용 항목이 적용되는 범위의 종류입니다. */
    enum class AutomationAllowScopeKind : uint8
    {
        Scenario, ///< 시나리오 이름(`<Scenario name="…">`)
        Suite,    ///< 호스트 테스트 스위트 이름(`SW_TEST_CASE( 스위트, … )` 의 앞 이름)
    };

    /** @struct AutomationAllowedLog @brief 허용 목록 항목 하나입니다. 다섯 칸이 모두 있어야 읽힙니다. */
    struct AutomationAllowedLog
    {
        string                   _id{};     ///< 로그 id(`"영역.사건"`). 와일드카드는 쓸 수 없다
        string                   _scope{};  ///< 범위 이름 — 이 시나리오나 스위트에서만 허용한다
        string                   _reason{}; ///< 왜 아직 고치지 않았는가
        string                   _plan{};   ///< 처리 계획 문서 이름(`py -3 -m Scripts plans` 의 이름)
        AutomationAllowScopeKind _scopeKind{ AutomationAllowScopeKind::Scenario };
    };
} // namespace sw

namespace sw
{
    /** @brief 로그 줄 하나의 판정입니다. */
    enum class AutomationLogVerdict : uint8
    {
        NotGated,   ///< 게이트가 보지 않는 줄(Info 이하, 실행기 줄, 방어 구간 줄)
        Declared,   ///< 시나리오의 `<ExpectLog>` 가 선언한 줄
        Allowed,    ///< 허용 항목(id + 범위)이 맞은 줄
        NotAllowed, ///< id 는 있으나 허용 항목이 없는 줄
        MissingID,  ///< id 가 없는 줄 — 허용 목록으로 가릴 수 없다
    };

    /** @struct AutomationLogJudgement @brief 한 실행에서 `judge` 가 모은 결과입니다. 처리하지 않은 줄이 하나라도 있으면 실패입니다. */
    struct AutomationLogJudgement
    {
        vector<string> _listUnhandled{}; ///< 처리하지 않은 줄 — `[Error] [호출자] (id=…) 본문` · `[Warning] [호출자] (no log id) 본문`
        vector<string> _listAllowedID{}; ///< 허용 항목이 맞은 id(중복 없이) — 보고에 적어 `scenario-logs --check` 가 낡은 항목을 센다
        uint32         _missingIDCount{ 0 };
        uint32         _notAllowedCount{ 0 };

        bool hasUnhandled() const { return _listUnhandled.empty() == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationLogPolicy
     * @brief 허용 목록과 이 실행의 범위(시나리오 · 스위트 이름)와 `<ExpectLog>` 선언을 들고 로그 줄을 판정합니다.
     */
    class SW_API AutomationLogPolicy
    {
    public:
        /** @brief 전역 허용 목록의 리소스 경로입니다. */
        static constexpr const utf8* kAllowListPath = "engine/automation/allowedlogs.json";
        /** @brief 실행기 자신의 줄 앞부분입니다(게이트가 보지 않는다). */
        static constexpr const utf8* kRunnerPrefix = "[Scenario]";
        /** @brief 테스트의 방어 구간이 붙이는 앞부분입니다(게이트가 보지 않는다). */
        static constexpr const utf8* kDefensivePrefix = "[Expected Defensive Test]";
        /** @brief 초안(`scenario-logs --regen`)이 사람이 채울 칸에 넣는 글입니다. 이 글이 남은 항목은 읽기 오류입니다. */
        static constexpr const utf8* kPlaceholderText = "TODO";

        /** @brief 전역 허용 목록을 리소스에서 읽어 더합니다. 파일이 없거나 형식이 틀리면 false 와 이유입니다. */
        [[nodiscard]] bool loadAllowList( string& outError );
        /** @brief 허용 목록 JSON 글을 읽어 더합니다(`{ "allowed": [ { "id", "scenario" 또는 "suite", "reason", "plan" } ] }`). 틀린 항목이 하나라도 있으면 아무것도 더하지 않습니다. */
        [[nodiscard]] bool parseAllowList( string_view jsonText, string_view sourceName, string& outError );
        /** @brief 허용 목록을 읽었는가(`loadAllowList` · `parseAllowList` 가 성공했는가). */
        bool isLoaded() const { return _bLoaded; }

        /** @brief 시나리오 @p name 의 범위를 켭니다(`scenario` 칸이 이 이름인 항목이 맞는다). 연결 실행처럼 한 프로세스에서 여럿이면 여러 번 부릅니다. */
        void selectScenario( string_view name );
        /** @brief 호스트 스위트 @p name 의 범위를 켭니다(`suite` 칸이 이 이름인 항목이 맞는다). */
        void selectSuite( string_view name );
        /** @brief 시나리오의 `<ExpectLog contains="…">` 선언 하나를 더합니다. id 없는 줄의 **본문 전체**와 같을 때만 그 줄을 가립니다. 빈 글은 무시합니다. */
        void declareExpected( string_view contains );
        /** @brief 시나리오의 `<ExpectLog id="…">` 선언 하나를 더합니다. 그 id 의 줄은 문구와 무관하게 통과입니다. 빈 글은 무시합니다. */
        void declareExpectedID( string_view id );
        /** @brief 켠 범위와 `<ExpectLog>` 선언을 지웁니다. 허용 목록은 그대로 둡니다(케이스마다 범위를 다시 정하는 호스트 판정기가 씁니다). */
        void clearScope();

        /** @brief 게이트가 보는 수준(Warning · Error)이면 true 입니다. */
        static bool isGatedLevel( LogLevel level ) { return level == LogLevel::Error || level == LogLevel::Warning; }
        /** @brief 게이트가 보지 않는 줄(실행기 줄 · 방어 구간 줄)이면 true 입니다. @p message 는 로그 본문입니다. */
        static bool isExemptMessage( string_view message );
        /** @brief @p text(줄 본문 전체)가 `<ExpectLog contains>` 선언 하나와 같으면 true 입니다(부분 문자열이 아니다). */
        bool isDeclared( string_view text ) const;
        /** @brief @p id 가 `<ExpectLog id>` 선언 하나와 같으면 true 입니다. 빈 id(id 없는 줄)는 항상 false 입니다. */
        bool isDeclaredID( string_view id ) const;

        /** @brief 줄 하나를 판정합니다. @p id 가 비었으면 id 없는 줄입니다. */
        AutomationLogVerdict classify( LogLevel level, string_view id, string_view message ) const;
        /** @brief 줄 하나를 판정해 처리하지 않은 줄이면 @p inoutJudgement 에 더하고, 허용 항목이 맞았으면 그 id 를 적습니다. */
        void judge( LogLevel level, string_view caller, string_view id, string_view message, AutomationLogJudgement& inoutJudgement ) const;

        const vector<AutomationAllowedLog>& getAllowed() const { return _listAllowed; }

    private:
        /** @brief 항목 하나를 검사해 @p outEntry 에 채웁니다. 틀리면 false 와 이유입니다. */
        [[nodiscard]] static bool   readEntry( const JSONValue& entry, string_view sourceName, size_t index, AutomationAllowedLog& outEntry, string& outError );
        [[nodiscard]] bool          readAllowList( const JSONValue& root, string_view sourceName, string& outError );
        const AutomationAllowedLog* findAllowed( string_view id ) const;

        vector<AutomationAllowedLog> _listAllowed{};
        vector<string>               _listExpected{};
        vector<string>               _listExpectedID{};
        vector<string>               _listScenario{};
        vector<string>               _listSuite{};
        bool                         _bLoaded{ false };
    };
} // namespace sw
