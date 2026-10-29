#include "pch.h"

#include "Engine/Audio/Windows/XAudio2AudioBackend.h"

#include "Core/Concurrency/StopToken.h"

#include "Engine/Audio/AudioClipDecoder.h"
#include "Engine/Audio/AudioEngine.h"

#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_WITH_CLIENT_CODE )
    #include "Engine/Foundation/EnginePlatformHeaders.h"
    #include "Engine/FileSystem/AssetIO.h"

namespace sw
{
    namespace
    {
        struct XAudio2AudioBackendInternal
        {
            /** @brief 스트리밍 버퍼 하나의 길이(프레임)입니다. 10.7 ms 입니다. */
            static constexpr uint32 kStreamBufferFrameCount = 512;
            /** @brief 장치에 걸어 둘 버퍼 수입니다. 지연은 대략 (수 − 1) × 버퍼 길이입니다. */
            static constexpr uint32 kStreamBufferCount = 3;
            /** @brief 확장형 형식의 IEEE float 하위 형식(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)입니다. ksmedia.h 를 끌어오지 않으려고 값을 적습니다. */
            static constexpr GUID kSubFormatIeeeFloat = {
                0x00000003,
                0x0000,
                0x0010,
                { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 }
            };

            /**
             * @brief Media Foundation · XAudio2 함수 표입니다. 처음 쓸 때 시스템 DLL 을 올려 한 번 채우고 프로세스 끝까지 둡니다.
             * @details **수명.** 표는 DLL 마다 참조 하나(`LoadLibraryExW`)를 가지고(`_hMfPlat` …) 프로세스 끝까지 놓지 않는다. 함수 포인터가 그 참조에 기대므로
             *          `GetModuleHandleW`(참조를 늘리지 않는다)로 빌린 모듈은 쓰지 않는다 — 다른 코드가 같은 DLL 을 내리면 포인터가 허공을 가리킨다.
             *          백엔드를 닫을 때 내리지 않는 이유: 대체 디코더(`decodeWithMediaFoundation`)는 클립 캐시의 워커 태스크가 백엔드와 무관하게 부르고,
             *          정적 소멸자 · `DllMain` 에서의 `FreeLibrary` 는 로더 잠금 아래라 금지다. 라이브러리 안의 전역 상태(Media Foundation 플랫폼 · COM)는
             *          `MFStartup`/`MFShutdown` · `CoInitializeEx`/`CoUninitialize` 쌍이 닫는다(장치 열기 · 닫기와 디코드 한 번마다 짝을 맞춘다).
             * @details import 라이브러리를 링크하지 않는다 — 없을 수 있는 DLL 이라(Media Foundation 이 빠진 Windows N) 일반 import 면 프로세스가
             *          뜨지 못하고, 지연 로드(/DELAYLOAD)는 lld 썽크가 첫 호출의 float 인자를 망가뜨려 쓰지 않는다.
             *          함수가 없으면 그 기능이 꺼진다(오프라인 렌더 · 대체 디코더 없음).
             */
            struct SystemAudioAPI
            {
                using PFN_MFStartup                          = HRESULT( WINAPI* )( ULONG, DWORD );
                using PFN_MFShutdown                         = HRESULT( WINAPI* )();
                using PFN_MFCreateMediaType                  = HRESULT( WINAPI* )( IMFMediaType** );
                using PFN_MFCreateSourceReaderFromByteStream = HRESULT( WINAPI* )( IMFByteStream*, IMFAttributes*, IMFSourceReader** );
                using PFN_MFCreateMFByteStreamOnStream       = HRESULT( WINAPI* )( IStream*, IMFByteStream** );
                using PFN_SHCreateMemStream                  = IStream*(WINAPI*)( const BYTE*, UINT );
                using PFN_XAudio2Create                      = HRESULT( WINAPI* )( IXAudio2**, UINT32, XAUDIO2_PROCESSOR );

                PFN_MFStartup                          _pfnMFStartup{ nullptr };
                PFN_MFShutdown                         _pfnMFShutdown{ nullptr };
                PFN_MFCreateMediaType                  _pfnMFCreateMediaType{ nullptr };
                PFN_MFCreateSourceReaderFromByteStream _pfnMFCreateSourceReaderFromByteStream{ nullptr };
                PFN_MFCreateMFByteStreamOnStream       _pfnMFCreateMFByteStreamOnStream{ nullptr };
                PFN_SHCreateMemStream                  _pfnSHCreateMemStream{ nullptr };
                PFN_XAudio2Create                      _pfnXAudio2Create{ nullptr };
                HMODULE                                _hMfPlat{ nullptr };      ///< mfplat.dll 참조(프로세스 끝까지)
                HMODULE                                _hMfReadWrite{ nullptr }; ///< mfreadwrite.dll 참조
                HMODULE                                _hShlwapi{ nullptr };     ///< shlwapi.dll 참조
                HMODULE                                _hXAudio2{ nullptr };     ///< xaudio2_9.dll 참조

