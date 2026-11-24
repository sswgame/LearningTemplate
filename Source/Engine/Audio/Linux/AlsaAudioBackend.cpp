/**
 * @file AlsaAudioBackend.cpp
 * @brief 리눅스 오디오 출력입니다. ALSA(`libasound.so.2`)의 기본 장치(`default`, PulseAudio · PipeWire 의 ALSA 플러그인 포함)에 엔진 출력을 씁니다.
 *
 * @note 믹스는 전부 엔진(`AudioEngine`)이 합니다. 이 백엔드는 장치를 열고, 자기 스트림 스레드에서 블록을 렌더해 `snd_pcm_writei` 로 씁니다(장치가 속도를 정합니다).
 *       `libasound` 는 링크하지 않고 실행 중에 엽니다 — 없는 기계(서버, 컨테이너)에서도 프로세스가 뜨고 오프라인 렌더로 진행합니다.
 *       ALSA 기본 장치는 스피커 배치를 알려 주지 않으므로 장치 마스크는 스테레오로 알립니다(`Auto` 는 스테레오). 5.1 · 7.1 은 사용자가 고르면 장치의 최대 채널 수 안에서 열고,
 *       채널 순서는 ALSA 순서(FL FR RL RR FC LFE SL SR)로 바꿔 씁니다.
 */
#include "pch.h"

#include "Core/Concurrency/StopToken.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Module/ModuleLibraryUtil.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioService.h"
#include "Engine/Audio/IAudioOutputDevice.h"

namespace sw
{
    struct AlsaAudioBackendImpl;

    /**
     * @class AlsaAudioBackend
     * @brief 리눅스 오디오 백엔드입니다.
     */
    class AlsaAudioBackend final : public IAudioOutputDevice
    {
    public:
        /** @brief 장치를 잡지 않은 채로 만듭니다. */
        explicit AlsaAudioBackend( AudioService& service );
        /** @brief 장치를 닫고 엔진을 내립니다. */
        ~AlsaAudioBackend() override;

        /** @brief `libasound` 를 열고 기본 재생 장치를 엽니다. 최대 채널 수를 알립니다. 라이브러리나 장치가 없으면 false 입니다. */
        [[nodiscard]] bool openDevice( AudioOutputDevice& outDevice ) override;
        /** @brief @p layout 의 채널 수 · 48 kHz · float 로 장치를 맞추고 스트림 스레드를 시작합니다. */
        [[nodiscard]] bool startOutput( AudioChannelLayout layout ) override;
        /** @brief 스트림 스레드를 멈추고 기다립니다. */
        void stopOutput() override;
        /** @brief 장치를 닫고 라이브러리를 놓습니다. */
        void closeDevice() override;
        /** @brief 스트림 스레드가 쓰기 실패를 복원한 수(언더런 · 일시정지 복귀)입니다. */
        uint64 getUnderrunCount() const override;

    private:
        /** @brief 스트림 스레드 본문입니다. 멈추라는 표시가 설 때까지 블록을 렌더해 씁니다. */
        void runStream( StopToken stopToken );

    private:
        AudioService&                    _service; ///< 이 장치가 당기는 서비스(엔진 · 유실 알림) — 서비스가 장치를 들고 먼저 내린다
        unique_ptr<AlsaAudioBackendImpl> _impl;    /**< 리눅스 전용 상태입니다. */
    };

    namespace
    {
        struct AlsaAudioBackendInternal
        {
            /** @brief 한 번에 렌더해 쓰는 프레임 수입니다(10.7 ms). */
            static constexpr uint32 kWriteFrameCount = 512;
            /** @brief 장치에 요청하는 지연(마이크로초)입니다. 쓰기 세 번 분량 안팎입니다. */
            static constexpr uint32 kLatencyMicroseconds = 32000;
            /** @brief ALSA 상수입니다(`alsa/pcm.h` 를 끌어오지 않으려고 값을 적습니다). */
            static constexpr int32 kStreamPlayback      = 0;  ///< SND_PCM_STREAM_PLAYBACK
            static constexpr int32 kFormatFloatLe       = 14; ///< SND_PCM_FORMAT_FLOAT_LE
            static constexpr int32 kAccessRwInterleaved = 3;  ///< SND_PCM_ACCESS_RW_INTERLEAVED
            /** @brief ALSA 채널 순서(FL FR RL RR FC LFE SL SR)의 자리마다 엔진 채널 번호(FL FR FC LFE BL BR SL SR)입니다. */
            static constexpr uint32 kArrAlsaToEngineChannel[audio::kMaxChannelCount] = { 0, 1, 4, 5, 2, 3, 6, 7 };

