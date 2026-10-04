#include "pch.h"

#include "Engine/Audio/Windows/XAudio2System.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Memory/MemoryProfiler.h"

#include "Engine/Audio/AudioClipDecoder.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Task/TaskManager.h"

    #include "Engine/Common/EnginePlatformHeaders.h"
    #include "Engine/Common/EngineServices.h"

namespace sw
{
    namespace
    {
        struct XAudio2SystemInternal
        {
            /**
             * @struct PcmClip
             * @brief 디코드한 소리 하나입니다. 형식은 **확장형까지** 담습니다(`_format.Format.cbSize` 가 22 면 확장형).
             * @details 24 비트 · 다채널 · 부동소수 WAV 는 DAW 가 흔히 내보내는 형식인데 `WAVEFORMATEX` 만으로는 그 하위 형식을 전하지 못합니다.
             */
            struct PcmClip
            {
                WAVEFORMATEXTENSIBLE _format{};
                vector<uint8>        _listData;
            };

            struct VoiceBuffer
            {
                IXAudio2SourceVoice* _pVoice{ nullptr };
                shared_ptr<PcmClip>  _pClip{ nullptr };
            };

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

            /** @brief 공통 디코더의 결과를 XAudio2 형식으로 옮깁니다. 확장형이었으면 확장형으로 넘깁니다(24 비트 · 다채널 · float). */
            static void movePcmToClip( AudioPcm&& pcm, PcmClip& outClip )
            {
                WAVEFORMATEXTENSIBLE fmt{};
                fmt.Format.wFormatTag      = pcm._bFloat ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
                fmt.Format.nChannels       = pcm._channelCount;
                fmt.Format.nSamplesPerSec  = pcm._sampleRate;
                fmt.Format.wBitsPerSample  = pcm._bitsPerSample;
                fmt.Format.nBlockAlign     = pcm.getBlockAlign();
                fmt.Format.nAvgBytesPerSec = pcm._sampleRate * fmt.Format.nBlockAlign;
                fmt.Format.cbSize          = 0;
                if ( pcm._bExtensible )
                {
                    fmt.Format.wFormatTag           = WAVE_FORMAT_EXTENSIBLE;
                    fmt.Format.cbSize               = 22;
                    fmt.Samples.wValidBitsPerSample = pcm._validBitsPerSample;
                    fmt.dwChannelMask               = pcm._channelMask;
                    // KSDATAFORMAT_SUBTYPE_PCM · _IEEE_FLOAT 와 같은 값이다(Data1 이 형식 태그, 나머지는 공통). ksmedia.h 의 GUID 정의를 끌어오지 않는다.
                    fmt.SubFormat = GUID{
                        pcm._bFloat ? DWORD{ WAVE_FORMAT_IEEE_FLOAT }
                                    : DWORD{ WAVE_FORMAT_PCM },
                        0x0000,
                        0x0010,
                        { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 }
                    };
                }
                outClip._format   = fmt;
                outClip._listData = std::move( pcm._listData );
            }

            /**
             * @brief WAV · OGG 를 공통 디코더로 풉니다. 바이트에서 풀므로 팩 안의 파일도 됩니다.
             * @param path 리소스 상대 경로와 절대 경로를 모두 받습니다. 리소스로 먼저 찾고, 없으면 파일로 읽습니다.
             */
            [[nodiscard]] static bool loadDecodedClip( string_view path, PcmClip& outClip )
            {
                vector<uint8> listFile;
                if ( ResourceUtil::readBinaryResource( path, listFile ) == false && FileUtil::readFile( path, listFile ) == false )
                    return false;
                AudioPcm pcm;
                if ( AudioClipDecoder::decode( path, listFile.data(), listFile.size(), pcm ) == false )
                    return false;
                movePcmToClip( std::move( pcm ), outClip );
                return true;
            }

