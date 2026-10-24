/**
 * @file LogTypes.h
 * @brief 로그 한 줄을 나타내는 값 타입들입니다(`LogLevel` · `LogEntry` · `LogRecord`).
 *
 * `Logger`(파사드)와 `ILogOutput`(출력 장치)이 **둘 다** 이 타입들을 쓰므로 따로 둡니다. 한쪽 헤더에 두면 장치 헤더가
 * 파사드를 include 하게 되어 의존 방향이 뒤집힙니다. `Logger.h` 가 이 헤더를 include 합니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Log/LogContext.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) LogLevel / LogEntry / LogRecord — 심각도 · 한 줄 기록 · 비동기 큐 레코드
    // ------------------------------------------------------------------------------
    /**
     * @enum LogLevel
     * @brief 로그의 심각도 수준입니다.
     */
    enum class LogLevel
    {
        Error,   /**< 심각한 오류. 프로그램 흐름에 영향을 준다 */
        Warning, /**< 경고. 잠재적인 문제가 있다 */
        Info,    /**< 일반 정보(초기화, 종료 등) */
        Trace,   /**< 자세한 디버그 추적 정보 */
        Count    /**< 로그 수준의 개수 */
    };

    /** @brief 로그 줄에 붙는 호스트 시계의 종류입니다(`Logger::setClockNumber`). */
    enum class LogClock : uint8
    {
        Tick = 0, ///< 고정 틱(전용 서버) — `[tick=N]`
        Frame     ///< 가변 프레임(리슨 호스트 App) — `[frame=N]`
    };

    /**
     * @brief 텍스트 로그 줄에서 id 가 든 줄의 본문 앞에 붙는 꼬리표 `[id:영역.사건] ` 의 모양입니다.
     * @details 쓰는 쪽(`AsyncLogSink`)과 읽는 쪽(자식 프로세스 출력만 보는 `AppScenarioTest` 의 로그 게이트)이 같은 정의를 쓴다.
     */
    struct LogIDTag
    {
        static constexpr const utf8* kOpen        = "[id:";
        static constexpr size_t      kOpenLength  = 4; ///< `kOpen` 의 글자 수
        static constexpr const utf8* kClose       = "] ";
        static constexpr size_t      kCloseLength = 2; ///< `kClose` 의 글자 수
    };
} // namespace sw

namespace sw
{
    /** @brief 싱크 · 리스너에 넘기는 로그 한 줄입니다. */
    struct LogEntry
    {
        string      _tag;
        string      _caller;
        string      _message;
        string      _file;
        string      _timeStamp;
        LogContext  _context{};      ///< 줄을 쓴 스레드의 로그 문맥(요청 추적 id · 주체) — 리스너가 줄을 거를 수 있게
        const utf8* _pID{ nullptr }; ///< 줄의 종류 id(`SW_LOG_ERROR_ID`)의 정적 글자. id 없이 남긴 줄은 nullptr — 문구가 바뀌어도 같은 사건을 가리킨다
        int32       _line{ 0 };
        LogLevel    _level = LogLevel::Info;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 비동기 lock-free 큐로 보내는 로그 레코드입니다. **출력 장치에 필요한 것이 모두** 들어 있습니다.
     * @details 포맷은 이미 끝나 있고(`_formatted`), 날짜 필드는 파일 출력이 시간별로 파일을 바꿀지 판단할 때 씁니다.
     *          콘솔은 `_level` 로 색만 고릅니다.
     */
    struct LogRecord
    {
        string   _formatted;
        int32    _year{ 0 };
        int32    _month{ 0 };
        int32    _day{ 0 };
        int32    _hour{ 0 };
        LogLevel _level = LogLevel::Info;
    };

    SW_DECLARE_MULTI_CAST_DELEGATE( void, LogWrittenMulticast, const LogEntry& );
    using LogWrittenDelegate = Delegate<void( const LogEntry& )>;
} // namespace sw