            using PFN_snd_pcm_open                       = int32 ( * )( void**, const utf8*, int32, int32 );
            using PFN_snd_pcm_close                      = int32 ( * )( void* );
            using PFN_snd_pcm_set_params                 = int32 ( * )( void*, int32, int32, uint32, uint32, int32, uint32 );
            using PFN_snd_pcm_writei                     = int64 ( * )( void*, const void*, uint64 ); ///< snd_pcm_sframes_t(long) · snd_pcm_uframes_t(unsigned long) 는 LP64 에서 64 비트다
            using PFN_snd_pcm_recover                    = int32 ( * )( void*, int32, int32 );
            using PFN_snd_pcm_drop                       = int32 ( * )( void* );
            using PFN_snd_pcm_hw_params_malloc           = int32 ( * )( void** );
            using PFN_snd_pcm_hw_params_free             = void ( * )( void* );
            using PFN_snd_pcm_hw_params_any              = int32 ( * )( void*, void* );
            using PFN_snd_pcm_hw_params_get_channels_max = int32 ( * )( const void*, uint32* );
            using PFN_snd_config_update_free_global      = int32 ( * )();

            /** @brief 교차 블록을 엔진 순서(FL FR FC LFE BL BR SL SR)에서 ALSA 순서(FL FR RL RR FC LFE SL SR)로 옮깁니다. */
            static void reorderToAlsa( const float32* pSource, float32* pTarget, uint32 frameCount, uint32 channelCount )
            {
                for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
                {
                    const size_t base = static_cast<size_t>( frameIndex ) * channelCount;
                    for ( uint32 slot = 0; slot < channelCount; ++slot )
                    {
                        pTarget[base + slot] = pSource[base + kArrAlsaToEngineChannel[slot]];
                    }
                }
            }
        };
    } // namespace

    /** @brief ALSA 백엔드의 상태 전부입니다. */
    struct AlsaAudioBackendImpl
    {
        AlsaAudioBackendInternal::PFN_snd_pcm_open                       _pfnOpen;
        AlsaAudioBackendInternal::PFN_snd_pcm_close                      _pfnClose;
        AlsaAudioBackendInternal::PFN_snd_pcm_set_params                 _pfnSetParams;
        AlsaAudioBackendInternal::PFN_snd_pcm_writei                     _pfnWritei;
        AlsaAudioBackendInternal::PFN_snd_pcm_recover                    _pfnRecover;
        AlsaAudioBackendInternal::PFN_snd_pcm_drop                       _pfnDrop;
        AlsaAudioBackendInternal::PFN_snd_pcm_hw_params_malloc           _pfnHwParamsMalloc;
        AlsaAudioBackendInternal::PFN_snd_pcm_hw_params_free             _pfnHwParamsFree;
        AlsaAudioBackendInternal::PFN_snd_pcm_hw_params_any              _pfnHwParamsAny;
        AlsaAudioBackendInternal::PFN_snd_pcm_hw_params_get_channels_max _pfnHwParamsGetChannelsMax;
        AlsaAudioBackendInternal::PFN_snd_config_update_free_global      _pfnConfigFreeGlobal; /**< `snd_pcm_open` 이 읽어 둔 전역 설정 트리를 놓습니다(닫는 길). */
        vector<float32>                                                  _listRender;          /**< 엔진이 렌더한 블록(엔진 채널 순서)입니다. */
        vector<float32>                                                  _listDevice;          /**< 장치에 쓰는 블록(ALSA 채널 순서)입니다. */
        thread                                                           _streamThread;        /**< 스트림 스레드입니다. */
        void*                                                            _pLibrary;            /**< `libasound.so.2` 핸들입니다. */
        void*                                                            _pPCM;                /**< 열린 재생 장치입니다. */
        uint32                                                           _channelCount;        /**< 스트림의 채널 수입니다. */
        atomic<uint64>                                                   _underrunCount;       /**< 쓰기 실패를 복원한 수입니다(스트림 스레드가 쓰고 게임 스레드가 읽는다). */