            /**
             * @brief Windows Media Foundation 으로 MP3 등 압축 오디오를 PCM 바이트 스트림으로 디코딩합니다.
             */
            [[nodiscard]] static bool loadViaMediaFoundation( string_view absPath, PcmClip& outClip )
            {
                ScopedThreadComAndMf threadComScope;

                IMFSourceReader* pReader{ nullptr };
                const wstring    wpath = StringUtil::utf8ToUtf16( string( absPath ).c_str() );
                HRESULT          hr    = MFCreateSourceReaderFromURL( wpath.c_str(), nullptr, &pReader );
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

                const UINT32 channels   = MFGetAttributeUINT32( pNative, MF_MT_AUDIO_NUM_CHANNELS, 2 );
                const UINT32 sampleRate = MFGetAttributeUINT32( pNative, MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100 );
                const UINT32 bits       = MFGetAttributeUINT32( pNative, MF_MT_AUDIO_BITS_PER_SAMPLE, 16 );
                pNative->Release();

                WAVEFORMATEX fmt{};
                fmt.wFormatTag      = WAVE_FORMAT_PCM;
                fmt.nChannels       = static_cast<WORD>( channels );
                fmt.nSamplesPerSec  = sampleRate;
                fmt.wBitsPerSample  = static_cast<WORD>( bits );
                fmt.nBlockAlign     = static_cast<WORD>( fmt.nChannels * fmt.wBitsPerSample / 8 );
                fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

                vector<uint8> listPcm;
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
                    DWORD maxLen = 0, curLen = 0;
                    pBuffer->Lock( &pData, &maxLen, &curLen );
                    if ( pData != nullptr && curLen > 0 )
                        listPcm.insert( listPcm.end(), pData, pData + curLen );
                    pBuffer->Unlock();
                    pBuffer->Release();
                }

                pReader->Release();

                if ( listPcm.empty() )
                    return false;
                outClip._format               = WAVEFORMATEXTENSIBLE{};
                outClip._format.Format        = fmt;
                outClip._format.Format.cbSize = 0;
                outClip._listData             = std::move( listPcm );
                return true;
            }

            /** @brief 확장자에 맞는 디코더로 PCM 을 뽑습니다. */
            [[nodiscard]] static bool loadClip( string_view path, PcmClip& outClip )
            {
                // WAV · OGG 는 공통 디코더가 읽는다(팩 안에서도 된다). 못 푼 OGG 는 실패다 — Media Foundation 은 Vorbis 를 모른다.
                // 공통 디코더가 못 읽는 WAV(ADPCM 등)와 다른 형식(MP3 등)은 Media Foundation 에 넘긴다.
                if ( AudioClipDecoder::isSupportedExtension( path ) )
                {
                    if ( loadDecodedClip( path, outClip ) )
                        return true;
                    if ( FileUtil::hasExtension( path, ".ogg" ) )
                        return false;
                }

                string absPath = ResourceUtil::getResourcePath( path );
                if ( absPath.empty() )
                    absPath = string( path );
                return loadViaMediaFoundation( absPath, outClip );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "XAudio2System" );

    /**
     * @brief XAudio2 백엔드의 상태 전부입니다. 헤더가 XAudio2 헤더를 끌어오지 않도록 여기 숨깁니다.
     * @note 볼륨과 음소거는 여기 없습니다. `IAudioSystem` 이 한 자리에서 들고 있고, 이 구조체는
     *       그 값을 살아 있는 보이스에 반영하기만 합니다.
     */
    struct XAudio2SystemImpl
    {
        /** @brief 재사용을 위해 들고 있을 유휴 보이스의 최대 개수입니다. */
        static constexpr size_t kMaxIdleVoices = 32;

