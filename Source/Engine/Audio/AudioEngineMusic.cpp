#include "pch.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioMixer.h"

namespace sw
{
    namespace
    {
        struct AudioEngineMusicInternal
        {
            /** @brief 스팅어 보이스의 구간 표시입니다(레이어가 아니라 구간이 바뀌어도 페이드하지 않는다). */
            static constexpr int32 kStingerSegment = -2;
            /** @brief 블록 하나의 길이(초)입니다. */
            static constexpr float32 kBlockSeconds = static_cast<float32>( audio::kBlockFrameCount ) / static_cast<float32>( audio::kSampleRate );
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "AudioEngine" );

    void AudioEngine::applyStartMusic( const Command& command )
    {
        if ( _pMusic != nullptr )
            applyStopMusic( command._value );
        _pMusic         = command._pMusic;
        _musicPlayingId = command._playingId;

        int32 startSegment = _pMusic->findSegmentIndex( _pMusic->_startSegment );
        if ( startSegment < 0 )
            startSegment = 0;
        _musicSegment        = startSegment;
        _musicSegmentStart   = _renderedFrameCount;
        _musicPendingSegment = -1;
        scheduleMusicSegment( startSegment, _renderedFrameCount, command._value );
    }

    void AudioEngine::applyStopMusic( float32 fadeSeconds )
    {
        const uint32 fadeFrames = static_cast<uint32>( MathUtil::max( 0.0f, fadeSeconds ) * static_cast<float32>( audio::kSampleRate ) );
        for ( VoiceSlot& slot : _listVoice )
        {
            if ( slot._bInUse == false || slot._playingId != _musicPlayingId || _musicPlayingId == 0 )
                continue;
            if ( slot._bWaitingForClip || fadeFrames == 0 )
                freeVoice( slot );
            else
                slot._voice.setFade( 0.0f, fadeFrames, 0, true );
        }
        _pMusic.reset();
        _musicSegment        = -1;
        _musicPendingSegment = -1;
    }

    uint64 AudioEngine::computeMusicSyncFrame( AudioMusicSync sync ) const
    {
        const uint64 now = _renderedFrameCount;
        if ( _musicSegment < 0 || sync == AudioMusicSync::Immediate )
            return now;
        const uint32  segmentIndex  = static_cast<uint32>( _musicSegment );
        const float64 framesPerBeat = _pMusic->computeFramesPerBeat( segmentIndex );
        float64       unit          = framesPerBeat;
        if ( sync == AudioMusicSync::NextBar )
            unit = framesPerBeat * static_cast<float64>( _pMusic->getBeatsPerBar( segmentIndex ) );
        else if ( sync == AudioMusicSync::SegmentEnd )
            unit = static_cast<float64>( _pMusic->computeSegmentFrames( segmentIndex ) );

        // 구간 시작에서 센 다음 경계 — 박 · 마디는 지금이 경계면 지금, 구간 끝은 지금 돌고 있는 한 바퀴의 끝.
        const float64 elapsed  = MathUtil::max( 0.0, static_cast<float64>( now ) - static_cast<float64>( _musicSegmentStart ) );
        const float64 steps    = sync == AudioMusicSync::SegmentEnd ? MathUtil::floor( elapsed / unit ) + 1.0 : MathUtil::ceil( elapsed / unit );
        uint64        boundary = _musicSegmentStart + static_cast<uint64>( steps * unit + 0.5 );
        if ( boundary < now )
            boundary += static_cast<uint64>( unit + 0.5 );
        return boundary;
    }

    void AudioEngine::applySetMusicSegment( const hashed_string& segment, bool bAtSegmentEnd )
    {
        if ( _pMusic == nullptr )
            return;
        const int32 targetSegment = _pMusic->findSegmentIndex( segment );
        if ( targetSegment < 0 )
        {
            SW_LOG_WARNING( "Unknown music segment '%#'", segment.c_str() );
            return;
        }

        const hashed_string             currentName = _musicSegment >= 0 ? _pMusic->_listSegment[static_cast<size_t>( _musicSegment )]._name : hashed_string{};
        const AudioMusicTransitionDesc* pRule       = _pMusic->findTransition( currentName, segment );
        const float32                   fadeIn      = pRule != nullptr ? pRule->_fadeInSeconds : 0.0f;

        // 이미 전환을 기다리는 중이면 목적지만 바꾼다 — 경계와 옛 구간의 페이드는 그대로다.
        if ( _musicPendingSegment >= 0 )
        {
            if ( targetSegment == _musicPendingSegment )
                return;
            for ( VoiceSlot& slot : _listVoice )
            {
                if ( slot._bInUse && slot._playingId == _musicPlayingId && slot._musicSegment == _musicPendingSegment )
                    freeVoice( slot );
            }
            _musicPendingSegment = targetSegment;
            scheduleMusicSegment( targetSegment, _musicPendingFrame, fadeIn );
            return;
        }
        if ( targetSegment == _musicSegment )
            return;

        const AudioMusicSync sync     = bAtSegmentEnd ? AudioMusicSync::SegmentEnd : ( pRule != nullptr ? pRule->_sync : _pMusic->_defaultSync );
        const uint64         boundary = computeMusicSyncFrame( sync );
        const uint32         delay    = static_cast<uint32>( boundary - _renderedFrameCount );
        const uint32         fadeOut  = pRule != nullptr ? static_cast<uint32>( pRule->_fadeOutSeconds * static_cast<float32>( audio::kSampleRate ) ) : 0u;

        // 옛 구간은 경계에서(샘플 단위) 빠지기 시작한다.
        for ( VoiceSlot& slot : _listVoice )
        {
            if ( slot._bInUse == false || slot._playingId != _musicPlayingId || slot._musicSegment != _musicSegment )
                continue;
            if ( slot._bWaitingForClip )
                freeVoice( slot );
            else
                slot._voice.setFade( 0.0f, fadeOut, delay, true );
        }
        scheduleMusicSegment( targetSegment, boundary, fadeIn );

        if ( pRule != nullptr && pRule->_stinger.empty() == false )
        {
            VoiceSlot* pSlot = allocateVoice();
            if ( pSlot != nullptr )
            {
                pSlot->_playingId       = _musicPlayingId;
                pSlot->_clipPath        = pRule->_stinger;
                pSlot->_busName         = _pMusic->_bus.empty() ? hashed_string( AudioBusNames::kMusic ) : _pMusic->_bus;
                pSlot->_busIndex        = resolveBusIndex( pSlot->_busName );
                pSlot->_musicSegment    = AudioEngineMusicInternal::kStingerSegment;
                pSlot->_startFrame      = boundary;
                pSlot->_priority        = 100;
                pSlot->_virtualMode     = AudioVirtualMode::KeepReal;
                pSlot->_bWaitingForClip = true;
                startWaitingVoice( *pSlot );
            }
        }
        _musicPendingSegment = targetSegment;
        _musicPendingFrame   = boundary;
    }