                /** @brief Media Foundation 디코드에 필요한 함수가 모두 있으면 true 입니다. */
                bool hasMediaFoundation() const
                {
                    return _pfnMFStartup != nullptr && _pfnMFShutdown != nullptr && _pfnMFCreateMediaType != nullptr && _pfnMFCreateSourceReaderFromByteStream != nullptr &&
                           _pfnMFCreateMFByteStreamOnStream != nullptr && _pfnSHCreateMemStream != nullptr;
                }
            };

            /** @brief 참조를 가진 모듈 @p hModule 에서 @p pSymbol 을 찾습니다. 모듈이나 함수가 없으면 nullptr 입니다. */
            template <typename TFunction>
            static TFunction findSystemFunction( HMODULE hModule, const utf8* pSymbol )
            {
                if ( hModule == nullptr )
                    return nullptr;
                return reinterpret_cast<TFunction>( reinterpret_cast<void*>( GetProcAddress( hModule, pSymbol ) ) );
            }

            /** @brief System32 의 DLL 마다 참조를 하나씩 잡고 함수 표를 채웁니다. 없는 DLL · 함수는 nullptr 로 남깁니다. */
            static SystemAudioAPI loadSystemAudioAPI()
            {
                SystemAudioAPI api{};
                api._hMfPlat              = LoadLibraryExW( L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 );
                api._hMfReadWrite         = LoadLibraryExW( L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 );
                api._hShlwapi             = LoadLibraryExW( L"shlwapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 );
                api._hXAudio2             = LoadLibraryExW( L"xaudio2_9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 );
                api._pfnMFStartup         = findSystemFunction<SystemAudioAPI::PFN_MFStartup>( api._hMfPlat, "MFStartup" );
                api._pfnMFShutdown        = findSystemFunction<SystemAudioAPI::PFN_MFShutdown>( api._hMfPlat, "MFShutdown" );
                api._pfnMFCreateMediaType = findSystemFunction<SystemAudioAPI::PFN_MFCreateMediaType>( api._hMfPlat, "MFCreateMediaType" );
                api._pfnMFCreateSourceReaderFromByteStream =
                    findSystemFunction<SystemAudioAPI::PFN_MFCreateSourceReaderFromByteStream>( api._hMfReadWrite, "MFCreateSourceReaderFromByteStream" );
                api._pfnMFCreateMFByteStreamOnStream = findSystemFunction<SystemAudioAPI::PFN_MFCreateMFByteStreamOnStream>( api._hMfPlat, "MFCreateMFByteStreamOnStream" );
                api._pfnSHCreateMemStream            = findSystemFunction<SystemAudioAPI::PFN_SHCreateMemStream>( api._hShlwapi, "SHCreateMemStream" );
                api._pfnXAudio2Create                = findSystemFunction<SystemAudioAPI::PFN_XAudio2Create>( api._hXAudio2, "XAudio2Create" );
                return api;
            }

            /** @brief 함수 표입니다. 처음 부를 때 채운다(정적 지역 초기화라 여러 스레드가 함께 불러도 한 번). */
            static const SystemAudioAPI& getSystemAudioAPI()
            {
                static const SystemAudioAPI s_api = loadSystemAudioAPI();
                return s_api;
            }

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

                    const SystemAudioAPI& api = getSystemAudioAPI();
                    if ( api.hasMediaFoundation() && SUCCEEDED( api._pfnMFStartup( MF_VERSION, MFSTARTUP_FULL ) ) )
                        _bMfInit = SW_TRUE;
                }

                ~ScopedThreadComAndMf()
                {
                    if ( _bMfInit == SW_TRUE )
                        getSystemAudioAPI()._pfnMFShutdown();
                    if ( _bCoInit == SW_TRUE )
                        CoUninitialize();
                }
            };

