#include "pch.h"

#include "Engine/Audio/Android/AAudioAudioBackend.h"

#include "Core/Log/Logger.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioRenderThread.h"
#include "Engine/Audio/AudioService.h"

#include <aaudio/AAudio.h>

namespace sw
{
    /** @brief AAudio 백엔드의 상태 전부입니다. */
    struct AAudioAudioBackendImpl
    {
        AudioRenderThread _renderThread; /**< 엔진을 앞서 렌더하는 스레드입니다(콜백은 링만 읽는다). */
        AAudioStream*     _pStream;      /**< 열린 출력 스트림입니다(시작 전 · 닫은 뒤에는 nullptr). */

        explicit AAudioAudioBackendImpl( AudioEngine& engine )
            : _renderThread{ engine }
            , _pStream{ nullptr }
        {
        }
    };

    /** @brief AAudio 의 C 콜백을 백엔드 멤버로 잇습니다(헤더의 friend). */
    struct AAudioAudioBackendCallback
    {
        /** @brief 데이터 콜백 — 장치의 실시간 스레드에서 불린다. 렌더 스레드가 채운 링에서 꺼내기만 한다(`AudioRenderThread::read`). */
        static aaudio_data_callback_result_t onData( AAudioStream* pStream, void* pUserData, void* pAudioData, int32_t frameCount )
        {
            (void)pStream;
            AAudioAudioBackend* const pBackend = static_cast<AAudioAudioBackend*>( pUserData );
            if ( frameCount > 0 )
                pBackend->renderCallback( static_cast<float32*>( pAudioData ), static_cast<uint32>( frameCount ) );
            return AAUDIO_CALLBACK_RESULT_CONTINUE;
        }

        /** @brief 오류 콜백 — 장치가 사라지면(헤드폰 뽑기 · 블루투스 끊김) 스트림이 멈춘다. 서비스에 알린다(`AudioService::notifyOutputLost`). */
        static void onError( AAudioStream* pStream, void* pUserData, aaudio_result_t error )
        {
            (void)pStream;
            AAudioAudioBackend* const pBackend = static_cast<AAudioAudioBackend*>( pUserData );
            SW_LOG_WARNING_ID( "AAudioAudioBackend.StreamLost", "AAudio output stream stopped (%#)", AAudio_convertResultToText( error ) );
            pBackend->_service.notifyOutputLost(); // 서비스가 오프라인으로 넘기고 기본 장치를 다시 연다
        }
    };

    AAudioAudioBackend::AAudioAudioBackend( AudioService& service )
        : _service{ service }
        , _impl{ make_unique<AAudioAudioBackendImpl>( service.getEngine() ) }
    {
    }

    AAudioAudioBackend::~AAudioAudioBackend()
    {
        // 서비스가 소멸자에서 출력 · 장치를 먼저 내린다(`AudioService::~AudioService`) — 여기서는 남은 것을 놓기만 한다.
    }

    bool AAudioAudioBackend::openDevice( AudioOutputDevice& outDevice )
    {
        // AAudio 는 스트림을 열기 전에 장치를 따로 잡지 않는다. 스피커 배치를 알려 주지 않으므로 스테레오로 알린다(휴대 기기의 기본 출력).
        outDevice._channelCount = audio::kStereoChannelCount;
        outDevice._channelMask  = AudioChannelLayoutUtil::getChannelMask( AudioChannelLayout::Stereo );
        return true;
    }