    void AudioEngine::scheduleMusicSegment( int32 segmentIndex, uint64 startFrame, float32 fadeInSeconds )
    {
        const AudioMusicSegmentDesc& segment = _pMusic->_listSegment[static_cast<size_t>( segmentIndex )];
        const hashed_string          bus     = _pMusic->_bus.empty() ? hashed_string( AudioBusNames::kMusic ) : _pMusic->_bus;
        for ( size_t layerIndex = 0; layerIndex < segment._listLayer.size(); ++layerIndex )
        {
            VoiceSlot* pSlot = allocateVoice();
            if ( pSlot == nullptr )
            {
                SW_LOG_WARNING( "Voice pool exhausted - dropped a music layer" );
                return;
            }
            pSlot->_playingId       = _musicPlayingId;
            pSlot->_clipPath        = segment._listLayer[layerIndex]._path;
            pSlot->_busName         = bus;
            pSlot->_busIndex        = resolveBusIndex( bus );
            pSlot->_musicSegment    = segmentIndex;
            pSlot->_musicLayer      = static_cast<int32>( layerIndex );
            pSlot->_musicGain       = computeMusicLayerGain( segmentIndex, static_cast<int32>( layerIndex ) );
            pSlot->_startFrame      = startFrame;
            pSlot->_fadeInSeconds   = fadeInSeconds;
            pSlot->_priority        = 100;
            pSlot->_virtualMode     = AudioVirtualMode::KeepReal;
            pSlot->_bLoop           = segment._bLoop;
            pSlot->_bWaitingForClip = true;
            startWaitingVoice( *pSlot );
        }
    }

    float32 AudioEngine::computeMusicLayerGain( int32 segmentIndex, int32 layerIndex ) const
    {
        const AudioMusicLayerDesc& layer    = _pMusic->_listSegment[static_cast<size_t>( segmentIndex )]._listLayer[static_cast<size_t>( layerIndex )];
        float32                    volumeDb = layer._volumeDb;
        if ( layer._parameter.empty() == false )
            volumeDb += AudioCurvePoint::evaluate( layer._listPoint, getParameterValue( layer._parameter ), 0.0f );
        return AudioMath::dbToLinear( volumeDb );
    }

    void AudioEngine::updateMusic()
    {
        if ( _pMusic == nullptr )
            return;
        const uint64 now      = _renderedFrameCount;
        const uint64 blockEnd = now + audio::kBlockFrameCount;

        // 경계가 이 블록 안에 들면 지금 구간을 넘긴다(소리는 보이스의 지연 · 페이드가 샘플 단위로 맞춘다).
        if ( _musicPendingSegment >= 0 && _musicPendingFrame < blockEnd )
        {
            _musicSegment        = _musicPendingSegment;
            _musicSegmentStart   = _musicPendingFrame;
            _musicPendingSegment = -1;
        }

        // 루프하지 않는 구간 — 다음 구간이 있으면 끝에 잇고, 없으면 끝나면 멈춘다.
        if ( _musicSegment >= 0 && _musicPendingSegment < 0 )
        {
            const AudioMusicSegmentDesc& segment = _pMusic->_listSegment[static_cast<size_t>( _musicSegment )];
            if ( segment._bLoop == false )
            {
                if ( segment._next.empty() == false )
                    applySetMusicSegment( segment._next, true );
                else if ( now >= _musicSegmentStart + _pMusic->computeSegmentFrames( static_cast<uint32>( _musicSegment ) ) )
                    _musicSegment = -1;
            }
        }

        // 세로 레이어: 파라미터 곡선의 게인으로 `_layerFadeSeconds` 동안 옮긴다.
        const float32 fadeSeconds = _pMusic->_layerFadeSeconds;
        const float32 maxStep     = fadeSeconds <= 0.0f ? MathUtil::kMaxFloat : AudioEngineMusicInternal::kBlockSeconds / fadeSeconds;
        for ( VoiceSlot& slot : _listVoice )
        {
            if ( slot._bInUse == false || slot._playingId != _musicPlayingId || slot._musicSegment < 0 || slot._musicLayer < 0 )
                continue;
            const float32 target = computeMusicLayerGain( slot._musicSegment, slot._musicLayer );
            const float32 delta  = target - slot._musicGain;
            slot._musicGain      = MathUtil::abs( delta ) <= maxStep ? target : slot._musicGain + ( delta > 0.0f ? maxStep : -maxStep );
        }
    }
} // namespace sw
