/**
 * @file SequencePlayer.h
 * @brief SequenceAsset 타임라인을 프레임 단위로 재생합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Sequencer/SequenceAsset.h"

namespace sw
{
    /**
     * @class SequencePlayer
     * @brief fps 로 프레임을 진행하고 collectActiveItems 로 현재 클립 · 이벤트를 찾습니다.
     */
    class SW_API SequencePlayer
    {
    public:
        SequencePlayer();

        /** @brief JSON 시퀀스를 로드합니다. */
        bool loadFromFile( string_view path );
        /** @brief 이미 파싱된 에셋을 설정합니다. */
        void setAsset( const SequenceAsset& asset );

        void play();
        void playFromFrame( int32 frame );
        void stop();
        void pause();
        void resume();
        void update( float32 deltaSeconds );

        void  setFramesPerSecond( float32 fps );
        void  seekToFrame( int32 frame );
        int32 getCurrentFrame() const;
        int32 getPreviousFrame() const { return _previousFrame; }
        /** @brief `getFrameBeforeWrap` 가 "이번 갱신은 되감지 않았다" 를 뜻할 때의 값입니다. */
        static constexpr int32 kNoLoopWrap = ( -2147483647 - 1 );
        /**
         * @brief 이번 `update` 가 루프를 되감았으면 되감기 **직전**의 프레임을, 아니면 `kNoLoopWrap` 을 반환합니다.
         * @details 되감으면 이전 프레임이 `_frameMin - 1` 로 돌아가 (직전 프레임, `_frameMax`] 구간을 아무도 보지 않았다 — 끝쪽 이벤트가
         *          루프마다 빠졌고, `_frameMax` 의 이벤트는 루프 중에 한 번도 뜨지 않았다. `SequenceTimelineUtil::applyPlayback` 이 이 값으로
         *          그 구간을 먼저 본다.
         */
        int32 getFrameBeforeWrap() const { return _frameBeforeWrap; }
        bool  isPlaying() const { return _bPlaying == SW_TRUE; }
        bool  isPaused() const { return _bPaused == SW_TRUE; }
        bool  isLoop() const { return _bLoop == SW_TRUE; }
        void  setLoop( bool bLoop ) { _bLoop = bLoop ? SW_TRUE : SW_FALSE; }

        void                 collectActiveItems( vector<const SequenceTrackItem*>& outListItem ) const;
        const SequenceAsset& getAsset() const { return _asset; }

    private:
        int32 computeFrame( float32 timeSeconds ) const;

        SequenceAsset          _asset;
        float32                _framesPerSecond;
        float32                _playbackTime;
        int32                  _previousFrame;
        int32                  _frameBeforeWrap; ///< `getFrameBeforeWrap`
        uint8                  _bPlaying : 1;
        uint8                  _bPaused  : 1;
        uint8                  _bLoop    : 1;
        [[maybe_unused]] uint8 _reserved : 5;
    };
} // namespace sw
