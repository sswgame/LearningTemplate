#include "pch.h"

#include "Engine/Audio/AudioRenderThread.h"

#include "Core/Concurrency/Futex.h"
#include "Core/Concurrency/ThreadName.h"
#include "Core/Process/Process.h"

#include "Engine/Audio/AudioEngine.h"

namespace sw
{
    AudioRenderThread::AudioRenderThread( AudioEngine& engine )
        : _engine{ engine }
        , _pRing{}
        , _listRender{}
        , _thread{}
        , _wakeWord{ 0 }
        , _underrunCount{ 0 }
        , _channelCount{ 0 }
        , _targetSampleCount{ 0 }
        , _realtimeViolations{}
    {
        _realtimeViolations._bAssertOnViolation.store( true, std::memory_order_relaxed );
    }

    AudioRenderThread::~AudioRenderThread()
    {
        stop();
    }

    void AudioRenderThread::start( uint32 channelCount, uint32 maxReadFrameCount )
    {
        stop();
        _channelCount      = channelCount;
        _targetSampleCount = ( maxReadFrameCount + kSpareFrameCount ) * channelCount;
        _pRing             = make_unique<CircularAudioBuffer>( _targetSampleCount );
        _listRender.assign( static_cast<size_t>( kRenderFrameCount ) * channelCount, 0.0f );
        _underrunCount.store( 0, std::memory_order_relaxed );
        // 첫 콜백이 빈 링을 보지 않게 여기서 채운다. 그 뒤로는 렌더 스레드가 오디오 스레드 역할을 맡는다(첫 렌더가 역할을 받는다).
        while ( hasRoom() )
        {
            renderNext();
        }
        _engine.releaseAudioThread();
        _thread = thread( ThreadCreationKey{}, &AudioRenderThread::run, this );
    }

    void AudioRenderThread::stop()
    {
        if ( _thread.joinable() == false )
            return;
        _thread.request_stop();
        _wakeWord.fetch_add( 1, std::memory_order_release );
        Futex::wakeAll( _wakeWord );
        _thread.join();
    }

    void AudioRenderThread::read( float32* pOutput, uint32 frameCount )
    {
        const RealtimeSection realtimeSection{ _realtimeViolations };
        const uint32          sampleCount = frameCount * _channelCount;
        const uint32          popped      = _pRing->pop( vector_reference<float32>( pOutput, sampleCount ) );
        if ( popped < sampleCount )
        {
            Memory::set( pOutput + popped, 0, static_cast<size_t>( sampleCount - popped ) * sizeof( float32 ) );
            _underrunCount.fetch_add( 1, std::memory_order_relaxed );
        }
        _wakeWord.fetch_add( 1, std::memory_order_release );
        Futex::wakeOne( _wakeWord );
    }

    void AudioRenderThread::run( const StopToken& stopToken )
    {
        ThreadCrashStack::initializeCurrentThread();
        ThreadName::setCurrentThreadName( "AudioRender" );
        ThreadUtil::setCurrentThreadTimeCritical(); // 작업 풀이 코어를 모두 채워도 링이 비기 전에 돌아야 한다(언리얼 오디오 렌더 스레드 `TPri_TimeCritical`)
        while ( stopToken.isStopRequested() == false )
        {
            // 워드를 먼저 읽고 자리를 본다 — 그 사이에 꺼낸 콜백은 워드를 바꿔 아래 잠들기가 바로 돌아온다.
            const uint32 observed = _wakeWord.load( std::memory_order_acquire );
            if ( hasRoom() )
                renderNext();
            else if ( stopToken.isStopRequested() == false )
                Futex::wait( _wakeWord, observed );
        }
    }

    void AudioRenderThread::renderNext()
    {
        _engine.render( _listRender.data(), kRenderFrameCount );
        const uint32 pushed = _pRing->push( _listRender );
        SW_ASSERT( pushed == static_cast<uint32>( _listRender.size() ) ); // 콜백은 자리를 늘리기만 하므로 `hasRoom` 이 본 자리가 그대로 있다
        (void)pushed;
    }
} // namespace sw
