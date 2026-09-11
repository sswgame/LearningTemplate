/**
 * @file ConsoleLogOutput.h
 * @brief 표준 출력 로그 장치 (Windows 콘솔 색상 / POSIX ANSI 이스케이프)
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
     * @brief 로그 한 줄을 표준 출력에 레벨 색과 함께 씁니다.
     * @details 콘솔이 없으면(배포본을 탐색기에서 실행하는 경우 등) 색을 포기하고 그냥 씁니다.
     *          Windows 는 그때 `OutputDebugStringA` 로도 보내 디버거에 붙어 있으면 보이게 합니다.
     */
    class SW_API ConsoleLogOutput final : public ILogOutput
    {
    public:
        ConsoleLogOutput();
        virtual ~ConsoleLogOutput() override;

        ConsoleLogOutput( const ConsoleLogOutput& )            = delete;
        ConsoleLogOutput& operator=( const ConsoleLogOutput& ) = delete;

        /** @brief 콘솔 코드 페이지를 UTF-8 로 맞추고 핸들·기본 색상을 캐시합니다. */
        bool open() override;
        /** @brief 표준 출력을 비웁니다 (핸들은 우리 것이 아니라 닫지 않습니다). */
        void close() override;
        /** @brief 레벨 색으로 한 줄을 씁니다. Error 는 즉시 flush 합니다. */
        void write( const LogRecord& record ) override;

    private:
        mutex  _mutex;                   ///< 이 장치 전용 — 파일 출력과 더 이상 락을 공유하지 않는다
        void*  _pCachedConsoleHandle;    ///< GetStdHandle(STD_OUTPUT_HANDLE) 캐시 (Windows)
        uint16 _defaultConsoleAttribute; ///< 초기 콘솔 텍스트 색상 속성 (Windows)
        bool   _bHasConsole;             ///< 표준 출력 콘솔 유효성 여부
    };
} // namespace sw
