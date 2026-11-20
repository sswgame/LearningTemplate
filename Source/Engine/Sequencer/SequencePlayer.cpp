#include "pch.h"

#include "Engine/Sequencer/SequencePlayer.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    SequencePlaybackStep SequencePlaybackStep::makeEmpty( int32 frame )
    {
        SequencePlaybackStep step;
        step._previousFrame = frame;
        step._frame         = frame;
        return step;
    }

    void SequencePlaybackStep::addSegment( int32 from, int32 to )
    {
        if ( from == to )
            return;
        SW_ASSERT( _segmentCount < kMaxSegmentCount );
        if ( _segmentCount >= kMaxSegmentCount )
            return;
        _arrSegment[_segmentCount]._from = from;
        _arrSegment[_segmentCount]._to   = to;
        ++_segmentCount;
    }

    SequencePlayer::SequencePlayer()
        : _asset{}
        , _positionTicks{ 0 }
        , _framesPerSecond{ 30.0f }
        , _playRate{ 1.0f }
        , _rangeStart{ 0 }
        , _rangeEnd{ 0 }
        , _loopMode{ SequenceLoopMode::None }
        , _bPlaying{ SW_FALSE }
        , _bPaused{ SW_FALSE }
        , _bPingPongReversed{ SW_FALSE }
        , _reserved{ 0 }
    {
        resetToAsset();
    }

    bool SequencePlayer::loadFromAsset( const AssetID& assetID )
    {
        const bool bLoaded = _asset.loadFromAsset( assetID );
        // 에셋을 바꾼 뒤에 범위를 다시 잡는다 — 앞 에셋의 범위가 남으면 새 에셋의 프레임 밖을 재생한다.
        resetToAsset();
        return bLoaded;
    }

    void SequencePlayer::setAsset( const SequenceAsset& asset )
    {
        _asset = asset;
        resetToAsset();
    }

    void SequencePlayer::resetToAsset()
    {
        _rangeStart = _asset._frameMin;
        _rangeEnd   = _asset._frameMax;
        stop();
    }

    int32 SequencePlayer::toFrame( int64 ticks )
    {
        // 내림 나눗셈 — 음수 프레임이 있는 시퀀스에서 0 쪽으로 자르면 한 프레임 어긋난다.
        const int64 frame = ticks >= 0 ? ticks / kSequenceTicksPerFrame : -( ( -ticks + kSequenceTicksPerFrame - 1 ) / kSequenceTicksPerFrame );
        return static_cast<int32>( frame );
    }

    bool SequencePlayer::isReversed() const
    {
        const bool bNegativeRate = _playRate < 0.0f;
        return bNegativeRate != ( _bPingPongReversed == SW_TRUE );
    }

    void SequencePlayer::play()
    {
        _bPingPongReversed = SW_FALSE;
        _positionTicks     = static_cast<int64>( _playRate < 0.0f ? _rangeEnd : _rangeStart ) * kSequenceTicksPerFrame;
        _bPlaying          = SW_TRUE;
        _bPaused           = SW_FALSE;
    }

    void SequencePlayer::playReverse()
    {
        _playRate = -MathUtil::abs( _playRate );
        play();
    }

    void SequencePlayer::playFromFrame( int32 frame )
    {
        seekToFrame( frame );
        _bPlaying = SW_TRUE;
        _bPaused  = SW_FALSE;
    }

    void SequencePlayer::stop()
    {
        _positionTicks     = static_cast<int64>( _rangeStart ) * kSequenceTicksPerFrame;
        _bPlaying          = SW_FALSE;
        _bPaused           = SW_FALSE;
        _bPingPongReversed = SW_FALSE;
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

    SequencePlaybackStep SequencePlayer::getStartStep() const
    {
        // 처음 프레임 하나 바깥에서 들어온 구간 — 처음 프레임에 걸린 이벤트가 "이미 지난 것" 이 되지 않는다.
        const int32          frame = getCurrentFrame();
        SequencePlaybackStep step  = SequencePlaybackStep::makeEmpty( frame );
        step._kind                 = SequencePlaybackStepKind::Start;
        step._previousFrame        = isReversed() ? frame + 1 : frame - 1;
        step.addSegment( step._previousFrame, frame );
        return step;
    }

    SequencePlaybackStep SequencePlayer::skipToEnd()
    {
        const int32 frameBefore   = getCurrentFrame();
        _positionTicks            = static_cast<int64>( isReversed() ? _rangeStart : _rangeEnd ) * kSequenceTicksPerFrame;
        _bPlaying                 = SW_FALSE;
        _bPaused                  = SW_FALSE;
        SequencePlaybackStep step = SequencePlaybackStep::makeEmpty( getCurrentFrame() );
        step._previousFrame       = frameBefore;
        step._kind                = SequencePlaybackStepKind::Jump;
        return step;
    }

    SequencePlaybackStep SequencePlayer::update( float32 deltaSeconds )
    {
        const int32 frameBefore = getCurrentFrame();
        if ( _bPlaying == SW_FALSE || _bPaused == SW_TRUE || ( deltaSeconds > 0.0f ) == false )
            return SequencePlaybackStep::makeEmpty( frameBefore );

        SequencePlaybackStep step        = SequencePlaybackStep::makeEmpty( frameBefore );
        const int64          startTicks  = static_cast<int64>( _rangeStart ) * kSequenceTicksPerFrame;
        const int64          endTicks    = static_cast<int64>( _rangeEnd ) * kSequenceTicksPerFrame;
        const int64          lengthTicks = endTicks - startTicks;
        if ( lengthTicks <= 0 )
        {
            // 길이 없는 시퀀스는 처음 프레임(시작 갱신)만 적용하고 끝난다 — 영영 끝나지 않는 재생을 두지 않는다.
            _bPlaying = SW_FALSE;
            return step;
        }

        const float64 deltaTicks = static_cast<float64>( deltaSeconds ) * static_cast<float64>( _framesPerSecond ) * static_cast<float64>( MathUtil::abs( _playRate ) ) *
                                   static_cast<float64>( kSequenceTicksPerFrame );
        const int64 advance = static_cast<int64>( deltaTicks + 0.5 );
        if ( advance <= 0 )
            return step;

        const bool  bReversed = isReversed();
        const int64 position  = _positionTicks + ( bReversed ? -advance : advance );
        // 반복하지 않으면 끝에 닿는 순간이 끝이다. 반복은 끝을 넘어야 되감는다(끝 프레임을 한 번 적용한 뒤).
        const bool bInside = _loopMode == SequenceLoopMode::None ? ( position > startTicks && position < endTicks ) : ( position >= startTicks && position <= endTicks );
        if ( bInside )
        {
            _positionTicks = position;
            step._frame    = getCurrentFrame();
            step.addSegment( frameBefore, step._frame );
            return step;
        }

        const int32 edgeFrame = bReversed ? _rangeStart : _rangeEnd;
        const int64 overTicks = bReversed ? startTicks - position : position - endTicks; // 0 이상(반복 없음은 0 도 끝이다)
        step.addSegment( frameBefore, edgeFrame );
        switch ( _loopMode )
        {
            case SequenceLoopMode::None:
            {
                _positionTicks = bReversed ? startTicks : endTicks;
                _bPlaying      = SW_FALSE;
                break;
            }
            case SequenceLoopMode::Loop:
            {
                // 같은 방향으로 반대쪽 끝에서 이어 간다. 들어온 구간은 그 끝 하나 바깥에서 시작한다(되감은 첫 프레임의 이벤트가 빠지지 않게).
                const int64 remainder = overTicks % lengthTicks;
                _positionTicks        = bReversed ? endTicks - remainder : startTicks + remainder;
                step.addSegment( bReversed ? _rangeEnd + 1 : _rangeStart - 1, getCurrentFrame() );
                break;
            }
            case SequenceLoopMode::PingPong:
            {
                // 끝에서 꺾일 때마다 방향이 뒤집힌다. 넘은 길이가 (0, 길이] 이면 한 번, (길이, 2 × 길이] 이면 두 번 꺾였다.
                const int64 flipCount = 1 + ( overTicks - 1 ) / lengthTicks;
                const int64 remainder = overTicks - ( flipCount - 1 ) * lengthTicks;
                if ( ( flipCount % 2 ) == 1 )
                {
                    _positionTicks     = bReversed ? startTicks + remainder : endTicks - remainder;
                    _bPingPongReversed = _bPingPongReversed == SW_TRUE ? SW_FALSE : SW_TRUE;
                    step.addSegment( edgeFrame, getCurrentFrame() );
                }
                else
                {
                    _positionTicks = bReversed ? endTicks - remainder : startTicks + remainder;
                    step.addSegment( bReversed ? _rangeEnd : _rangeStart, getCurrentFrame() );
                }
                break;
            }
        }
        step._frame = getCurrentFrame();
        return step;
    }

    void SequencePlayer::setFramesPerSecond( float32 fps )
    {
        if ( MathUtil::isFinite( fps ) == false || fps <= 0.0f )
        {
            SW_LOG_ERROR_ID( "SequencePlayer.FramesPerSecondInvalid", "Sequence frames per second must be above 0 (got %#) - keeping %#", fps, _framesPerSecond );
            return;
        }
        _framesPerSecond = fps;
    }

    bool SequencePlayer::setPlayRate( float32 playRate )
    {
        if ( MathUtil::isFinite( playRate ) == false || playRate == 0.0f )
        {
            SW_LOG_ERROR_ID( "SequencePlayer.PlayRateInvalid", "Sequence play rate must be a finite non-zero number (got %#; negative plays backwards) - keeping %#", playRate, _playRate );
            return false;
        }
        _playRate = playRate;
        return true;
    }

    bool SequencePlayer::setPlaybackRange( int32 startFrame, int32 endFrame )
    {
        if ( startFrame < _asset._frameMin || endFrame > _asset._frameMax || startFrame >= endFrame )
        {
            SW_LOG_ERROR_ID( "SequencePlayer.PlaybackRangeInvalid", "Sequence playback range %#..%# must lie inside %#..%# with start before end - keeping %#..%#", startFrame, endFrame,
                             _asset._frameMin, _asset._frameMax, _rangeStart, _rangeEnd );
            return false;
        }
        _rangeStart = startFrame;
        _rangeEnd   = endFrame;
        seekToFrame( getCurrentFrame() );
        return true;
    }

    void SequencePlayer::seekToFrame( int32 frame )
    {
        const int32 clamped = MathUtil::clamp( frame, _rangeStart, _rangeEnd );
        _positionTicks      = static_cast<int64>( clamped ) * kSequenceTicksPerFrame;
    }

    void SequencePlayer::collectActiveItems( vector<const SequenceTrackItem*>& outListItem ) const
    {
        _asset.collectActiveItems( getCurrentFrame(), outListItem );
    }
} // namespace sw
