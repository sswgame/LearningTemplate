#include "pch.h"

#include "Core/Diagnostics/DataRaceReporter.h"

#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/string.h"
#include "Core/Diagnostics/CallStackCapture.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 경합 한 건을 Error 로그와 호출 스택 줄로 남깁니다.
         * @details 호출 스택은 검출기의 `triggerDataRace` 와 이 함수 두 프레임을 건너뛰어, 경합을 일으킨 컨테이너 호출부터 보입니다.
         */
        void report( const utf8* pMessage, const void* pContext, uint32 readerCount, uint32 writerCount )
        {
            CallStack callStack;
            CallStackCapture::capture( callStack, 2 );
            string stackTrace = CallStackCapture::symbolize( callStack );

            SW_LOG_ERROR_ID( "DataRaceDetector.ContextReadersWriters", "%# (context: %p, readers: %#, writers: %#)", pMessage, pContext, readerCount, writerCount );

            StringUtil::forEachLine( stackTrace, []( string_view line )
            {
                if ( line.empty() == false )
                    SW_LOG_ERROR_ID( "DataRaceDetector.StackFrame", "  %#", string( line ).c_str() );
            } );
        }

        /** @brief 건 보고기의 등록입니다. 프로세스 끝까지 쥔다 — 상수 초기화라 다른 TU 의 정적 초기화가 먼저 `install` 을 불러도 된다. */
        RaceDetectContext::ReportFunctionSubscription s_reportFunctionRegistration{};

        /** @brief 프로그램 시작 때 보고기를 겁니다. `Core` 목적 파일을 통째로 넣는 실행 파일(Engine.dll)은 이것으로 충분합니다. */
        [[maybe_unused]] const bool s_bDataRaceReporterInstalled = []() noexcept
        {
            DataRaceReporter::install();
            return true;
        }();
    } // namespace
} // namespace sw

namespace sw
{
    void DataRaceReporter::install()
    {
        // 정적 초기화와 파서(`ReflectionParser` 는 Core 를 정적 링크해 초기화가 빠질 수 있어 직접 부른다)가 함께 부를 수 있다 — 이미 건 주인이면 다시 걸지 않는다.
        if ( s_reportFunctionRegistration.isActive() )
            return;
        s_reportFunctionRegistration = RaceDetectContext::bindReportFunction( &report );
    }
} // namespace sw