    bool AAudioAudioBackend::startOutput( AudioChannelLayout layout )
    {
        const uint32         channelCount = AudioChannelLayoutUtil::getChannelCount( layout );
        AAudioStreamBuilder* pBuilder     = nullptr;
        aaudio_result_t      result       = AAudio_createStreamBuilder( &pBuilder );
        if ( result != AAUDIO_OK || pBuilder == nullptr )
        {
            SW_LOG_WARNING_ID( "AAudioAudioBackend.BuilderFailed", "AAudio stream builder could not be created (%#). Rendering offline.", AAudio_convertResultToText( result ) );
            return false;
        }
        AAudioStreamBuilder_setDirection( pBuilder, AAUDIO_DIRECTION_OUTPUT );
        AAudioStreamBuilder_setSharingMode( pBuilder, AAUDIO_SHARING_MODE_SHARED );
        AAudioStreamBuilder_setPerformanceMode( pBuilder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY );
        AAudioStreamBuilder_setFormat( pBuilder, AAUDIO_FORMAT_PCM_FLOAT );
        AAudioStreamBuilder_setChannelCount( pBuilder, static_cast<int32_t>( channelCount ) );
        AAudioStreamBuilder_setSampleRate( pBuilder, static_cast<int32_t>( audio::kSampleRate ) );
        AAudioStreamBuilder_setDataCallback( pBuilder, &AAudioAudioBackendCallback::onData, this );
        AAudioStreamBuilder_setErrorCallback( pBuilder, &AAudioAudioBackendCallback::onError, this );
        result = AAudioStreamBuilder_openStream( pBuilder, &_impl->_pStream );
        AAudioStreamBuilder_delete( pBuilder );
        if ( result != AAUDIO_OK || _impl->_pStream == nullptr )
        {
            _impl->_pStream = nullptr;
            SW_LOG_WARNING_ID( "AAudioAudioBackend.OpenFailed", "AAudio output stream could not be opened for %# channels (%#). Rendering offline.", channelCount, AAudio_convertResultToText( result ) );
            return false;
        }
        // 엔진은 48 kHz · 그 채널 수로만 렌더한다 — 장치가 다른 값을 고르면(공유 모드가 리샘플하지 않는 기기) 그대로 틀면 음높이가 틀린다. 오류로 멈춘다.
        const bool bFormatMatches = AAudioStream_getSampleRate( _impl->_pStream ) == static_cast<int32_t>( audio::kSampleRate ) &&
                                    AAudioStream_getChannelCount( _impl->_pStream ) == static_cast<int32_t>( channelCount ) && AAudioStream_getFormat( _impl->_pStream ) == AAUDIO_FORMAT_PCM_FLOAT;
        if ( bFormatMatches == false )
        {
            SW_LOG_ERROR_ID( "AAudioAudioBackend.FormatMismatch", "AAudio opened %# Hz · %# channels instead of %# Hz · %# channels float. Rendering offline.", AAudioStream_getSampleRate( _impl->_pStream ),
                             AAudioStream_getChannelCount( _impl->_pStream ), audio::kSampleRate, channelCount );
            (void)AAudioStream_close( _impl->_pStream ); // 닫는 길이라 결과를 볼 일이 없다
            _impl->_pStream = nullptr;
            return false;
        }
        // 콜백 크기는 정해져 있지 않다 — 프레임 수가 콜백마다 달라질 수 있고, AAudio 는 내부 버퍼를 "크기" 까지 버스트 단위로 채운다(블루투스는 버스트 하나가 960 프레임). 그래서 잇달아 당길 수
        // 있는 양은 버퍼 크기이고 링을 그만큼 + 여유로 둔다. 크기를 버스트 둘로 맞춘다 — 콜백 크기를 고정(`setFramesPerDataCallback`)하면 버스트 하나에 콜백이 잇달아 오고 변환 버퍼가 더해져
        // 같은 양이 필요하고 지연만 는다.
        // https://developer.android.com/ndk/guides/audio/aaudio/aaudio#tuning-buffers · https://developer.android.com/ndk/reference/group/audio#aaudiostreambuilder_setframesperdatacallback
        constexpr int32_t kBurstCount    = 2;
        const int32_t     framesPerBurst = AAudioStream_getFramesPerBurst( _impl->_pStream );
        const int32_t     bufferFrames   = AAudioStream_setBufferSizeInFrames( _impl->_pStream, framesPerBurst * kBurstCount ); // 실제로 잡힌 크기를 돌려준다(음수는 오류)
        if ( framesPerBurst <= 0 || bufferFrames < framesPerBurst )
        {
            SW_LOG_ERROR_ID( "AAudioAudioBackend.BufferSizeFailed", "AAudio reported %# frames per burst and a buffer of %# frames. Rendering offline.", framesPerBurst, bufferFrames );
            stopOutput();
            return false;
        }
        _impl->_renderThread.start( channelCount, static_cast<uint32>( bufferFrames ) ); // 첫 콜백 앞에 링을 채운다
        result = AAudioStream_requestStart( _impl->_pStream );
        if ( result != AAUDIO_OK )
        {
            SW_LOG_WARNING_ID( "AAudioAudioBackend.StartFailed", "AAudio output stream could not start (%#). Rendering offline.", AAudio_convertResultToText( result ) );
            stopOutput();
            return false;
        }
        SW_LOG_INFO_ID( "AAudioAudioBackend.StreamReady", "AAudio stream ready (%# Hz, %# channels, %# frames per burst).", audio::kSampleRate, channelCount,
                        AAudioStream_getFramesPerBurst( _impl->_pStream ) );
        return true;
    }

    void AAudioAudioBackend::renderCallback( float32* pOutput, uint32 frameCount )
    {
        _impl->_renderThread.read( pOutput, frameCount );
    }

    void AAudioAudioBackend::stopOutput()
    {
        if ( _impl == nullptr || _impl->_pStream == nullptr )
            return;
        // requestStop 은 콜백이 끝나기를 기다리지 않는다 — close 가 콜백이 다 돌아오기를 기다린 뒤 닫는다(그 뒤에는 엔진을 부르지 않는다).
        (void)AAudioStream_requestStop( _impl->_pStream );
        (void)AAudioStream_close( _impl->_pStream );
        _impl->_pStream = nullptr;
        _impl->_renderThread.stop(); // 콜백이 더 오지 않는다 — 이 뒤로는 엔진을 렌더하는 스레드가 없다
    }

    void AAudioAudioBackend::closeDevice()
    {
        stopOutput();
    }

    uint64 AAudioAudioBackend::getUnderrunCount() const
    {
        if ( _impl->_pStream == nullptr )
            return 0;
        const int32_t underrunCount = AAudioStream_getXRunCount( _impl->_pStream );
        const uint64  deviceCount   = underrunCount > 0 ? static_cast<uint64>( underrunCount ) : 0u; // 음수는 오류 코드다
        return deviceCount + _impl->_renderThread.getUnderrunCount();
    }

    SW_REGISTER_AUDIO_BACKEND( AAudioAudioBackend ); // Android 클라이언트의 장치 백엔드(`AudioService::create` 가 등록표에서 고른다)
} // namespace sw
