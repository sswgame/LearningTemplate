#include "pch.h"

#include "Core/Diagnostics/DataRaceReporter.h"

#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/string.h"
#include "Core/Log/Logger.h"
#include "Core/Process/CallStackCapture.h"

namespace sw
{
    SW_LOG_CALLER( "DataRaceDetector" );

    namespace
    {
        struct DataRaceReporterInternal
        {
            /**
             * @brief 경합 한 건을 Error 로그와 호출 스택 줄로 남깁니다.
             * @details 호출 스택은 검출기의 `triggerDataRace` 와 이 함수 두 프레임을 건너뛰어, 경합을 일으킨 컨테이너 호출부터 보입니다.
             */
            static void report( const utf8* pMessage, const void* pContext, uint32 readerCount, uint32 writerCount )
            {
                CallStack callStack;
                CallStackCapture::capture( callStack, 2 );
                string stackTrace = CallStackCapture::symbolize( callStack );

                SW_LOG_ERROR( "%s (ctx: %p, readers: %u, writers: %u)", pMessage, pContext, readerCount, writerCount );

                size_t start{ 0 };
                while ( start < stackTrace.size() )
                {
                    size_t end = stackTrace.find( '\n', start );
                    if ( end == string::npos )
                        end = stackTrace.size();
                    string line = stackTrace.substr( start, end - start );
                    if ( line.empty() == false )
                        SW_LOG_ERROR( "  %s", line.c_str() );
                    start = end + 1;
                }
            }
        };

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
        RaceDetectContext::setReportFunction( &DataRaceReporterInternal::report );
    }
} // namespace sw
