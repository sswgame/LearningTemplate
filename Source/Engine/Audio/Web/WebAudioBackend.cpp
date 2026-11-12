#include "pch.h"

#include "Engine/Audio/Web/WebAudioBackend.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/CircularAudioBuffer.h"

#include <emscripten/atomic.h>
#include <emscripten/em_macros.h>

namespace sw
{
    /**
     * @brief 믹서 스레드와 AudioWorklet 이 함께 보는 상태입니다. Wasm 메모리(SharedArrayBuffer) 안에 있다.
     * @details 워클릿은 AudioContext 를 닫은 뒤에도 한동안 `process` 를 돌 수 있다 — 그래서 이 상태는 C++ 가 지우지 않고, 닫기가 끝난 뒤 브라우저 쪽이
     *          `swWebAudioReleaseStream` 으로 돌려준다(그 전에 지우면 워클릿이 놓인 힙에 읽기 위치를 쓴다).
     */
    struct WebAudioStreamState
    {
        CircularAudioBuffer _ring;   ///< 믹서 스레드 → 워클릿 샘플(인터리브)입니다.
        atomic<uint32>      _status; ///< 0 이면 흐른다. 브라우저 쪽이 출력을 잃으면(워클릿 오류 · 모듈 읽기 실패) 1 을 쓴다.

        explicit WebAudioStreamState( uint32 capacity )
            : _ring{ capacity }
            , _status{ 0 }
        {
        }
    };
} // namespace sw

// 브라우저 쪽 본문은 WebAudioBackend.js(Emscripten JS 라이브러리)다. 메인(브라우저) 스레드로 넘겨 부른다(`__proxy: 'sync'`).
extern "C"
{
    int  swWebAudioIsAvailable();
    int  swWebAudioStart( uint32* pReadIndex, uint32* pWriteIndex, float32* pSample, uint32 capacity, uint32 channelCount, uint32 sampleRate, uint32* pStatus, void* pState );
    void swWebAudioStop( int contextID );

    /** @brief (브라우저 메인 스레드) AudioContext 를 다 닫은 뒤 JS 가 공유 상태를 돌려준다 — 그때서야 워클릿이 링을 만지지 않는다. */
    EMSCRIPTEN_KEEPALIVE void swWebAudioReleaseStream( void* pState )
    {
        const sw::unique_ptr<sw::WebAudioStreamState> state( static_cast<sw::WebAudioStreamState*>( pState ) );
    }
}

namespace sw
{
    namespace
    {
        struct WebAudioBackendInternal
        {
            /** @brief 믹서가 한 번에 렌더해 넣는 프레임 수입니다(10.7 ms — ALSA 백엔드와 같다). */
            static constexpr uint32 kMixFrameCount = 512;
            /** @brief 링이 담는 프레임 수입니다(42.7 ms). 워클릿의 128 프레임 묶음 사이에 믹서가 늦어도 끊기지 않을 만큼 · 지연이 눈에 띄지 않을 만큼. */
            static constexpr uint32 kRingFrameCount = 2048;
            /** @brief 자리를 기다리다 정지 · 유실을 다시 보는 간격(나노초)입니다. 워클릿이 꺼낼 때마다 깨우므로 평소에는 닿지 않는다. */
            static constexpr int64 kWaitNanoseconds = 20'000'000;
        };
    } // namespace

    /** @brief 웹 백엔드의 상태 전부입니다. */
    struct WebAudioBackendImpl
    {
        vector<float32>      _listRender;   /**< 엔진이 렌더한 블록(인터리브)입니다. */
        thread               _mixerThread;  /**< 믹서 스레드입니다. */
        WebAudioStreamState* _pState;       /**< 열린 스트림의 공유 상태입니다. 닫으면 브라우저 쪽에 넘기고 비운다. */
        int32                _contextID;    /**< JS 쪽 AudioContext 번호입니다(0 은 없음). */
        uint32               _channelCount; /**< 스트림의 채널 수입니다. */

        WebAudioBackendImpl()
            : _listRender{}
            , _mixerThread{}
            , _pState{ nullptr }
            , _contextID{ 0 }
            , _channelCount{ 0 }
        {
        }
    };

    WebAudioBackend::WebAudioBackend()
        : _impl{ make_unique<WebAudioBackendImpl>() }
    {
    }

    WebAudioBackend::~WebAudioBackend()
    {
        shutdown();
    }

    bool WebAudioBackend::openDevice( AudioOutputDevice& outDevice )
    {
        if ( swWebAudioIsAvailable() == 0 )
        {
            SW_LOG_WARNING_ID( "WebAudioBackend.WebAudioUnavailable", "This browser has no AudioContext with AudioWorklet (or the page is not cross-origin isolated). Rendering offline." );
            return false;
        }
        // 브라우저는 스피커 배치를 알려 주지 않는다 — 스테레오로 알린다(`Auto` 는 스테레오, 명시 5.1 · 7.1 은 오류).
        outDevice._channelCount = audio::kStereoChannelCount;
        outDevice._channelMask  = AudioChannelLayoutUtil::getChannelMask( AudioChannelLayout::Stereo );
        return true;
    }

