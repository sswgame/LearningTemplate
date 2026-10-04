/**
 * @file AudioEngine.h
 * @brief 플랫폼과 무관한 오디오 엔진 — 명령 큐 · 보이스 · 믹서 그래프 · 이벤트 · 파라미터 · 스냅샷 · 공간화를 들고, 출력 장치가 당겨 가는 `render` 하나로 소리를 만듭니다.
 * @details **스레드.** 게임 스레드는 명령만 쌓고(`postEvent` · `playClip` · `setParameter` …), 오디오 스레드(장치 콜백 — 장치가 없으면 게임 스레드의
 *          `IAudioSystem::update`)가 `render` 에서 블록마다 명령을 꺼내 적용합니다. 오디오 상태는 오디오 스레드만 만지고, 게임 스레드가 읽는 것
 *          (재생 중인지 · 보이스 수)은 블록마다 게시한 사본입니다.
 *
 *          **블록 순서.** 명령 → 파라미터(seek) → 스냅샷 세기 · 적용 → 에미터 가림 추종 → 보이스 목표(볼륨 · 팬 · 피치 · 로우패스, 들림 정도)
 *          → 실제/가상 보이스 고르기(우선순위 · 들림) → 섞기(가상은 위치만) → 버스 그래프 → 게시.
 *
 *          **결정성.** 같은 명령 · 같은 씨앗(`setRandomSeed`)이면 `render` 가 같은 샘플을 냅니다 — 장치 없이 버퍼에 렌더해 숫자로 재는 시험이
 *          이것에 기댑니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/AudioMusic.h"
#include "Engine/Audio/AudioSpatial.h"
#include "Engine/Audio/AudioTypes.h"
#include "Engine/Audio/AudioVoice.h"

namespace sw
{
    struct AudioMixerDesc;
    struct AudioSnapshotDesc;

    class AudioMixer;

    /** @brief 클립 하나를 바로 재생할 때의 설정입니다(이벤트 없이 — `IAudioSystem::play` · `playMusic`). */
    struct AudioClipPlayParams
    {
        hashed_string    _attenuation{};          ///< 감쇠 프리셋 이름 — 비었거나 에미터가 0 이면 공간화하지 않는다(2D)
        AudioEmitterId   _emitterId{ 0 };         ///< 소리 내는 자리
        float32          _volumeDb{ 0.0f };       ///< 볼륨(dB)
        float32          _pitchSemitones{ 0.0f }; ///< 피치(반음)
        float32          _pan{ 0.0f };            ///< 팬 [-1, 1]
        float32          _fadeInSeconds{ 0.0f };  ///< 0 에서 오르는 시간(초)
        int32            _priority{ 50 };         ///< 보이스가 모자랄 때 높은 쪽이 남는다
        AudioVirtualMode _virtual{ AudioVirtualMode::Virtualize };
        bool             _bLoop{ false }; ///< 끝에서 처음으로 돌아간다
    };
} // namespace sw

namespace sw
{
    /** @brief 마지막 블록에서 게시한 엔진 상태입니다. */
    struct AudioEngineStats
    {
        uint64 _renderedFrameCount{ 0 }; ///< 지금까지 렌더한 프레임
        uint32 _voiceCount{ 0 };         ///< 살아 있는 보이스(실제 + 가상 + 클립 대기)
        uint32 _realVoiceCount{ 0 };     ///< 섞은 보이스
        uint32 _virtualVoiceCount{ 0 };  ///< 위치만 진행한 보이스
        uint32 _instanceCount{ 0 };      ///< 살아 있는 이벤트 인스턴스
        uint32 _peakRealVoiceCount{ 0 }; ///< 지금까지 한 블록에 섞은 보이스의 최대
        uint64 _playedEventCount{ 0 };   ///< 지금까지 낸 이벤트(인스턴스를 만든 것)
        uint64 _droppedEventCount{ 0 };  ///< 쿨다운 · 상한(Reject) · 칸 부족으로 버린 이벤트
    };
} // namespace sw

