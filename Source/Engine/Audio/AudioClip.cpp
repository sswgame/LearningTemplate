#include "pch.h"

#include "Engine/Audio/AudioClip.h"

#include "Core/File/FileUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Audio/AudioClipDecoder.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "AudioClipStore" );

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
        const uint32 bytesPerSample = pcm._bitsPerSample / 8u;
        if ( pcm._channelCount == 0 || pcm._sampleRate == 0 || bytesPerSample == 0 || bytesPerSample > 4 )
            return false;
        if ( pcm._bFloat == SW_TRUE && bytesPerSample != 4 )
            return false;

        const uint32 sourceChannelCount = pcm._channelCount;
        const uint32 frameBytes         = sourceChannelCount * bytesPerSample;
        const uint32 frameCount         = static_cast<uint32>( pcm._listData.size() / frameBytes );
        const uint32 targetChannelCount = sourceChannelCount >= 2 ? 2u : 1u;

        AudioClipData clip;
        clip._sampleRate   = pcm._sampleRate;
        clip._channelCount = static_cast<uint16>( targetChannelCount );
        clip._frameCount   = frameCount;
        clip._listSample.resize( static_cast<size_t>( frameCount ) * targetChannelCount );

        const uint8* pFrame = pcm._listData.data();
        for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            for ( uint32 channelIndex = 0; channelIndex < targetChannelCount; ++channelIndex )
            {
                const uint8*  pSample                                                                   = pFrame + static_cast<size_t>( channelIndex ) * bytesPerSample;
                const float32 value                                                                     = pcm._bFloat == SW_TRUE ? AudioClipInternal::readFloatSample( pSample )
                                                                                                                                 : AudioClipInternal::readIntegerSample( pSample, bytesPerSample );
                clip._listSample[static_cast<size_t>( frameIndex ) * targetChannelCount + channelIndex] = value;
            }
            pFrame += frameBytes;
        }
        outClip = std::move( clip );
        return true;
    }

    void AudioClipData::copyMonoSamples( vector<float32>& outListSample ) const
    {
        outListSample.assign( _frameCount, 0.0f );
        if ( _channelCount == 0 )
            return;
        const float32 scale = 1.0f / static_cast<float32>( _channelCount );
        for ( uint32 frameIndex = 0; frameIndex < _frameCount; ++frameIndex )
        {
            float32 sum = 0.0f;
            for ( uint32 channelIndex = 0; channelIndex < _channelCount; ++channelIndex )
                sum += _listSample[static_cast<size_t>( frameIndex ) * _channelCount + channelIndex];
            outListSample[frameIndex] = sum * scale;
        }
    }

    AudioClipStore::AudioClipStore()
        : _mapPathToEntry{}
        , _pFallbackDecoder{ nullptr }
        , _mutex{}
    {
    }

    AudioClipStore::~AudioClipStore() = default;

    shared_ptr<const AudioClipData> AudioClipStore::findClip( const hashed_string& path ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              it = _mapPathToEntry.find( path );
        if ( it == _mapPathToEntry.end() || it->second._status != AudioClipStatus::Ready )
            return nullptr;
        return it->second._pClip;
    }

    AudioClipStatus AudioClipStore::getStatus( const hashed_string& path ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              it = _mapPathToEntry.find( path );
        return it == _mapPathToEntry.end() ? AudioClipStatus::Missing : it->second._status;
    }

    shared_ptr<const AudioClipData> AudioClipStore::loadClip( const hashed_string& path )
    {
        {
            std::scoped_lock<mutex> lock{ _mutex };
            const auto              it = _mapPathToEntry.find( path );
            if ( it != _mapPathToEntry.end() && it->second._status == AudioClipStatus::Ready )
                return it->second._pClip;
        }
        shared_ptr<const AudioClipData> pClip = decodeClip( path );
        storeResult( path, pClip );
        return pClip;
    }

    void AudioClipStore::requestClip( const hashed_string& path )
    {
        if ( path.empty() )
            return;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            Entry&                  entry = _mapPathToEntry[path];
            if ( entry._status == AudioClipStatus::Ready || entry._status == AudioClipStatus::Loading )
                return;
            entry._status = AudioClipStatus::Loading;
        }

        if ( engine::areEngineServicesBound() == false )
        {
            storeResult( path, decodeClip( path ) );
            return;
        }
        engine::getTaskManager()
            .emplaceTask( "AudioClipDecode", SW_DELEGATE_METHOD( TaskArgsDelegate, &AudioClipStore::decodeClipTask, this ),
                          MakeTaskArgs( string( path.view() ) ) )
            .submit();
    }

    void AudioClipStore::addClip( const hashed_string& path, shared_ptr<const AudioClipData> pClip )
    {
        storeResult( path, std::move( pClip ) );
    }

    void AudioClipStore::clear()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _mapPathToEntry.clear();
    }

    shared_ptr<const AudioClipData> AudioClipStore::decodeClip( const hashed_string& path ) const
    {
        const string_view pathView = path.view();
        AudioPcm          pcm;
        bool              bDecoded = false;
        if ( AudioClipDecoder::isSupportedExtension( pathView ) )
        {
            vector<uint8> bytes;
            const bool    bRead = ResourceUtil::readBinaryResource( pathView, bytes ) ||
                               ( FileUtil::exists( pathView ) && FileUtil::readFile( pathView, bytes ) );
            if ( bRead )
                bDecoded = AudioClipDecoder::decode( pathView, bytes.data(), bytes.size(), pcm );
        }
        // 공통 디코더가 못 읽는 WAV(ADPCM 등)와 다른 형식(MP3 등)은 백엔드의 대체 디코더에 넘긴다. OGG 는 넘기지 않는다(Media Foundation 은 Vorbis 를 모른다).
        if ( bDecoded == false && _pFallbackDecoder != nullptr && FileUtil::hasExtension( pathView, ".ogg" ) == false )
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

    void AudioClipStore::decodeClipTask( const TaskArgs& args )
    {
        const hashed_string path( string_view( args.get<string>( 0 ) ) );
        storeResult( path, decodeClip( path ) );
    }

    void AudioClipStore::storeResult( const hashed_string& path, shared_ptr<const AudioClipData> pClip )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        Entry&                  entry = _mapPathToEntry[path];
        entry._status                 = pClip != nullptr ? AudioClipStatus::Ready : AudioClipStatus::Failed;
        entry._pClip                  = std::move( pClip );
    }
} // namespace sw
