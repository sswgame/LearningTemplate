/**
 * @file LogTypes.h
 * @brief 로그 한 줄을 나타내는 값 타입들입니다(`LogLevel` · `LogEntry` · `LogRecord`).
 *
 * `Logger`(파사드)와 `ILogOutput`(출력 장치)이 **둘 다** 이 타입들을 쓰므로 따로 둡니다. 한쪽 헤더에 두면 장치 헤더가
 * 파사드를 include 하게 되어 의존 방향이 뒤집힙니다.
 *
 * `Logger.h` 가 이 헤더를 include 하므로, 기존에 `Logger.h` 만 include 하던 코드는 그대로 동작합니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"

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

    /** @brief 싱크 · 리스너에 넘기는 로그 한 줄입니다. */
    struct LogEntry
    {
        string   _tag;
        string   _caller;
        string   _message;
        string   _file;
        string   _timeStamp;
        int32    _line{ 0 };
        LogLevel _level = LogLevel::Info;
    };

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
