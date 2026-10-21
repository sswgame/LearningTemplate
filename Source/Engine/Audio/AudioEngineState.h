/**
 * @file AudioEngineState.h
 * @brief `AudioEngine` 이 값 멤버로 가지는 부품입니다. 적응형 음악의 재생 위치와, 오디오 스레드가 게임 스레드에 게시하는 사본을 따로 둡니다.
 * @details 둘 다 비가상이고 할당을 늘리지 않습니다. 오디오 상태를 바꾸는 순서는 `AudioEngine` 이 정합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Audio/AudioMusic.h"
#include "Engine/Audio/AudioTypes.h"

namespace sw
{
    /** @brief 마지막 블록에서 게시한 엔진 상태입니다. */
    struct AudioEngineStats
    {
        uint64 _renderedFrameCount{ 0 };      ///< 지금까지 렌더한 프레임 수다.
        uint32 _voiceCount{ 0 };              ///< 살아 있는 보이스 수다. 실제, 가상, 클립 대기 보이스를 모두 센다.
        uint32 _realVoiceCount{ 0 };          ///< 섞은 보이스 수다.
        uint32 _virtualVoiceCount{ 0 };       ///< 위치만 진행한 보이스 수다.
        uint32 _instanceCount{ 0 };           ///< 살아 있는 이벤트 인스턴스 수다.
        uint32 _peakRealVoiceCount{ 0 };      ///< 지금까지 한 블록에 섞은 보이스 수의 최댓값이다.
        uint64 _playedEventCount{ 0 };        ///< 지금까지 낸 이벤트 수다. 인스턴스를 만든 이벤트만 센다.
        uint64 _droppedEventCount{ 0 };       ///< 쿨다운, 상한(Reject), 슬롯 부족으로 버린 이벤트 수다.
        uint32 _maxVoiceCount{ 0 };           ///< 믹서 그래프의 보이스 상한(실제 + 가상)이다. 그래프가 없으면 0 이다.
        uint32 _maxRealVoiceCount{ 0 };       ///< 믹서 그래프의 실제 보이스 상한이다. 넘는 보이스는 가상이 된다.
        uint64 _droppedSubtitleCueCount{ 0 }; ///< 게임 스레드가 가져가지 않아 상한(`AudioPublishedState::kMaxSubtitleCueCount`)을 넘겨 버린 자막 신호 수다.
        uint64 _publishNanoseconds{ 0 };      ///< 지난 블록에서 게시(`AudioEngine::publishState`)에 쓴 오디오 스레드 시간(ns)이다. 인스턴스마다 보이스를 훑는 이벤트 창 사본이 대부분이다.
        uint64 _peakPublishNanoseconds{ 0 };  ///< 지금까지 한 블록의 게시 시간 최댓값(ns)이다.
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 자막 키를 가진 클립이 실제로 시작했다는 신호입니다. 오디오 스레드가 이벤트 인스턴스를 만들 때 쌓고, 게임 스레드가 가져가 자막 서비스에 올립니다.
     * @details 쿨다운 · 동시 재생 상한으로 버려진 재생은 신호를 내지 않는다(언리얼은 활성 사운드가 시작할 때 `FSubtitleManager` 에 큐를 넣는다).
     *          키는 현지화 전의 `Namespace.Key` 이며, 해석은 받는 쪽(`UISubtitleService::postAudioCues`)이 지금 문화권으로 한다.
     */
    struct AudioSubtitleCue
    {
        hashed_string  _speakerKey{};   ///< 이벤트의 화자 키다(`AudioEventDesc::_subtitleSpeakerKey`). 비면 이름 줄을 접는다.
        hashed_string  _textKey{};      ///< 시작한 클립의 자막 키다(`AudioClipEntry::_subtitleKey`).
        AudioPlayingID _playingID{ 0 }; ///< 재생 id 다.
    };
} // namespace sw

