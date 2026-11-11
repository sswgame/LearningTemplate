/**
 * @file AutomationLogCapture.h
 * @brief 자동화 시나리오 동안 남은 로그 줄을 모읍니다. `<ExpectLog>` 가 이것으로 줄을 셉니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/LogTypes.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    struct AutomationLogJudgement;

    class AutomationLogPolicy;

    /** @struct AutomationLogLine @brief 시나리오 동안 남은 로그 한 줄(`ExpectLog` 가 세고 로그 게이트가 본다)입니다. */
    struct AutomationLogLine
    {
        string   _message{};
        string   _id{}; ///< 줄의 종류 id(`SW_LOG_WARNING_ID`). id 없이 남긴 줄은 빈 글 — 허용 목록으로 가릴 수 없다
        uint32   _frameIndex{ 0 };
        LogLevel _level{ LogLevel::Info };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationLogCapture
     * @brief 전역 로그 리스너를 등록해 시나리오 동안의 로그 줄을 프레임 번호와 함께 모읍니다(`AutomationRunner` 의 값 멤버).
     * @details 로그는 아무 스레드에서나 오므로 줄 목록은 잠금으로 보호하고, 줄에 적을 프레임 번호는 원자 변수로 읽는다.
     *          실행기 자신의 줄(`[Scenario]`)은 모으지 않는다 — 실패 줄이 단언 문구를 반복해 `ExpectLog` 를 맞추지 않게 하기 위해서다.
     *          리스너가 `this` 를 가리키므로 복사 · 이동하지 않는다.
     */
    class SW_DEVTOOLS_API AutomationLogCapture
    {
    public:
        AutomationLogCapture();
        /** @brief 아직 등록돼 있으면 리스너를 해제합니다. */
        ~AutomationLogCapture();

        AutomationLogCapture( const AutomationLogCapture& )            = delete;
        AutomationLogCapture& operator=( const AutomationLogCapture& ) = delete;

        /** @brief 전역 로그 리스너를 등록해 모으기 시작합니다. */
        void start();
        /** @brief 리스너를 해제합니다. 모은 줄은 그대로 둡니다. 이미 멈췄으면 아무것도 하지 않습니다. */
        void stop();

        /** @brief 이후 들어오는 줄에 적을 시나리오 프레임 번호입니다(게임 스레드가 프레임 시작에 정한다). */
        void setFrameIndex( uint32 frameIndex ) { _frameIndex.store( frameIndex, std::memory_order_relaxed ); }
        /**
         * @brief 프레임 @p sinceFrame 이후 줄 가운데 종류 id 가 @p id 이고 본문이 @p contains 를 담은 줄 수입니다(@p contains 가 비면 id 만 본다).
         * @details 사건은 id 로 센다 — 문구가 바뀌어도 같은 사건을 센다. @p contains 는 그 사건의 값(경로 · 수)을 좁히는 데만 쓴다.
         */
        uint32 countLinesWithID( const string& id, const string& contains, uint32 sinceFrame ) const;
        /** @brief 모은 줄을 모두 @p policy 로 판정해 처리하지 않은 줄 · 허용 항목이 맞은 id 를 돌려줍니다. */
        AutomationLogJudgement evaluateLines( const AutomationLogPolicy& policy ) const;
        /** @brief 모은 줄 가운데 @p needle 을 담은 줄이 있으면 true 입니다(프레임 무관). */
        bool containsLine( string_view needle ) const;
        /** @brief 지금까지 모은 Warning · Error 줄 수입니다(허용 판정 전 — 진행 표시용, 판정은 `evaluateLines`). 아무 스레드에서나 읽는다. */
        uint32 getWarningErrorCount() const { return _warningErrorCount.load( std::memory_order_relaxed ); }

    private:
        void onLogWritten( const LogEntry& entry );

    private:
        mutable mutex                          _mutex;
        vector<AutomationLogLine> _listLogLine SW_GUARDED_BY( _mutex ); ///< 시나리오 동안 남은 로그(상한 `kMaxLogLine`) — 로그를 쓴 스레드가 넣는다
        Subscription                           _listenerSubscription;   ///< 전역 로그 리스너 구독(`start` ~ `stop`). 비어 있으면 듣지 않는다
        atomic<uint32>                         _frameIndex;             ///< 로그 줄에 적을 프레임(로그 스레드가 읽는다)
        atomic<uint32>                         _warningErrorCount;      ///< 모은 Warning · Error 줄 수(로그를 쓴 스레드가 올린다, 상한 없이 센다)
    };
} // namespace sw
