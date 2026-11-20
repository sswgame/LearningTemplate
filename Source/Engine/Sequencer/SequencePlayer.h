/**
 * @file SequencePlayer.h
 * @brief SequenceAsset 타임라인을 정수 틱으로 재생합니다 — 속도(음수 = 역방향) · 반복 방식 · 재생 범위 · 끝으로 건너뛰기.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/PlaybackSegment.h"

#include "Engine/Sequencer/SequenceAsset.h"

namespace sw
{
    /** @brief 시퀀스 한 프레임의 틱 수입니다. 2 · 3 · 4 · 5 · 6 · 8 로 나뉘어 반 · 3 분의 1 배속의 갱신이 정수로 떨어진다(언리얼 `FFrameTime` 의 서브프레임 자리). */
    constexpr int64 kSequenceTicksPerFrame = 960;

    /** @brief 갱신 하나가 지난 프레임 구간입니다 — 발화 규칙은 공용 커서 구간(`PlaybackSegment`)과 같다(애니메이션 알림도 같은 형식을 쓴다). */
    using SequencePlaybackSegment = PlaybackSegment<int32>;

    /** @brief 갱신의 종류입니다. 카메라 신호가 "앞에 덮던 컷" 을 어디서 볼지를 정한다. */
    enum class SequencePlaybackStepKind : uint8
    {
        Continue = 0, ///< 이어서 진행했다 — 앞 컷은 `_previousFrame` 의 컷이다
        Start,        ///< 막 틀었다 — 앞에 덮던 컷이 없다(재생 범위 밖의 프레임을 앞 컷으로 보지 않는다)
        Jump,         ///< 점프했다(`skipToEnd`) — 구간이 없어 이벤트를 쏘지 않고, 컷만 앞 프레임과 견준다
    };

    /**
     * @struct SequencePlaybackStep
     * @brief 갱신 한 번의 결과입니다 — 지난 구간(많아야 둘)과 적용할 지금 프레임입니다. 값으로 돌려받는다(`SequencePlayer::update` · `getStartStep` · `skipToEnd`).
     * @details 멈춘 · 일시정지한 갱신은 구간이 없어 구조상 발화할 수 없다(예전에는 "이전 프레임" 이 남아 직전 이벤트가 매 틱 다시 났다).
     *          구간이 둘인 것은 반복이 되감았거나(끝 구간 + 처음 구간) 왕복이 꺾였을 때(끝까지 + 돌아온 구간)다. 한 갱신에 여러 바퀴를 넘으면
     *          첫 바퀴의 끝 구간과 마지막 바퀴의 앞 구간만 본다 — 언리얼 Sequencer 도 통째로 지난 바퀴는 평가하지 않는다.
     */
    struct SequencePlaybackStep
    {
        static constexpr uint32 kMaxSegmentCount = 2;

        SequencePlaybackSegment  _arrSegment[kMaxSegmentCount]{};
        uint32                   _segmentCount{ 0 };
        int32                    _previousFrame{ 0 }; ///< 이 갱신 전의 프레임(멈춘 갱신이면 `_frame` 과 같다)
        int32                    _frame{ 0 };         ///< 적용할 지금 프레임
        SequencePlaybackStepKind _kind{ SequencePlaybackStepKind::Continue };

        /** @brief 아무 프레임도 지나지 않은 갱신입니다. */
        [[nodiscard]] static SequencePlaybackStep makeEmpty( int32 frame );
        /** @brief 빈 구간이 아니면 구간 하나를 더합니다(둘을 넘기면 단언). */
        void addSegment( int32 from, int32 to );
    };

    /**
     * @class SequencePlayer
     * @brief 시퀀스의 재생 위치를 정수 틱(`kSequenceTicksPerFrame`)으로 진행합니다. 오브젝트에 적용하는 일은 하지 않는다(`SequenceTimelineUtil`).
     * @details 위치가 정수라 반복 · 긴 재생 · 느린 속도에서도 프레임이 어긋나지 않는다(예전 `float32` 초 누적의 자리). 갱신마다 `dt × fps × |속도|` 를 틱으로 반올림해 더한다.
     *          범위 `[시작, 끝]` 의 위치 `p` 에서 프레임은 `floor(p / 960)` 이고, 시작에서 끝까지 `(끝 − 시작) / fps / |속도|` 초가 걸린다.
     */
    class SW_API SequencePlayer
    {
    public:
        SequencePlayer();

        /** @brief JSON 시퀀스를 로드합니다. 재생 범위는 그 시퀀스 전체로 돌아간다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 이미 파싱된 에셋을 설정합니다. 재생 범위는 그 시퀀스 전체로 돌아간다. */
        void setAsset( const SequenceAsset& asset );

        /** @brief 진행 방향의 처음(정방향이면 범위 시작, 역방향이면 범위 끝)에서 틉니다. 왕복이 뒤집은 방향은 처음으로 돌아간다. */
        void play();
        /** @brief 속도를 음수로 두고(크기는 그대로) 범위 끝에서 거꾸로 틉니다(언리얼 `PlayReverse`). */
        void playReverse();
        /** @brief @p frame 으로 점프해 틉니다. 그 프레임의 이벤트는 쏘지 않는다(점프). */
        void playFromFrame( int32 frame );
        void stop();
        void pause();
        void resume();
        /** @brief 시간을 나아가게 하고 이번에 지난 구간을 돌려줍니다. 재생 중이 아니거나 일시정지면 빈 갱신입니다. 반복이 아닌데 끝에 닿으면 재생을 멈춘다. */
        [[nodiscard]] SequencePlaybackStep update( float32 deltaSeconds );
        /** @brief `play` · `playReverse` 직후의 첫 적용이 볼 갱신입니다(처음 프레임의 이벤트까지 — 처음 프레임 하나 바깥에서 들어온 구간). */
        [[nodiscard]] SequencePlaybackStep getStartStep() const;
        /**
         * @brief 진행 방향의 끝 프레임으로 점프하고 재생을 멈춥니다(언리얼 `GoToEndAndStop`). 돌려준 갱신을 적용하면 끝 상태가 된다.
         * @details 점프라 사이의 이벤트는 쏘지 않는다. 재생 중이 아니어도 끝으로 옮긴다.
         */
        [[nodiscard]] SequencePlaybackStep skipToEnd();

        void setFramesPerSecond( float32 fps );
        /** @brief 재생 속도입니다. 부호가 방향이다(음수 = 역방향, 언리얼 `SetPlayRate`). 0 · 유한하지 않은 값은 오류를 남기고 거절한다(false). */
        [[nodiscard]] bool setPlayRate( float32 playRate );
        float32            getPlayRate() const { return _playRate; }
        /** @brief 지금 거꾸로 진행하는지입니다(속도 부호와 왕복이 뒤집은 방향을 함께 본다). */
        bool             isReversed() const;
        void             setLoopMode( SequenceLoopMode loopMode ) { _loopMode = loopMode; }
        SequenceLoopMode getLoopMode() const { return _loopMode; }
        /**
         * @brief 재생 범위를 정합니다(언리얼 `SetFrameRange`). 에셋의 `[_frameMin, _frameMax]` 안이고 @p startFrame < @p endFrame 이어야 한다 — 아니면 오류를 남기고 거절한다(false).
         * @details 위치가 범위 밖이면 범위 안으로 옮긴다. 에셋을 바꾸면 전체 범위로 돌아간다.
         */
        [[nodiscard]] bool setPlaybackRange( int32 startFrame, int32 endFrame );
        int32              getPlaybackStart() const { return _rangeStart; }
        int32              getPlaybackEnd() const { return _rangeEnd; }
        /** @brief @p frame 으로 점프합니다(범위로 자른다). 재생 상태는 그대로이고 이벤트를 쏘지 않는다. */
        void  seekToFrame( int32 frame );
        int32 getCurrentFrame() const { return toFrame( _positionTicks ); }
        bool  isPlaying() const { return _bPlaying == SW_TRUE; }
        bool  isPaused() const { return _bPaused == SW_TRUE; }

        void                 collectActiveItems( vector<const SequenceTrackItem*>& outListItem ) const;
        const SequenceAsset& getAsset() const { return _asset; }

    private:
        /** @brief 재생 범위를 에셋 전체로 되돌리고 멈춥니다. */
        void resetToAsset();
        /** @brief 틱 위치 @p ticks 의 프레임입니다(내림). */
        static int32 toFrame( int64 ticks );

    private:
        SequenceAsset          _asset;
        int64                  _positionTicks;
        float32                _framesPerSecond;
        float32                _playRate;
        int32                  _rangeStart;
        int32                  _rangeEnd;
        SequenceLoopMode       _loopMode;
        uint8                  _bPlaying          : 1;
        uint8                  _bPaused           : 1;
        uint8                  _bPingPongReversed : 1; ///< 왕복이 방향을 뒤집었다(속도 부호와 곱한다)
        [[maybe_unused]] uint8 _reserved          : 5;
    };
} // namespace sw
