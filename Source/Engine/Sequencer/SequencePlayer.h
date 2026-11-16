/**
 * @file SequencePlayer.h
 * @brief SequenceAsset 타임라인을 프레임 단위로 재생합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Sequencer/SequenceAsset.h"

namespace sw
{
    /**
     * @class SequencePlayer
     * @brief fps 로 프레임을 진행하고 collectActiveItems 로 현재 클립과 이벤트를 찾습니다.
     */
    /**
     * @struct SequencePlaybackStep
     * @brief 갱신 한 번이 지난 프레임 구간입니다 — 이벤트 · 카메라 신호는 이 구간(`_previousFrame < 시작 <= _frame`)만 본다.
     * @details 값으로 돌려받는다(`SequencePlayer::update` · `getStartStep`). 멈춘 · 일시정지한 갱신은 빈 구간(`_previousFrame == _frame`)이라 구조상 발화할 수 없다 —
     *          예전에는 일시정지가 플레이어의 "이전 프레임" 을 그대로 두어 직전에 지난 이벤트가 매 틱 다시 났다(언리얼 Sequencer 의 빈 평가 범위 · 유니티 Signal).
     */
    struct SequencePlaybackStep
    {
        /** @brief `_frameBeforeWrap` 가 "이번 갱신은 되감지 않았다" 를 뜻할 때의 값입니다. */
        static constexpr int32 kNoLoopWrap = ( -2147483647 - 1 );
        int32                  _previousFrame{ 0 };
        int32                  _frame{ 0 };
        /**
         * @brief 이번 갱신이 루프를 되감았으면 되감기 **직전**의 프레임, 아니면 `kNoLoopWrap` 입니다.
         * @details 되감으면 이전 프레임이 `_frameMin - 1` 로 돌아가 (직전 프레임, `_frameMax`] 구간을 아무도 보지 않게 된다 — 끝쪽 이벤트가 루프마다 빠진다.
         *          `SequenceTimelineUtil::applyPlayback` 이 이 값으로 그 구간을 먼저 본다.
         */
        int32 _frameBeforeWrap{ kNoLoopWrap };
        /** @brief 아무 프레임도 지나지 않은 구간입니다. */
        [[nodiscard]] static SequencePlaybackStep makeEmpty( int32 frame ) { return SequencePlaybackStep{ frame, frame, kNoLoopWrap }; }
    };

    class SW_API SequencePlayer
    {
    public:
        static constexpr int32 kNoLoopWrap = SequencePlaybackStep::kNoLoopWrap;
        SequencePlayer();

        /** @brief JSON 시퀀스를 로드합니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 이미 파싱된 에셋을 설정합니다. */
        void setAsset( const SequenceAsset& asset );

        void play();
        void playFromFrame( int32 frame );
        void stop();
        void pause();
        void resume();
        /** @brief 시간을 나아가게 하고 이번에 지난 구간을 돌려줍니다. 재생 중이 아니거나 일시정지면 빈 구간입니다. */
        [[nodiscard]] SequencePlaybackStep update( float32 deltaSeconds );
        /** @brief `play` · `playFromFrame` 직후의 첫 적용이 볼 구간입니다(시작 프레임의 이벤트까지 — 이전 프레임은 시작보다 하나 앞). */
        [[nodiscard]] SequencePlaybackStep getStartStep() const;

        void  setFramesPerSecond( float32 fps );
        void  seekToFrame( int32 frame );
        int32 getCurrentFrame() const;
        bool  isPlaying() const { return _bPlaying == SW_TRUE; }
        bool  isPaused() const { return _bPaused == SW_TRUE; }
        bool  isLoop() const { return _bLoop == SW_TRUE; }
        void  setLoop( bool bLoop ) { _bLoop = bLoop ? SW_TRUE : SW_FALSE; }

        void                 collectActiveItems( vector<const SequenceTrackItem*>& outListItem ) const;
        const SequenceAsset& getAsset() const { return _asset; }

    private:
        int32 computeFrame( float32 timeSeconds ) const;

    private:
        SequenceAsset          _asset;
        float32                _framesPerSecond;
        float32                _playbackTime;
        int32                  _previousFrame;
        uint8                  _bPlaying : 1;
        uint8                  _bPaused  : 1;
        uint8                  _bLoop    : 1;
        [[maybe_unused]] uint8 _reserved : 5;
    };
} // namespace sw
