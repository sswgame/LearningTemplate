#include "pch.h"

#include "Engine/Audio/Windows/XAudio2System.h"

#include "Engine/Audio/AudioClipDecoder.h"
#include "Engine/Audio/AudioEngine.h"

#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_WITH_CLIENT_CODE )
    #include "Engine/Common/EnginePlatformHeaders.h"
    #include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    namespace
    {
        struct XAudio2SystemInternal
        {
            /** @brief 스트리밍 버퍼 하나의 길이(프레임)입니다 — 10.7 ms. */
            static constexpr uint32 kStreamBufferFrameCount = 512;
            /** @brief 장치에 걸어 둘 버퍼 수입니다. 지연은 대략 (수 − 1) × 버퍼 길이입니다. */
            static constexpr uint32 kStreamBufferCount = 3;

            /**
             * @brief 부르는 스레드에서 COM 과 Media Foundation 이 초기화되어 있게 하는 RAII 도우미입니다.
             */
            struct ScopedThreadComAndMf
            {
                uint8                  _bCoInit  : 1;
                uint8                  _bMfInit  : 1;
                [[maybe_unused]] uint8 _reserved : 6;

                ScopedThreadComAndMf()
                    : _bCoInit{ SW_FALSE }
                    , _bMfInit{ SW_FALSE }
                    , _reserved{ 0 }
                {
                    const HRESULT hrCo = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
                    if ( SUCCEEDED( hrCo ) )
                        _bCoInit = SW_TRUE;

                    const HRESULT hrMf = MFStartup( MF_VERSION );
                    if ( SUCCEEDED( hrMf ) )
                        _bMfInit = SW_TRUE;
                }

                ~ScopedThreadComAndMf()
                {
                    if ( _bMfInit == SW_TRUE )
                        MFShutdown();
                    if ( _bCoInit == SW_TRUE )
                        CoUninitialize();
                }
            };

            /**
             * @brief 공통 디코더가 다루지 않는 형식(MP3 · ADPCM WAV 등)을 Media Foundation 으로 16 비트 PCM 으로 풉니다. 클립 캐시의 대체 디코더입니다.
             */
            static bool decodeWithMediaFoundation( string_view path, AudioPcm& outPcm )
            {
                string absPath = ResourceUtil::getResourcePath( path );
                if ( absPath.empty() )
                    absPath = string( path );

                ScopedThreadComAndMf threadComScope;
                IMFSourceReader*     pReader{ nullptr };
                const wstring        wpath = StringUtil::utf8ToUtf16( absPath.c_str() );
                HRESULT              hr    = MFCreateSourceReaderFromURL( wpath.c_str(), nullptr, &pReader );
                if ( FAILED( hr ) || pReader == nullptr )
                    return false;

                IMFMediaType* pPartial{ nullptr };
                // 실패하면 pPartial 이 nullptr 인 채로 돌아온다. 결과를 보지 않으면 바로 널 역참조다.
                if ( FAILED( MFCreateMediaType( &pPartial ) ) || pPartial == nullptr )
                {
                    pReader->Release();
                    return false;
                }
                pPartial->SetGUID( MF_MT_MAJOR_TYPE, MFMediaType_Audio );
                pPartial->SetGUID( MF_MT_SUBTYPE, MFAudioFormat_PCM );
                pReader->SetCurrentMediaType( static_cast<DWORD>( MF_SOURCE_READER_FIRST_AUDIO_STREAM ), nullptr, pPartial );
                pPartial->Release();

                IMFMediaType* pNative{ nullptr };
                hr = pReader->GetCurrentMediaType( static_cast<DWORD>( MF_SOURCE_READER_FIRST_AUDIO_STREAM ), &pNative );
                if ( FAILED( hr ) || pNative == nullptr )
                {
                    pReader->Release();
                    return false;
                }
                AudioPcm pcm;
                pcm._channelCount  = static_cast<uint16>( MFGetAttributeUINT32( pNative, MF_MT_AUDIO_NUM_CHANNELS, 2 ) );
                pcm._sampleRate    = MFGetAttributeUINT32( pNative, MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100 );
                pcm._bitsPerSample = static_cast<uint16>( MFGetAttributeUINT32( pNative, MF_MT_AUDIO_BITS_PER_SAMPLE, 16 ) );
                pNative->Release();

                for ( ;; )
                {
                    DWORD           flags{ 0 };
                    IMFSample*      pSample{ nullptr };
                    IMFMediaBuffer* pBuffer{ nullptr };
                    hr = pReader->ReadSample( static_cast<DWORD>( MF_SOURCE_READER_FIRST_AUDIO_STREAM ), 0, nullptr, &flags, nullptr, &pSample );
                    if ( FAILED( hr ) || ( ( flags & MF_SOURCE_READERF_ENDOFSTREAM ) != 0 ) )
                        break;
                    if ( pSample == nullptr )
                        continue;
                    hr = pSample->ConvertToContiguousBuffer( &pBuffer );
                    pSample->Release();
                    if ( FAILED( hr ) || pBuffer == nullptr )
                        continue;
                    BYTE* pData{ nullptr };
                    DWORD maxLength     = 0;
                    DWORD currentLength = 0;
                    pBuffer->Lock( &pData, &maxLength, &currentLength );
                    if ( pData != nullptr && currentLength > 0 )
                        pcm._listData.insert( pcm._listData.end(), pData, pData + currentLength );
                    pBuffer->Unlock();
                    pBuffer->Release();
                }
                pReader->Release();
                if ( pcm._listData.empty() )
                    return false;
                outPcm = std::move( pcm );
                return true;
            }

            /**
             * @brief 스트리밍 보이스의 콜백입니다. 버퍼 하나가 끝날 때마다(XAudio2 처리 스레드) 다음 버퍼를 렌더해 제출합니다.
             */
            struct StreamCallback : public IXAudio2VoiceCallback
            {
                AudioEngine*         _pEngine{ nullptr };
                IXAudio2SourceVoice* _pVoice{ nullptr };
                vector<float32>      _listBuffer{}; ///< 버퍼 `kStreamBufferCount` 개를 이어 붙인 것
                uint32               _nextBuffer{ 0 };
                std::atomic<bool>    _bStopping{ false };

                virtual ~StreamCallback() = default;

                /** @brief 다음 버퍼를 렌더해 제출합니다. */
                void submitNext()
                {
                    if ( _bStopping.load( std::memory_order_acquire ) || _pVoice == nullptr )
                        return;
                    float32* pBuffer = _listBuffer.data() + static_cast<size_t>( _nextBuffer ) * kStreamBufferFrameCount * audio::kChannelCount;
                    _pEngine->render( pBuffer, kStreamBufferFrameCount );
                    XAUDIO2_BUFFER buffer{};
                    buffer.AudioBytes = kStreamBufferFrameCount * audio::kChannelCount * sizeof( float32 );
                    buffer.pAudioData = reinterpret_cast<const BYTE*>( pBuffer );
                    (void)_pVoice->SubmitSourceBuffer( &buffer );
                    _nextBuffer = ( _nextBuffer + 1 ) % kStreamBufferCount;
                }

                void STDMETHODCALLTYPE OnVoiceProcessingPassStart( UINT32 ) noexcept override {}
                void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() noexcept override {}
                void STDMETHODCALLTYPE OnStreamEnd() noexcept override {}
                void STDMETHODCALLTYPE OnBufferStart( void* ) noexcept override {}
                void STDMETHODCALLTYPE OnBufferEnd( void* ) noexcept override { submitNext(); }
                void STDMETHODCALLTYPE OnLoopEnd( void* ) noexcept override {}
                void STDMETHODCALLTYPE OnVoiceError( void*, HRESULT ) noexcept override {}
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "XAudio2System" );

    /**
     * @brief XAudio2 백엔드의 상태 전부입니다. 헤더가 XAudio2 헤더를 끌어오지 않도록 여기 숨깁니다.
     */
    struct XAudio2SystemImpl
    {
        XAudio2SystemInternal::StreamCallback _callback;            /**< 스트리밍 콜백(버퍼 포함)입니다. */
        IXAudio2*                             _pXAudio;             /**< XAudio2 엔진입니다. */
        IXAudio2MasteringVoice*               _pMasterVoice;        /**< 마스터링 보이스입니다(볼륨은 엔진의 master 버스가 맡는다). */
        IXAudio2SourceVoice*                  _pStreamVoice;        /**< 엔진 출력을 흘리는 소스 보이스입니다. */
        uint8                                 _bComInitialized : 1; /**< 이 시스템이 COM 을 초기화했는지 여부입니다. */
        uint8                                 _bMfInitialized  : 1; /**< 이 시스템이 Media Foundation 을 초기화했는지 여부입니다. */
        [[maybe_unused]] uint8                _reservedAudio   : 6; /**< 비트필드 패딩입니다. */

        /** @brief 아직 아무 장치도 잡지 않은 상태로 둡니다. */
        XAudio2SystemImpl()
            : _callback{}
            , _pXAudio{ nullptr }
            , _pMasterVoice{ nullptr }
            , _pStreamVoice{ nullptr }
            , _bComInitialized{ SW_FALSE }
            , _bMfInitialized{ SW_FALSE }
            , _reservedAudio{ 0 }
        {
        }
    };

    XAudio2System::XAudio2System()
        : _impl{ make_unique<XAudio2SystemImpl>() }
    {
        getEngine().getClipStore().setFallbackDecoder( &XAudio2SystemInternal::decodeWithMediaFoundation );
    }

    XAudio2System::~XAudio2System()
    {
        shutdown();
    }

    bool XAudio2System::openOutput()
    {
        HRESULT hr = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
        if ( hr == S_OK || hr == S_FALSE )
            _impl->_bComInitialized = SW_TRUE;
        else if ( hr != RPC_E_CHANGED_MODE )
            SW_LOG_WARNING( "CoInitializeEx failed (0x%#).", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ) );

        const HRESULT mfHr = MFStartup( MF_VERSION );
        if ( SUCCEEDED( mfHr ) )
            _impl->_bMfInitialized = SW_TRUE;
        else
            SW_LOG_WARNING( "MFStartup failed (0x%#).", Fmt( static_cast<uint32>( mfHr ), Format( 8, Format::Padding::Zero ).hex() ) );

        hr = XAudio2Create( &_impl->_pXAudio, 0, XAUDIO2_DEFAULT_PROCESSOR );
        if ( FAILED( hr ) || _impl->_pXAudio == nullptr )
        {
            SW_LOG_WARNING( "XAudio2Create failed (0x%#). Rendering offline.", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ) );
            _impl->_pXAudio = nullptr;
            closeOutput();
            return false;
        }
        hr = _impl->_pXAudio->CreateMasteringVoice( &_impl->_pMasterVoice, audio::kChannelCount, audio::kSampleRate );
        if ( FAILED( hr ) || _impl->_pMasterVoice == nullptr )
        {
            // 출력 장치가 없는 기계(CI 러너 · 서버 · 원격 세션)는 고장이 아니다 — 오프라인 렌더로 돈다는 것만 알린다. 장치가 있는데 실패한 것만 경고다.
            if ( hr == HRESULT_FROM_WIN32( ERROR_NOT_FOUND ) )
                SW_LOG_INFO( "No audio output device (0x%#). Rendering offline.", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ) );
            else
                SW_LOG_WARNING( "CreateMasteringVoice failed (0x%#). Rendering offline.", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ) );
            _impl->_pMasterVoice = nullptr;
            closeOutput();
            return false;
        }

        WAVEFORMATEX format{};
        format.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels       = static_cast<WORD>( audio::kChannelCount );
        format.nSamplesPerSec  = audio::kSampleRate;
        format.wBitsPerSample  = 32;
        format.nBlockAlign     = static_cast<WORD>( audio::kChannelCount * sizeof( float32 ) );
        format.nAvgBytesPerSec = audio::kSampleRate * format.nBlockAlign;
        hr                     = _impl->_pXAudio->CreateSourceVoice( &_impl->_pStreamVoice, &format, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &_impl->_callback );
        if ( FAILED( hr ) || _impl->_pStreamVoice == nullptr )
        {
            SW_LOG_WARNING( "CreateSourceVoice failed (0x%#). Rendering offline.", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ) );
            _impl->_pStreamVoice = nullptr;
            closeOutput();
            return false;
        }

        XAudio2SystemInternal::StreamCallback& callback = _impl->_callback;
        callback._pEngine                               = &getEngine();
        callback._pVoice                                = _impl->_pStreamVoice;
        callback._nextBuffer                            = 0;
        callback._bStopping.store( false, std::memory_order_release );
        callback._listBuffer.assign( static_cast<size_t>( XAudio2SystemInternal::kStreamBufferFrameCount ) * audio::kChannelCount *
                                         XAudio2SystemInternal::kStreamBufferCount,
                                     0.0f );
        // 처음 버퍼들은 여기서 채운다. 이후는 버퍼가 끝날 때마다 콜백이 하나씩 채운다.
        for ( uint32 bufferIndex = 0; bufferIndex < XAudio2SystemInternal::kStreamBufferCount; ++bufferIndex )
            callback.submitNext();
        _impl->_pStreamVoice->Start( 0 );
        SW_LOG_INFO( "XAudio2 stream ready (%# Hz, %# frames x %#).", audio::kSampleRate, XAudio2SystemInternal::kStreamBufferFrameCount,
                     XAudio2SystemInternal::kStreamBufferCount );
        return true;
    }

    void XAudio2System::closeOutput()
    {
        if ( _impl == nullptr )
            return;
        // 콜백이 더 제출하지 않게 먼저 표시하고, 보이스를 부순다 — DestroyVoice 는 처리 중인 콜백이 끝날 때까지 기다린다.
        _impl->_callback._bStopping.store( true, std::memory_order_release );
        if ( _impl->_pStreamVoice != nullptr )
        {
            _impl->_pStreamVoice->Stop( 0 );
            _impl->_pStreamVoice->DestroyVoice();
            _impl->_pStreamVoice = nullptr;
        }
        _impl->_callback._pVoice = nullptr;
        if ( _impl->_pMasterVoice != nullptr )
        {
            _impl->_pMasterVoice->DestroyVoice();
            _impl->_pMasterVoice = nullptr;
        }
        if ( _impl->_pXAudio != nullptr )
        {
            _impl->_pXAudio->Release();
            _impl->_pXAudio = nullptr;
        }
        if ( _impl->_bMfInitialized == SW_TRUE )
        {
            MFShutdown();
            _impl->_bMfInitialized = SW_FALSE;
        }
        if ( _impl->_bComInitialized == SW_TRUE )
        {
            CoUninitialize();
            _impl->_bComInitialized = SW_FALSE;
        }
    }
} // namespace sw
#endif
