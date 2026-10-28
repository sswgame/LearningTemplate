#include "pch.h"

#include "Core/Log/LogContext.h"
#include "Core/LogSink/AsyncLogSink.h"
#include "Core/LogSink/ILogOutput.h"
#include "Core/LogSink/JSONLinesLogOutput.h"

#include "TestFramework/TestFramework.h"

// JSON lines 로그 — 칸과 이스케이프, 문맥의 추적 id, 장치가 달렸을 때만 싱크가 줄마다 JSON 을 만든다.

using namespace sw;

namespace
{
    /** @brief 구조화 칸을 읽는 장치입니다 — 달리면 싱크가 줄마다 `_json` 을 채웁니다. */
    class JSONCaptureOutput final : public ILogOutput
    {
    public:
        [[nodiscard]] bool open() override { return true; }
        void               close() override {}
        bool               needsStructuredFields() const override { return true; }
        void               write( const LogRecord& record ) override
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _listJSON.push_back( record._json );
        }

        vector<string> _listJSON{};
        mutex          _mutex{};
    };

    /** @brief 구조화 칸을 읽지 않는 장치입니다 — 달려도 싱크가 JSON 을 만들지 않습니다. */
    class PlainCaptureOutput final : public ILogOutput
    {
    public:
        [[nodiscard]] bool open() override { return true; }
        void               close() override {}
        void               write( const LogRecord& record ) override
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _listJSON.push_back( record._json );
        }

        vector<string> _listJSON{};
        mutex          _mutex{};
    };
} // namespace

SW_TEST_CASE( JSONLinesLogOutputTest, EscapesQuotesBackslashesAndControlCharacters )
{
    string escaped;
    JSONLinesLogOutput::appendEscaped( escaped, "a\"b\\c\nd\te\x01" );
    SW_EXPECT_TRUE_MSG( escaped == "a\\\"b\\\\c\\nd\\te\\u0001", escaped.c_str() );
    string korean;
    JSONLinesLogOutput::appendEscaped( korean, "서버" );
    SW_EXPECT_TRUE( korean == "서버" ); // UTF-8 이어짐 바이트는 그대로
}

SW_TEST_CASE( JSONLinesLogOutputTest, LineHasTheFieldsThatArePresent )
{
    JSONLogFields fields;
    fields._time             = "2026-10-28 9:5:3";
    fields._unixMilliseconds = 1234;
    fields._level            = LogLevel::Warning;
    fields._tag              = "Server";
    fields._message          = "hello \"world\"";
    fields._file             = "A.cpp";
    fields._line             = 7;
    string line;
    JSONLinesLogOutput::appendLine( line, fields );
    SW_EXPECT_TRUE_MSG( line == "{\"time\":\"2026-10-28 9:5:3\",\"ts\":1234,\"level\":\"warning\",\"tag\":\"Server\",\"msg\":\"hello \\\"world\\\"\",\"file\":\"A.cpp\",\"line\":7}\n",
                        line.c_str() ); // 호출자 · 틱 · 추적 id · 계정 · id 칸은 없는 값이라 빠진다

    fields._caller               = "Probe";
    fields._id                   = "Audit.Login";
    fields._bHasClock            = true;
    fields._clock                = LogClock::Tick;
    fields._clockNumber          = 42;
    fields._context._traceID     = LogTraceID{ 0x0123456789abcdefull, 0x1 };
    fields._context._principalID = 0x42;
    line.clear();
    JSONLinesLogOutput::appendLine( line, fields );
    SW_EXPECT_TRUE_MSG( line.find( "\"caller\":\"Probe\",\"tick\":42,\"trace\":\"0123456789abcdef0000000000000001\",\"acct\":\"0000000000000042\",\"id\":\"Audit.Login\",\"msg\"" ) != string::npos,
                        line.c_str() );
    SW_EXPECT_TRUE( line.back() == '\n' );
}

SW_TEST_CASE( JSONLinesLogOutputTest, SinkBuildsJSONOnlyWhenAStructuredOutputIsAttached )
{
    AsyncLogSink logger;
    logger.initialize();
    unique_ptr<PlainCaptureOutput> plain  = make_unique<PlainCaptureOutput>();
    PlainCaptureOutput*            pPlain = plain.get();
    SW_ASSERT_TRUE( logger.addOutput( std::move( plain ) ) );
    logger.writeLog( LogLevel::Info, "Test", "Probe", "before", __FILE__, __LINE__ );

    unique_ptr<JSONCaptureOutput> json  = make_unique<JSONCaptureOutput>();
    JSONCaptureOutput*            pJSON = json.get();
    SW_ASSERT_TRUE( logger.addOutput( std::move( json ) ) );
    LogContext context;
    context._traceID = LogTraceID{ 0, 9 };
    {
        ScopedLogContext scope( context );
        logger.writeLogWithID( LogLevel::Error, "Test", "Probe", "after", __FILE__, __LINE__, "Test.Json" );
    }
    logger.shutdown();

    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( pPlain->_listJSON.size() ) );
    SW_EXPECT_TRUE( pPlain->_listJSON[0].empty() ); // 장치가 달리기 전의 줄은 JSON 을 만들지 않았다
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( pJSON->_listJSON.size() ) );
    const string& line = pJSON->_listJSON[0];
    SW_EXPECT_TRUE_MSG( line.find( "\"level\":\"error\"" ) != string::npos, line.c_str() );
    SW_EXPECT_TRUE_MSG( line.find( "\"trace\":\"00000000000000000000000000000009\"" ) != string::npos, line.c_str() );
    SW_EXPECT_TRUE_MSG( line.find( "\"id\":\"Test.Json\",\"msg\":\"after\"" ) != string::npos, line.c_str() );
}
