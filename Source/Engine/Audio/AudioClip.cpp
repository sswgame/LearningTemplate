#include "pch.h"

#include "Engine/Audio/AudioClip.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Audio/AudioChannelLayout.h"
#include "Engine/Audio/AudioClipDecoder.h"
#include "Engine/Audio/AudioDownmix.h"
#include "Engine/Audio/AudioStream.h"
#include "Engine/Audio/AudioTypes.h"
#include "Engine/Audio/Graph/AudioGraph.h"
#include "Engine/Audio/Graph/AudioGraphDef.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"

namespace sw
{
    SW_LOG_CALLER( "AudioClipCache" );

    SW_ASSET_TYPE( Audio, AudioClipData::kAssetType );

    namespace
    {
        struct AudioClipInternal
        {
            /** @brief 리틀 엔디언 정수 샘플 하나를 [-1, 1] float 로 읽습니다. */
            static float32 readIntegerSample( const uint8* pSample, uint32 bytesPerSample )
            {
                switch ( bytesPerSample )
                {
                    case 1:
                    {
                        return ( static_cast<float32>( pSample[0] ) - 128.0f ) / 128.0f;
                    }
                    case 2:
                    {
                        const int16 value = static_cast<int16>( static_cast<uint16>( pSample[0] ) | ( static_cast<uint16>( pSample[1] ) << 8 ) );
                        return static_cast<float32>( value ) / 32768.0f;
                    }
                    case 3:
                    {
                        // 24 비트를 32 비트 위쪽에 올려 부호를 살린다.
                        const int32 value = static_cast<int32>( ( static_cast<uint32>( pSample[0] ) << 8 ) | ( static_cast<uint32>( pSample[1] ) << 16 ) |
                                                                ( static_cast<uint32>( pSample[2] ) << 24 ) );
                        return static_cast<float32>( value ) / 2147483648.0f;
                    }
                    case 4:
                    {
                        const int32 value = static_cast<int32>( static_cast<uint32>( pSample[0] ) | ( static_cast<uint32>( pSample[1] ) << 8 ) |
                                                                ( static_cast<uint32>( pSample[2] ) << 16 ) | ( static_cast<uint32>( pSample[3] ) << 24 ) );
                        return static_cast<float32>( value ) / 2147483648.0f;
                    }
                    default:
                    {
                        return 0.0f;
                    }
                }
            }

            /** @brief 리틀 엔디언 float32 샘플 하나를 읽습니다. */
            static float32 readFloatSample( const uint8* pSample )
            {
                float32 value = 0.0f;
                Memory::copy( &value, pSample, sizeof( value ) );
                return value;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AudioClipData::convertPcm( const AudioPcm& pcm, AudioClipData& outClip )
    {
        const uint32 bytesPerSample        = pcm._bitsPerSample / 8u;
        const bool   bUnsupportedPcmLayout = pcm._channelCount == 0 || pcm._sampleRate == 0 || bytesPerSample == 0 || bytesPerSample > 4;
        if ( bUnsupportedPcmLayout )
            return false;
        if ( pcm._bFloat == SW_TRUE && bytesPerSample != 4 )
            return false;

        const uint32 sourceChannelCount = pcm._channelCount;
        const uint32 frameBytes         = sourceChannelCount * bytesPerSample;
        const uint32 frameCount         = static_cast<uint32>( pcm._listData.size() / frameBytes );
        const uint32 targetChannelCount = sourceChannelCount >= audio::kStereoChannelCount ? audio::kStereoChannelCount : 1u;

        // 3 채널 이상은 채널 마스크의 배치로 명시 다운믹스한다(센터 대사와 서라운드가 사라지지 않는다). 배치를 모르면 오류다.
        const bool   bDownmix = sourceChannelCount > audio::kStereoChannelCount;
        AudioDownmix downmix;
        if ( bDownmix && AudioDownmix::makeForWaveMask( sourceChannelCount, pcm._channelMask, downmix ) == false )
            return false;

        AudioClipData clip;
        clip._sampleRate   = pcm._sampleRate;
        clip._channelCount = static_cast<uint16>( targetChannelCount );
        clip._frameCount   = frameCount;
        clip._listSample.resize( static_cast<size_t>( frameCount ) * targetChannelCount );

        float32      arrSourceFrame[AudioDownmix::kMaxSourceChannelCount]{};
        const uint8* pFrame = pcm._listData.data();
        for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            const uint32 readChannelCount = bDownmix ? sourceChannelCount : targetChannelCount;
            for ( uint32 channelIndex = 0; channelIndex < readChannelCount; ++channelIndex )
            {
                const uint8* pSample         = pFrame + static_cast<size_t>( channelIndex ) * bytesPerSample;
                arrSourceFrame[channelIndex] = pcm._bFloat == SW_TRUE ? AudioClipInternal::readFloatSample( pSample ) : AudioClipInternal::readIntegerSample( pSample, bytesPerSample );
            }
            float32* pTarget = clip._listSample.data() + static_cast<size_t>( frameIndex ) * targetChannelCount;
            if ( bDownmix )
            {
                downmix.apply( arrSourceFrame, pTarget[0], pTarget[1] );
            }
            else
            {
                for ( uint32 channelIndex = 0; channelIndex < targetChannelCount; ++channelIndex )
                {
                    pTarget[channelIndex] = arrSourceFrame[channelIndex];
                }
            }
            pFrame += frameBytes;
        }
        outClip = std::move( clip );
        return true;
    }

