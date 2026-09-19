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
        const bool bLoaded = _asset.loadFromFile( path );
        // **자산을 바꾼 뒤에 멈춘다.** 예전에는 `stop()` 이 먼저였고, 그 안의
        // `_previousFrame = _asset._frameMin` 이 **바뀌기 전 자산**의 시작 프레임을 집었다.
        // 새 자산이 100 프레임에서 시작하고 직전 값이 0 이면, 첫 갱신이 `applyFrame(100, 0)` 이
        // 되어 100 이하의 이벤트가 전부 한꺼번에 발화한다. `getCurrentFrame()` 은 재생 시각으로
        // 그때그때 구하므로 새 자산을 따르는데 `getPreviousFrame()` 만 옛 자산을 따르는,
        // 둘이 어긋나는 상태이기도 했다.
        stop();
        return bLoaded;
    }

    void SequencePlayer::setAsset( const SequenceAsset& asset )
    {
        _asset = asset;
        stop();
    }

    void SequencePlayer::play()
    {
        _playbackTime = 0.0f;
        // **첫 프레임보다 하나 앞**에서 시작한다. 이전 프레임을 `_frameMin` 으로 두면 첫 프레임에
        // 걸린 이벤트가 처음부터 "이미 지난 것" 이라 영영 발화하지 않는다 — 이벤트 판정은
        // `previousFrame < start <= frame`(지나갔는가) 이기 때문이다.
        _previousFrame = _asset._frameMin - 1;
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
            // 한 바퀴를 돌았으면 `play()` 와 같은 자리에서 다시 시작해야 한다. 그러지 않으면
            // 이전 프레임이 끝쪽(`_frameMax` 근처)인 채로 남아, 되감긴 첫 프레임의 이벤트가
            // "이미 지난 것" 이 되어 **루프마다 빠진다.**
            _previousFrame = _asset._frameMin - 1;
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