        IXAudio2*                                                         _pXAudio;             /**< XAudio2 엔진입니다. */
        IXAudio2MasteringVoice*                                           _pMasterVoice;        /**< 마스터 보이스입니다. 음소거가 걸리는 유일한 자리입니다. */
        IXAudio2SourceVoice*                                              _pMusicVoice;         /**< 루프 재생 중인 배경음악 보이스입니다. */
        shared_ptr<XAudio2SystemInternal::PcmClip>                        _pMusicClip;          /**< 배경음악 보이스가 읽고 있는 PCM 입니다. */
        vector<XAudio2SystemInternal::VoiceBuffer>                        _listActiveVoice;     /**< 재생 중인 효과음 보이스입니다. */
        vector<XAudio2SystemInternal::VoiceBuffer>                        _listIdleVoice;       /**< 재사용을 기다리는 보이스입니다. */
        unordered_map<string, shared_ptr<XAudio2SystemInternal::PcmClip>> _mapClipCache;        /**< 경로 → 디코드된 PCM 캐시입니다. */
        string                                                            _musicPath;           /**< 마지막으로 요청된 배경음악 경로입니다. */
        uint64                                                            _musicGeneration;     /**< 배경음악 요청 번호입니다. 늦게 도착한 디코드를 버리는 데 씁니다. */
        mutex                                                             _clipCacheMutex;      /**< `_mapClipCache` 를 지킵니다. */
        mutex                                                             _voiceMutex;          /**< 보이스 · `_musicPath` · `_musicGeneration` 을 지킵니다. */
        uint8                                                             _bInitialized    : 1; /**< initialize 가 끝났는지 여부입니다. */
        uint8                                                             _bComInitialized : 1; /**< 이 시스템이 COM 을 초기화했는지 여부입니다. */
        uint8                                                             _bMfInitialized  : 1; /**< 이 시스템이 Media Foundation 을 초기화했는지 여부입니다. */
        [[maybe_unused]] uint8                                            _reservedAudio   : 5; /**< 비트필드 패딩입니다. */

        /** @brief 아직 아무 장치도 잡지 않은 상태로 둡니다. */
        XAudio2SystemImpl()
            : _pXAudio{ nullptr }
            , _pMasterVoice{ nullptr }
            , _pMusicVoice{ nullptr }
            , _pMusicClip{ nullptr }
            , _listActiveVoice{}
            , _listIdleVoice{}
            , _mapClipCache{}
            , _musicPath{}
            , _musicGeneration{ 0 }
            , _bInitialized{ SW_FALSE }
            , _bComInitialized{ SW_FALSE }
            , _bMfInitialized{ SW_FALSE }
            , _reservedAudio{ 0 } {}

        /** @brief 같은 보이스로 재생할 수 있는 포맷인지 봅니다. */
        static bool isFormatEqual( const WAVEFORMATEXTENSIBLE& lhs, const WAVEFORMATEXTENSIBLE& rhs )
        {
            const bool bSameBase = lhs.Format.wFormatTag == rhs.Format.wFormatTag && lhs.Format.nChannels == rhs.Format.nChannels &&
                                   lhs.Format.nSamplesPerSec == rhs.Format.nSamplesPerSec && lhs.Format.wBitsPerSample == rhs.Format.wBitsPerSample;
            if ( bSameBase == false || lhs.Format.wFormatTag != WAVE_FORMAT_EXTENSIBLE )
                return bSameBase;
            return lhs.dwChannelMask == rhs.dwChannelMask && Memory::compare( &lhs.SubFormat, &rhs.SubFormat, sizeof( lhs.SubFormat ) ) == 0;
        }

        /** @brief 캐시에 있으면 그것을, 없으면 디코드해 캐시에 넣고 반환합니다. */
        shared_ptr<XAudio2SystemInternal::PcmClip> getOrLoadClip( string_view path )
        {
            const string key( path );
            {
                std::scoped_lock<mutex> lock{ _clipCacheMutex };
                auto                    it = _mapClipCache.find( key );
                if ( it != _mapClipCache.end() )
                    return it->second;
            }

            XAudio2SystemInternal::PcmClip clip{};
            if ( XAudio2SystemInternal::loadClip( path, clip ) == false )
                return nullptr;

            auto pShared = make_shared<XAudio2SystemInternal::PcmClip>( std::move( clip ) );
            {
                std::scoped_lock<mutex> lock{ _clipCacheMutex };
                _mapClipCache[key] = pShared;
            }
            return pShared;
        }
    };

    XAudio2System::XAudio2System()
        : _impl{ make_unique<XAudio2SystemImpl>() }
    {
    }

    XAudio2System::~XAudio2System()
    {
        XAudio2System::shutdown();
    }