            /**
             * @brief 공통 디코더가 다루지 않는 형식(MP3, ADPCM WAV 등)을 Media Foundation 으로 16 비트 PCM 으로 디코드합니다. 클립 캐시의 대체 디코더입니다.
             * @details 바이트는 `AssetIO` 로 읽어 메모리 스트림으로 넘긴다 — 디스크 경로(URL)로 열면 팩으로 쿠킹한 빌드에서 열 파일이 없다.
             */
            static bool decodeWithMediaFoundation( const AssetID& assetID, AudioPcm& outPcm )
            {
                const string_view     path = assetID.view();
                const SystemAudioAPI& api  = getSystemAudioAPI();
                if ( api.hasMediaFoundation() == false )
                    return false; // Media Foundation 이 없는 Windows(N 에디션) — 공통 디코더가 못 푸는 형식은 재생하지 않는다
                vector<uint8> bytes;
                if ( AssetIO::readBinaryAsset( assetID, bytes ) == false || bytes.empty() )
                    return false;
                ScopedThreadComAndMf threadComScope;
                IStream*             pMemoryStream = api._pfnSHCreateMemStream( bytes.data(), static_cast<UINT>( bytes.size() ) );
                if ( pMemoryStream == nullptr )
                    return false;
                IMFByteStream* pByteStream{ nullptr };
                HRESULT        hr = api._pfnMFCreateMFByteStreamOnStream( pMemoryStream, &pByteStream );
                pMemoryStream->Release();
                if ( FAILED( hr ) || pByteStream == nullptr )
                    return false;
                // 원본 이름을 알려 주면 소스 리졸버가 확장자로 형식을 고른다(내용 추측만으로는 일부 형식을 못 고른다).
                IMFAttributes* pStreamAttributes{ nullptr };
                if ( SUCCEEDED( pByteStream->QueryInterface( IID_PPV_ARGS( &pStreamAttributes ) ) ) && pStreamAttributes != nullptr )
                {
                    const wstring originName = StringUtil::utf8ToUTF16( string( path ).c_str() );
                    pStreamAttributes->SetString( MF_BYTESTREAM_ORIGIN_NAME, originName.c_str() );
                    pStreamAttributes->Release();
                }
                IMFSourceReader* pReader{ nullptr };
                hr = api._pfnMFCreateSourceReaderFromByteStream( pByteStream, nullptr, &pReader );
                pByteStream->Release();
                if ( FAILED( hr ) || pReader == nullptr )
                    return false;

                IMFMediaType* pPartial{ nullptr };
                // 실패하면 pPartial 이 nullptr 인 채로 돌아온다. 결과를 보지 않으면 바로 널 역참조다.
                if ( FAILED( api._pfnMFCreateMediaType( &pPartial ) ) || pPartial == nullptr )
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
                uint32               _channelCount{ 0 }; ///< 엔진 레이아웃의 채널 수다. 장치로 보내는 교차 버퍼의 프레임 크기다.
                vector<float32>      _listBuffer{};      ///< 버퍼 `kStreamBufferCount` 개를 이어 붙인 것이다.
                uint32               _nextBuffer{ 0 };
                StopSource           _stop;

                virtual ~StreamCallback() = default;

                /** @brief 다음 버퍼를 렌더해 제출합니다. */
                void submitNext()
                {
                    if ( _stop.isStopRequested() || _pVoice == nullptr )
                        return;
                    float32* pBuffer = _listBuffer.data() + static_cast<size_t>( _nextBuffer ) * kStreamBufferFrameCount * _channelCount;
                    _pEngine->render( pBuffer, kStreamBufferFrameCount );
                    XAUDIO2_BUFFER buffer{};
                    buffer.AudioBytes = kStreamBufferFrameCount * _channelCount * sizeof( float32 );
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
    SW_LOG_CALLER( "XAudio2AudioBackend" );

    /**
     * @brief XAudio2 백엔드의 상태 전부입니다. 헤더가 XAudio2 헤더를 끌어오지 않도록 여기 숨깁니다.
     */
    struct XAudio2AudioBackendImpl
    {
        XAudio2AudioBackendInternal::StreamCallback _callback;            /**< 스트리밍 콜백(버퍼 포함)입니다. */
        IXAudio2*                                   _pXAudio;             /**< XAudio2 엔진입니다. */
        IXAudio2MasteringVoice*                     _pMasterVoice;        /**< 마스터링 보이스입니다(볼륨은 엔진의 master 버스가 맡는다). */
        IXAudio2SourceVoice*                        _pStreamVoice;        /**< 엔진 출력을 흘리는 소스 보이스입니다. */
        uint8                                       _bComInitialized : 1; /**< 이 시스템이 COM 을 초기화했는지 여부입니다. */
        uint8                                       _bMfInitialized  : 1; /**< 이 시스템이 Media Foundation 을 초기화했는지 여부입니다. */
        [[maybe_unused]] uint8                      _reservedAudio   : 6; /**< 비트필드 패딩입니다. */

        /** @brief 아직 아무 장치도 잡지 않은 상태로 둡니다. */
        XAudio2AudioBackendImpl()
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

    XAudio2AudioBackend::XAudio2AudioBackend()
        : _impl{ make_unique<XAudio2AudioBackendImpl>() }
    {
        getEngine().getClipStore().setFallbackDecoder( &XAudio2AudioBackendInternal::decodeWithMediaFoundation );
    }

    XAudio2AudioBackend::~XAudio2AudioBackend()
    {
        shutdown();
    }

    bool XAudio2AudioBackend::openDevice( AudioOutputDevice& outDevice )
    {
        HRESULT hr = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
        if ( hr == S_OK || hr == S_FALSE )
            _impl->_bComInitialized = SW_TRUE;
        else if ( hr != RPC_E_CHANGED_MODE )
            SW_LOG_WARNING( "CoInitializeEx failed (0x%#).", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ) );

        const XAudio2AudioBackendInternal::SystemAudioAPI& api = XAudio2AudioBackendInternal::getSystemAudioAPI();
        if ( api.hasMediaFoundation() == false )
            SW_LOG_WARNING( "Media Foundation is not available (mfplat.dll / mfreadwrite.dll) - clips the built-in decoders cannot read will not play." );
        else
        {
            const HRESULT mfHr = api._pfnMFStartup( MF_VERSION, MFSTARTUP_FULL );
            if ( SUCCEEDED( mfHr ) )
                _impl->_bMfInitialized = SW_TRUE;
            else
                SW_LOG_WARNING( "MFStartup failed (0x%#).", Fmt( static_cast<uint32>( mfHr ), Format( 8, Format::Padding::Zero ).hex() ) );
        }

        if ( api._pfnXAudio2Create == nullptr )
        {
            SW_LOG_WARNING( "XAudio2 is not available (xaudio2_9.dll). Rendering offline." );
            closeDevice();
            return false;
        }
        hr = api._pfnXAudio2Create( &_impl->_pXAudio, 0, XAUDIO2_DEFAULT_PROCESSOR );
        if ( FAILED( hr ) || _impl->_pXAudio == nullptr )
        {
            SW_LOG_WARNING( "XAudio2Create failed (0x%#). Rendering offline.", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ) );
            _impl->_pXAudio = nullptr;
            closeDevice();
            return false;
        }
        // 마스터링 보이스는 장치의 채널 수 그대로 연다. 엔진 레이아웃은 그 채널 수와 마스크를 보고 서비스가 정한다(협상).
        hr = _impl->_pXAudio->CreateMasteringVoice( &_impl->_pMasterVoice, XAUDIO2_DEFAULT_CHANNELS, audio::kSampleRate );
        if ( FAILED( hr ) || _impl->_pMasterVoice == nullptr )
        {
            // 출력 장치가 없는 기계(CI 러너, 서버, 원격 세션)는 고장이 아니다. 오프라인 렌더로 동작한다는 것만 알린다. 장치가 있는데 실패한 경우만 경고한다.
            if ( hr == HRESULT_FROM_WIN32( ERROR_NOT_FOUND ) )
                SW_LOG_INFO( "No audio output device (0x%#). Rendering offline.", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ) );
            else
                SW_LOG_WARNING( "CreateMasteringVoice failed (0x%#). Rendering offline.", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ) );
            _impl->_pMasterVoice = nullptr;
            closeDevice();
            return false;
        }
        XAUDIO2_VOICE_DETAILS details{};
        _impl->_pMasterVoice->GetVoiceDetails( &details );
        DWORD channelMask = 0;
        if ( FAILED( _impl->_pMasterVoice->GetChannelMask( &channelMask ) ) )
            channelMask = 0; // 마스크를 모르는 장치는 Auto 에서 스테레오로, 명시 모드는 채널 수로만 판단한다
        outDevice._channelCount = details.InputChannels;
        outDevice._channelMask  = static_cast<uint32>( channelMask );
        SW_LOG_INFO( "Audio device: %# channels (mask 0x%#).", outDevice._channelCount, Fmt( outDevice._channelMask, Format().hex() ) );
        return true;
    }