        AlsaAudioBackendImpl()
            : _pfnOpen{ nullptr }
            , _pfnClose{ nullptr }
            , _pfnSetParams{ nullptr }
            , _pfnWritei{ nullptr }
            , _pfnRecover{ nullptr }
            , _pfnDrop{ nullptr }
            , _pfnHwParamsMalloc{ nullptr }
            , _pfnHwParamsFree{ nullptr }
            , _pfnHwParamsAny{ nullptr }
            , _pfnHwParamsGetChannelsMax{ nullptr }
            , _pfnConfigFreeGlobal{ nullptr }
            , _listRender{}
            , _listDevice{}
            , _streamThread{}
            , _pLibrary{ nullptr }
            , _pPCM{ nullptr }
            , _channelCount{ 0 }
            , _underrunCount{ 0 }
        {
        }

        /** @brief 라이브러리에서 함수를 찾습니다. 하나라도 없으면 false 입니다. */
        bool bindFunctions()
        {
            _pfnOpen                   = reinterpret_cast<AlsaAudioBackendInternal::PFN_snd_pcm_open>( ModuleLibraryUtil::getDynamicSymbol( _pLibrary, "snd_pcm_open" ) );
            _pfnClose                  = reinterpret_cast<AlsaAudioBackendInternal::PFN_snd_pcm_close>( ModuleLibraryUtil::getDynamicSymbol( _pLibrary, "snd_pcm_close" ) );
            _pfnSetParams              = reinterpret_cast<AlsaAudioBackendInternal::PFN_snd_pcm_set_params>( ModuleLibraryUtil::getDynamicSymbol( _pLibrary, "snd_pcm_set_params" ) );
            _pfnWritei                 = reinterpret_cast<AlsaAudioBackendInternal::PFN_snd_pcm_writei>( ModuleLibraryUtil::getDynamicSymbol( _pLibrary, "snd_pcm_writei" ) );
            _pfnRecover                = reinterpret_cast<AlsaAudioBackendInternal::PFN_snd_pcm_recover>( ModuleLibraryUtil::getDynamicSymbol( _pLibrary, "snd_pcm_recover" ) );
            _pfnDrop                   = reinterpret_cast<AlsaAudioBackendInternal::PFN_snd_pcm_drop>( ModuleLibraryUtil::getDynamicSymbol( _pLibrary, "snd_pcm_drop" ) );
            _pfnHwParamsMalloc         = reinterpret_cast<AlsaAudioBackendInternal::PFN_snd_pcm_hw_params_malloc>( ModuleLibraryUtil::getDynamicSymbol( _pLibrary, "snd_pcm_hw_params_malloc" ) );
            _pfnHwParamsFree           = reinterpret_cast<AlsaAudioBackendInternal::PFN_snd_pcm_hw_params_free>( ModuleLibraryUtil::getDynamicSymbol( _pLibrary, "snd_pcm_hw_params_free" ) );
            _pfnHwParamsAny            = reinterpret_cast<AlsaAudioBackendInternal::PFN_snd_pcm_hw_params_any>( ModuleLibraryUtil::getDynamicSymbol( _pLibrary, "snd_pcm_hw_params_any" ) );
            _pfnHwParamsGetChannelsMax = reinterpret_cast<AlsaAudioBackendInternal::PFN_snd_pcm_hw_params_get_channels_max>(
                ModuleLibraryUtil::getDynamicSymbol( _pLibrary, "snd_pcm_hw_params_get_channels_max" ) );
            _pfnConfigFreeGlobal = reinterpret_cast<AlsaAudioBackendInternal::PFN_snd_config_update_free_global>(
                ModuleLibraryUtil::getDynamicSymbol( _pLibrary, "snd_config_update_free_global" ) );
            const bool bCoreFunctions  = _pfnOpen != nullptr && _pfnClose != nullptr && _pfnSetParams != nullptr && _pfnWritei != nullptr && _pfnRecover != nullptr;
            const bool bQueryFunctions = _pfnDrop != nullptr && _pfnHwParamsMalloc != nullptr && _pfnHwParamsFree != nullptr && _pfnHwParamsAny != nullptr &&
                                         _pfnHwParamsGetChannelsMax != nullptr && _pfnConfigFreeGlobal != nullptr;
            return bCoreFunctions && bQueryFunctions;
        }
    };

