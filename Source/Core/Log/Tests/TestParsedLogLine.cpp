#include "pch.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Log/ParsedLogLine.h"
#include "Core/Log/Sink/AsyncLogSink.h"
#include "Core/Log/Sink/ILogOutput.h"
#include "Core/Testing/TestFramework.h"

// 로그 줄 모양 — 싱크가 쓴 줄을 읽는 쪽이 같은 수준 · id · 본문으로 되읽는다(자식 프로세스 중계가 기댄다). 로그 줄이 아닌 줄은 거절한다.

using namespace sw;

namespace
{
    /** @brief 완성된 줄을 모으는 출력 장치입니다. */
    class LogLineCaptureOutput final : public ILogOutput
    {
    public:
        mutex              _mutex;
        vector<string>     _listFormatted;
        [[nodiscard]] bool open() override { return true; }
        void               close() override {}
        void               write( const LogRecord& record ) override
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _listFormatted.push_back( record._formatted );
        }
    };
} // namespace

SW_TEST_CASE( LogLineFormatTest, ParsesWhatTheSinkWrote )
{
    AsyncLogSink logger;
    logger.initialize();
    unique_ptr<LogLineCaptureOutput> output   = make_unique<LogLineCaptureOutput>();
    LogLineCaptureOutput*            pCapture = output.get();
    SW_REQUIRE_TRUE( logger.addOutput( std::move( output ) ) );
    logger.writeLogWithID( LogLevel::Warning, "Test", "value [Error] - inside", __FILE__, __LINE__, "Probe.Event" );
    logger.writeLogWithID( LogLevel::Error, "Test", "no caller", __FILE__, __LINE__, "Probe.NoCaller" );
    logger.shutdown();

    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( pCapture->_listFormatted.size() ) );
    ParsedLogLine parsed;
    const string& first = pCapture->_listFormatted[0];
    SW_REQUIRE_TRUE( ParsedLogLine::tryParse( string_view( first ).substr( 0, first.find( '\n' ) ), parsed ) );
    SW_EXPECT_TRUE( parsed._level == LogLevel::Warning ); // 본문의 "[Error]" 를 수준으로 읽지 않는다
    SW_EXPECT_TRUE( parsed._id == "Probe.Event" );
    SW_EXPECT_TRUE( parsed._message == "value [Error] - inside" );
    const string& second = pCapture->_listFormatted[1];
    SW_REQUIRE_TRUE( ParsedLogLine::tryParse( string_view( second ).substr( 0, second.find( '\n' ) ), parsed ) );
    SW_EXPECT_TRUE( parsed._level == LogLevel::Error );
    SW_EXPECT_TRUE( parsed._id == "Probe.NoCaller" );
    SW_EXPECT_TRUE( parsed._message == "no caller" );
    SW_EXPECT_FALSE( ParsedLogLine::tryParse( " -> D:/a.cpp:12", parsed ) ); // 소스 위치 줄
    SW_EXPECT_FALSE( ParsedLogLine::tryParse( "plain stdout text", parsed ) );
}