    bool WebAudioBackend::startOutput( AudioChannelLayout layout )
    {
        using Internal                               = WebAudioBackendInternal;
        const uint32                    channelCount = AudioChannelLayoutUtil::getChannelCount( layout );
        unique_ptr<WebAudioStreamState> state        = make_unique<WebAudioStreamState>( Internal::kRingFrameCount * channelCount );
        CircularAudioBuffer&            ring         = state->_ring;
        const int32                     contextID    = swWebAudioStart( ring.getReadIndexAddress(), ring.getWriteIndexAddress(), ring.getSampleData(), ring.getCapacity(), channelCount, audio::kSampleRate,
                                                                        reinterpret_cast<uint32*>( &state->_status ), state.get() );
        if ( contextID == 0 )
        {
            // 워클릿을 만들기 전에 졌다 — 링을 읽는 쪽이 없으니 여기서 놓는다.
            SW_LOG_WARNING_ID( "WebAudioBackend.StartFailed", "Could not open a %# Hz AudioContext with %# channels (see the browser console). Rendering offline.", audio::kSampleRate, channelCount );
            return false;
        }
        _impl->_pState       = state.release(); // 이제 링은 워클릿도 본다 — 놓는 것은 브라우저 쪽이다(`swWebAudioReleaseStream`)
        _impl->_contextID    = contextID;
        _impl->_channelCount = channelCount;
        _impl->_listRender.assign( static_cast<size_t>( Internal::kMixFrameCount ) * channelCount, 0.0f );
        _impl->_mixerThread = thread( ThreadCreateKey{}, &WebAudioBackend::runMixer, this );
        SW_LOG_INFO_ID( "WebAudioBackend.StreamReady", "Web Audio stream ready (%# Hz, %# channels, ring %# frames). Sound starts after the first user gesture.", audio::kSampleRate,
                        channelCount, Internal::kRingFrameCount );
        return true;
    }

    void WebAudioBackend::runMixer( StopToken stopToken )
    {
        using Internal               = WebAudioBackendInternal;
        WebAudioStreamState& state   = *_impl->_pState;
        CircularAudioBuffer& ring    = state._ring;
        const uint32         samples = static_cast<uint32>( _impl->_listRender.size() );
        while ( stopToken.isStopRequested() == false )
        {
            if ( state._status.load( std::memory_order_acquire ) != 0 )
            {
                notifyOutputLost(); // 서비스가 스트림을 닫고 오프라인으로 넘긴 뒤 다시 연다
                return;
            }
            const uint32 readIndex = emscripten_atomic_load_u32( ring.getReadIndexAddress() );
            if ( ring.getRemainder() < samples )
            {
                // 워클릿이 꺼낼 때마다 읽기 위치를 깨운다(`Atomics.notify`). 그 사이 값이 바뀌었으면 바로 돌아온다. 결과는 위 조건을 다시 보면 된다.
                (void)emscripten_atomic_wait_u32( ring.getReadIndexAddress(), readIndex, Internal::kWaitNanoseconds );
                continue;
            }
            getEngine().render( _impl->_listRender.data(), Internal::kMixFrameCount );
            const uint32 pushed = ring.push( _impl->_listRender );
            SW_ASSERT( pushed == samples ); // 소비자는 자리를 늘리기만 하므로 위에서 본 자리가 그대로 있다
            (void)pushed;
        }
    }

    void WebAudioBackend::stopOutput()
    {
        if ( _impl == nullptr || _impl->_pState == nullptr )
            return;
        _impl->_mixerThread.request_stop();
        (void)emscripten_atomic_notify( _impl->_pState->_ring.getReadIndexAddress(), EMSCRIPTEN_NOTIFY_ALL_WAITERS ); // 자리를 기다리는 믹서를 깨운다(깨운 수는 볼 일이 없다)
        if ( _impl->_mixerThread.joinable() )
            _impl->_mixerThread.join();
        swWebAudioStop( _impl->_contextID ); // 닫기가 끝나면 브라우저 쪽이 공유 상태를 놓는다
        _impl->_pState    = nullptr;
        _impl->_contextID = 0;
    }

    void WebAudioBackend::closeDevice()
    {
        stopOutput();
    }

    SW_REGISTER_AUDIO_BACKEND( WebAudioBackend ); // 웹 클라이언트의 장치 백엔드(`IAudioService::create` 가 등록표에서 고른다)
} // namespace sw