    AlsaAudioBackend::AlsaAudioBackend( AudioService& service )
        : _service{ service }
        , _impl{ make_unique<AlsaAudioBackendImpl>() }
    {
    }

    AlsaAudioBackend::~AlsaAudioBackend()
    {
        // 서비스가 소멸자에서 출력 · 장치를 먼저 내린다(`AudioService::~AudioService`) — 여기서는 남은 것을 놓기만 한다.
    }

    bool AlsaAudioBackend::openDevice( AudioOutputDevice& outDevice )
    {
        using Internal   = AlsaAudioBackendInternal;
        _impl->_pLibrary = ModuleLibraryUtil::loadSystemLibrary( "libasound.so.2" );
        if ( _impl->_pLibrary == nullptr )
        {
            SW_LOG_INFO_ID( "AlsaAudioBackend.ALSALibasoundNotAvailable", "ALSA (libasound.so.2) is not available. Rendering offline." );
            return false;
        }
        if ( _impl->bindFunctions() == false )
        {
            SW_LOG_WARNING_ID( "AlsaAudioBackend.LibasoundLacksPCMFunctions", "libasound.so.2 lacks the PCM functions. Rendering offline." );
            closeDevice();
            return false;
        }
        const int32 openResult = _impl->_pfnOpen( &_impl->_pPCM, "default", Internal::kStreamPlayback, 0 );
        if ( openResult < 0 || _impl->_pPCM == nullptr )
        {
            SW_LOG_INFO_ID( "AlsaAudioBackend.NoALSAPlaybackDevice", "No ALSA playback device (error %#). Rendering offline.", openResult );
            _impl->_pPCM = nullptr;
            closeDevice();
            return false;
        }
        // 기본 장치가 받는 최대 채널 수다. 스피커 배치는 알려 주지 않으므로 마스크는 스테레오다(`Auto` 는 스테레오, 명시 모드는 채널 수로 판단).
        uint32 maxChannels = audio::kStereoChannelCount;
        void*  pHwParams   = nullptr;
        if ( _impl->_pfnHwParamsMalloc( &pHwParams ) == 0 && pHwParams != nullptr )
        {
            if ( _impl->_pfnHwParamsAny( _impl->_pPCM, pHwParams ) == 0 )
                (void)_impl->_pfnHwParamsGetChannelsMax( pHwParams, &maxChannels ); // 실패하면 스테레오로 둔 값을 쓴다 — ALSA 가 재생 장치에 2 채널은 늘 받는다
            _impl->_pfnHwParamsFree( pHwParams );
        }
        outDevice._channelCount = maxChannels;
        outDevice._channelMask  = AudioChannelLayoutUtil::getChannelMask( AudioChannelLayout::Stereo );
        SW_LOG_INFO_ID( "AlsaAudioBackend.ALSADeviceDefaultUp", "ALSA device 'default': up to %# channels.", maxChannels );
        return true;
    }

    bool AlsaAudioBackend::startOutput( AudioChannelLayout layout )
    {
        using Internal = AlsaAudioBackendInternal;
        if ( _impl->_pPCM == nullptr )
            return false;
        const uint32 channelCount = AudioChannelLayoutUtil::getChannelCount( layout );
        const int32  setResult    = _impl->_pfnSetParams( _impl->_pPCM, Internal::kFormatFloatLe, Internal::kAccessRwInterleaved, channelCount, audio::kSampleRate, 1,
                                                          Internal::kLatencyMicroseconds );
        if ( setResult < 0 )
        {
            SW_LOG_WARNING_ID( "AlsaAudioBackend.SndPcmSetParams", "snd_pcm_set_params failed for %# channels (error %#). Rendering offline.", channelCount, setResult );
            return false;
        }
        _impl->_channelCount = channelCount;
        _impl->_listRender.assign( static_cast<size_t>( Internal::kWriteFrameCount ) * channelCount, 0.0f );
        _impl->_listDevice.assign( static_cast<size_t>( Internal::kWriteFrameCount ) * channelCount, 0.0f );
        _impl->_streamThread = thread( ThreadCreationKey{}, &AlsaAudioBackend::runStream, this );
        SW_LOG_INFO_ID( "AlsaAudioBackend.ALSAStreamReadyHz", "ALSA stream ready (%# Hz, %# channels, %# frames per write).", audio::kSampleRate, channelCount, Internal::kWriteFrameCount );
        return true;
    }