    bool XAudio2AudioBackend::startOutput( AudioChannelLayout layout )
    {
        if ( _impl->_pXAudio == nullptr || _impl->_pMasterVoice == nullptr )
            return false;
        // 소스 형식에 레이아웃의 채널 마스크를 실어 XAudio2 가 스피커 자리대로 장치 채널에 잇게 한다(장치가 더 많은 채널을 가져도 같은 자리에 간다).
        const uint32         channelCount = AudioChannelLayoutUtil::getChannelCount( layout );
        WAVEFORMATEXTENSIBLE format{};
        format.Format.wFormatTag           = WAVE_FORMAT_EXTENSIBLE;
        format.Format.nChannels            = static_cast<WORD>( channelCount );
        format.Format.nSamplesPerSec       = audio::kSampleRate;
        format.Format.wBitsPerSample       = 32;
        format.Format.nBlockAlign          = static_cast<WORD>( channelCount * sizeof( float32 ) );
        format.Format.nAvgBytesPerSec      = audio::kSampleRate * format.Format.nBlockAlign;
        format.Format.cbSize               = sizeof( WAVEFORMATEXTENSIBLE ) - sizeof( WAVEFORMATEX );
        format.Samples.wValidBitsPerSample = 32;
        format.dwChannelMask               = AudioChannelLayoutUtil::getChannelMask( layout );
        format.SubFormat                   = XAudio2AudioBackendInternal::kSubFormatIeeeFloat;
        HRESULT hr                         = _impl->_pXAudio->CreateSourceVoice( &_impl->_pStreamVoice, &format.Format, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &_impl->_callback );
        if ( FAILED( hr ) || _impl->_pStreamVoice == nullptr )
        {
            SW_LOG_WARNING( "CreateSourceVoice failed (0x%#). Rendering offline.", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ) );
            _impl->_pStreamVoice = nullptr;
            return false;
        }

