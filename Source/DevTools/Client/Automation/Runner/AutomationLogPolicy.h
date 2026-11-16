/**
 * @file AutomationLogPolicy.h
 * @brief 자동화 실행의 로그 게이트입니다. 시나리오 구간에 남은 `[Warning]` · `[Error]` 줄 가운데 처리하지 않은 줄을 가려냅니다.
 * @details 언리얼 Automation 의 `AddExpectedError`(기대한 줄 선언)와 "경고를 실패로" 설정을 합친 자리다. 줄 하나는 아래 순서로 판정한다(`classify`).
 *          1. 실행기 줄(`[Scenario]`)은 보지 않는다.
 *          2. 시나리오의 `<ExpectLog id="…">` 가 선언한 id 의 줄은 통과다(`count="0"` 은 선언이 아니다). 선언은 id 로만 한다 — 문장으로는 선언하지 못한다.
 *          3. 로그 id(`SW_LOG_WARNING_ID` · `SW_LOG_ERROR_ID`)가 없는 줄은 허용할 수 없다 — 소스에 id 를 붙인다(`MissingID`).
 *          4. id 가 있으면 그 시나리오의 `<AllowLog id="…" reason="…"/>` 가 허용한다. 없으면 `NotAllowed` 다.
 *          허용은 시나리오 파일 안에 사유와 함께 둔다 — 예전의 전역 허용 목록(모든 시나리오의 허용이 한 파일에 모이고 범위 · 계획 칸을 손으로 맞추던 것)은 없다.
 *          한 벌을 세션(`AutomationSession`)이 들고 단계의 판정(`AutomationEvaluator`)과 에디터 콘솔이 함께 읽는다 — 선언한 줄을 게이트는 통과시키고 콘솔은 흐리게 그린다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/LogTypes.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    /** @brief 로그 줄 하나의 판정입니다. */
    enum class AutomationLogResult : uint8
    {
        NotChecked, ///< 게이트가 보지 않는 줄(Info 이하, 실행기 줄)
        Declared,   ///< 시나리오의 `<ExpectLog>` 가 선언한 줄
        Allowed,    ///< 시나리오의 `<AllowLog>` 가 허용한 id 의 줄
        NotAllowed, ///< id 는 있으나 허용하지 않은 줄
        MissingID,  ///< id 가 없는 줄 — 허용할 수 없다
    };

    /** @struct AutomationLogCheckResult @brief 한 실행에서 `evaluate` 가 모은 결과입니다. 처리하지 않은 줄이 하나라도 있으면 실패입니다. */
    struct AutomationLogCheckResult
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
    class AutomationScenario;

    /**
     * @class AutomationLogPolicy
     * @brief 시나리오의 `<ExpectLog>` 선언과 `<AllowLog>` 허용을 들고 로그 줄을 판정합니다.
     * @details 단계의 실행기 · 판정은 단계마다 새로 서고 사라지지만 이 객체는 세션 수명이다(세션이 `shared_ptr` 로 들고 판정 · 콘솔이 같은 것을 나눠 든다 — 단계 객체를
     *          다른 스레드가 가리키지 않는다). 로그는 아무 스레드에서나 오고 콘솔은 받는 순간 묻기 때문에 목록은 안에서 잠근다(`_mutex`). 단계가 바뀌면 `prepare` 가 앞 단계의 선언을 지운다.
     *          언리얼 Automation 의 `AddExpectedError` 도 기대한 오류를 로그 출력에서 따로 표시한다 — 판정과 표시가 한 선언을 읽는다.
     */
    class SW_DEVTOOLS_API AutomationLogPolicy
    {
    public:
        /** @brief 실행기 자신의 줄 앞부분입니다(게이트가 보지 않는다). */
        static constexpr const utf8* kRunnerPrefix = "[Scenario]";

        /**
         * @brief 시나리오 하나의 선언과 허용으로 바꿉니다(앞 단계의 것은 지운다). `<AllowLog id>` 는 허용, `<ExpectLog id>` 는 선언이다 — `count="0"` 은
         *        "이 줄이 없어야 한다" 는 단언이지 선언이 아니다(그 줄이 나오면 단언과 로그 게이트가 모두 진다).
         */
        void prepare( const AutomationScenario& scenario );
        /** @brief 선언과 허용을 지웁니다(단계의 판정이 사라질 때 — 단계 사이의 줄은 어느 시나리오의 선언도 아니다). */
        void clear();

        /** @brief 게이트가 보는 수준(Warning · Error)이면 true 입니다. */
        static bool isCheckedLevel( LogLevel level ) { return level == LogLevel::Error || level == LogLevel::Warning; }
        /** @brief 게이트가 보지 않는 줄(실행기 줄)이면 true 입니다. @p message 는 로그 본문입니다. */
        static bool isExemptMessage( string_view message );

        /** @brief 줄 하나를 판정합니다. @p id 가 비었으면 id 없는 줄입니다. 아무 스레드에서나 부를 수 있다(에디터 콘솔은 `Declared` 를 흐리게 그린다). */
        AutomationLogResult classify( LogLevel level, string_view id, string_view message ) const;
        /** @brief 줄 하나를 판정해 처리하지 않은 줄이면 @p inoutJudgement 에 더하고, 허용이 맞았으면 그 id 를 적습니다. */
        void evaluate( LogLevel level, string_view id, string_view message, AutomationLogCheckResult& inoutResult ) const;

    private:
        /** @brief @p id 가 `<ExpectLog id>` 선언 하나와 같으면 true 입니다. 빈 id(id 없는 줄)는 항상 false 입니다. */
        bool isDeclaredIDLocked( string_view id ) const SW_REQUIRES_LOCK( _mutex );

    private:
        mutable mutex                  _mutex{};
        vector<string> _listExpectedID SW_GUARDED_BY( _mutex ){};
        vector<string> _listAllowedID  SW_GUARDED_BY( _mutex ){};
    };
} // namespace sw
