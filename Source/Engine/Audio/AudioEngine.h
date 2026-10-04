/**
 * @file AudioEngine.h
 * @brief 플랫폼과 무관한 오디오 엔진 — 명령 큐 · 보이스 · 믹서 그래프를 들고, 출력 장치가 당겨 가는 `render` 하나로 소리를 만듭니다.
 * @details **스레드.** 게임 스레드는 명령만 쌓고(`playClip` · `stop` · 버스 볼륨 …), 오디오 스레드(장치 콜백 — 장치가 없으면 게임 스레드의
 *          `IAudioSystem::update`)가 `render` 에서 블록마다 명령을 꺼내 적용합니다. 오디오 상태는 오디오 스레드만 만지고, 게임 스레드가 읽는 것
 *          (재생 중인지 · 보이스 수)은 블록마다 게시한 사본입니다.
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
#include "Engine/Audio/AudioSpatial.h"
#include "Engine/Audio/AudioTypes.h"
#include "Engine/Audio/AudioVoice.h"

namespace sw
{
    struct AudioMixerDesc;

    class AudioMixer;

    /** @brief 클립 하나를 바로 재생할 때의 설정입니다(이벤트 없이 — `IAudioSystem::play` · `playMusic`). */
    struct AudioClipPlayParams
    {
        float32        _volumeDb{ 0.0f };       ///< 볼륨(dB)
        float32        _pitchSemitones{ 0.0f }; ///< 피치(반음)
        float32        _pan{ 0.0f };            ///< 팬 [-1, 1]
        float32        _fadeInSeconds{ 0.0f };  ///< 0 에서 오르는 시간(초)
        hashed_string  _attenuation{};          ///< 감쇠 프리셋 이름 — 비었거나 에미터가 0 이면 공간화하지 않는다(2D)
        AudioEmitterId _emitterId{ 0 };         ///< 소리 내는 자리
        bool           _bLoop{ false };         ///< 끝에서 처음으로 돌아간다
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

        // --- 게임 스레드 -----------------------------------------------------------------------------------------------

        /**
         * @brief 믹서 그래프를 바꿉니다. 그래프는 여기서 만들고 다음 블록에 들어갑니다(살아 있는 보이스는 같은 이름의 버스로 옮겨 갑니다).
         * @return 그래프가 검사를 통과하지 못하면 false 이고 지금 그래프를 그대로 씁니다.
         */
        [[nodiscard]] bool loadMixer( const AudioMixerDesc& mixerDesc );
        /** @brief 클립 캐시입니다. */
        AudioClipStore& getClipStore() { return _clipStore; }
        /** @brief 컨테이너 · 범위가 쓰는 난수의 씨앗입니다(시험의 결정성). 다음 블록부터 적용됩니다. */
        void setRandomSeed( uint64 seed );

        /**
         * @brief 클립을 버스 하나로 재생합니다. 클립이 캐시에 없으면 디코드를 요청하고, 준비되면 시작합니다.
         * @param bus 비었거나 모르는 이름이면 `sfx` 입니다.
         * @return 재생 id 입니다. 경로가 비었으면 0 입니다.
         */
        AudioPlayingId playClip( const hashed_string& path, const hashed_string& bus, const AudioClipPlayParams& params );
        /** @brief 재생을 멈춥니다. @p fadeSeconds 동안 줄인 뒤 멈춥니다(0 이면 바로). */
        void stop( AudioPlayingId playingId, float32 fadeSeconds );
        /** @brief 재생을 일시정지 · 재개합니다. */
        void setPaused( AudioPlayingId playingId, bool bPaused );
        /** @brief 버스의 사용자 볼륨(선형, [0, 1])입니다 — 설정 메뉴가 씁니다. 그래프를 바꿔도 이름으로 남습니다. */
        void setBusUserVolume( const hashed_string& bus, float32 volume );
        /** @brief 버스를 음소거합니다. */
        void setBusMuted( const hashed_string& bus, bool bMuted );
        /** @brief 버스를 솔로로 둡니다. */
        void setBusSolo( const hashed_string& bus, bool bSolo );

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
        /** @brief 지금 그래프입니다(오디오 스레드 · 시험). */
        const AudioMixer* getMixer() const { return _pMixer.get(); }

    private:
        /** @brief 명령 종류입니다. */
        enum class CommandType : uint8
        {
            PlayClip,
            Stop,
            SetPaused,
            SetBusUserVolume,
            SetBusMuted,
            SetBusSolo,
            SwapMixer,
            SetRandomSeed,
            SetListener,
            SetEmitter,
            SetEmitterOcclusion,
            RemoveEmitter,
        };

        /** @brief 게임 스레드가 쌓는 명령 하나입니다. 종류마다 쓰는 칸만 채웁니다. */
        struct Command
        {
            shared_ptr<AudioMixer> _pMixer{};     ///< SwapMixer
            hashed_string          _name{};       ///< 버스
            hashed_string          _path{};       ///< 클립 경로
            AudioClipPlayParams    _clipParams{}; ///< PlayClip
            AudioListenerState     _listener{};   ///< SetListener
            float3                 _position{};   ///< SetEmitter
            float3                 _velocity{};   ///< SetEmitter
            AudioPlayingId         _playingId{ 0 };
            AudioEmitterId         _emitterId{ 0 };
            uint32                 _index{ 0 };    ///< 리스너 번호
            uint64                 _seed{ 0 };     ///< SetRandomSeed
            float32                _value{ 0.0f }; ///< 볼륨 · 페이드 시간
            CommandType            _type{ CommandType::PlayClip };
            bool                   _bFlag{ false }; ///< 음소거 · 솔로 · 일시정지
        };

        /** @brief 버스 이름에 남는 사용자 상태입니다(그래프를 바꾸면 다시 건다). */
        struct BusUserState
        {
            hashed_string _bus{};
            float32       _volume{ 1.0f };
            bool          _bMuted{ false };
            bool          _bSolo{ false };
        };

        /** @brief 보이스 칸 하나입니다. */
        struct VoiceSlot
        {
            AudioVoice     _voice{};
            hashed_string  _clipPath{};
            hashed_string  _busName{};
            hashed_string  _attenuationName{};
            AudioPlayingId _playingId{ 0 };
            AudioEmitterId _emitterId{ 0 };
            int32          _attenuationIndex{ -1 }; ///< 지금 그래프의 감쇠 프리셋(-1 = 2D)
            float32        _volume{ 1.0f };         ///< 기본 볼륨(선형)
            float32        _pan{ 0.0f };
            float32        _pitchRatio{ 1.0f };
            float32        _fadeInSeconds{ 0.0f };
            uint32         _busIndex{ 0 };
            bool           _bLoop{ false };
            bool           _bInUse{ false };
            bool           _bWaitingForClip{ false }; ///< 클립 디코드를 기다린다
            bool           _bPausedRequest{ false };  ///< 클립을 기다리는 동안 들어온 일시정지
        };

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
        /** @brief 보이스 하나의 이번 블록 목표(볼륨 · 팬 · 피치 · 로우패스)를 정합니다. */
        void updateVoiceTargets( VoiceSlot& slot );
        /** @brief 감쇠 프리셋 이름을 지금 그래프의 번호로 풉니다(-1 = 없음). */
        int32 resolveAttenuationIndex( const hashed_string& name ) const;
        /** @brief 게임 스레드가 읽을 상태를 게시합니다. */
        void publishState();
        /** @brief 명령을 쌓습니다. */
        void pushCommand( Command&& command );

    private:
        AudioClipStore _clipStore; /**< 경로 → 클립 캐시입니다. */
        // 게임 스레드 → 오디오 스레드
        vector<Command> _listPendingCommand;  /**< 쌓인 명령입니다(`_commandMutex`). */
        vector<Command> _listApplyingCommand; /**< 오디오 스레드가 꺼내 적용 중인 명령입니다. */
        mutex           _commandMutex;        /**< `_listPendingCommand` 를 지킵니다. */
        atomic<uint64>  _nextPlayingId;       /**< 다음 재생 id 입니다. */
        // 오디오 스레드
        shared_ptr<AudioMixer>                           _pMixer;                         /**< 지금 그래프입니다. */
        vector<VoiceSlot>                                _listVoice;                      /**< 보이스 칸입니다. */
        vector<BusUserState>                             _listBusUserState;               /**< 버스 이름의 사용자 상태입니다. */
        vector<float32>                                  _listBlockOutput;                /**< 마지막 블록의 출력입니다(스테레오 교차). */
        vector<float32>                                  _listVoiceScratch;               /**< 보이스 로우패스 작업 버퍼입니다(블록 길이). */
        unordered_map<AudioEmitterId, AudioEmitterState> _mapEmitter;                     /**< 에미터 id → 상태입니다. */
        AudioListenerState                               _arrListener[kMaxListenerCount]; /**< 리스너입니다. */
        AudioRandom                                      _random;                         /**< 컨테이너 · 범위 난수입니다. */
        uint64                                           _renderedFrameCount;             /**< 지금까지 렌더한 프레임입니다. */
        uint64                                           _appliedPlayingId;               /**< 적용한 재생 명령의 가장 큰 id 입니다. */
        uint32                                           _blockCursor;                    /**< `_listBlockOutput` 에서 다음에 내보낼 프레임입니다. */
        // 게시(오디오 스레드 → 게임 스레드)
        vector<AudioPlayingId> _listPublishedPlaying; /**< 살아 있는 재생 id(정렬)입니다. */
        AudioEngineStats       _publishedStats;       /**< 마지막 블록의 상태입니다. */
        atomic<uint64>         _publishedAppliedId;   /**< 게시 시점에 적용된 재생 id 입니다. */
        mutable mutex          _publishMutex;         /**< 게시 사본을 지킵니다. */
        atomic<bool>           _bInitialized;         /**< 초기화되었는지입니다. */
    };
} // namespace sw
