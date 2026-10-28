#include "pch.h"

#include "Engine/Automation/Runner/AutomationLogCapture.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Automation/Runner/AutomationLogPolicy.h"

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
        : _listLogLine{}
        , _mutex{}
        , _listenerSubscription{}
        , _frameIndex{ 0 }
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

    uint32 AutomationLogCapture::countMatchingLines( const string& needle, uint32 sinceFrame ) const
    {
        uint32            matchCount = 0;
        ScopedLock<mutex> lock{ _mutex };
        for ( const AutomationLogLine& line : _listLogLine )
        {
            if ( line._frameIndex >= sinceFrame && line._message.find( needle ) != string::npos )
                ++matchCount;
        }
        return matchCount;
    }

    uint32 AutomationLogCapture::countLinesWithID( const string& id, uint32 sinceFrame ) const
    {
        uint32            matchCount = 0;
        ScopedLock<mutex> lock{ _mutex };
        for ( const AutomationLogLine& line : _listLogLine )
        {
            if ( line._frameIndex >= sinceFrame && line._id == id )
                ++matchCount;
        }
        return matchCount;
    }

    AutomationLogJudgement AutomationLogCapture::judgeLines( const AutomationLogPolicy& policy ) const
    {
        AutomationLogJudgement judgement;
        ScopedLock<mutex>      lock{ _mutex };
        for ( const AutomationLogLine& line : _listLogLine )
        {
            policy.judge( line._level, line._caller, line._id, line._message, judgement );
        }
        return judgement;
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
        ScopedLock<mutex> lock{ _mutex };
        if ( _listLogLine.size() >= AutomationLogCaptureInternal::kMaxLogLine )
            return;
        AutomationLogLine line{};
        line._message    = entry._message;
        line._caller     = entry._caller;
        line._id         = entry._pID != nullptr ? entry._pID : "";
        line._level      = entry._level;
        line._frameIndex = _frameIndex.load( std::memory_order_relaxed );
        _listLogLine.push_back( std::move( line ) );
    }
} // namespace sw
