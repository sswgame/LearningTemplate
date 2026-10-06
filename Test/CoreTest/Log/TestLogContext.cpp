#include "pch.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Log/ILogOutput.h"
#include "Core/Log/LogContext.h"
#include "Core/Log/Logger.h"

#include "TestFramework/TestFramework.h"

#include <thread>

// 로그 문맥 — 범위가 앞 문맥을 되돌림, 스레드마다 따로, 꼬리표 글(있는 칸만), 문맥 있는 줄에만 꼬리표 · 없는 줄은 예전과 같은 글, 리스너의 LogEntry 에 문맥.

using namespace sw;

namespace
{
    /** @brief 완성된 줄을 모으는 출력 장치입니다(로거 하나에 붙인다). */
    class LogContextCaptureOutput final : public ILogOutput
    {
    public:
        bool open() override { return true; }
        void close() override {}
        void write( const LogRecord& record ) override
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _listFormatted.push_back( record._formatted );
        }

        vector<string> _listFormatted{};
        mutex          _mutex{};
    };

    struct LogContextEntryCapture
    {
        vector<LogEntry> _listEntry{};

        void onLog( const LogEntry& entry ) { _listEntry.push_back( entry ); }
    };

    void readOtherThread( bool* pOutEmpty ) { *pOutEmpty = LogContext::getCurrent().isEmpty(); }

    string formatTagOf( const LogContext& context )
    {
        utf8        arrTag[LogContext::kMaxTagSize];
        const int32 length = context.formatTag( arrTag, LogContext::kMaxTagSize );
        return string( arrTag, static_cast<size_t>( length ) );
    }
} // namespace

SW_TEST_CASE( LogContextTest, ScopeRestoresThePreviousContext )
{
    SW_EXPECT_TRUE( LogContext::getCurrent().isEmpty() );
    LogContext outer;
    outer._principalId = 1;
    {
        ScopedLogContext outerScope( outer );
        LogContext       inner;
        inner._traceId = LogTraceId{ 0, 9 };
        {
            ScopedLogContext innerScope( inner );
            SW_EXPECT_TRUE( LogContext::getCurrent()._traceId == inner._traceId );
            SW_EXPECT_EQUAL( LogContext::getCurrent()._principalId, uint64( 0 ) );
        }
        SW_EXPECT_EQUAL( LogContext::getCurrent()._principalId, uint64( 1 ) );
    }
    SW_EXPECT_TRUE( LogContext::getCurrent().isEmpty() );
}

SW_TEST_CASE( LogContextTest, ThreadsDoNotShareTheContext )
{
    LogContext context;
    context._principalId = 7;
    ScopedLogContext scope( context );
    bool             bOtherEmpty = false;
    std::thread      thread( &readOtherThread, &bOtherEmpty );
    thread.join();
    SW_EXPECT_TRUE( bOtherEmpty );
    SW_EXPECT_EQUAL( LogContext::getCurrent()._principalId, uint64( 7 ) );
    const LogTraceId first = LogTraceId::makeRandom();
    SW_EXPECT_TRUE( first.isValid() );
    SW_EXPECT_TRUE( first != LogTraceId::makeRandom() );
}

SW_TEST_CASE( LogContextTest, TagHasOnlyThePresentFields )
{
    LogContext context;
    SW_EXPECT_TRUE( formatTagOf( context ).empty() );
    context._traceId = LogTraceId{ 0x0123456789abcdefull, 0x1 };
    SW_EXPECT_TRUE( formatTagOf( context ) == "[trace=0123456789abcdef0000000000000001] " );
    context._principalId = 0x42;
    const string both    = formatTagOf( context );
    SW_EXPECT_TRUE( both == "[trace=0123456789abcdef0000000000000001 acct=0000000000000042] " );
    SW_EXPECT_EQUAL( static_cast<int32>( both.size() ), LogContext::kMaxTagSize - 1 ); // 가장 긴 꼬리표가 버퍼에 꼭 맞는다
    context._traceId = LogTraceId{};
    SW_EXPECT_TRUE( formatTagOf( context ) == "[acct=0000000000000042] " );
    utf8 arrSmall[LogContext::kMaxTagSize - 1];
    SW_EXPECT_EQUAL( context.formatTag( arrSmall, LogContext::kMaxTagSize - 1 ), 0 ); // 모자란 버퍼는 빈 글
    SW_EXPECT_EQUAL( arrSmall[0], utf8( '\0' ) );
}

SW_TEST_CASE( LogContextTest, OnlyLinesWithAContextCarryTheTag )
{
    Logger logger;
    logger.initialize();
    unique_ptr<LogContextCaptureOutput> output   = make_unique<LogContextCaptureOutput>();
    LogContextCaptureOutput*            pCapture = output.get();
    SW_ASSERT_TRUE( logger.addOutput( std::move( output ) ) );
    LogContextEntryCapture entryCapture;
    (void)logger.addLogWrittenListener( LogWrittenDelegate::create<&LogContextEntryCapture::onLog>( &entryCapture ) );

    logger.writeLog( LogLevel::Info, "Test", "Probe", "plain line", __FILE__, __LINE__ );
    LogContext context;
    context._traceId     = LogTraceId{ 0x0123456789abcdefull, 0x1 };
    context._principalId = 0x42;
    {
        ScopedLogContext scope( context );
        logger.writeLog( LogLevel::Info, "Test", "Probe", "context line", __FILE__, __LINE__ );
    }
    logger.shutdown();

    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( pCapture->_listFormatted.size() ) );
    const string& plain  = pCapture->_listFormatted[0];
    const string& tagged = pCapture->_listFormatted[1];
    SW_EXPECT_TRUE_MSG( plain.find( "[Test] [Probe] [Info] - plain line\n" ) != string::npos, plain.c_str() ); // 문맥 없는 줄은 예전 형식 그대로
    SW_EXPECT_TRUE_MSG( tagged.find( "[Test] [Probe] [Info] [trace=0123456789abcdef0000000000000001 acct=0000000000000042] - context line\n" ) != string::npos,
                        tagged.c_str() );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( entryCapture._listEntry.size() ) );
    SW_EXPECT_TRUE( entryCapture._listEntry[0]._context.isEmpty() );
    SW_EXPECT_TRUE( entryCapture._listEntry[1]._context._traceId == context._traceId );
    SW_EXPECT_EQUAL( entryCapture._listEntry[1]._context._principalId, uint64( 0x42 ) );
}
