#include "pch.h"

#include "Engine/Audio/Android/AAudioAudioBackend.h"

#include "Core/Log/Logger.h"

#include "Engine/Audio/AudioEngine.h"

#include <aaudio/AAudio.h>

namespace sw
{
    /** @brief AAudio 백엔드의 상태 전부입니다. */
    struct AAudioAudioBackendImpl
    {
        AAudioStream* _pStream;      /**< 열린 출력 스트림입니다(시작 전 · 닫은 뒤에는 nullptr). */
        uint32        _channelCount; /**< 스트림의 채널 수입니다. */

        AAudioAudioBackendImpl()
            : _pStream{ nullptr }
            , _channelCount{ 0 }
        {
        }
    };

    /** @brief AAudio 의 C 콜백을 백엔드 멤버로 잇습니다(헤더의 friend). */
    struct AAudioAudioBackendCallback
    {
        /** @brief 데이터 콜백 — 장치의 실시간 스레드에서 불린다. 엔진이 그 자리에 렌더한다(`AudioEngine::render` 는 장치 콜백용이다). */
        static aaudio_data_callback_result_t onData( AAudioStream* pStream, void* pUserData, void* pAudioData, int32_t frameCount )
        {
            (void)pStream;
            AAudioAudioBackend* const pBackend = static_cast<AAudioAudioBackend*>( pUserData );
            if ( frameCount > 0 )
                pBackend->renderCallback( static_cast<float32*>( pAudioData ), static_cast<uint32>( frameCount ) );
            return AAUDIO_CALLBACK_RESULT_CONTINUE;
        }

        /** @brief 오류 콜백 — 장치가 사라지면(헤드폰 뽑기 · 블루투스 끊김) 스트림이 멈춘다. 서비스에 알린다(`IAudioService::notifyOutputLost`). */
        static void onError( AAudioStream* pStream, void* pUserData, aaudio_result_t error )
        {
            (void)pStream;
            AAudioAudioBackend* const pBackend = static_cast<AAudioAudioBackend*>( pUserData );
            SW_LOG_WARNING_ID( "AAudioAudioBackend.StreamLost", "AAudio output stream stopped (%#)", AAudio_convertResultToText( error ) );
            pBackend->notifyOutputLost(); // 서비스가 오프라인으로 넘기고 기본 장치를 다시 연다
        }
    };

    AAudioAudioBackend::AAudioAudioBackend()
        : _impl{ make_unique<AAudioAudioBackendImpl>() }
    {
    }

    AAudioAudioBackend::~AAudioAudioBackend()
    {
        shutdown();
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
        _impl->_channelCount = channelCount;
        result               = AAudioStream_requestStart( _impl->_pStream );
        if ( result != AAUDIO_OK )
        {
            SW_LOG_WARNING_ID( "AAudioAudioBackend.StartFailed", "AAudio output stream could not start (%#). Rendering offline.", AAudio_convertResultToText( result ) );
            (void)AAudioStream_close( _impl->_pStream );
            _impl->_pStream = nullptr;
            return false;
        }
        SW_LOG_INFO_ID( "AAudioAudioBackend.StreamReady", "AAudio stream ready (%# Hz, %# channels, %# frames per burst).", audio::kSampleRate, channelCount,
                        AAudioStream_getFramesPerBurst( _impl->_pStream ) );
        return true;
    }

    void AAudioAudioBackend::renderCallback( float32* pOutput, uint32 frameCount )
    {
        getEngine().render( pOutput, frameCount );
    }

    void AAudioAudioBackend::stopOutput()
    {
        if ( _impl == nullptr || _impl->_pStream == nullptr )
            return;
        // requestStop 은 콜백이 끝나기를 기다리지 않는다 — close 가 콜백이 다 돌아오기를 기다린 뒤 닫는다(그 뒤에는 엔진을 부르지 않는다).
        (void)AAudioStream_requestStop( _impl->_pStream );
        (void)AAudioStream_close( _impl->_pStream );
        _impl->_pStream = nullptr;
    }

    void AAudioAudioBackend::closeDevice()
    {
        stopOutput();
    }

    SW_REGISTER_AUDIO_BACKEND( AAudioAudioBackend ); // Android 클라이언트의 장치 백엔드(`IAudioService::create` 가 등록표에서 고른다)
} // namespace sw
