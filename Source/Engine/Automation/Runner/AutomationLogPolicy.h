/**
 * @file AutomationLogPolicy.h
 * @brief 자동화 실행의 로그 게이트입니다. 시나리오 구간에 남은 `[Warning]` · `[Error]` 줄 가운데 처리하지 않은 줄을 가려냅니다.
 * @details 언리얼 Automation 의 `AddExpectedError`(기대한 줄 선언)와 "경고를 실패로" 설정을 합친 자리다. 줄 하나는 아래 순서로 판정한다(`classify`).
 *          1. 실행기 줄(`[Scenario]`)은 보지 않는다.
 *          2. 시나리오의 `<ExpectLog>` 가 선언한 줄은 통과다(`count="0"` 은 선언이 아니다). id 가 있는 줄은 `id="…"` 로만, id 없는 줄은 `contains="…"` 가 **본문 전체와 같을 때만**
 *             선언된다 — 부분 문자열로 다른 줄까지 가리지 못한다.
 *          3. 로그 id(`SW_LOG_WARNING_ID` · `SW_LOG_ERROR_ID`)가 없는 줄은 허용할 수 없다 — 소스에 id 를 붙인다(`MissingID`).
 *          4. id 가 있으면 그 시나리오의 `<AllowLog id="…" reason="…"/>` 가 허용한다. 없으면 `NotAllowed` 다.
 *          허용은 시나리오 파일 안에 사유와 함께 둔다 — 예전의 전역 허용 목록(모든 시나리오의 허용이 한 파일에 모이고 범위 · 계획 칸을 손으로 맞추던 것)은 없다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/LogTypes.h"

namespace sw
{
    /** @brief 로그 줄 하나의 판정입니다. */
    enum class AutomationLogVerdict : uint8
    {
        NotGated,   ///< 게이트가 보지 않는 줄(Info 이하, 실행기 줄)
        Declared,   ///< 시나리오의 `<ExpectLog>` 가 선언한 줄
        Allowed,    ///< 시나리오의 `<AllowLog>` 가 허용한 id 의 줄
        NotAllowed, ///< id 는 있으나 허용하지 않은 줄
        MissingID,  ///< id 가 없는 줄 — 허용할 수 없다
    };

    /** @struct AutomationLogJudgement @brief 한 실행에서 `judge` 가 모은 결과입니다. 처리하지 않은 줄이 하나라도 있으면 실패입니다. */
    struct AutomationLogJudgement
    {
        vector<string> _listUnhandled{}; ///< 처리하지 않은 줄 — `[Error] [호출자] (id=…) 본문` · `[Warning] [호출자] (no log id) 본문`
        vector<string> _listAllowedID{}; ///< 허용이 맞은 id(중복 없이) — 보고에 적는다(허용했는데 나오지 않는 id 는 낡은 허용이다)
        uint32         _missingIDCount{ 0 };
        uint32         _notAllowedCount{ 0 };

        bool hasUnhandled() const { return _listUnhandled.empty() == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationLogPolicy
     * @brief 시나리오의 `<ExpectLog>` 선언과 `<AllowLog>` 허용을 들고 로그 줄을 판정합니다.
     */
    class SW_API AutomationLogPolicy
    {
    public:
        /** @brief 실행기 자신의 줄 앞부분입니다(게이트가 보지 않는다). */
        static constexpr const utf8* kRunnerPrefix = "[Scenario]";

        /** @brief 시나리오의 `<ExpectLog contains="…">` 선언 하나를 더합니다. id 없는 줄의 **본문 전체**와 같을 때만 그 줄을 가립니다. 빈 글은 무시합니다. */
        void declareExpected( string_view contains );
        /** @brief 시나리오의 `<ExpectLog id="…">` 선언 하나를 더합니다. 그 id 의 줄은 문구와 무관하게 통과입니다. 빈 글은 무시합니다. */
        void declareExpectedID( string_view id );
        /** @brief 시나리오의 `<AllowLog id="…">` 하나를 더합니다(나와도 되는 줄 — 나오지 않아도 실패가 아니다). 빈 글은 무시합니다. */
        void allowID( string_view id );
        /** @brief 선언과 허용을 지웁니다(연결 실행의 다음 시나리오 앞에서). */
        void clear();

        /** @brief 게이트가 보는 수준(Warning · Error)이면 true 입니다. */
        static bool isGatedLevel( LogLevel level ) { return level == LogLevel::Error || level == LogLevel::Warning; }
        /** @brief 게이트가 보지 않는 줄(실행기 줄)이면 true 입니다. @p message 는 로그 본문입니다. */
        static bool isExemptMessage( string_view message );
        /** @brief @p text(줄 본문 전체)가 `<ExpectLog contains>` 선언 하나와 같으면 true 입니다(부분 문자열이 아니다). */
        bool isDeclared( string_view text ) const;
        /** @brief @p id 가 `<ExpectLog id>` 선언 하나와 같으면 true 입니다. 빈 id(id 없는 줄)는 항상 false 입니다. */
        bool isDeclaredID( string_view id ) const;

        /** @brief 줄 하나를 판정합니다. @p id 가 비었으면 id 없는 줄입니다. */
        AutomationLogVerdict classify( LogLevel level, string_view id, string_view message ) const;
        /** @brief 줄 하나를 판정해 처리하지 않은 줄이면 @p inoutJudgement 에 더하고, 허용이 맞았으면 그 id 를 적습니다. */
        void judge( LogLevel level, string_view caller, string_view id, string_view message, AutomationLogJudgement& inoutJudgement ) const;

    private:
        vector<string> _listExpected{};
        vector<string> _listExpectedID{};
        vector<string> _listAllowedID{};
    };
} // namespace sw
