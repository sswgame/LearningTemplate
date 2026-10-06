/**
 * @file ConsoleLogOutput.h
 * @brief 표준 출력 로그 장치입니다(Windows 콘솔 색상 / POSIX ANSI 이스케이프).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Log/ILogOutput.h"

namespace sw
{
    /**
     * @class ConsoleLogOutput
     * @brief 로그 한 줄을 수준별 색과 함께 표준 출력에 씁니다.
     * @details 콘솔이 없으면(배포본을 탐색기에서 실행한 경우 등) 색 없이 그냥 씁니다. Windows 에서는 그때
     *          `OutputDebugStringA` 로도 보내, 디버거가 붙어 있으면 보이게 합니다.
     */
    class SW_API ConsoleLogOutput final : public ILogOutput
    {
    public:
        ConsoleLogOutput();
        virtual ~ConsoleLogOutput() override;

        ConsoleLogOutput( const ConsoleLogOutput& )            = delete;
        ConsoleLogOutput& operator=( const ConsoleLogOutput& ) = delete;

        /** @brief 콘솔 코드 페이지를 UTF-8 로 맞추고, 핸들과 기본 색상을 캐시합니다. */
        [[nodiscard]] bool open() override;
        /** @brief 표준 출력을 비웁니다(핸들은 이 장치가 연 것이 아니므로 닫지 않습니다). */
        void close() override;
        /** @brief 수준별 색으로 한 줄을 씁니다. Error 는 바로 flush 합니다(`setFlushEveryLine` 이면 모든 줄). */
        void write( const LogRecord& record ) override;

        /**
         * @brief 줄마다 표준 출력을 비울지 정합니다(기본 꺼짐 — Error 만 비운다). 전용 서버가 켭니다: 자식 프로세스로 띄운 시험 · journald ·
         *        `docker logs` 가 "Dedicated server ready" 같은 줄을 바로 봐야 한다. 프로세스 전역입니다.
         */
        static void setFlushEveryLine( bool bFlushEveryLine );

    private:
        mutex                   _mutex;                   ///< 이 장치 전용. 파일 출력과 락을 공유하지 않는다
        void*                   _pCachedConsoleHandle;    ///< GetStdHandle(STD_OUTPUT_HANDLE) 캐시(Windows)
        [[maybe_unused]] uint16 _defaultConsoleAttribute; ///< 처음 콘솔 텍스트 색상 속성(Windows. 다른 플랫폼은 읽지 않는다)
        bool                    _bHasConsole;             ///< 표준 출력이 콘솔(터미널)인지 — 아니면 색을 쓰지 않는다(리눅스는 journald · 파일 · 파이프에 이스케이프가 섞이지 않게)
    };
} // namespace sw