    void AudioClipData::copyMonoSamples( vector<float32>& outListSample ) const
    {
        outListSample.assign( _frameCount, 0.0f );
        if ( _channelCount == 0 || _listSample.empty() )
            return;
        const float32 scale = 1.0f / static_cast<float32>( _channelCount );
        for ( uint32 frameIndex = 0; frameIndex < _frameCount; ++frameIndex )
        {
            float32 sum = 0.0f;
            for ( uint32 channelIndex = 0; channelIndex < _channelCount; ++channelIndex )
            {
                sum += _listSample[static_cast<size_t>( frameIndex ) * _channelCount + channelIndex];
            }
            outListSample[frameIndex] = sum * scale;
        }
    }

    AudioClipCache::AudioClipCache()
        : _mapPathToEntry{}
        , _pFallbackDecoder{ nullptr }
        , _streamMinSeconds{ kDefaultStreamMinSeconds }
        , _mutex{}
        , _pendingDecodeCount{ 0 }
    {
    }

    AudioClipCache::~AudioClipCache()
    {
        // 띄운 디코드 태스크가 끝나기 전에 지워지면 태스크가 해제된 맵 · 뮤텍스에 쓴다. 태스크는 엔진 서비스가 묶여 있을 때만 띄우므로 같은 태스크 매니저로 기다린다.
        if ( _pendingDecodeCount.load( std::memory_order_acquire ) == 0 )
            return;
        if ( engine::areEngineServicesBound() == false )
        {
            SW_LOG_ERROR( "AudioClipCache destroyed with %# decode task(s) in flight and no task manager to wait on", _pendingDecodeCount.load() );
            return;
        }
        constexpr uint32 kWaitSliceMs = 10;
        while ( _pendingDecodeCount.load( std::memory_order_acquire ) > 0 )
        {
            (void)engine::getSubsystem<TaskManager>().waitAll( kWaitSliceMs ); // 기다리는 동안 그 태스크를 이 스레드가 돕기도 한다
        }
    }

    shared_ptr<const AudioClipData> AudioClipCache::findClip( const hashed_string& path ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              it = _mapPathToEntry.find( path );
        if ( it == _mapPathToEntry.end() || it->second._status != AssetLoadState::Ready )
            return nullptr;
        return it->second._pClip;
    }

    std::optional<AssetLoadState> AudioClipCache::findLoadState( const hashed_string& path ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              it = _mapPathToEntry.find( path );
        if ( it == _mapPathToEntry.end() )
            return std::nullopt;
        return it->second._status;
    }

    shared_ptr<const AudioClipData> AudioClipCache::loadClip( const hashed_string& path )
    {
        {
            std::scoped_lock<mutex> lock{ _mutex };
            const auto              it = _mapPathToEntry.find( path );
            if ( it != _mapPathToEntry.end() && it->second._status == AssetLoadState::Ready )
                return it->second._pClip;
        }
        shared_ptr<const AudioClipData> pClip = decodeClip( path );
        storeResult( path, pClip );
        return pClip;
    }

