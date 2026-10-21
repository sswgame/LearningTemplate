#include "pch.h"

#include "Engine/Audio/AudioStream.h"

#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Audio/AudioChannelLayout.h"
#include "Engine/Audio/AudioClipDecoder.h"

namespace sw
{
    namespace
    {
        /** @brief 압축 바이트가 디코더보다 오래 살도록 소스의 바이트 공유 포인터를 함께 쥐는 감싸개입니다. */
        class OwningStreamDecoder final : public IAudioStreamDecoder
        {
        public:
            OwningStreamDecoder( shared_ptr<const vector<uint8>> pBytes, unique_ptr<IAudioStreamDecoder> pInner )
                : _pBytes{ std::move( pBytes ) }
                , _pInner{ std::move( pInner ) }
            {
            }

            uint32 readFrames( float32* pOut, uint32 frameCount ) override { return _pInner->readFrames( pOut, frameCount ); }
            bool   seekToFrame( uint32 frame ) override { return _pInner->seekToFrame( frame ); }

        private:
            shared_ptr<const vector<uint8>> _pBytes;
            unique_ptr<IAudioStreamDecoder> _pInner;
        };

        /** @brief WAV 의 16 비트 정수나 32 비트 float 샘플을 바이트 안에서 바로 읽는 디코더입니다. */
        class WavStreamDecoder final : public IAudioStreamDecoder
        {
        public:
            WavStreamDecoder( shared_ptr<const vector<uint8>> pBytes, size_t dataOffset, uint32 frameCount, uint16 channelCount, bool bFloat )
                : _pBytes{ std::move( pBytes ) }
                , _dataOffset{ dataOffset }
                , _frameCount{ frameCount }
                , _position{ 0 }
                , _channelCount{ channelCount }
                , _bFloat{ bFloat }
            {
            }

            uint32 readFrames( float32* pOut, uint32 frameCount ) override
            {
                const uint32 count = MathUtil::min( frameCount, _frameCount - _position );
                if ( count == 0 )
                    return 0;
                const size_t bytesPerSample = _bFloat ? sizeof( float32 ) : sizeof( int16 );
                const size_t sampleOffset   = static_cast<size_t>( _position ) * _channelCount;
                const size_t sampleCount    = static_cast<size_t>( count ) * _channelCount;
                const uint8* pSource        = _pBytes->data() + _dataOffset + sampleOffset * bytesPerSample;
                if ( _bFloat )
                {
                    Memory::copy( pOut, pSource, sampleCount * sizeof( float32 ) );
                }
                else
                {
                    for ( size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex )
                    {
                        const int16 value = static_cast<int16>( static_cast<uint16>( pSource[sampleIndex * 2] ) | ( static_cast<uint16>( pSource[sampleIndex * 2 + 1] ) << 8 ) );
                        pOut[sampleIndex] = static_cast<float32>( value ) / 32768.0f;
                    }
                }
                _position += count;
                return count;
            }

            bool seekToFrame( uint32 frame ) override
            {
                if ( _frameCount < frame )
                    return false;
                _position = frame;
                return true;
            }

        private:
            shared_ptr<const vector<uint8>> _pBytes;
            size_t                          _dataOffset;
            uint32                          _frameCount;
            uint32                          _position;
            uint16                          _channelCount;
            bool                            _bFloat;
        };
    } // namespace
} // namespace sw

namespace sw
{
    AudioStreamWindow::AudioStreamWindow() = default;

    bool AudioStreamWindow::open( const AudioStreamSource& source )
    {
        close();
        unique_ptr<IAudioStreamDecoder> pDecoder = source.openDecoder();
        if ( pDecoder == nullptr )
            return false;
        _pDecoder     = shared_ptr<IAudioStreamDecoder>( std::move( pDecoder ) );
        _channelCount = source.getInfo()._channelCount;
        _downmix      = source.getDownmix();
        _chunkFrames  = kChunkFrames;
        _listWindow.reserve( static_cast<size_t>( _chunkFrames + kRetainFrames ) * _channelCount );
        return true;
    }

    bool AudioStreamWindow::openDecoder( unique_ptr<IAudioStreamDecoder> pDecoder, uint16 channelCount, uint32 chunkFrames )
    {
        close();
        const bool bValidDecoder = pDecoder != nullptr && 1 <= channelCount && channelCount <= audio::kStereoChannelCount && chunkFrames > 0;
        if ( bValidDecoder == false )
            return false;
        _pDecoder     = shared_ptr<IAudioStreamDecoder>( std::move( pDecoder ) );
        _channelCount = channelCount;
        _downmix      = AudioDownmix{};
        _chunkFrames  = chunkFrames;
        _listWindow.reserve( static_cast<size_t>( _chunkFrames + kRetainFrames ) * _channelCount );
        return true;
    }

    void AudioStreamWindow::close()
    {
        _pDecoder.reset();
        _listWindow.clear();
        _windowStart  = 0;
        _windowFrames = 0;
    }

    bool AudioStreamWindow::refill( uint32 retainFrames )
    {
        const uint32 retained = MathUtil::min( retainFrames, _windowFrames );
        if ( retained > 0 && retained < _windowFrames )
        {
            // 지나간 프레임을 버리고 마지막 몇 프레임만 앞으로 옮긴다.
            const uint32 dropped = _windowFrames - retained;
            Memory::move( _listWindow.data(), _listWindow.data() + static_cast<size_t>( dropped ) * _channelCount, static_cast<size_t>( retained ) * _channelCount * sizeof( float32 ) );
            _windowStart += dropped;
        }
        else if ( retained == 0 )
        {
            _windowFrames = 0;
        }
        _listWindow.resize( static_cast<size_t>( retained + _chunkFrames ) * _channelCount );
        const uint32 decoded = _pDecoder->readFrames( _listWindow.data() + static_cast<size_t>( retained ) * _channelCount, _chunkFrames );
        _windowFrames        = retained + decoded;
        _decodedFrameCount += decoded;
        return decoded > 0;
    }

