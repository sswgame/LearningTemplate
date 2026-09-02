#include "pch.h"

#include "Engine/Sequencer/SequencePlayer.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    SequencePlayer::SequencePlayer()
        : _asset{}
        , _framesPerSecond{ 30.0f }
        , _playbackTime{ 0.0f }
        , _previousFrame{ 0 }
        , _bPlaying{ SW_FALSE }
        , _bPaused{ SW_FALSE }
        , _bLoop{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool SequencePlayer::loadFromFile( string_view path )
    {
        stop();
        return _asset.loadFromFile( path );
    }

    void SequencePlayer::setAsset( const SequenceAsset& asset )
    {
        stop();
        _asset = asset;
    }

    void SequencePlayer::play()
    {
        _playbackTime  = 0.0f;
        _previousFrame = _asset._frameMin;
        _bPlaying      = SW_TRUE;
        _bPaused       = SW_FALSE;
    }

    void SequencePlayer::playFromFrame( int32 frame )
    {
        seekToFrame( frame );
        _bPlaying = SW_TRUE;
        _bPaused  = SW_FALSE;
    }

    void SequencePlayer::stop()
    {
        _playbackTime  = 0.0f;
        _previousFrame = _asset._frameMin;
        _bPlaying      = SW_FALSE;
        _bPaused       = SW_FALSE;
    }

    void SequencePlayer::pause()
    {
        _bPaused = SW_TRUE;
    }

    void SequencePlayer::resume()
    {
        if ( _bPlaying == SW_TRUE )
            _bPaused = SW_FALSE;
    }

    void SequencePlayer::update( float32 deltaSeconds )
    {
        if ( _bPlaying == SW_FALSE || _bPaused == SW_TRUE )
            return;
        if ( deltaSeconds < 0.0f )
            deltaSeconds = 0.0f;

        _previousFrame = getCurrentFrame();
        _playbackTime += deltaSeconds;

        const int32 span = _asset._frameMax - _asset._frameMin;
        if ( span <= 0 )
            return;

        const float32 fps         = ( _framesPerSecond > 0.0f ) ? _framesPerSecond : 30.0f;
        const float32 durationSec = static_cast<float32>( span ) / fps;
        if ( _playbackTime < durationSec )
            return;

        if ( _bLoop == SW_TRUE )
        {
            _playbackTime = MathUtil::fmod( _playbackTime, durationSec );
            if ( _playbackTime < 0.0f )
                _playbackTime += durationSec;
        }
        else
        {
            _playbackTime = durationSec;
            _bPlaying     = SW_FALSE;
        }
    }

    void SequencePlayer::setFramesPerSecond( float32 fps )
    {
        _framesPerSecond = ( fps > 0.0f ) ? fps : 30.0f;
    }

    void SequencePlayer::seekToFrame( int32 frame )
    {
        int32 clamped = frame;
        if ( clamped < _asset._frameMin )
            clamped = _asset._frameMin;
        if ( clamped > _asset._frameMax )
            clamped = _asset._frameMax;
        const float32 fps = ( _framesPerSecond > 0.0f ) ? _framesPerSecond : 30.0f;
        _playbackTime     = static_cast<float32>( clamped - _asset._frameMin ) / fps;
        _previousFrame    = clamped;
    }

    int32 SequencePlayer::getCurrentFrame() const
    {
        return computeFrame( _playbackTime );
    }

    void SequencePlayer::collectActiveItems( vector<const SequenceTrackItem*>& outListItem ) const
    {
        _asset.collectActiveItems( getCurrentFrame(), outListItem );
    }

    int32 SequencePlayer::computeFrame( float32 timeSeconds ) const
    {
        const float32 fps   = ( _framesPerSecond > 0.0f ) ? _framesPerSecond : 30.0f;
        int32         frame = _asset._frameMin + static_cast<int32>( timeSeconds * fps );
        if ( frame < _asset._frameMin )
            frame = _asset._frameMin;
        if ( frame > _asset._frameMax )
            frame = _asset._frameMax;
        return frame;
    }
} // namespace sw