    void AlsaAudioBackend::runStream( StopToken stopToken )
    {
        using Internal            = AlsaAudioBackendInternal;
        const uint32 channelCount = _impl->_channelCount;
        // 두 채널(스테레오 · 바이노럴)은 순서가 같다. 5.1 · 7.1 만 ALSA 순서로 옮긴다.
        const bool bReorder = channelCount > audio::kStereoChannelCount;
        ThreadUtil::setCurrentThreadTimeCritical(); // 작업 풀이 코어를 모두 채워도 장치 버퍼가 비기 전에 돌아야 한다(`AudioRenderThread` 와 같다)
        while ( stopToken.isStopRequested() == false )
        {
            _service.getEngine().render( _impl->_listRender.data(), Internal::kWriteFrameCount );
            const float32* pWrite = _impl->_listRender.data();
            if ( bReorder )
            {
                Internal::reorderToAlsa( _impl->_listRender.data(), _impl->_listDevice.data(), Internal::kWriteFrameCount, channelCount );
                pWrite = _impl->_listDevice.data();
            }
            uint32 written = 0;
            while ( written < Internal::kWriteFrameCount && stopToken.isStopRequested() == false )
            {
                const int64 result = _impl->_pfnWritei( _impl->_pPCM, pWrite + static_cast<size_t>( written ) * channelCount, Internal::kWriteFrameCount - written );
                if ( result < 0 )
                {
                    // 언더런 · 일시정지 복귀는 복원하고 이어 쓴다. 복원하지 못하면 장치를 잃은 것이다 — 알리고 스레드를 끝낸다(예전에는 쉬지 않고 다시 렌더 · 쓰기를 돌았다).
                    if ( _impl->_pfnRecover( _impl->_pPCM, static_cast<int32>( result ), 1 ) < 0 )
                    {
                        _service.notifyOutputLost();
                        return;
                    }
                    _impl->_underrunCount.fetch_add( 1, std::memory_order_relaxed );
                    continue;
                }
                written += static_cast<uint32>( result );
            }
        }
    }

    void AlsaAudioBackend::stopOutput()
    {
        if ( _impl == nullptr )
            return;
        _impl->_streamThread.request_stop(); // 스레드 토큰으로 알린다(`runStream`)
        // 막혀 있는 writei 를 바로 풀기 위해 버퍼를 버린다.
        if ( _impl->_pPCM != nullptr && _impl->_pfnDrop != nullptr )
            (void)_impl->_pfnDrop( _impl->_pPCM ); // 스레드를 멈추려는 것이라 결과는 보지 않는다
        if ( _impl->_streamThread.joinable() )
            _impl->_streamThread.join();
    }

    void AlsaAudioBackend::closeDevice()
    {
        if ( _impl == nullptr )
            return;
        stopOutput();
        if ( _impl->_pPCM != nullptr )
        {
            (void)_impl->_pfnClose( _impl->_pPCM ); // 닫는 길이라 실패해도 할 일이 없다
            _impl->_pPCM = nullptr;
        }
        // snd_pcm_open 은 열기에 실패해도 alsa.conf 트리를 라이브러리 전역 캐시에 남긴다(CI ASan 의 LeakSanitizer 누수가 이것이었다).
        // 장치를 닫은 뒤 · 라이브러리를 내리기 전에 놓는다. 이 프로세스에서 libasound 를 쓰는 곳은 이 백엔드 하나다.
        if ( _impl->_pfnConfigFreeGlobal != nullptr )
        {
            (void)_impl->_pfnConfigFreeGlobal(); // 캐시를 비우는 것뿐이라 실패해도 닫기는 이어 간다
            _impl->_pfnConfigFreeGlobal = nullptr;
        }
        if ( _impl->_pLibrary != nullptr )
        {
            ModuleLibraryUtil::unloadDynamicLibrary( _impl->_pLibrary );
            _impl->_pLibrary = nullptr;
        }
    }

    uint64 AlsaAudioBackend::getUnderrunCount() const
    {
        return _impl->_underrunCount.load( std::memory_order_relaxed );
    }

    SW_REGISTER_AUDIO_BACKEND( AlsaAudioBackend ); // 리눅스 클라이언트의 장치 백엔드(`AudioService::create` 가 등록표에서 고른다)
} // namespace sw
