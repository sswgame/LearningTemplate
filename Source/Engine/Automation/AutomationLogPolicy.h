/**
 * @file AutomationLogPolicy.h
 * @brief 자동화 실행의 로그 게이트입니다. 시나리오가 남긴 `[Warning]` · `[Error]` 가운데 허용하지 않은 줄을 실패로 봅니다.
 * @details 언리얼 Automation 의 `AddExpectedError`(기대한 줄 선언)와 "경고를 실패로" 설정을 합친 자리다. 허용은 두 곳에서 온다.
 *          - 전역 허용 목록 `Resource/engine/automation/allowedlogs.json` — 항목마다 부분 문자열(`contains`)과 이유(`reason`)가 있다. 이유 없는 항목은 읽기 오류다.
 *          - 시나리오의 `<ExpectLog contains="…">` — 시나리오가 일부러 만드는 줄을 그 자리에서 선언한다.
 *          `[Scenario]` 로 시작하는 실행기 줄과 `[Expected Defensive Test]` 표식 줄은 처음부터 보지 않는다.
 *          `AutomationRunner`(프로세스 안, 시나리오 구간)와 `AppScenarioTest`(프로세스 출력 전체, 기동 · 종료 포함)가 같은 목록을 쓴다.
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

    /** @struct AutomationAllowedLog @brief 허용한 로그 줄 하나(부분 문자열과 이유)입니다. */
    struct AutomationAllowedLog
    {
        string _contains{};
        string _reason{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationLogPolicy
     * @brief 허용 목록을 들고 로그 줄이 게이트를 지나는지 판정합니다. 상태는 허용 목록뿐입니다.
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

        /** @brief 전역 허용 목록을 리소스에서 읽어 더합니다. 파일이 없거나 형식이 틀리면 false 와 이유입니다. */
        [[nodiscard]] bool loadAllowList( string& outError );
        /** @brief 허용 목록 JSON 글을 읽어 더합니다(`{ "allowed": [ { "contains": "…", "reason": "…" } ] }`). */
        [[nodiscard]] bool parseAllowList( string_view jsonText, string_view sourceName, string& outError );
        /** @brief 허용 항목 하나를 더합니다(시나리오의 `<ExpectLog>` 같은 선언). */
        void addAllowed( string_view contains, string_view reason );

        /** @brief 게이트가 보는 수준(Warning · Error)이면 true 입니다. */
        static bool isGatedLevel( LogLevel level ) { return level == LogLevel::Error || level == LogLevel::Warning; }
        /** @brief 게이트가 보지 않는 줄(실행기 줄 · 방어 구간 줄)이면 true 입니다. @p message 는 로그 본문입니다. */
        static bool isExemptMessage( string_view message );
        /** @brief @p text(로그 본문이나 출력 한 줄 전체)가 허용 항목 하나라도 담으면 true 입니다. */
        bool isAllowed( string_view text ) const;

        const vector<AutomationAllowedLog>& getAllowed() const { return _listAllowed; }

    private:
        [[nodiscard]] bool readAllowList( const JSONValue& root, string_view sourceName, string& outError );

        vector<AutomationAllowedLog> _listAllowed{};
    };
} // namespace sw
