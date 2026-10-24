/**
 * @file AutomationLogCapture.h
 * @brief 자동화 시나리오 동안 남은 로그 줄을 모읍니다. `<ExpectLog>` 가 이것으로 줄을 셉니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Log/LogTypes.h"

namespace sw
{
    struct AutomationLogJudgement;

    class AutomationLogPolicy;

    /** @struct AutomationLogLine @brief 시나리오 동안 남은 로그 한 줄(`ExpectLog` 가 세고 로그 게이트가 본다)입니다. */
    struct AutomationLogLine
    {
        string   _message{};
        string   _caller{}; ///< 로그 호출자 이름(`LocalizationManager` 등) — 게이트 실패 줄에 적는다
        string   _id{};     ///< 줄의 종류 id(`SW_LOG_WARNING_ID`). id 없이 남긴 줄은 빈 글 — 허용 목록으로 가릴 수 없다
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
    class SW_API AutomationLogCapture
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
        /** @brief 프레임 @p sinceFrame 이후 줄 가운데 @p needle 을 포함한 줄 수입니다. */
        uint32 countMatchingLines( const string& needle, uint32 sinceFrame ) const;
        /** @brief 모은 줄을 모두 @p policy 로 판정해 처리하지 않은 줄 · 허용 항목이 맞은 id 를 돌려줍니다. */
        AutomationLogJudgement judgeLines( const AutomationLogPolicy& policy ) const;
        /** @brief 모은 줄 가운데 @p needle 을 담은 줄이 있으면 true 입니다(프레임 무관). */
        bool containsLine( string_view needle ) const;

    private:
        void onLogWritten( const LogEntry& entry );

        vector<AutomationLogLine> _listLogLine; ///< 시나리오 동안 남은 로그(상한 `kMaxLogLine`)
        mutable mutex             _mutex;       ///< `_listLogLine` 을 보호합니다.
        DelegateHandle            _listenerHandle;
        atomic<uint32>            _frameIndex; ///< 로그 줄에 적을 프레임(로그 스레드가 읽는다)
        bool                      _bListening;
    };
} // namespace sw