    void AudioClipCache::requestClip( const hashed_string& path )
    {
        if ( path.empty() )
            return;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            // 새 엔트리는 `Loading` 으로 생긴다 — 있던 엔트리가 준비됐거나 디코드 중이면 다시 띄우지 않는다(실패한 것만 다시 시도한다).
            const auto it = _mapPathToEntry.find( path );
            if ( it != _mapPathToEntry.end() && it->second._status != AssetLoadState::Failed )
                return;
            _mapPathToEntry[path]._status = AssetLoadState::Loading;
        }

        // 이 호스트(전용 서버)의 패키지에 없는 종류다 — 디코드 작업을 띄우지 않고 없는 것으로 친다(경고 없음, ResourcePaths::setHostTarget).
        if ( ResourcePaths::isExcludedForHost( path.view() ) )
        {
            storeResult( path, nullptr );
            return;
        }
        if ( engine::areEngineServicesBound() == false )
        {
            storeResult( path, decodeClip( path ) );
            return;
        }
        _pendingDecodeCount.fetch_add( 1, std::memory_order_acq_rel );
        engine::getSubsystem<TaskManager>()
            .emplaceTask( "AudioClipDecode", SW_DELEGATE_METHOD( TaskArgsDelegate, &AudioClipCache::decodeClipTask, this ),
                          MakeTaskArgs( string( path.view() ) ) )
            .submit();
    }

    bool AudioClipCache::findReadyPath( string_view path, hashed_string& outPath ) const
    {
        const string            wanted = FileUtil::normalizeSeparators( path );
        std::scoped_lock<mutex> lock{ _mutex };
        for ( const auto& entry : _mapPathToEntry )
        {
            const bool bSamePath = entry.second._status == AssetLoadState::Ready && StringUtil::equals( FileUtil::normalizeSeparators( entry.first.view() ), wanted, true );
            if ( bSamePath )
            {
                outPath = entry.first;
                return true;
            }
        }
        return false;
    }

    bool AudioClipCache::reloadClip( const hashed_string& path )
    {
        shared_ptr<const AudioClipData> pClip = decodeClip( path );
        if ( pClip == nullptr )
        {
            SW_LOG_ERROR( "Reload failed, keeping the previous sound: %#", path.c_str() );
            return false;
        }
        storeResult( path, std::move( pClip ) );
        return true;
    }

    void AudioClipCache::addClip( const hashed_string& path, shared_ptr<const AudioClipData> pClip )
    {
        storeResult( path, std::move( pClip ) );
    }

    void AudioClipCache::clear()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _mapPathToEntry.clear();
    }

