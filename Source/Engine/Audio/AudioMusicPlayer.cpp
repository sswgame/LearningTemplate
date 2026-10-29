#include "pch.h"

#include "Engine/Audio/AudioMusicPlayer.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Audio/AudioMusic.h"
#include "Engine/Audio/AudioTypes.h"

namespace sw
{
    SW_LOG_CALLER( "AudioMusicPlayer" );

    void AudioMusicPlayer::start( IAudioMusicVoices& voices, shared_ptr<const AudioMusicDesc> pMusic, AudioPlayingID playingID, float32 fadeSeconds )
    {
        if ( _playback._pDesc != nullptr )
            stop( voices, fadeSeconds );
        _playback._pDesc     = std::move( pMusic );
        _playback._playingID = playingID;

        int32 startSegment = _playback._pDesc->findSegmentIndex( _playback._pDesc->_startSegment );
        if ( startSegment < 0 )
            startSegment = 0;
        _playback._segment        = startSegment;
        _playback._segmentStart   = voices.getMusicClock();
        _playback._pendingSegment = -1;
        scheduleSegment( voices, startSegment, voices.getMusicClock(), fadeSeconds );
    }

    void AudioMusicPlayer::stop( IAudioMusicVoices& voices, float32 fadeSeconds )
    {
        const uint32 fadeFrames = static_cast<uint32>( MathUtil::max( 0.0f, fadeSeconds ) * static_cast<float32>( audio::kSampleRate ) );
        if ( _playback._playingID != 0 )
        {
            if ( fadeFrames == 0 )
                voices.freeMusicVoices( _playback._playingID, kAnySegment );
            else
                voices.fadeMusicVoices( _playback._playingID, kAnySegment, fadeFrames, 0 );
        }
        reset();
    }

    void AudioMusicPlayer::reset()
    {
        _playback._pDesc.reset();
        _playback._segment        = -1;
        _playback._pendingSegment = -1;
    }

    uint64 AudioMusicPlayer::computeSyncFrame( uint64 now, AudioMusicSync sync ) const
    {
        if ( _playback._segment < 0 || sync == AudioMusicSync::Immediate )
            return now;
        const uint32  segmentIndex  = static_cast<uint32>( _playback._segment );
        const float64 framesPerBeat = _playback._pDesc->computeFramesPerBeat( segmentIndex );
        float64       unit          = framesPerBeat;
        if ( sync == AudioMusicSync::NextBar )
            unit = framesPerBeat * static_cast<float64>( _playback._pDesc->getBeatsPerBar( segmentIndex ) );
        else if ( sync == AudioMusicSync::SegmentEnd )
            unit = static_cast<float64>( _playback._pDesc->computeSegmentFrames( segmentIndex ) );

        // 구간 시작에서 센 다음 경계를 구한다. 박과 마디는 지금이 경계면 지금이고, 구간 끝은 지금 돌고 있는 한 바퀴의 끝이다.
        const float64 elapsed  = MathUtil::max( 0.0, static_cast<float64>( now ) - static_cast<float64>( _playback._segmentStart ) );
        const float64 steps    = sync == AudioMusicSync::SegmentEnd ? MathUtil::floor( elapsed / unit ) + 1.0 : MathUtil::ceil( elapsed / unit );
        uint64        boundary = _playback._segmentStart + static_cast<uint64>( steps * unit + 0.5 );
        if ( boundary < now )
            boundary += static_cast<uint64>( unit + 0.5 );
        return boundary;
    }

    void AudioMusicPlayer::scheduleTransition( IAudioMusicVoices& voices, const hashed_string& segment, bool bAtSegmentEnd )
    {
        if ( _playback._pDesc == nullptr )
            return;
        const int32 targetSegment = _playback._pDesc->findSegmentIndex( segment );
        if ( targetSegment < 0 )
        {
            SW_LOG_ERROR( "Unknown music segment '%#'", segment.c_str() );
            return;
        }

        const hashed_string             currentName = _playback._segment >= 0 ? _playback._pDesc->_listSegment[static_cast<size_t>( _playback._segment )]._name : hashed_string{};
        const AudioMusicTransitionDesc* pRule       = _playback._pDesc->findTransition( currentName, segment );
        const float32                   fadeIn      = pRule != nullptr ? pRule->_fadeInSeconds : 0.0f;

        // 이미 전환을 기다리는 중이면 목적지만 바꾼다. 경계와 이전 구간의 페이드는 그대로다.
        if ( _playback._pendingSegment >= 0 )
        {
            if ( targetSegment == _playback._pendingSegment )
                return;
            voices.freeMusicVoices( _playback._playingID, _playback._pendingSegment );
            _playback._pendingSegment = targetSegment;
            scheduleSegment( voices, targetSegment, _playback._pendingFrame, fadeIn );
            return;
        }
        if ( targetSegment == _playback._segment )
            return;

        const uint64         now      = voices.getMusicClock();
        const AudioMusicSync sync     = bAtSegmentEnd ? AudioMusicSync::SegmentEnd : ( pRule != nullptr ? pRule->_sync : _playback._pDesc->_defaultSync );
        const uint64         boundary = computeSyncFrame( now, sync );
        const uint32         delay    = static_cast<uint32>( boundary - now );
        const uint32         fadeOut  = pRule != nullptr ? static_cast<uint32>( pRule->_fadeOutSeconds * static_cast<float32>( audio::kSampleRate ) ) : 0u;

        // 옛 구간은 경계에서(샘플 단위) 빠지기 시작한다.
        voices.fadeMusicVoices( _playback._playingID, _playback._segment, fadeOut, delay );
        scheduleSegment( voices, targetSegment, boundary, fadeIn );

        if ( pRule != nullptr && pRule->_stinger.empty() == false )
        {
            AudioMusicVoiceRequest stinger;
            stinger._clipPath   = pRule->_stinger;
            stinger._bus        = getBus();
            stinger._playingID  = _playback._playingID;
            stinger._startFrame = boundary;
            stinger._segment    = kStingerSegment;
            (void)voices.startMusicVoice( stinger ); // 풀이 비었으면 스팅어만 빠진다(구간 전환은 그대로)
        }
        _playback._pendingSegment = targetSegment;
        _playback._pendingFrame   = boundary;
    }