    bool XAudio2System::initialize()
    {
        if ( _impl == nullptr )
            _impl = make_unique<XAudio2SystemImpl>();
        if ( _impl->_bInitialized == SW_TRUE )
            return true;

        HRESULT hr = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
        if ( hr == S_OK || hr == S_FALSE )
            _impl->_bComInitialized = SW_TRUE;
        else if ( hr == RPC_E_CHANGED_MODE )
            _impl->_bComInitialized = SW_FALSE;
        else
            SW_LOG_WARNING( "CoInitializeEx failed (0x%#).", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ) );

        const HRESULT mfHr = MFStartup( MF_VERSION );
        if ( SUCCEEDED( mfHr ) )
            _impl->_bMfInitialized = SW_TRUE;
        else
            SW_LOG_WARNING( "MFStartup failed (0x%#).", Fmt( static_cast<uint32>( mfHr ), Format( 8, Format::Padding::Zero ).hex() ) );

        IXAudio2* pXAudio{ nullptr };
        hr = XAudio2Create( &pXAudio, 0, XAUDIO2_DEFAULT_PROCESSOR );
        if ( FAILED( hr ) || pXAudio == nullptr )
        {
            SW_LOG_WARNING( "XAudio2Create failed (0x%#). Running null audio.", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ) );
            _impl->_bInitialized = SW_TRUE;
            return true;
        }

