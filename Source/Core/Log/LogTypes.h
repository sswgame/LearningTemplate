/**
 * @file LogTypes.h
 * @brief 로그 한 줄을 나타내는 값 타입들 (`LogLevel` · `LogEntry` · `LogRecord`)
 *
 * `Logger`(파사드)와 `ILogOutput`(장치)이 **둘 다** 이 타입들을 쓰므로 따로 둡니다. 한쪽 헤더에
 * 두면 장치 헤더가 파사드를 include 하게 되어 방향이 뒤집힙니다.
 *
 * `Logger.h` 가 이 헤더를 include 하므로, 기존에 `Logger.h` 만 include 하던 코드는 그대로입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) LogLevel / LogEntry / LogRecord — 심각도 + 한 줄 기록 + 비동기 큐 레코드
    // ------------------------------------------------------------------------------
    /**
     * @enum LogLevel
     * @brief 로그의 중요도/심각도 레벨 식별자
     */
    enum class LogLevel
    {
        Error,   /**< 심각한 오류, 프로그램 흐름에 영향 */
        Warning, /**< 경고, 잠재적인 문제 내포 */
        Info,    /**< 일반적인 정보 (초기화, 종료 등) */
        Trace,   /**< 상세한 디버그 추적 정보 */
        Count    /**< 로그 레벨의 총 개수 */
    };

    /** @brief 싱크·리스너에 넘기는 한 줄입니다. */
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
     * @brief 비동기 락-프리 큐 전송용 로그 레코드 — **출력 장치가 필요로 하는 전부**입니다.
     * @details 포맷은 이미 끝나 있고(`_formatted`), 날짜 필드는 파일 출력이 시간별 롤오버를
     *          판단하는 데 씁니다. 콘솔은 `_level` 로 색만 고릅니다.
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