    shared_ptr<const AudioClipData> AudioClipCache::decodeClip( const hashed_string& path ) const
    {
        const string_view pathView = path.view();
        if ( ResourcePaths::isExcludedForHost( pathView ) )
            return nullptr; // 이 호스트의 패키지에 없는 종류 — 없는 것으로 친다(위 requestClip 과 같다)
        if ( StringUtil::endsWith( pathView, AudioGraphDef::kExtension, true ) )
            return decodeGraph( path );
        AudioPcm pcm;
        bool     bDecoded = false;
        if ( AudioClipDecoder::isSupportedExtension( pathView ) )
        {
            vector<uint8> bytes;
            const bool    bRead = ResourceIO::readBinaryResource( pathView, bytes ); // 낱개 파일 허용 정책은 ResourceIO 한 곳이 본다
            if ( bRead )
            {
                // 긴 소리는 풀지 않고 압축 바이트로 스트리밍한다(음수 기준이면 끈다). 짧거나 스트리밍할 수 없는 형식이면 바이트가 그대로 남아 아래에서 통째로 푼다.
                if ( 0.0f <= _streamMinSeconds )
                {
                    shared_ptr<const AudioStreamSource> pStream = AudioStreamSource::tryCreate( pathView, bytes, _streamMinSeconds );
                    if ( pStream != nullptr )
                    {
                        shared_ptr<AudioClipData> pClip = make_shared<AudioClipData>();
                        pClip->_pStream                 = pStream;
                        pClip->_frameCount              = pStream->getInfo()._frameCount;
                        pClip->_sampleRate              = pStream->getInfo()._sampleRate;
                        pClip->_channelCount            = pStream->getPlaybackChannelCount();
                        return pClip;
                    }
                }
                bDecoded = AudioClipDecoder::decode( pathView, bytes.data(), bytes.size(), pcm );
            }
        }
        // 공통 디코더가 못 읽는 WAV(ADPCM 등)와 다른 형식(MP3 등)은 백엔드의 대체 디코더에 넘긴다. OGG 는 넘기지 않는다(Media Foundation 은 Vorbis 를 모른다).
        const bool bUseFallbackDecoder = bDecoded == false && _pFallbackDecoder != nullptr && FileUtil::hasExtension( pathView, ".ogg" ) == false;
        if ( bUseFallbackDecoder )
            bDecoded = _pFallbackDecoder( pathView, pcm );
        if ( bDecoded == false )
        {
            SW_LOG_WARNING( "Failed to decode: %#", pathView );
            return nullptr;
        }

        shared_ptr<AudioClipData> pClip = make_shared<AudioClipData>();
        if ( AudioClipData::convertPcm( pcm, *pClip ) == false || pClip->_frameCount == 0 )
        {
            SW_LOG_WARNING( "Unsupported sample format: %#", pathView );
            return nullptr;
        }
        return pClip;
    }

    shared_ptr<const AudioClipData> AudioClipCache::decodeGraph( const hashed_string& path ) const
    {
        const string_view pathView = path.view();
        AudioGraphDef     def;
        if ( def.loadFromResource( pathView ) == false )
            return nullptr; // 읽기 · 검사가 오류를 남겼다
        // 그래뉼러 소스는 같은 캐시 규칙으로 푼다(이 작업 안에서 바로). 그래프 안의 그래프와 스트리밍 클립은 AudioGraph::create 가 오류로 멈춘다.
        vector<shared_ptr<const AudioClipData>> listNodeClip( def._listNode.size() );
        for ( size_t nodeIndex = 0; nodeIndex < def._listNode.size(); ++nodeIndex )
        {
            const AudioGraphNodeDesc& node = def._listNode[nodeIndex];
            if ( node._clip.isSet() == false )
                continue;
            const string_view clipPath = node._clip.get();
            if ( StringUtil::endsWith( clipPath, AudioGraphDef::kExtension, true ) )
            {
                SW_LOG_ERROR( "%#: node '%#' uses another audio graph '%#' as its clip", pathView, node._name.c_str(), clipPath );
                return nullptr;
            }
            listNodeClip[nodeIndex] = decodeClip( hashed_string( clipPath ) );
        }
        shared_ptr<const AudioGraph> pGraph = AudioGraph::create( def, listNodeClip, pathView );
        if ( pGraph == nullptr )
            return nullptr;
        shared_ptr<AudioClipData> pClip = make_shared<AudioClipData>();
        pClip->_frameCount              = pGraph->getFrameCount();
        pClip->_sampleRate              = audio::kSampleRate;
        pClip->_channelCount            = 1;
        pClip->_pGraph                  = std::move( pGraph );
        return pClip;
    }

    void AudioClipCache::decodeClipTask( const TaskArgs& args )
    {
        const hashed_string path( string_view( args.get<string>( 0 ) ) );
        storeResult( path, decodeClip( path ) );
        // 마지막으로 이 캐시를 건드리는 줄이다 — 0 이 되는 순간 소멸자가 진행할 수 있다.
        _pendingDecodeCount.fetch_sub( 1, std::memory_order_acq_rel );
    }

    void AudioClipCache::storeResult( const hashed_string& path, shared_ptr<const AudioClipData> pClip )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        Entry&                  entry = _mapPathToEntry[path];
        entry._status                 = pClip != nullptr ? AssetLoadState::Ready : AssetLoadState::Failed;
        entry._pClip                  = std::move( pClip );
    }
} // namespace sw