    bool AudioStreamWindow::readFrame( uint32 frame, float32& outLeft, float32& outRight )
    {
        outLeft  = 0.0f;
        outRight = 0.0f;
        if ( _pDecoder == nullptr )
            return false;
        const bool bInWindow = _windowStart <= frame && frame - _windowStart < _windowFrames;
        if ( bInWindow == false )
        {
            const bool bAppend = _windowFrames > 0 && frame == _windowStart + _windowFrames;
            if ( bAppend )
            {
                if ( refill( kRetainFrames ) == false )
                    return false;
            }
            else
            {
                // 되감기: 창 밖(루프를 한 바퀴 돌았거나 위치를 옮겼다)이다.
                if ( _pDecoder->seekToFrame( frame ) == false )
                    return false;
                ++_seekCount;
                _windowStart  = frame;
                _windowFrames = 0;
                if ( refill( 0 ) == false )
                    return false;
            }
            if ( frame < _windowStart || _windowFrames <= frame - _windowStart )
                return false;
        }
        const float32* pFrame = _listWindow.data() + static_cast<size_t>( frame - _windowStart ) * _channelCount;
        if ( _downmix._channelCount > 0 )
        {
            _downmix.apply( pFrame, outLeft, outRight );
            return true;
        }
        outLeft  = pFrame[0];
        outRight = _channelCount >= 2 ? pFrame[1] : pFrame[0];
        return true;
    }

    shared_ptr<const AudioStreamSource> AudioStreamSource::tryCreate( string_view path, vector<uint8>& inoutBytes, float32 minSeconds )
    {
        shared_ptr<AudioStreamSource> pSource = make_shared<AudioStreamSource>();

        if ( FileUtil::hasExtension( path, ".ogg" ) )
        {
            AudioStreamInfo                 info;
            unique_ptr<IAudioStreamDecoder> pProbe = AudioClipDecoder::openOggStream( inoutBytes.data(), inoutBytes.size(), info );
            if ( pProbe == nullptr )
                return nullptr;
            pSource->_info           = info;
            pSource->_format         = Format::Vorbis;
            const bool bNeedsDownmix = info._channelCount > 2;
            if ( bNeedsDownmix )
            {
                uint32     arrSpeakerBit[AudioDownmix::kMaxSourceChannelCount]{};
                const bool bDownmixReady = AudioDownmix::getVorbisSpeakers( info._channelCount, arrSpeakerBit ) &&
                                           AudioDownmix::makeForSpeakers( arrSpeakerBit, info._channelCount, pSource->_downmix );
                if ( bDownmixReady == false )
                    return nullptr;
            }
        }
        else if ( FileUtil::hasExtension( path, ".wav" ) )
        {
            AudioPcm format;
            size_t   dataOffset{ 0 };
            uint32   dataBytes{ 0 };
            if ( AudioClipDecoder::parseWavLayout( inoutBytes.data(), inoutBytes.size(), format, dataOffset, dataBytes ) == false )
                return nullptr;
            const bool bChannelsOk              = 1 <= format._channelCount && format._channelCount <= AudioDownmix::kMaxSourceChannelCount;
            const bool bPcm16                   = format._bFloat == SW_FALSE && format._bitsPerSample == 16;
            const bool bFloat32                 = format._bFloat == SW_TRUE && format._bitsPerSample == 32;
            const bool bUnsupportedStreamFormat = bChannelsOk == false || ( bPcm16 == false && bFloat32 == false ) || format._sampleRate == 0;
            if ( bUnsupportedStreamFormat )
                return nullptr;
            const bool bNeedsDownmix = format._channelCount > 2;
            if ( bNeedsDownmix && AudioDownmix::makeForWaveMask( format._channelCount, format._channelMask, pSource->_downmix ) == false )
                return nullptr;
            pSource->_info._sampleRate   = format._sampleRate;
            pSource->_info._channelCount = format._channelCount;
            pSource->_info._frameCount   = dataBytes / format.getBlockAlign();
            pSource->_dataOffset         = dataOffset;
            pSource->_dataBytes          = dataBytes;
            pSource->_format             = bFloat32 ? Format::WavFloat32 : Format::WavPcm16;
        }
        else
        {
            return nullptr;
        }
        const float64 minFrames = static_cast<float64>( minSeconds ) * static_cast<float64>( pSource->_info._sampleRate );
        if ( pSource->_info._frameCount == 0 || static_cast<float64>( pSource->_info._frameCount ) < minFrames )
            return nullptr;
        pSource->_pBytes = make_shared<const vector<uint8>>( std::move( inoutBytes ) );
        inoutBytes.clear();
        return pSource;
    }

    unique_ptr<IAudioStreamDecoder> AudioStreamSource::openDecoder() const
    {
        if ( _pBytes == nullptr )
            return nullptr;
        if ( _format == Format::Vorbis )
        {
            AudioStreamInfo                 info;
            unique_ptr<IAudioStreamDecoder> pInner = AudioClipDecoder::openOggStream( _pBytes->data(), _pBytes->size(), info );
            if ( pInner == nullptr )
                return nullptr;
            return sw::make_unique<OwningStreamDecoder>( _pBytes, std::move( pInner ) );
        }
        return sw::make_unique<WavStreamDecoder>( _pBytes, _dataOffset, _info._frameCount, _info._channelCount, _format == Format::WavFloat32 );
    }
} // namespace sw
