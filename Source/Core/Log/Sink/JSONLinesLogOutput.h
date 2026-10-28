/**
 * @file JSONLinesLogOutput.h
 * @brief JSON lines 로그 장치입니다. 한 줄에 JSON 객체 하나를 표준 출력에 씁니다(컨테이너 · 로그 수집기가 그대로 먹는 모양).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Log/LogContext.h"
#include "Core/Log/LogTypes.h"
#include "Core/Log/Sink/ILogOutput.h"

namespace sw
{
    /** @brief JSON 한 줄의 재료입니다. 글 칸은 줄을 만드는 동안만 빌려 씁니다. */
    struct JSONLogFields
    {
        string_view _time{};                ///< 지역 시각 글(`2026-10-28 9:5:3`)
        int64       _unixMilliseconds{ 0 }; ///< UTC 유닉스 밀리초(수집기가 시각 글을 해석하지 않아도 되게)
        LogLevel    _level{ LogLevel::Info };
        string_view _tag{};
        string_view _caller{};  ///< 비어 있으면 칸을 뺀다
        string_view _message{}; ///< 유효한 UTF-8 이어야 한다(잘못된 바이트는 부르는 쪽이 `\xNN` 으로 바꾼다)
        string_view _id{};      ///< 줄의 종류 id(`SW_LOG_ERROR_ID`). 비어 있으면 칸을 뺀다
        string_view _file{};
        int32       _line{ 0 };
        LogContext  _context{}; ///< 요청 추적 id · 계정 id. 있는 칸만 `trace` · `acct` 로 쓴다
        bool        _bHasClock{ false };
        LogClock    _clock{ LogClock::Tick };
        uint64      _clockNumber{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class JSONLinesLogOutput
     * @brief 줄마다 `{"time":…,"level":…,"tag":…,"msg":…}` 한 줄을 표준 출력에 씁니다.
     * @details 칸: `time` · `ts`(유닉스 밀리초) · `level`(`error` · `warning` · `info` · `trace`) · `tag` · `caller` · `tick`/`frame` · `trace` · `acct` · `id` · `msg` · `file` · `line`.
     *          `trace` 는 로그 문맥의 요청 추적 id(16 진 32 자)라서 감사 줄과 요청을 한 값으로 묶는다. 표준 출력에는 이 장치 하나만 쓰도록 서버가 콘솔 장치를
     *          끈다(`ConsoleLogOutput::setMuted`). 줄은 싱크가 만들어 `LogRecord::_json` 에 담아 오므로 이 장치는 쓰기만 한다.
     */
    class SW_API JSONLinesLogOutput final : public ILogOutput
    {
    public:
        JSONLinesLogOutput();
        virtual ~JSONLinesLogOutput() override;

        JSONLinesLogOutput( const JSONLinesLogOutput& )            = delete;
        JSONLinesLogOutput& operator=( const JSONLinesLogOutput& ) = delete;

        [[nodiscard]] bool open() override { return true; }
        void               close() override;
        void               write( const LogRecord& record ) override;
        void               flushWithoutWaiting() override;
        bool               needsStructuredFields() const override { return true; }

        /** @brief @p fields 를 JSON 한 줄(끝의 줄바꿈 포함)로 만들어 @p outLine 뒤에 붙입니다. 싱크와 테스트가 같이 씁니다. */
        static void appendLine( string& outLine, const JSONLogFields& fields );
        /** @brief @p text 를 JSON 문자열 본문으로 이스케이프해 붙입니다(따옴표는 붙이지 않는다). 제어 문자는 `\u00XX` 입니다. */
        static void appendEscaped( string& outText, string_view text );

    private:
        mutex _mutex; ///< 이 장치 전용
    };
} // namespace sw