    void AudioMusicPlayer::scheduleSegment( IAudioMusicVoices& voices, int32 segmentIndex, uint64 startFrame, float32 fadeInSeconds )
    {
        const AudioMusicSegmentDesc& segment = _playback._pDesc->_listSegment[static_cast<size_t>( segmentIndex )];
        const hashed_string          bus     = getBus();
        for ( size_t layerIndex = 0; layerIndex < segment._listLayer.size(); ++layerIndex )
        {
            AudioMusicVoiceRequest request;
            request._clipPath      = segment._listLayer[layerIndex]._path;
            request._bus           = bus;
            request._playingID     = _playback._playingID;
            request._startFrame    = startFrame;
            request._segment       = segmentIndex;
            request._layer         = static_cast<int32>( layerIndex );
            request._gain          = computeLayerGain( voices, segmentIndex, static_cast<int32>( layerIndex ) );
            request._fadeInSeconds = fadeInSeconds;
            request._bLoop         = segment._bLoop;
            if ( voices.startMusicVoice( request ) == false )
            {
                SW_LOG_WARNING( "Voice pool exhausted - dropped a music layer" );
                return;
            }
        }
    }

    hashed_string AudioMusicPlayer::getBus() const
    {
        return _playback._pDesc->_bus.empty() ? hashed_string( AudioBusNames::kMusic ) : _playback._pDesc->_bus;
    }

    float32 AudioMusicPlayer::computeLayerGain( const IAudioMusicVoices& voices, int32 segmentIndex, int32 layerIndex ) const
    {
        const AudioMusicLayerDesc& layer    = _playback._pDesc->_listSegment[static_cast<size_t>( segmentIndex )]._listLayer[static_cast<size_t>( layerIndex )];
        float32                    volumeDb = layer._volumeDb;
        if ( layer._parameter.empty() == false )
            volumeDb += AudioCurvePoint::evaluate( layer._listPoint, voices.findMusicParameter( layer._parameter ) ).value_or( 0.0f );
        return AudioMath::dbToLinear( volumeDb );
    }

    void AudioMusicPlayer::update( IAudioMusicVoices& voices )
    {
        if ( _playback._pDesc == nullptr )
            return;
        const uint64 now      = voices.getMusicClock();
        const uint64 blockEnd = now + audio::kBlockFrameCount;

        // 경계가 이 블록 안에 들어오면 지금 구간을 넘긴다(소리는 보이스의 지연과 페이드가 샘플 단위로 맞춘다).
        if ( _playback._pendingSegment >= 0 && _playback._pendingFrame < blockEnd )
        {
            _playback._segment        = _playback._pendingSegment;
            _playback._segmentStart   = _playback._pendingFrame;
            _playback._pendingSegment = -1;
        }

        // 루프하지 않는 구간이다. 다음 구간이 있으면 끝에 연결하고, 없으면 끝나면 멈춘다.
        if ( _playback._segment >= 0 && _playback._pendingSegment < 0 )
        {
            const AudioMusicSegmentDesc& segment = _playback._pDesc->_listSegment[static_cast<size_t>( _playback._segment )];
            if ( segment._bLoop == false )
            {
                if ( segment._next.empty() == false )
                    scheduleTransition( voices, segment._next, true );
                else if ( now >= _playback._segmentStart + _playback._pDesc->computeSegmentFrames( static_cast<uint32>( _playback._segment ) ) )
                    _playback._segment = -1;
            }
        }

        // 세로 레이어: 파라미터 곡선의 게인으로 `_layerFadeSeconds` 동안 옮긴다.
        const float32 fadeSeconds = _playback._pDesc->_layerFadeSeconds;
        const float32 maxStep     = fadeSeconds <= 0.0f ? MathUtil::kMaxFloat : audio::kBlockSeconds / fadeSeconds;
        voices.stepMusicLayerGains( _playback._playingID, maxStep, *this );
    }
} // namespace sw