        XAudio2AudioBackendInternal::StreamCallback& callback = _impl->_callback;
        callback._pEngine                                     = &getEngine();
        callback._pVoice                                      = _impl->_pStreamVoice;
        callback._channelCount                                = channelCount;
        callback._nextBuffer                                  = 0;
        callback._stop                                        = StopSource{};
        callback._listBuffer.assign( static_cast<size_t>( XAudio2AudioBackendInternal::kStreamBufferFrameCount ) * channelCount *
                                         XAudio2AudioBackendInternal::kStreamBufferCount,
                                     0.0f );
        // 처음 버퍼들은 여기서 채운다. 이후는 버퍼가 끝날 때마다 콜백이 하나씩 채운다.
        for ( uint32 bufferIndex = 0; bufferIndex < XAudio2AudioBackendInternal::kStreamBufferCount; ++bufferIndex )
        {
            callback.submitNext();
        }
        getEngine().releaseAudioThread(); // 처음 버퍼는 이 스레드가 렌더했다 — 이후 렌더는 XAudio2 콜백 스레드가 맡는다(첫 호출이 역할을 받는다)
        hr = _impl->_pStreamVoice->Start( 0 );
        if ( FAILED( hr ) )
        {
            SW_LOG_WARNING( "Starting the XAudio2 stream failed (0x%#).", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ) );
            stopOutput();
            return false;
        }
        SW_LOG_INFO( "XAudio2 stream ready (%# Hz, %# channels, %# frames x %#).", audio::kSampleRate, channelCount, XAudio2AudioBackendInternal::kStreamBufferFrameCount,
                     XAudio2AudioBackendInternal::kStreamBufferCount );
        return true;
    }

    void XAudio2AudioBackend::stopOutput()
    {
        if ( _impl == nullptr )
            return;
        // 콜백이 더 제출하지 않게 먼저 표시하고, 보이스를 파괴한다. DestroyVoice 는 처리 중인 콜백이 끝날 때까지 기다린다.
        _impl->_callback._stop.requestStop();
        if ( _impl->_pStreamVoice != nullptr )
        {
            (void)_impl->_pStreamVoice->Stop( 0 ); // 바로 아래에서 보이스를 파괴한다
            _impl->_pStreamVoice->DestroyVoice();
            _impl->_pStreamVoice = nullptr;
        }
        _impl->_callback._pVoice = nullptr;
    }

    void XAudio2AudioBackend::closeDevice()
    {
        if ( _impl == nullptr )
            return;
        stopOutput();
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
            XAudio2AudioBackendInternal::getSystemAudioAPI()._pfnMFShutdown();
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
