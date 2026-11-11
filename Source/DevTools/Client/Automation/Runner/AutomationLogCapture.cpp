#include "pch.h"

#include "DevTools/Client/Automation/Runner/AutomationLogCapture.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Client/Automation/Runner/AutomationLogPolicy.h"

namespace sw
{
    namespace
    {
        struct AutomationLogCaptureInternal
        {
            /** @brief 시나리오 동안 모으는 로그 줄 상한입니다. 넘치면 더 모으지 않습니다(`ExpectLog` 는 그때까지만 셉니다). */
            static constexpr size_t kMaxLogLine = 20000;
        };
    } // namespace
} // namespace sw

namespace sw
{
    AutomationLogCapture::AutomationLogCapture()
        : _mutex{}
        , _listLogLine{}
        , _listenerSubscription{}
        , _frameIndex{ 0 }
        , _warningErrorCount{ 0 }
    {
    }

    AutomationLogCapture::~AutomationLogCapture()
    {
        stop();
    }

    void AutomationLogCapture::start()
    {
        if ( _listenerSubscription.isActive() )
            return;
        _listenerSubscription = Logger::registerGlobalListener( SW_DELEGATE_METHOD( LogWrittenDelegate, &AutomationLogCapture::onLogWritten, this ) );
    }

    void AutomationLogCapture::stop()
    {
        _listenerSubscription.reset();
    }

    uint32 AutomationLogCapture::countLinesWithID( const string& id, const string& contains, uint32 sinceFrame ) const
    {
        uint32            matchCount = 0;
        ScopedLock<mutex> lock{ _mutex };
        for ( const AutomationLogLine& line : _listLogLine )
        {
            const bool bMatch = line._frameIndex >= sinceFrame && line._id == id && ( contains.empty() || line._message.find( contains ) != string::npos );
            if ( bMatch )
                ++matchCount;
        }
        return matchCount;
    }

    AutomationLogCheckResult AutomationLogCapture::evaluateLines( const AutomationLogPolicy& policy ) const
    {
        AutomationLogCheckResult result;
        ScopedLock<mutex>        lock{ _mutex };
        for ( const AutomationLogLine& line : _listLogLine )
        {
            policy.judge( line._level, line._id, line._message, result );
        }
        return result;
    }

    bool AutomationLogCapture::containsLine( string_view needle ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        return VectorUtil::containsIf( _listLogLine, [&]( const AutomationLogLine& line )
        { return string_view( line._message ).find( needle ) != string_view::npos; } );
    }

    void AutomationLogCapture::onLogWritten( const LogEntry& entry )
    {
        // 실행기 자신의 줄([Scenario])은 세지 않는다. 실패 줄이 단언 문구를 반복해 ExpectLog 를 맞추지 않게 하기 위해서다.
        if ( StringUtil::startsWith( string_view{ entry._message }, AutomationLogPolicy::kRunnerPrefix ) )
            return;
        // 시나리오 안에서 도는 단위 테스트(에디터 `editor.unitTests`)의 케이스가 기대한 줄은 그 케이스가 판정한다 — 두 번 세지 않는다.
        if ( test::isExpectedLog( entry._pID ) )
            return;
        if ( entry._level == LogLevel::Warning || entry._level == LogLevel::Error )
            (void)_warningErrorCount.fetch_add( 1, std::memory_order_relaxed ); // 진행 표시용 수 — 판정은 끝의 로그 게이트(`evaluateLines`)가 한다
        ScopedLock<mutex> lock{ _mutex };
        if ( _listLogLine.size() >= AutomationLogCaptureInternal::kMaxLogLine )
            return;
        AutomationLogLine line{};
        line._message    = entry._message;
        line._id         = entry._pID != nullptr ? entry._pID : "";
        line._level      = entry._level;
        line._frameIndex = _frameIndex.load( std::memory_order_relaxed );
        _listLogLine.push_back( std::move( line ) );
    }
} // namespace sw