namespace sw
{
    /** @brief 살아 있는 이벤트 인스턴스 하나의 마지막 블록 상태입니다. 에디터의 오디오 이벤트 창이 읽습니다(언리얼 오디오 디버거의 활성 사운드 목록). */
    struct AudioInstanceStatus
    {
        hashed_string  _event{};                   ///< 이벤트 이름이다.
        hashed_string  _bus{};                     ///< 이벤트가 보내는 버스다.
        AudioPlayingID _playingID{ 0 };            ///< 재생 id 다.
        float32        _elapsedSeconds{ 0.0f };    ///< 시작부터 흐른 시간(초)이다.
        float32        _remainingSeconds{ -1.0f }; ///< 가장 긴 보이스가 끝날 때까지 남은 시간(초)이다. 반복이거나 클립을 아직 모르면 -1 이다.
        uint32         _voiceCount{ 0 };           ///< 인스턴스의 보이스 수다.
        uint32         _virtualVoiceCount{ 0 };    ///< 그중 가상 보이스 수다.
        bool           _bStopping{ false };        ///< 페이드아웃으로 멈추는 중이다.
    };
} // namespace sw

namespace sw
{
    /** @brief 믹서 버스 하나의 마지막 블록 상태입니다. 에디터의 오디오 믹서 창이 읽습니다. */
    struct AudioBusStatus
    {
        hashed_string _name{};
        int32         _parentIndex{ -1 };  ///< 부모 버스의 번호(`AudioEngine::getBusStatuses` 목록 안). master 는 -1
        float32       _volumeDb{ 0.0f };   ///< 데이터 볼륨(dB)
        float32       _userVolume{ 1.0f }; ///< 사용자 볼륨(선형, [0, 1])
        float32       _gain{ 0.0f };       ///< 페이더 게인(선형) — 데이터 × 사용자 × 스냅샷 × 음소거 · 솔로
        float32       _peak{ 0.0f };       ///< 마지막 블록 출력 피크(절댓값 최대)
        bool          _bMuted{ false };
        bool          _bSolo{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 적응형 음악의 현재 위치입니다. 마지막 블록에서 게시한 값입니다. */
    struct AudioMusicStatus
    {
        hashed_string _segment{};     ///< 현재 구간이다. 전환을 기다리는 동안은 이전 구간이다.
        float64       _beat{ 0.0 };   ///< 구간 시작에서 센 박(소수)
        uint64        _bar{ 0 };      ///< 구간 시작에서 센 마디 수다.
        float32       _tempo{ 0.0f }; ///< 현재 템포(BPM)다.
        bool          _bPlaying{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 오디오 스레드가 가진 적응형 음악의 재생 위치입니다. 구간 전환 예약을 함께 담습니다. */
    struct AudioMusicPlayback
    {
        shared_ptr<const AudioMusicDesc> _pDesc{};              ///< 지금 음악이다.
        AudioPlayingID                   _playingID{ 0 };       ///< 음악의 재생 id 다.
        uint64                           _segmentStart{ 0 };    ///< 현재 구간의 0 박이 있는 렌더 프레임이다.
        uint64                           _pendingFrame{ 0 };    ///< 예약된 전환의 렌더 프레임이다.
        int32                            _segment{ -1 };        ///< 지금 구간이다(-1 = 없음).
        int32                            _pendingSegment{ -1 }; ///< 예약된 다음 구간이다(-1 = 없음).

        /** @brief @p renderedFrameCount 시각의 음악 위치를 만듭니다. 음악이 없으면 재생하지 않는 상태입니다. */
        AudioMusicStatus makeStatus( uint64 renderedFrameCount ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 오디오 스레드가 블록마다 게시하고 게임 스레드가 읽는 사본입니다. 자기 잠금으로 지킵니다. */
    class AudioPublishedState
    {
    public:
        /** @brief 게임 스레드가 가져가지 않은 자막 신호를 쌓아 두는 상한입니다. 넘으면 가장 오래된 것을 버리고 `_droppedSubtitleCueCount` 에 셉니다(자막을 보이는 UI 가 없는 헤드리스 구성). */
        static constexpr uint32 kMaxSubtitleCueCount = 32;

        /** @brief 게시한 재생 목록과 상태를 비웁니다. */
        void clear();
        /**
         * @brief 새 사본을 게시합니다. @p fillPlaying 이 잠금 안에서 재생 id 를 채우고 보이스 수를 반환합니다.
         * @details 목록은 채운 뒤 정렬하고, 적용한 재생 id 는 맨 끝에 release 로 씁니다.
         */
        template <typename FillFunc>
        void publish( AudioEngineStats stats, const AudioMusicStatus& music, uint64 appliedPlayingID, FillFunc&& fillPlaying )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _listPlaying.clear();
            stats._voiceCount = fillPlaying( _listPlaying );
            std::sort( _listPlaying.begin(), _listPlaying.end() );
            _stats = stats;
            _music = music;
            _appliedPlayingID.store( appliedPlayingID, std::memory_order_release );
        }
        /** @brief 믹서 버스의 상태를 게시합니다. @p fillBus 가 잠금 안에서 목록(비어 있는 재사용 버퍼)을 채웁니다. */
        template <typename FillFunc>
        void publishBuses( FillFunc&& fillBus )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _listBusStatus.clear();
            fillBus( _listBusStatus );
        }
        /** @brief 이벤트 인스턴스의 상태를 게시합니다. @p fillInstance 가 잠금 안에서 목록(비어 있는 재사용 버퍼)을 채웁니다. */
        template <typename FillFunc>
        void publishInstances( FillFunc&& fillInstance )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _listInstanceStatus.clear();
            fillInstance( _listInstanceStatus );
        }
        /** @brief 재생이 살아 있는지 반환합니다. 아직 적용하지 않은 id 면 @p nextPlayingID 보다 작을 때(발급한 id 일 때) true 입니다. */
        bool isPlaying( AudioPlayingID playingID, uint64 nextPlayingID ) const;
        /** @brief 마지막 블록의 상태입니다. */
        AudioEngineStats getStats() const;
        /** @brief 마지막 블록의 음악 위치입니다. */
        AudioMusicStatus getMusicStatus() const;
        /** @brief 마지막 블록의 버스 상태를 @p outListStatus 에 복사합니다(비우고 채웁니다). 아직 게시한 것이 없으면 비어 있습니다. */
        void getBusStatuses( vector<AudioBusStatus>& outListStatus ) const;
        /** @brief 마지막 블록의 이벤트 인스턴스 상태를 @p outListStatus 에 복사합니다(비우고 채웁니다). */
        void getInstanceStatuses( vector<AudioInstanceStatus>& outListStatus ) const;
        /** @brief 자막 신호 하나를 쌓습니다(오디오 스레드). 상한을 넘으면 가장 오래된 것을 버리고 셉니다. */
        void addSubtitleCue( const AudioSubtitleCue& cue );
        /** @brief 쌓인 자막 신호를 시작한 순서대로 @p outListCue 에 옮기고 비웁니다(게임 스레드). */
        void takeSubtitleCues( vector<AudioSubtitleCue>& outListCue );

    private:
        vector<AudioPlayingID>      _listPlaying{};                /**< 살아 있는 재생 id 입니다. 정렬되어 있습니다. */
        vector<AudioBusStatus>      _listBusStatus{};              /**< 버스 상태입니다(믹서 그래프 순서). */
        vector<AudioInstanceStatus> _listInstanceStatus{};         /**< 살아 있는 이벤트 인스턴스 상태입니다(인스턴스 슬롯 순서). */
        vector<AudioSubtitleCue>    _listSubtitleCue{};            /**< 게임 스레드가 아직 가져가지 않은 자막 신호입니다(시작한 순서). */
        uint64                      _droppedSubtitleCueCount{ 0 }; /**< 상한을 넘겨 버린 자막 신호 수입니다. `getStats` 가 붙입니다. */
        AudioEngineStats            _stats{};                      /**< 마지막 블록의 상태입니다. */
        AudioMusicStatus            _music{};                      /**< 마지막 블록의 음악 위치입니다. */
        atomic<uint64>              _appliedPlayingID{ 0 };        /**< 게시 시점에 적용된 재생 id 입니다. */
        mutable mutex               _mutex{};                      /**< 위 사본을 지킵니다. */
    };
} // namespace sw