        IXAudio2MasteringVoice* pMasterVoice{ nullptr };
        hr = pXAudio->CreateMasteringVoice( &pMasterVoice );
        if ( FAILED( hr ) || pMasterVoice == nullptr )
        {
            SW_LOG_WARNING( "CreateMasteringVoice failed (0x%#).", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ) );
            pXAudio->Release();
            _impl->_bInitialized = SW_TRUE;
            return true;
        }

        _impl->_pXAudio      = pXAudio;
        _impl->_pMasterVoice = pMasterVoice;
        _impl->_pMasterVoice->SetVolume( getEffectiveMasterVolume() );
        SW_LOG_INFO( "XAudio2 mastering voice ready." );

        _impl->_bInitialized = SW_TRUE;
        return true;
    }

    /**
     * @brief 오디오 시스템을 내리고 모든 사운드 보이스와 XAudio2 마스터링 보이스를 파괴합니다.
     */
    void XAudio2System::shutdown()
    {
        if ( _impl == nullptr || _impl->_bInitialized == SW_FALSE )
            return;
        // 소멸자가 `XAudio2System::shutdown()` 을 한정해서 부르므로 파괴 중 가상 디스패치는 없다.
        // 여기서 `stopMusic()` 까지 한정하면 **정상 종료 경로**에서 파생 재정의가 무시되므로 두지 않는다.
        // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
        stopMusic(); // 자기 안에서 `_voiceMutex` 를 잡는다. 여기서 들고 있으면 안 된다.

        {
            // **잠그고 부순다.** 오디오는 `EngineLoop::shutdown` 에서 **TaskManager 보다 먼저**
            // 내려가므로, 바로 이 순간에도 워커가 `playDecodedClipTask` 안에서 `_listActiveVoice`
            // 에 `push_back` 하고 있을 수 있다. 잠금 없이 훑으면 그 push_back 이 벡터를 재할당할 때 아래 루프의 참조가
            // **해제된 메모리**를 가리키고, 뒤늦게 잠금을 얻은 워커는 이미 `Release()` 한 `_pXAudio` 로 보이스를 만든다.
            // `_bInitialized` 를 잠금 안에서 **먼저** 내려, 기다리던 워커가 그것을 보고 돌아가게 한다.
            std::scoped_lock<mutex> lock{ _impl->_voiceMutex };
            _impl->_bInitialized = SW_FALSE;

            for ( XAudio2SystemInternal::VoiceBuffer& voiceBuffer : _impl->_listActiveVoice )
            {
                if ( voiceBuffer._pVoice != nullptr )
                {
                    voiceBuffer._pVoice->Stop( 0 );
                    voiceBuffer._pVoice->DestroyVoice();
                }
            }
            _impl->_listActiveVoice.clear();
            for ( XAudio2SystemInternal::VoiceBuffer& idleBuffer : _impl->_listIdleVoice )
            {
                if ( idleBuffer._pVoice != nullptr )
                {
                    idleBuffer._pVoice->Stop( 0 );
                    idleBuffer._pVoice->DestroyVoice();
                }
            }
            _impl->_listIdleVoice.clear();
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
        }

        if ( _impl->_bMfInitialized != SW_FALSE )
        {
            MFShutdown();
            _impl->_bMfInitialized = SW_FALSE;
        }
        if ( _impl->_bComInitialized != SW_FALSE )
        {
            CoUninitialize();
            _impl->_bComInitialized = SW_FALSE;
        }
        {
            std::scoped_lock<mutex> lock{ _impl->_clipCacheMutex };
            _impl->_mapClipCache.clear();
        }
        SW_LOG_INFO( "Shut down." );
    }

    bool XAudio2System::isInitialized() const
    {
        return _impl != nullptr && _impl->_bInitialized == SW_TRUE;
    }

    string XAudio2System::getMusicPath() const
    {
        if ( _impl == nullptr )
            return {};
        std::scoped_lock<mutex> lock{ _impl->_voiceMutex };
        return _impl->_musicPath;
    }

    /**
     * @brief 매 프레임 재생이 끝난(큐 버퍼가 0 이 된) 원샷 보이스를 찾아 유휴 풀에 두거나 파괴합니다.
     */
    void XAudio2System::update( float32 )
    {
        SW_MEMORY_SCOPE( Audio );
        if ( _impl == nullptr )
            return;
        std::scoped_lock<mutex>                     lock{ _impl->_voiceMutex };
        vector<XAudio2SystemInternal::VoiceBuffer>& voices = _impl->_listActiveVoice;
        for ( size_t voiceIndex = 0; voiceIndex < voices.size(); )
        {
            XAudio2SystemInternal::VoiceBuffer& voiceBuffer = voices[voiceIndex];
            if ( voiceBuffer._pVoice == nullptr )
            {
                if ( voiceIndex + 1 < voices.size() )
                    voiceBuffer = std::move( voices.back() );
                voices.pop_back();
                continue;
            }
            XAUDIO2_VOICE_STATE state{};
            voiceBuffer._pVoice->GetState( &state );
            if ( state.BuffersQueued == 0 )
            {
                voiceBuffer._pVoice->Stop( 0 );
                voiceBuffer._pVoice->FlushSourceBuffers();
                if ( _impl->_listIdleVoice.size() < XAudio2SystemImpl::kMaxIdleVoices )
                    _impl->_listIdleVoice.push_back( std::move( voiceBuffer ) );
                else
                    voiceBuffer._pVoice->DestroyVoice();
                VectorUtil::removeAtSwap( voices, voiceIndex );
                continue;
            }
            ++voiceIndex;
        }
    }

    /**
     * @brief 효과음(SFX)을 한 번 재생합니다. 디코드와 재생은 워커 태스크에서 비동기로 합니다.
     */
    bool XAudio2System::play( string_view path )
    {
        return playInternal( path, false );
    }

    bool XAudio2System::preload( string_view path )
    {
        SW_MEMORY_SCOPE( Audio );
        if ( path.empty() || _impl == nullptr )
            return false;
        return _impl->getOrLoadClip( path ) != nullptr;
    }

    /**
     * @brief 배경음악(BGM)을 루프로 재생합니다. 재생 중인 BGM 이 있으면 교체합니다.
     */
    bool XAudio2System::playMusic( string_view path )
    {
        if ( path.empty() || _impl == nullptr )
            return false;

        {
            std::scoped_lock<mutex> lock{ _impl->_voiceMutex };
            if ( _impl->_musicPath == path && _impl->_pMusicVoice != nullptr )
                return true;
        }

        // 있는 곡인지 **멈추기 전에** 본다 — stopMusic() 을 먼저 부르면 없는 곡을 요청했을 때 틀어져 있던 BGM 만 꺼진다.
        if ( ResourceUtil::hasResource( path ) == false )
        {
            SW_LOG_WARNING( "Audio resource not found: %#", path );
            return false;
        }

        stopMusic();
        return playInternal( path, true );
    }

    /**
     * @brief 재생 중인 배경음악(BGM)을 멈추고 보이스와 PCM 을 놓습니다.
     */
    void XAudio2System::stopMusic()
    {
        if ( _impl == nullptr )
            return;
        std::scoped_lock<mutex> lock{ _impl->_voiceMutex };
        if ( _impl->_pMusicVoice != nullptr )
        {
            _impl->_pMusicVoice->Stop( 0 );
            _impl->_pMusicVoice->DestroyVoice();
            _impl->_pMusicVoice = nullptr;
        }
        _impl->_pMusicClip.reset();
        _impl->_musicPath.clear();
        // 아직 디코드 중인 요청이 뒤늦게 도착해 멈춘 음악을 되살리지 못하게 세대를 올린다.
        ++_impl->_musicGeneration;
    }

    void XAudio2System::pauseMusic()
    {
        if ( _impl == nullptr )
            return;
        std::scoped_lock<mutex> lock{ _impl->_voiceMutex };
        if ( _impl->_pMusicVoice != nullptr )
            _impl->_pMusicVoice->Stop( 0 );
    }

    void XAudio2System::resumeMusic()
    {
        if ( _impl == nullptr )
            return;
        std::scoped_lock<mutex> lock{ _impl->_voiceMutex };
        if ( _impl->_pMusicVoice != nullptr )
            _impl->_pMusicVoice->Start( 0 );
    }

    void XAudio2System::applyVolume()
    {
        if ( _impl == nullptr )
            return;

        // 음소거는 **마스터 보이스 한 자리에만** 건다 — 음악 · 효과음 보이스에도 걸면 "음소거 → 볼륨 조절 → 음소거 해제" 뒤
        // 그 보이스들이 0 인 채로 남는다.
        if ( _impl->_pMasterVoice != nullptr )
            _impl->_pMasterVoice->SetVolume( getEffectiveMasterVolume() );

        std::scoped_lock<mutex> lock{ _impl->_voiceMutex };
        if ( _impl->_pMusicVoice != nullptr )
            _impl->_pMusicVoice->SetVolume( getMusicVolume() );

        const float32 sfxVolume = getSfxVolume();
        for ( XAudio2SystemInternal::VoiceBuffer& voiceBuffer : _impl->_listActiveVoice )
        {
            if ( voiceBuffer._pVoice != nullptr )
                voiceBuffer._pVoice->SetVolume( sfxVolume );
        }
    }

    void XAudio2System::playDecodedClipTask( const TaskArgs& args )
    {
        if ( _impl == nullptr || _impl->_pXAudio == nullptr )
            return;

        const string requestedPath   = args.get<string>( 0 );
        const bool   bLoop           = args.get<bool>( 1 );
        const uint64 musicGeneration = args.get<uint64>( 2 );

        shared_ptr<XAudio2SystemInternal::PcmClip> pClip = _impl->getOrLoadClip( requestedPath );
        if ( pClip == nullptr || pClip->_listData.empty() )
        {
            SW_LOG_WARNING( "Failed to decode: %#", requestedPath );
            return;
        }

        std::scoped_lock<mutex> lock{ _impl->_voiceMutex };

        // 이 함수 첫 줄의 `_pXAudio` 검사는 **잠금 밖**이라 못 믿는다. 디코드(느리다)와 잠금 대기
        // 사이에 `shutdown()` 이 끝났을 수 있고, 그러면 `_pXAudio` 는 이미 `Release()` 된 것이다.
        // 잠금 안에서 다시 본다.
        if ( _impl->_bInitialized == SW_FALSE || _impl->_pXAudio == nullptr )
            return;

        // 요청 번호로 거른다. 경로로 거르면 A → B → A 처럼 같은 곡으로 돌아올 때 늦게 온 첫 A 도 통과해
        // 음악 보이스가 둘이 된다(앞의 것은 멈추지도 않는다).
        if ( bLoop && _impl->_musicGeneration != musicGeneration )
            return;

        IXAudio2SourceVoice* pVoice{ nullptr };
        if ( bLoop == false )
        {
            for ( size_t idx = 0; idx < _impl->_listIdleVoice.size(); ++idx )
            {
                if ( _impl->_listIdleVoice[idx]._pClip != nullptr &&
                     XAudio2SystemImpl::isFormatEqual( _impl->_listIdleVoice[idx]._pClip->_format, pClip->_format ) )
                {
                    pVoice = _impl->_listIdleVoice[idx]._pVoice;
                    VectorUtil::removeAtSwap( _impl->_listIdleVoice, idx );
                    break;
                }
            }
        }

        HRESULT hr = S_OK;
        if ( pVoice == nullptr )
        {
            hr = _impl->_pXAudio->CreateSourceVoice( &pVoice, &pClip->_format.Format );
            if ( FAILED( hr ) || pVoice == nullptr )
            {
                SW_LOG_WARNING( "CreateSourceVoice failed (0x%#)", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ) );
                return;
            }
        }

        XAUDIO2_BUFFER audioBuffer{};
        audioBuffer.AudioBytes = static_cast<UINT32>( pClip->_listData.size() );
        audioBuffer.pAudioData = std::as_const( pClip->_listData ).data();
        if ( bLoop )
        {
            audioBuffer.LoopCount = XAUDIO2_LOOP_INFINITE;
            _impl->_pMusicClip    = pClip;
            _impl->_pMusicVoice   = pVoice;
            // 음소거는 마스터 보이스가 맡는다. 여기서는 음악 볼륨만 건다.
            pVoice->SetVolume( getMusicVolume() );
        }
        else
        {
            audioBuffer.Flags = XAUDIO2_END_OF_STREAM;
            _impl->_listActiveVoice.push_back( XAudio2SystemInternal::VoiceBuffer{} );
            XAudio2SystemInternal::VoiceBuffer& slot = _impl->_listActiveVoice.back();
            slot._pClip                              = pClip;
            slot._pVoice                             = pVoice;
            pVoice->SetVolume( getSfxVolume() );
        }

        hr = pVoice->SubmitSourceBuffer( &audioBuffer );
        if ( FAILED( hr ) )
        {
            SW_LOG_WARNING( "SubmitSourceBuffer failed (0x%#)", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ) );
            if ( bLoop )
            {
                _impl->_pMusicVoice->DestroyVoice();
                _impl->_pMusicVoice = nullptr;
                _impl->_pMusicClip.reset();
                _impl->_musicPath.clear();
                ++_impl->_musicGeneration;
            }
            return;
        }
        pVoice->Start( 0 );
        SW_LOG_TRACE( "Playing %# (%# loop=%#)", requestedPath, static_cast<uint32>( audioBuffer.AudioBytes ),
                      bLoop ? 1 : 0 );
    }

    /**
     * @brief 경로를 확인하고 디코드 · 재생을 워커 태스크(`playDecodedClipTask`)로 넘깁니다. 루프면 음악 경로와 요청 번호를 먼저 적습니다.
     */
    bool XAudio2System::playInternal( string_view path, bool bLoop )
    {
        SW_MEMORY_SCOPE( Audio );
        if ( _impl == nullptr || _impl->_bInitialized == SW_FALSE )
            return false;

        if ( path.empty() )
            return false;

        if ( ResourceUtil::hasResource( path ) == false )
        {
            SW_LOG_WARNING( "Audio resource not found: %#", path );
            return false;
        }

        const string requestedPath = string( path );

        // **제출보다 먼저**(잠금 안에서) 기록한다. 클립이 캐시에 있으면 워커가 먼저 도착하므로, 제출 뒤에 적으면 워커가
        // "요청한 곡이 아니다" 로 판단하고 조용히 돌아가 BGM 이 아무 말 없이 시작되지 않는다.
        uint64 musicGeneration = 0;
        if ( bLoop )
        {
            std::scoped_lock<mutex> lock{ _impl->_voiceMutex };
            _impl->_musicPath = requestedPath;
            musicGeneration   = ++_impl->_musicGeneration;
        }

        if ( _impl->_pXAudio == nullptr )
        {
            SW_LOG_TRACE( "play (null audio fallback): %#", path );
            return true;
        }

        engine::getTaskManager()
            .emplaceTask(
                "XAudio2Play",
                SW_DELEGATE_METHOD( TaskArgsDelegate, &XAudio2System::playDecodedClipTask, this ),
                MakeTaskArgs( requestedPath, bLoop, musicGeneration ) )
            .submit();

        return true;
    }
} // namespace sw
#endif
