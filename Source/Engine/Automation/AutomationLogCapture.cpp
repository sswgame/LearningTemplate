#include "pch.h"

#include "Engine/Automation/AutomationLogCapture.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"

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
        , _listenerHandle{}
        , _frameIndex{ 0 }
        , _bListening{ false }
    {
    }

    AutomationLogCapture::~AutomationLogCapture()
    {
        stop();
    }

    void AutomationLogCapture::start()
    {
        if ( _bListening )
            return;
        _listenerHandle = Logger::registerGlobalListener( SW_DELEGATE_METHOD( LogWrittenDelegate, &AutomationLogCapture::onLogWritten, this ) );
        _bListening     = _listenerHandle.isValid();
    }

    void AutomationLogCapture::stop()
    {
        if ( _bListening == false )
            return;
        Logger::unregisterGlobalListener( _listenerHandle );
        _bListening = false;
    }

    uint32 AutomationLogCapture::countMatchingLines( const string& needle, uint32 sinceFrame ) const
    {
        uint32                  matchCount = 0;
        std::scoped_lock<mutex> lock{ _mutex };
        for ( const AutomationLogLine& line : _listLogLine )
        {
            if ( line._frameIndex >= sinceFrame && line._message.find( needle ) != string::npos )
                ++matchCount;
        }
        return matchCount;
    }

    void AutomationLogCapture::onLogWritten( const LogEntry& entry )
    {
        // 실행기 자신의 줄([Scenario])은 세지 않는다. 실패 줄이 단언 문구를 반복해 ExpectLog 를 맞추지 않게 하기 위해서다.
        if ( StringUtil::startsWith( string_view{ entry._message }, "[Scenario]" ) )
            return;
        std::scoped_lock<mutex> lock{ _mutex };
        if ( _listLogLine.size() >= AutomationLogCaptureInternal::kMaxLogLine )
            return;
        AutomationLogLine line{};
        line._message    = entry._message;
        line._frameIndex = _frameIndex.load( std::memory_order_relaxed );
        _listLogLine.push_back( std::move( line ) );
    }
} // namespace sw