namespace sw
{
    /** @brief 적응형 음악의 지금 자리입니다(마지막 블록에서 게시). */
    struct AudioMusicStatus
    {
        hashed_string _segment{};     ///< 지금 구간(전환을 기다리는 동안은 옛 구간)
        float64       _beat{ 0.0 };   ///< 구간 시작에서 센 박(소수)
        uint64        _bar{ 0 };      ///< 구간 시작에서 센 마디
        float32       _tempo{ 0.0f }; ///< 지금 템포(BPM)
        bool          _bPlaying{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AudioEngine
     * @brief 오디오 엔진 하나입니다. `IAudioSystem` 이 하나를 들고, 시험은 장치 없이 직접 만들어 `render` 로 버퍼에 렌더합니다.
     */
    class SW_API AudioEngine
    {
    public:
        /** @brief 리스너 칸 수입니다(분할 화면 넷). */
        static constexpr uint32 kMaxListenerCount = 4;

        AudioEngine();
        ~AudioEngine();

        AudioEngine( const AudioEngine& )            = delete;
        AudioEngine& operator=( const AudioEngine& ) = delete;

        /** @brief 믹서 그래프를 만들고 받을 준비를 합니다. 그래프가 검사를 통과하지 못하면 false 입니다. */
        [[nodiscard]] bool initialize( const AudioMixerDesc& mixerDesc );
        /** @brief 보이스와 그래프를 놓습니다. 장치가 `render` 를 더 부르지 않게 한 뒤에 부릅니다. */
        void shutdown();
        /** @brief 초기화되었는지입니다. */
        bool isInitialized() const { return _bInitialized.load( std::memory_order_acquire ); }

        // --- 게임 스레드: 데이터 ----------------------------------------------------------------------------------------

        /**
         * @brief 믹서 그래프를 바꿉니다. 그래프는 여기서 만들고 다음 블록에 들어갑니다(살아 있는 보이스는 같은 이름의 버스로 옮겨 갑니다).
         * @return 그래프가 검사를 통과하지 못하면 false 이고 지금 그래프를 그대로 씁니다.
         */
        [[nodiscard]] bool loadMixer( const AudioMixerDesc& mixerDesc );
        /**
         * @brief 이벤트 라이브러리를 이름(보통 리소스 경로)으로 올립니다. 같은 이름이 있으면 바꿉니다(핫 리로드). 클립 디코드를 요청해 둡니다.
         * @return 이벤트의 버스 · 감쇠가 지금 그래프 · 라이브러리에 없거나, 다른 라이브러리와 이벤트 · 파라미터 이름이 겹치면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool loadEventLibrary( const hashed_string& libraryName, const AudioEventLibrary& library );
        /** @brief 라이브러리를 내립니다. 재생 중인 인스턴스는 끝까지 갑니다. */
        void unloadEventLibrary( const hashed_string& libraryName );
        /** @brief 올린 라이브러리에 그 이벤트가 있는지입니다. */
        bool hasEvent( const hashed_string& eventName ) const;
        /** @brief 클립 캐시입니다. */
        AudioClipStore& getClipStore() { return _clipStore; }
        /** @brief 컨테이너 · 범위가 쓰는 난수의 씨앗입니다(시험의 결정성). 다음 블록부터 적용됩니다. */
        void setRandomSeed( uint64 seed );

        // --- 게임 스레드: 재생 ------------------------------------------------------------------------------------------

        /**
         * @brief 이벤트를 냅니다 — 컨테이너가 클립을 고르고, 범위 · 쿨다운 · 동시 재생 상한 · 우선순위를 데이터대로 적용합니다.
         * @param emitterId 0 이면 공간화하지 않습니다(UI · 2D).
         * @return 재생 id 입니다. 모르는 이벤트면 0 이고 경고를 남깁니다. 쿨다운 · 상한으로 버려진 재생은 다음 블록부터 `isPlaying` 이 false 입니다.
         */
        AudioPlayingId postEvent( const hashed_string& eventName, AudioEmitterId emitterId = 0 );
        /**
         * @brief 클립을 버스 하나로 재생합니다. 클립이 캐시에 없으면 디코드를 요청하고, 준비되면 시작합니다.
         * @param bus 비었거나 모르는 이름이면 `sfx` 입니다.
         * @return 재생 id 입니다. 경로가 비었으면 0 입니다.
         */
        AudioPlayingId playClip( const hashed_string& path, const hashed_string& bus, const AudioClipPlayParams& params );
        /** @brief 재생을 멈춥니다. @p fadeSeconds 동안 줄인 뒤 멈춥니다(0 이면 바로, 음수면 이벤트의 `_fadeOutSeconds`). */
        void stop( AudioPlayingId playingId, float32 fadeSeconds );
        /** @brief 에미터의 재생을 모두 멈춥니다(음수 페이드는 이벤트의 값). */
        void stopEmitter( AudioEmitterId emitterId, float32 fadeSeconds );
        /** @brief 재생을 일시정지 · 재개합니다. */
        void setPaused( AudioPlayingId playingId, bool bPaused );

        // --- 게임 스레드: 적응형 음악 ------------------------------------------------------------------------------------

        /**
         * @brief 적응형 음악을 시작합니다(시작 구간부터, 지금 음악은 @p fadeSeconds 로 빠짐). 레이어 클립의 디코드를 요청해 둡니다.
         * @return 음악의 재생 id 입니다(구간이 바뀌어도 같다). 서술이 nullptr 이면 0 입니다.
         */
        AudioPlayingId startMusic( shared_ptr<const AudioMusicDesc> pMusic, float32 fadeSeconds );
        /** @brief 구간을 바꿉니다 — 전환 규칙의 맞춤 지점(박 · 마디 · 구간 끝)에서 샘플 단위로 바꾸고 스팅어를 냅니다. 같은 구간이면 아무것도 하지 않습니다. */
        void setMusicSegment( const hashed_string& segment );
        /** @brief 음악을 @p fadeSeconds 동안 줄여 멈춥니다. */
        void stopMusic( float32 fadeSeconds );
        /** @brief 음악의 지금 자리(구간 · 박 · 마디)입니다. */
        AudioMusicStatus getMusicStatus() const;

        // --- 게임 스레드: 믹스 ------------------------------------------------------------------------------------------

        /** @brief 버스의 사용자 볼륨(선형, [0, 1])입니다 — 설정 메뉴가 씁니다. 그래프를 바꿔도 이름으로 남습니다. */
        void setBusUserVolume( const hashed_string& bus, float32 volume );
        /** @brief 버스를 음소거합니다. */
        void setBusMuted( const hashed_string& bus, bool bMuted );
        /** @brief 버스를 솔로로 둡니다. */
        void setBusSolo( const hashed_string& bus, bool bSolo );
        /** @brief 게임 파라미터(RTPC)를 정합니다. 라이브러리의 범위로 묶고 `_seekSpeed` 로 따라갑니다. */
        void setParameter( const hashed_string& name, float32 value );
        /** @brief 에미터 하나에만 걸리는 파라미터 값입니다(전역 값보다 앞선다). */
        void setEmitterParameter( AudioEmitterId emitterId, const hashed_string& name, float32 value );
        /** @brief 스냅샷을 켭니다 — `_fadeInSeconds` 동안 세기 1 로. 이미 켜져 있으면 가장 나중에 켠 것으로 순서만 바꿉니다. */
        void startSnapshot( const hashed_string& name );
        /** @brief 스냅샷을 끕니다 — `_fadeOutSeconds` 동안 세기 0 으로. */
        void stopSnapshot( const hashed_string& name );
        /** @brief 스냅샷 세기를 직접 정합니다(0..1, 페이드 없음) — 리버브 존 경계 블렌드처럼 게임 값이 세기일 때. */
        void setSnapshotIntensity( const hashed_string& name, float32 intensity );

        // --- 게임 스레드: 공간 ------------------------------------------------------------------------------------------

        /**
         * @brief 리스너를 정합니다(보통 게임 카메라). 여럿이면(분할 화면) 보이스마다 가장 크게 들리는 리스너로 공간화합니다.
         * @param listenerIndex `kMaxListenerCount` 보다 작아야 합니다. `_bActive` 가 false 면 그 리스너를 끕니다.
         */
        void setListener( uint32 listenerIndex, const AudioListenerState& state );
        /** @brief 에미터의 자리 · 속도를 정합니다(처음이면 만듭니다). */
        void setEmitter( AudioEmitterId emitterId, const float3& position, const float3& velocity );
        /** @brief 에미터의 가림 값(0..1)을 정합니다 — 엔진이 `AudioOcclusionDesc::_smoothingSeconds` 로 따라갑니다. */
        void setEmitterOcclusion( AudioEmitterId emitterId, float32 occlusion );
        /** @brief 에미터를 지웁니다. 그 자리의 소리는 마지막 자리에 남아 끝까지 재생합니다. */
        void removeEmitter( AudioEmitterId emitterId );

        // --- 게임 스레드: 읽기 ------------------------------------------------------------------------------------------

        /** @brief 재생이 아직 살아 있는지입니다(디코드 대기 · 가상 포함). 명령이 아직 처리되지 않았으면 true 입니다. */
        bool isPlaying( AudioPlayingId playingId ) const;
        /** @brief 마지막 블록의 상태입니다. */
        AudioEngineStats getStats() const;

        // --- 오디오 스레드 --------------------------------------------------------------------------------------------------

        /**
         * @brief @p frameCount 프레임을 렌더해 @p pOutput(스테레오 교차 float32, [-1, 1])에 씁니다. 장치 콜백이 부릅니다.
         * @details 안에서는 블록(`audio::kBlockFrameCount`) 단위로 돌고, 블록을 시작할 때 쌓인 명령을 적용합니다. 초기화 전이면 0 을 씁니다.
         */
        void render( float32* pOutput, uint32 frameCount );
        /** @brief 버스 하나의 마지막 블록 피크입니다(오디오 스레드 · 시험). 없는 버스면 0 입니다. */
        float32 getBusPeak( const hashed_string& bus ) const;
        /** @brief 파라미터의 지금 값(seek 뒤)입니다(오디오 스레드 · 시험). 모르면 0 입니다. */
        float32 getParameterValue( const hashed_string& name ) const;
        /** @brief 스냅샷의 지금 세기입니다(오디오 스레드 · 시험). */
        float32 getSnapshotIntensity( const hashed_string& name ) const;
        /** @brief 지금 그래프입니다(오디오 스레드 · 시험). */
        const AudioMixer* getMixer() const { return _pMixer.get(); }

    private:
        /** @brief 명령 종류입니다. */
        enum class CommandType : uint8
        {
            PlayClip,
            PostEvent,
            Stop,
            StopEmitter,
            SetPaused,
            SetBusUserVolume,
            SetBusMuted,
            SetBusSolo,
            SwapMixer,
            SetLibrary,
            SetRandomSeed,
            SetListener,
            SetEmitter,
            SetEmitterOcclusion,
            SetEmitterParameter,
            RemoveEmitter,
            SetParameter,
            StartSnapshot,
            StopSnapshot,
            SetSnapshotIntensity,
            StartMusic,
            SetMusicSegment,
            StopMusic,
        };

        /** @brief 게임 스레드가 쌓는 명령 하나입니다. 종류마다 쓰는 칸만 채웁니다. */
        struct Command
        {
            shared_ptr<AudioMixer>              _pMixer{};     ///< SwapMixer
            shared_ptr<const AudioEventLibrary> _pLibrary{};   ///< SetLibrary(nullptr 이면 내림)
            shared_ptr<const AudioMusicDesc>    _pMusic{};     ///< StartMusic
            hashed_string                       _name{};       ///< 버스 · 이벤트 · 파라미터 · 스냅샷 · 라이브러리
            hashed_string                       _path{};       ///< 클립 경로
            AudioClipPlayParams                 _clipParams{}; ///< PlayClip
            AudioListenerState                  _listener{};   ///< SetListener
            float3                              _position{};   ///< SetEmitter
            float3                              _velocity{};   ///< SetEmitter
            AudioPlayingId                      _playingId{ 0 };
            AudioEmitterId                      _emitterId{ 0 };
            uint64                              _seed{ 0 };     ///< SetRandomSeed
            float32                             _value{ 0.0f }; ///< 볼륨 · 페이드 · 파라미터 · 세기
            uint32                              _index{ 0 };    ///< 리스너 번호
            CommandType                         _type{ CommandType::PlayClip };
            bool                                _bFlag{ false }; ///< 음소거 · 솔로 · 일시정지
        };

        /** @brief 버스 이름에 남는 사용자 상태입니다(그래프를 바꾸면 다시 건다). */
        struct BusUserState
        {
            hashed_string _bus{};
            float32       _volume{ 1.0f };
            bool          _bMuted{ false };
            bool          _bSolo{ false };
        };

        /** @brief 에미터 파라미터 값 하나입니다. */
        struct EmitterParameter
        {
            hashed_string _name{};
            float32       _value{ 0.0f };
        };

        /** @brief 에미터 하나 — 공간 상태와 에미터 파라미터입니다. */
        struct EmitterRecord
        {
            AudioEmitterState        _state{};
            vector<EmitterParameter> _listParameter{};
            bool                     _bRemoved{ false }; ///< 지워 달라고 했다 — 이 자리의 보이스가 끝나면 지운다
        };

        /** @brief 파라미터 하나의 실행 상태입니다. */
        struct ParameterState
        {
            float32 _value{ 0.0f };
            float32 _target{ 0.0f };
            float32 _minValue{ -1.0e30f };
            float32 _maxValue{ 1.0e30f };
            float32 _seekSpeed{ 0.0f };
        };

        /** @brief 이벤트 하나의 실행 상태입니다(이름으로 남는다 — 라이브러리를 바꿔도 쿨다운 · 순서 커서가 이어진다). */
        struct EventState
        {
            shared_ptr<const AudioEventLibrary> _pLibrary{};
            const AudioEventDesc*               _pDesc{ nullptr };
            uint64                              _lastPostFrame{ 0 };
            uint32                              _sequenceCursor{ 0 };
            int32                               _lastRandomIndex{ -1 };
            bool                                _bPostedOnce{ false };
        };

        /** @brief 이벤트 인스턴스 하나(재생 id 하나)입니다. */
        struct EventInstance
        {
            shared_ptr<const AudioEventLibrary> _pLibrary{}; ///< 서술이 사는 라이브러리(내려도 인스턴스가 쥔다)
            const AudioEventDesc*               _pDesc{ nullptr };
            AudioPlayingId                      _playingId{ 0 };
            AudioEmitterId                      _emitterId{ 0 };
            uint64                              _startFrame{ 0 };
            float32                             _audibility{ 0.0f }; ///< 지난 블록 보이스 목표 게인의 최대
            float32                             _distance{ 0.0f };   ///< 지난 블록의 리스너 거리
            bool                                _bInUse{ false };
            bool                                _bStopping{ false }; ///< 뺏겨 페이드아웃 중 — 상한 셈에서 뺀다
        };

        /** @brief 스냅샷 하나의 실행 상태입니다(켠 순서 목록). */
        struct SnapshotState
        {
            hashed_string            _name{};
            const AudioSnapshotDesc* _pDesc{ nullptr };
            float32                  _intensity{ 0.0f };
            float32                  _target{ 0.0f };
            bool                     _bDriven{ false }; ///< 게임이 세기를 직접 정한다
        };

        /** @brief 보이스 칸 하나입니다. */
        struct VoiceSlot
        {
            AudioVoice       _voice{};
            hashed_string    _clipPath{};
            hashed_string    _busName{};
            hashed_string    _attenuationName{};
            AudioPlayingId   _playingId{ 0 };
            AudioEmitterId   _emitterId{ 0 };
            float32          _volume{ 1.0f }; ///< 기본 볼륨(선형)
            float32          _pan{ 0.0f };
            float32          _pitchRatio{ 1.0f };
            float32          _fadeInSeconds{ 0.0f };
            float32          _audibility{ 0.0f }; ///< 이번 블록 목표 게인(버스 페이더는 넣지 않는다)
            float32          _distance{ 0.0f };
            uint32           _busIndex{ 0 };
            int32            _attenuationIndex{ -1 }; ///< 감쇠 표 번호(-1 = 2D)
            int32            _instanceIndex{ -1 };    ///< 이벤트 인스턴스(-1 = 클립 재생)
            int32            _priority{ 50 };
            int32            _musicSegment{ -1 }; ///< 음악 레이어의 구간(-1 = 음악 아님)
            int32            _musicLayer{ -1 };   ///< 음악 레이어 번호
            float32          _musicGain{ 1.0f };  ///< 파라미터로 페이드하는 레이어 게인
            uint64           _startFrame{ 0 };    ///< 음악: 소리가 시작할 렌더 프레임(클립이 늦게 와도 박을 지킨다)
            AudioVirtualMode _virtualMode{ AudioVirtualMode::Virtualize };
            bool             _bLoop{ false };
            bool             _bInUse{ false };
            bool             _bWaitingForClip{ false }; ///< 클립 디코드를 기다린다
            bool             _bPausedRequest{ false };  ///< 클립을 기다리는 동안 들어온 일시정지
            bool             _bVirtual{ false };        ///< 이번 블록에 섞지 않는다
        };

        // AudioEngine.cpp — 명령 · 보이스 · 렌더
        /** @brief 쌓인 명령을 꺼내 적용합니다. */
        void applyCommands();
        /** @brief 명령 하나를 적용합니다. */
        void applyCommand( Command& command );
        /** @brief 블록 하나를 렌더해 `_listBlockOutput` 에 둡니다. */
        void renderBlock();
        /** @brief 빈 보이스 칸을 잡습니다. 없으면 nullptr 입니다. */
        VoiceSlot* allocateVoice();
        /** @brief 보이스 칸을 돌려줍니다. */
        void freeVoice( VoiceSlot& slot );
        /** @brief 재생 id(또는 에미터)의 보이스를 멈춥니다(@p fadeSeconds 음수면 이벤트 값). */
        void stopVoices( AudioPlayingId playingId, AudioEmitterId emitterId, float32 fadeSeconds );
        /** @brief 클립을 기다리는 칸에 클립이 왔으면 시작합니다. 실패한 클립의 칸은 돌려줍니다. */
        void startWaitingVoice( VoiceSlot& slot );
        /** @brief 버스 이름을 지금 그래프의 번호로 풉니다(없으면 sfx, 그것도 없으면 master). */
        uint32 resolveBusIndex( const hashed_string& bus ) const;
        /** @brief 사용자 상태(볼륨 · 음소거 · 솔로)를 지금 그래프에 겁니다. */
        void applyBusUserState( const BusUserState& state );
        /** @brief 이름의 사용자 상태 칸입니다(없으면 만듭니다). */
        BusUserState& findOrAddBusUserState( const hashed_string& bus );
        /** @brief 에미터의 가림 값을 이 블록만큼 목표로 따라가게 합니다. */
        void updateEmitters();
        /** @brief 보이스 하나의 이번 블록 목표(볼륨 · 팬 · 피치 · 로우패스 · 들림)를 정합니다. */
        void updateVoiceTargets( VoiceSlot& slot );
        /** @brief 지워 달라고 한 에미터 중 보이스가 남지 않은 것을 지웁니다. */
        void sweepRemovedEmitters();
        /** @brief 게임 스레드가 읽을 상태를 게시합니다. */
        void publishState( uint32 realCount, uint32 virtualCount );
        /** @brief 명령을 쌓습니다. */
        void pushCommand( Command&& command );

        // AudioEngineEvents.cpp — 이벤트 · 파라미터 · 스냅샷 · 보이스 제한
        /** @brief 이벤트를 냅니다(쿨다운 · 상한 · 컨테이너). */
        void applyPostEvent( const Command& command );
        /** @brief 라이브러리를 바꾸거나 내리고 이벤트 · 감쇠 표를 다시 짓습니다. */
        void applySetLibrary( const Command& command );
        /** @brief 감쇠 표(그래프 + 라이브러리)를 다시 짓고 보이스의 번호를 이름으로 다시 풉니다. */
        void rebuildAttenuationTable();
        /** @brief 이벤트 이름 표를 다시 짓습니다(이름으로 남은 상태는 이어 간다). */
        void rebuildEventTable();
        /** @brief 감쇠 프리셋 이름의 표 번호입니다(-1 = 없음). */
        int32 resolveAttenuationIndex( const hashed_string& name ) const;
        /** @brief 파라미터를 이 블록만큼 목표로 옮깁니다. */
        void updateParameters();
        /** @brief 이 에미터에서 본 파라미터 값입니다(에미터 값 → 전역 값 → 0). */
        float32 findParameterValue( AudioEmitterId emitterId, const hashed_string& name ) const;
        /** @brief 스냅샷 세기를 옮기고 버스 오프셋 · 센드 · 이펙트 파라미터에 겁니다. */
        void updateSnapshots();
        /** @brief 그래프가 바뀐 뒤 스냅샷 서술을 이름으로 다시 풉니다. */
        void rebindSnapshots();
        /** @brief 스냅샷 칸입니다(없으면 만듭니다 — 그래프에 없는 이름이면 nullptr). */
        SnapshotState* findOrAddSnapshot( const hashed_string& name );
        /** @brief 실제로 섞을 보이스를 고르고(우선순위 · 들림) 나머지는 가상 · 정지로 둡니다. */
        void selectRealVoices();
        /** @brief 인스턴스의 들림 · 거리를 모으고 보이스가 없는 인스턴스를 비웁니다. */
        void updateInstances();
        /** @brief 빈 인스턴스 칸을 잡습니다. */
        int32 allocateInstance();

        // AudioEngineMusic.cpp — 적응형 음악
        /** @brief 음악을 시작합니다(지금 블록에서 시작 구간). */
        void applyStartMusic( const Command& command );
        /** @brief 구간 전환을 맞춤 지점에 예약합니다(@p bAtSegmentEnd 면 규칙과 상관없이 구간 끝 — 루프하지 않는 구간의 다음). */
        void applySetMusicSegment( const hashed_string& segment, bool bAtSegmentEnd );
        /** @brief 음악 보이스를 @p fadeSeconds 로 멈춥니다. */
        void applyStopMusic( float32 fadeSeconds );
        /** @brief 예약된 전환의 시각이 오면 구간을 넘기고, 루프하지 않는 구간의 끝을 다음 구간으로 잇고, 레이어 게인을 파라미터로 옮깁니다. */
        void updateMusic();
        /** @brief 지금 구간에서 @p sync 의 다음 맞춤 지점(렌더 프레임)입니다. */
        uint64 computeMusicSyncFrame( AudioMusicSync sync ) const;
        /** @brief 구간의 레이어 보이스를 @p startFrame 에 시작하도록 만듭니다. */
        void scheduleMusicSegment( int32 segmentIndex, uint64 startFrame, float32 fadeInSeconds );
        /** @brief 레이어의 목표 게인(선형)입니다 — 볼륨 + 파라미터 곡선. */
        float32 computeMusicLayerGain( int32 segmentIndex, int32 layerIndex ) const;

    private:
        using LibraryMap   = unordered_map<hashed_string, shared_ptr<const AudioEventLibrary>, hashed_string::HashFunc>;
        using EventMap     = unordered_map<hashed_string, EventState, hashed_string::HashFunc>;
        using ParameterMap = unordered_map<hashed_string, ParameterState, hashed_string::HashFunc>;

        AudioClipStore _clipStore; /**< 경로 → 클립 캐시입니다. */
        // 게임 스레드
        vector<Command>                  _listPendingCommand;  /**< 쌓인 명령입니다(`_commandMutex`). */
        vector<Command>                  _listApplyingCommand; /**< 오디오 스레드가 꺼내 적용 중인 명령입니다. */
        LibraryMap                       _mapGameLibrary;      /**< 게임 스레드가 아는 라이브러리입니다. */
        shared_ptr<const AudioMixerDesc> _pGameMixerDesc;      /**< 게임 스레드가 아는 그래프입니다(이름 대조). */
        mutex                            _commandMutex;        /**< `_listPendingCommand` 를 지킵니다. */
        mutable mutex                    _libraryMutex;        /**< `_mapGameLibrary` · `_pGameMixerDesc` 를 지킵니다(이벤트는 병렬 틱에서도 낸다). */
        atomic<uint64>                   _nextPlayingId;       /**< 다음 재생 id 입니다. */
        // 오디오 스레드
        shared_ptr<AudioMixer>                       _pMixer;                         /**< 지금 그래프입니다. */
        vector<VoiceSlot>                            _listVoice;                      /**< 보이스 칸입니다. */
        vector<uint32>                               _listVoiceOrder;                 /**< 보이스 고르기 작업 목록입니다. */
        vector<EventInstance>                        _listInstance;                   /**< 이벤트 인스턴스 칸입니다. */
        vector<BusUserState>                         _listBusUserState;               /**< 버스 이름의 사용자 상태입니다. */
        vector<float32>                              _listBlockOutput;                /**< 마지막 블록의 출력입니다(스테레오 교차). */
        vector<float32>                              _listVoiceScratch;               /**< 보이스 로우패스 작업 버퍼입니다(블록 길이). */
        vector<AudioAttenuationDesc>                 _listAttenuation;                /**< 감쇠 표(그래프 프리셋 + 라이브러리 프리셋)입니다. */
        vector<SnapshotState>                        _listSnapshot;                   /**< 켠 스냅샷(켠 순서)입니다. */
        shared_ptr<const AudioMusicDesc>             _pMusic;                         /**< 지금 음악입니다. */
        AudioPlayingId                               _musicPlayingId;                 /**< 음악의 재생 id 입니다. */
        uint64                                       _musicSegmentStart;              /**< 지금 구간의 0 박이 든 렌더 프레임입니다. */
        uint64                                       _musicPendingFrame;              /**< 예약된 전환의 렌더 프레임입니다. */
        int32                                        _musicSegment;                   /**< 지금 구간입니다(-1 = 없음). */
        int32                                        _musicPendingSegment;            /**< 예약된 다음 구간입니다(-1 = 없음). */
        LibraryMap                                   _mapLibrary;                     /**< 올린 라이브러리입니다. */
        EventMap                                     _mapEventState;                  /**< 이벤트 이름 → 상태입니다. */
        ParameterMap                                 _mapParameter;                   /**< 파라미터 이름 → 상태입니다. */
        unordered_map<AudioEmitterId, EmitterRecord> _mapEmitter;                     /**< 에미터 id → 상태입니다. */
        AudioListenerState                           _arrListener[kMaxListenerCount]; /**< 리스너입니다. */
        AudioRandom                                  _random;                         /**< 컨테이너 · 범위 난수입니다. */
        uint64                                       _renderedFrameCount;             /**< 지금까지 렌더한 프레임입니다. */
        uint64                                       _appliedPlayingId;               /**< 적용한 재생 명령의 가장 큰 id 입니다. */
        uint64                                       _playedEventCount;               /**< 낸 이벤트 수입니다(게시용). */
        uint64                                       _droppedEventCount;              /**< 버린 이벤트 수입니다(게시용). */
        uint32                                       _peakRealVoiceCount;             /**< 한 블록에 섞은 보이스의 최대입니다(게시용). */
        uint32                                       _blockCursor;                    /**< `_listBlockOutput` 에서 다음에 내보낼 프레임입니다. */
        // 게시(오디오 스레드 → 게임 스레드)
        vector<AudioPlayingId> _listPublishedPlaying; /**< 살아 있는 재생 id(정렬)입니다. */
        AudioEngineStats       _publishedStats;       /**< 마지막 블록의 상태입니다. */
        AudioMusicStatus       _publishedMusic;       /**< 마지막 블록의 음악 자리입니다. */
        atomic<uint64>         _publishedAppliedId;   /**< 게시 시점에 적용된 재생 id 입니다. */
        mutable mutex          _publishMutex;         /**< 게시 사본을 지킵니다. */
        atomic<bool>           _bInitialized;         /**< 초기화되었는지입니다. */
    };
} // namespace sw
