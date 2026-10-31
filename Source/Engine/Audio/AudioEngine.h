/**
 * @file AudioEngine.h
 * @brief 플랫폼과 무관한 오디오 엔진입니다. 명령 큐, 보이스, 믹서 그래프, 이벤트, 파라미터, 스냅샷, 공간화를 보관하고, 출력 장치가 호출하는 `render` 하나로 소리를 만듭니다.
 * @details **스레드.** 게임 스레드는 명령만 쌓는다(`postEvent`, `playClip`, `setParameter` 등).
 *          오디오 스레드가 `render` 에서 블록마다 명령을 꺼내 적용한다. 오디오 스레드는 장치 콜백이고, 장치가 없으면 게임 스레드의 `IAudioService::update` 다.
 *          오디오 상태는 오디오 스레드만 만진다. 게임 스레드가 읽는 값(재생 중인지, 보이스 수)은 블록마다 게시한 사본이다.
 *
 *          **블록 순서.** 명령, 파라미터(seek), 스냅샷 세기와 적용, 에미터 가림 추종, 보이스 목표(볼륨, 팬, 피치, 로우패스, 들림 정도),
 *          실제/가상 보이스 선택(우선순위와 들림), 믹싱(가상 보이스는 위치만 진행), 버스 그래프, 게시 순서로 처리한다.
 *
 *          **결정성.** 같은 명령과 같은 시드(`setRandomSeed`)를 주면 `render` 는 같은 샘플을 만든다.
 *          장치 없이 버퍼에 렌더해 측정값을 확인하는 테스트가 이 성질에 의존한다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/DeterministicRandom.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Audio/AudioBlock.h"
#include "Engine/Audio/AudioChannelLayout.h"
#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioEngineState.h"
#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/AudioMusic.h"
#include "Engine/Audio/AudioMusicPlayer.h"
#include "Engine/Audio/AudioSnapshotMixer.h"
#include "Engine/Audio/AudioSpatial.h"
#include "Engine/Audio/AudioTypes.h"
#include "Engine/Audio/AudioVoice.h"

namespace sw
{
    struct AudioMixerDesc;

    class AudioMixer;

    /** @brief 클립 하나를 이벤트 없이 바로 재생할 때의 설정입니다. `IAudioService::play` 와 `playMusic` 이 씁니다. */
    struct AudioClipPlayParams
    {
        hashed_string    _attenuation{};          ///< 감쇠 프리셋 이름이다. 비었거나 에미터가 0 이면 공간화하지 않는다(2D).
        AudioEmitterID   _emitterID{ 0 };         ///< 소리를 내는 에미터다.
        float32          _volumeDb{ 0.0f };       ///< 볼륨(dB)
        float32          _pitchSemitones{ 0.0f }; ///< 피치(반음)
        float32          _pan{ 0.0f };            ///< 팬이다([-1, 1]).
        float32          _fadeInSeconds{ 0.0f };  ///< 0 에서 오르는 시간(초)
        int32            _priority{ 50 };         ///< 보이스가 모자랄 때 우선순위가 높은 쪽이 남는다.
        AudioVirtualMode _virtual{ AudioVirtualMode::Virtualize };
        bool             _bLoop{ false }; ///< 끝에서 처음으로 돌아간다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class AudioEngine
     * @brief 오디오 엔진 하나입니다. `IAudioService` 이 하나를 가지고 있고, 테스트는 장치 없이 직접 만들어 `render` 로 버퍼에 렌더합니다.
     * @details 적응형 음악은 `AudioMusicPlayer`, 스냅샷 · 버스 사용자 상태는 `AudioSnapshotMixer` 가 맡는다. 엔진은 명령 큐 · 이벤트 라이브러리 · 보이스 · 에미터를 들고,
     *          음악 플레이어에게는 보이스 풀의 창(`IAudioMusicVoices`)만 비공개 상속으로 내준다 — 플레이어는 보이스 슬롯의 생김새를 모른다.
     */
    class SW_API AudioEngine : private IAudioMusicVoices
    {
    public:
        /** @brief 리스너 슬롯 수입니다. 분할 화면 4개를 지원합니다. */
        static constexpr uint32 kMaxListenerCount = 4;

        AudioEngine();
        ~AudioEngine() override;

        AudioEngine( const AudioEngine& )            = delete;
        AudioEngine& operator=( const AudioEngine& ) = delete;

        /**
         * @brief 믹서 그래프를 만들고 요청을 받을 준비를 합니다. 그래프가 검사를 통과하지 못하면 false 를 반환합니다.
         * @param layout 출력 채널 구성입니다. 레이아웃은 장치를 닫은 뒤에만(`setChannelLayout`) 바꿉니다.
         * @details 그래프의 이펙트가 레이아웃을 지원하지 않으면(이펙트 종류 표의 지원 레이아웃) false 입니다.
         */
        [[nodiscard]] bool initialize( const AudioMixerDesc& mixerDesc, AudioChannelLayout layout );
        /** @brief 출력 채널 구성입니다. `render` 의 교차 출력이 이 채널 수를 가집니다. */
        AudioChannelLayout getChannelLayout() const { return _layout; }
        /**
         * @brief 출력 채널 구성을 바꿉니다. 지금 그래프를 새 레이아웃으로 다시 만들고, 재생 중인 보이스는 이어서 다음 블록부터 새 레이아웃의 게인으로 램프합니다.
         * @details 장치가 `render` 를 부르지 않을 때(출력을 닫은 뒤)만 부릅니다. 밀린 명령을 먼저 적용합니다. 그래프가 새 레이아웃을 받지 못하면 false 이고 지금 구성을 그대로 씁니다.
         */
        [[nodiscard]] bool setChannelLayout( AudioChannelLayout layout );
        /** @brief 보이스와 그래프를 해제합니다. 장치가 `render` 를 더 호출하지 않게 한 뒤에 호출해야 합니다. */
        void shutdown();
        /** @brief 초기화되었는지입니다. */
        bool isInitialized() const { return _bInitialized.load( std::memory_order_acquire ); }

        // --- 게임 스레드: 데이터 ----------------------------------------------------------------------------------------

        /**
         * @brief 믹서 그래프를 교체합니다. 그래프는 여기서 만들고 다음 블록부터 적용됩니다. 재생 중인 보이스는 같은 이름의 버스로 옮겨 갑니다.
         * @return 그래프가 검사를 통과하지 못하면 false 를 반환하고, 현재 그래프를 그대로 씁니다.
         */
        [[nodiscard]] bool loadMixer( const AudioMixerDesc& mixerDesc );
        /**
         * @brief 이벤트 라이브러리를 이름(보통 리소스 경로)으로 로드합니다. 같은 이름이 있으면 교체합니다(핫 리로드). 클립 디코드를 요청해 둡니다.
         * @return 이벤트의 버스나 감쇠가 현재 그래프와 라이브러리에 없거나, 다른 라이브러리와 이벤트나 파라미터 이름이 겹치면 오류를 남기고 false 를 반환합니다.
         */
        [[nodiscard]] bool loadEventLibrary( const hashed_string& libraryName, const AudioEventLibrary& library );
        /** @brief 라이브러리를 언로드합니다. 재생 중인 인스턴스는 끝까지 재생합니다. */
        void unloadEventLibrary( const hashed_string& libraryName );
        /** @brief 로드한 라이브러리에 그 이벤트가 있는지 반환합니다. */
        bool hasEvent( const hashed_string& eventName ) const;
        /**
         * @brief 그 이름(보통 리소스 경로)으로 로드한 라이브러리를 찾아 로드할 때의 이름을 @p outLibraryName 에 돌려줍니다. 구분자와 대소문자를 무시하고, 이름을 intern 하지 않습니다.
         * @details 핫 리로드가 바뀐 파일마다 묻는 길이라 모르는 경로로 `hashed_string` 을 만들지 않는다. 돌려준 이름으로 다시 로드해야 같은 라이브러리를 바꾼다.
         */
        [[nodiscard]] bool findEventLibraryName( string_view libraryName, hashed_string& outLibraryName ) const;
        /** @brief 클립 캐시입니다. */
        AudioClipCache& getClipStore() { return _clipStore; }
        /** @brief 컨테이너와 범위가 쓰는 난수의 시드를 정합니다. 테스트에서 결정성을 얻으려고 씁니다. 다음 블록부터 적용됩니다. */
        void setRandomSeed( uint64 seed );

        // --- 게임 스레드: 재생 ------------------------------------------------------------------------------------------

        /**
         * @brief 이벤트를 재생합니다. 컨테이너가 클립을 고르고, 범위, 쿨다운, 동시 재생 상한, 우선순위를 데이터대로 적용합니다.
         * @param emitterID 0 이면 공간화하지 않습니다(UI, 2D).
         * @return 재생 id 입니다. 모르는 이벤트면 0 을 반환하고 경고를 남깁니다. 쿨다운이나 상한 때문에 버려진 재생은 다음 블록부터 `isPlaying` 이 false 입니다.
         */
        AudioPlayingID postEvent( const hashed_string& eventName, AudioEmitterID emitterID = 0 );
        /**
         * @brief 클립을 버스 하나로 재생합니다. 클립이 캐시에 없으면 디코드를 요청하고, 준비되면 시작합니다.
         * @param bus 비었거나 모르는 이름이면 `sfx` 입니다.
         * @return 재생 id 입니다. 경로가 비었으면 0 입니다.
         */
        AudioPlayingID playClip( const hashed_string& path, const hashed_string& bus, const AudioClipPlayParams& params );
        /** @brief 재생을 멈춥니다. @p fadeSeconds 동안 줄인 뒤 멈춥니다. 0 이면 바로 멈추고, 음수면 이벤트의 `_fadeOutSeconds` 를 씁니다. */
        void stop( AudioPlayingID playingID, float32 fadeSeconds );
        /** @brief 에미터의 재생을 모두 멈춥니다. 페이드가 음수면 이벤트의 값을 씁니다. */
        void stopEmitter( AudioEmitterID emitterID, float32 fadeSeconds );
        /** @brief 재생을 일시정지하거나 재개합니다. */
        void setPaused( AudioPlayingID playingID, bool bPaused );

        // --- 게임 스레드: 적응형 음악 ------------------------------------------------------------------------------------

        /**
         * @brief 적응형 음악을 시작합니다. 시작 구간부터 재생하고, 재생 중인 음악은 @p fadeSeconds 동안 줄입니다. 레이어 클립의 디코드를 요청해 둡니다.
         * @return 음악의 재생 id 입니다. 구간이 바뀌어도 같습니다. 설명자가 nullptr 이면 0 을 반환합니다.
         */
        AudioPlayingID startMusic( shared_ptr<const AudioMusicDesc> pMusic, float32 fadeSeconds );
        /** @brief 구간을 바꿉니다. 전환 규칙의 맞춤 지점(박, 마디, 구간 끝)에서 샘플 단위로 바꾸고 스팅어를 냅니다. 같은 구간이면 아무것도 하지 않습니다. */
        void setMusicSegment( const hashed_string& segment );
        /** @brief 음악을 @p fadeSeconds 동안 줄여 멈춥니다. */
        void stopMusic( float32 fadeSeconds );
        /** @brief 음악의 현재 위치(구간, 박, 마디)를 반환합니다. */
        AudioMusicStatus getMusicStatus() const;

        // --- 게임 스레드: 믹스 ------------------------------------------------------------------------------------------

        /** @brief 버스의 사용자 볼륨(선형, [0, 1])을 정합니다. 설정 메뉴가 씁니다. 그래프를 바꿔도 버스 이름으로 유지됩니다. */
        void setBusUserVolume( const hashed_string& bus, float32 volume );
        /** @brief 버스를 음소거합니다. */
        void setBusMuted( const hashed_string& bus, bool bMuted );
        /** @brief 버스를 솔로로 둡니다. */
        void setBusSolo( const hashed_string& bus, bool bSolo );
        /** @brief 게임 파라미터(RTPC)를 정합니다. 라이브러리의 범위로 묶고 `_seekSpeed` 로 따라갑니다. */
        void setParameter( const hashed_string& name, float32 value );
        /** @brief 에미터 하나에만 적용되는 파라미터 값을 정합니다. 전역 값보다 우선합니다. */
        void setEmitterParameter( AudioEmitterID emitterID, const hashed_string& name, float32 value );
        /** @brief 스냅샷을 켭니다. `_fadeInSeconds` 동안 세기를 1 로 올립니다. 이미 켜져 있으면 가장 나중에 켠 것으로 순서만 바꿉니다. */
        void startSnapshot( const hashed_string& name );
        /** @brief 스냅샷을 끕니다. `_fadeOutSeconds` 동안 세기를 0 으로 내립니다. */
        void stopSnapshot( const hashed_string& name );
        /** @brief 스냅샷 세기를 직접 정합니다(0..1, 페이드 없음). 리버브 존 경계 블렌드처럼 게임 값이 곧 세기일 때 씁니다. */
        void setSnapshotIntensity( const hashed_string& name, float32 intensity );

        // --- 게임 스레드: 공간 ------------------------------------------------------------------------------------------

        /**
         * @brief 리스너를 정합니다(보통 게임 카메라). 여럿이면(분할 화면) 보이스마다 가장 크게 들리는 리스너로 공간화합니다.
         * @param listenerIndex `kMaxListenerCount` 보다 작아야 합니다. `_bActive` 가 false 면 그 리스너를 끕니다.
         */
        void setListener( uint32 listenerIndex, const AudioListenerState& state );
        /** @brief 에미터의 위치와 속도를 정합니다. 처음이면 에미터를 만듭니다. */
        void setEmitter( AudioEmitterID emitterID, const float3& position, const float3& velocity );
        /**
         * @brief 에미터까지 소리가 오는 길(가림 0..1, 회절 0..1, 문을 돌아 오는 겉보기 위치)을 정합니다. 가림 · 회절은 엔진이 `AudioOcclusionDesc::_smoothingSeconds` 로 따라갑니다.
         * @details 가림만 있으면 `AudioPropagation::makeOcclusion( value )` 를 넘깁니다.
         */
        void setEmitterPropagation( AudioEmitterID emitterID, const AudioPropagation& propagation );
        /** @brief 에미터를 지웁니다. 그 에미터의 소리는 마지막 위치에 남아 끝까지 재생합니다. */
        void removeEmitter( AudioEmitterID emitterID );

        // --- 게임 스레드: 읽기 ------------------------------------------------------------------------------------------

        /** @brief 재생이 아직 살아 있는지 반환합니다. 디코드 대기와 가상 보이스도 살아 있는 것으로 봅니다. 명령이 아직 처리되지 않았으면 true 입니다. */
        bool isPlaying( AudioPlayingID playingID ) const;
        /** @brief 마지막 블록의 상태입니다. */
        AudioEngineStats getStats() const;
        /** @brief 믹서 버스마다 마지막 블록의 상태(사용자 볼륨 · 음소거 · 솔로 · 피크)를 @p outListStatus 에 담습니다(비우고 채웁니다). 오디오 스레드가 블록마다 게시한 사본입니다. */
        void getBusStatuses( vector<AudioBusStatus>& outListStatus ) const;
        /** @brief 로드한 라이브러리의 이벤트 이름을 @p outListName 에 담습니다(비우고 채웁니다). 라이브러리 로드 순서는 정해져 있지 않습니다. */
        void collectEventNames( vector<hashed_string>& outListName ) const;
        /** @brief 이벤트 라이브러리를 로드하거나 내릴 때마다 오르는 세대입니다. 이름 목록을 캐시하는 쪽(오디오 믹서 창)이 이것이 바뀔 때만 다시 모읍니다. */
        uint32 getEventLibraryGeneration() const { return _libraryGeneration.load( std::memory_order_acquire ); }
        /**
         * @brief 로드한 라이브러리에서 이벤트 설명자를 찾아 사본을 @p outDesc 에, 라이브러리 이름(리소스 경로)을 @p outLibraryName 에 담습니다.
         * @details 에디터의 오디오 이벤트 창이 읽습니다. @p pOutAttenuation 이 있으면 이벤트의 감쇠 프리셋(라이브러리 것을 먼저, 없으면 믹서 그래프 것)을 담고, 감쇠가 없으면 기본값으로 둡니다.
         * @return 이벤트가 없으면 false 입니다.
         */
        [[nodiscard]] bool findEventDesc( const hashed_string& eventName, AudioEventDesc& outDesc, hashed_string& outLibraryName,
                                          AudioAttenuationDesc* pOutAttenuation = nullptr ) const;
        /** @brief 살아 있는 이벤트 인스턴스마다 마지막 블록의 상태(이벤트 · 버스 · 남은 시간 · 가상 보이스 수)를 @p outListStatus 에 담습니다(비우고 채웁니다). */
        void getInstanceStatuses( vector<AudioInstanceStatus>& outListStatus ) const;
        /**
         * @brief 자막 키를 가진 클립이 시작했다는 신호를 시작한 순서대로 @p outListCue 에 옮기고 비웁니다(비우고 채웁니다).
         * @details 엔진 프레임이 UI 갱신 직전에 한 번 가져가 자막 서비스에 올린다(`Engine.cpp`). 가져가는 곳은 하나다 — 둘이면 한쪽만 받는다.
         */
        void takeSubtitleCues( vector<AudioSubtitleCue>& outListCue );

        // --- 오디오 스레드 --------------------------------------------------------------------------------------------------

        /**
         * @brief @p frameCount 프레임을 렌더해 @p pOutput(레이아웃의 채널 수만큼 교차한 float32, [-1, 1])에 씁니다. 장치 콜백이 부릅니다.
         * @details 안에서는 블록(`audio::kBlockFrameCount`) 단위로 평면 렌더하고, 블록을 시작할 때 쌓인 명령을 적용한다. 장치 경계에서 교차로 바꾼다. 초기화 전이면 0 을 쓴다.
         */
        void render( float32* pOutput, uint32 frameCount );
        /** @brief 오디오 스레드 역할을 놓습니다. 출력을 닫거나 열기 직전에 부른다 — 다음에 `render` 를 부르는 스레드(새 장치 스레드 · 오프라인 렌더)가 역할을 받는다. */
        void releaseAudioThread() { _audioThread.unbind(); }
        /** @brief 버스 하나의 마지막 블록 피크를 반환합니다. 오디오 스레드와 테스트가 씁니다. 없는 버스면 0 입니다. */
        float32 getBusPeak( const hashed_string& bus ) const;
        /** @brief 파라미터의 현재 값(seek 이후)을 반환합니다. 오디오 스레드와 테스트가 씁니다. 모르는 파라미터면 0 입니다. */
        float32 getParameterValue( const hashed_string& name ) const;
        /** @brief 스냅샷 세기 · 버스 사용자 상태입니다(`getSnapshotIntensity`). 오디오 스레드와 테스트가 씁니다. */
        const AudioSnapshotMixer& getSnapshotMixer() const { return _snapshotMixer; }
        /** @brief 현재 그래프를 반환합니다. 오디오 스레드와 테스트가 씁니다. */
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
            SetEmitterPropagation,
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

        /** @brief 게임 스레드가 쌓는 명령 하나입니다. 종류마다 쓰는 필드만 채웁니다. */
        struct Command
        {
            shared_ptr<AudioMixer>              _pMixer{};      ///< SwapMixer
            shared_ptr<const AudioEventLibrary> _pLibrary{};    ///< SetLibrary 가 쓴다. nullptr 이면 언로드한다.
            shared_ptr<const AudioMusicDesc>    _pMusic{};      ///< StartMusic
            hashed_string                       _name{};        ///< 버스, 이벤트, 파라미터, 스냅샷, 라이브러리의 이름이다.
            hashed_string                       _path{};        ///< 클립 경로다.
            AudioClipPlayParams                 _clipParams{};  ///< PlayClip
            AudioListenerState                  _listener{};    ///< SetListener
            float3                              _position{};    ///< SetEmitter
            float3                              _velocity{};    ///< SetEmitter
            AudioPropagation                    _propagation{}; ///< SetEmitterPropagation
            AudioPlayingID                      _playingID{ 0 };
            AudioEmitterID                      _emitterID{ 0 };
            uint64                              _seed{ 0 };     ///< SetRandomSeed
            float32                             _value{ 0.0f }; ///< 볼륨, 페이드, 파라미터, 세기 값이다.
            uint32                              _index{ 0 };    ///< 리스너 번호다.
            CommandType                         _type{ CommandType::PlayClip };
            bool                                _bFlag{ false }; ///< 음소거, 솔로, 일시정지 플래그다.
        };

        /** @brief 에미터 파라미터 값 하나입니다. */
        struct EmitterParameter
        {
            hashed_string _name{};
            float32       _value{ 0.0f };
        };

        /** @brief 에미터 하나입니다. 공간 상태와 에미터 파라미터를 담습니다. */
        struct EmitterRecord
        {
            AudioEmitterState        _state{};
            vector<EmitterParameter> _listParameter{};
            bool                     _bRemoved{ false }; ///< 지워 달라고 요청했다. 이 에미터의 보이스가 끝나면 지운다.
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

        /** @brief 이벤트 하나의 실행 상태입니다. 이름으로 유지되므로 라이브러리를 바꿔도 쿨다운과 순서 커서가 이어집니다. */
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
            shared_ptr<const AudioEventLibrary> _pLibrary{}; ///< 설명자가 있는 라이브러리다. 언로드해도 인스턴스가 참조한다.
            const AudioEventDesc*               _pDesc{ nullptr };
            AudioPlayingID                      _playingID{ 0 };
            AudioEmitterID                      _emitterID{ 0 };
            uint64                              _startFrame{ 0 };
            float32                             _audibility{ 0.0f }; ///< 지난 블록 보이스 목표 게인의 최댓값이다.
            float32                             _distance{ 0.0f };   ///< 지난 블록의 리스너 거리다.
            bool                                _bInUse{ false };
            bool                                _bStopping{ false }; ///< 우선순위에서 밀려 페이드아웃 중이다. 동시 재생 상한을 셀 때 뺀다.
        };

        /** @brief 보이스 슬롯 하나입니다. */
        struct VoiceSlot
        {
            AudioVoice       _voice{};
            hashed_string    _clipPath{};
            hashed_string    _busName{};
            hashed_string    _attenuationName{};
            AudioPlayingID   _playingID{ 0 };
            AudioEmitterID   _emitterID{ 0 };
            float32          _volume{ 1.0f }; ///< 기본 볼륨(선형)
            float32          _pan{ 0.0f };
            float32          _pitchRatio{ 1.0f };
            float32          _fadeInSeconds{ 0.0f };
            float32          _audibility{ 0.0f }; ///< 이번 블록 목표 게인(버스 페이더는 넣지 않는다)
            float32          _distance{ 0.0f };
            uint32           _busIndex{ 0 };
            int32            _attenuationIndex{ -1 }; ///< 감쇠 테이블 번호다(-1 = 2D).
            int32            _instanceIndex{ -1 };    ///< 이벤트 인스턴스(-1 = 클립 재생)
            int32            _priority{ 50 };
            int32            _musicSegment{ -1 }; ///< 음악 레이어의 구간이다(-1 = 음악 아님).
            int32            _musicLayer{ -1 };   ///< 음악 레이어 번호다.
            float32          _musicGain{ 1.0f };  ///< 파라미터로 페이드하는 레이어 게인이다.
            uint64           _startFrame{ 0 };    ///< 음악에서 소리가 시작할 렌더 프레임이다. 클립이 늦게 와도 박을 지킨다.
            AudioVirtualMode _virtualMode{ AudioVirtualMode::Virtualize };
            bool             _bLoop{ false };
            bool             _bInUse{ false };
            bool             _bWaitingForClip{ false }; ///< 클립 디코드를 기다린다
            bool             _bPausedRequest{ false };  ///< 클립을 기다리는 동안 들어온 일시정지 요청이다.
            bool             _bVirtual{ false };        ///< 이번 블록에 섞지 않는다
        };

        // AudioEngine.cpp: 명령, 보이스, 렌더
        /** @brief 쌓인 명령을 꺼내 적용합니다. */
        void applyCommands();
        /** @brief 명령 하나를 적용합니다. */
        void applyCommand( Command& command );
        /** @brief 클립 재생 명령으로 보이스를 잡고 클립 불러오기를 시작합니다. 보이스가 없으면 경고하고 버립니다. */
        void applyPlayClip( const Command& command );
        /** @brief 에미터의 이름 붙은 파라미터를 넣거나 고칩니다. */
        void applySetEmitterParameter( const Command& command );
        /** @brief 만든 그래프로 바꿉니다(사용자 버스 상태 다시 적용, 보이스 버스 다시 찾기, 감쇠 · 스냅샷 다시 묶기). 그래프 레이아웃은 `_layout` 과 같아야 합니다. */
        void adoptMixer( shared_ptr<AudioMixer> pMixer );
        /** @brief 블록 하나를 렌더해 `_blockOutput`(평면)에 둡니다. */
        void renderBlock();
        /** @brief 빈 보이스 슬롯을 잡습니다. 없으면 nullptr 를 반환합니다. */
        VoiceSlot* allocateVoice();
        /** @brief 보이스 슬롯을 반환합니다. */
        void freeVoice( VoiceSlot& slot );
        /** @brief 재생 id 나 에미터의 보이스를 멈춥니다. @p fadeSeconds 가 음수면 이벤트의 값을 씁니다. */
        void stopVoices( AudioPlayingID playingID, AudioEmitterID emitterID, float32 fadeSeconds );
        /** @brief 클립을 기다리는 슬롯에 클립이 도착했으면 시작합니다. 디코드에 실패한 클립의 슬롯은 반환합니다. */
        void startWaitingVoice( VoiceSlot& slot );
        /** @brief 버스 이름을 현재 그래프의 번호로 찾습니다. 없으면 sfx 를, 그것도 없으면 master 를 씁니다. */
        uint32 resolveBusIndex( const hashed_string& bus ) const;
        /** @brief 에미터의 가림 값이 이번 블록 동안 목표값을 따라가게 합니다. */
        void updateEmitters();
        /** @brief 보이스 하나의 이번 블록 목표(볼륨, 팬, 피치, 로우패스, 들림)를 정합니다. */
        void updateVoiceTargets( VoiceSlot& slot );
        /** @brief 삭제를 요청받은 에미터 중에서 보이스가 남지 않은 것을 지웁니다. */
        void sweepRemovedEmitters();
        /** @brief 게임 스레드가 읽을 상태를 게시합니다. */
        void publishState( uint32 realCount, uint32 virtualCount );
        /** @brief 명령을 쌓습니다. */
        void pushCommand( Command&& command );
        /**
         * @brief 이벤트 라이브러리를 검사하고 맞으면 표에 넣습니다(`loadEventLibrary` 의 잠금 안 부분). 오류는 @p outListError 에 모으고, 넣었으면 @p outLibrary 가 그 라이브러리입니다.
         * @details 로그 · 클립 요청 · 명령은 부르는 쪽이 잠금을 놓은 뒤에 한다(잠금은 잎 잠금, 결정 5-3).
         */
        void loadEventLibraryLocked( const hashed_string& libraryName, const AudioEventLibrary& library, vector<string>& outListError,
                                     shared_ptr<const AudioEventLibrary>& outLibrary ) SW_REQUIRES_LOCK( _libraryMutex );

        // AudioEngine.cpp: 이벤트, 파라미터, 보이스 제한
        /** @brief 이벤트를 재생합니다. 쿨다운, 상한, 컨테이너를 적용합니다. */
        void applyPostEvent( const Command& command );
        /** @brief 쿨다운과 동시 재생 상한을 봅니다. 상한이면 뺏을 인스턴스를 멈춥니다. 버려야 하면(버린 수를 올리고) false 입니다. */
        [[nodiscard]] bool tryAdmitEvent( const EventState& state, const AudioEventDesc& event );
        /** @brief 이벤트의 살아 있는 인스턴스 수와, 뺏을 인스턴스(정책에 가장 맞는 것)의 번호를 구합니다. 없으면 -1 입니다. */
        int32 findStealVictim( const AudioEventDesc& event, uint32& outLiveCount ) const;
        /** @brief 컨테이너 종류에 따라 이번에 낼 첫 클립과 개수를 고릅니다(무작위는 앞서 낸 것을 피하고, 순차는 커서를 옮긴다). */
        void pickEventClips( EventState& inoutState, const AudioEventDesc& event, uint32& outFirstClip, uint32& outPickCount );
        /** @brief 라이브러리를 교체하거나 언로드하고, 이벤트와 감쇠 테이블을 다시 만듭니다. */
        void applySetLibrary( const Command& command );
        /** @brief 감쇠 테이블(그래프와 라이브러리의 프리셋)을 다시 만들고, 보이스의 번호를 이름으로 다시 찾습니다. */
        void rebuildAttenuationTable();
        /** @brief 이벤트 이름 테이블을 다시 만듭니다. 이름으로 유지되는 상태는 이어 갑니다. */
        void rebuildEventTable();
        /** @brief 감쇠 프리셋 이름의 테이블 번호를 반환합니다. 없으면 -1 입니다. */
        int32 resolveAttenuationIndex( const hashed_string& name ) const;
        /** @brief 파라미터를 이 블록만큼 목표로 옮깁니다. */
        void updateParameters();
        /** @brief 이 에미터에서 본 파라미터 값을 반환합니다. 에미터 값, 전역 값, 0 순서로 찾습니다. */
        float32 findParameterValue( AudioEmitterID emitterID, const hashed_string& name ) const;
        /** @brief 실제로 섞을 보이스를 고르고(우선순위와 들림), 나머지는 가상이나 정지 상태로 둡니다. */
        void selectRealVoices();
        /** @brief 인스턴스의 들림과 거리를 모으고, 보이스가 없는 인스턴스를 비웁니다. */
        void updateInstances();
        /** @brief 빈 인스턴스 슬롯을 잡습니다. */
        int32 allocateInstance();

        // AudioEngine.cpp: 음악 플레이어에게 내주는 보이스 풀의 창(`IAudioMusicVoices`)
        uint64             getMusicClock() const override { return _renderedFrameCount; }
        float32            findMusicParameter( const hashed_string& name ) const override { return getParameterValue( name ); }
        [[nodiscard]] bool startMusicVoice( const AudioMusicVoiceRequest& request ) override;
        void               fadeMusicVoices( AudioPlayingID playingID, int32 segment, uint32 fadeFrames, uint32 delayFrames ) override;
        void               freeMusicVoices( AudioPlayingID playingID, int32 segment ) override;
        void               stepMusicLayerGains( AudioPlayingID playingID, float32 maxStep, const AudioMusicPlayer& player ) override;
        /** @brief 슬롯이 재생 @p playingID 의 구간 @p segment(`AudioMusicPlayer::kAnySegment` 면 모두) 음악 보이스인지 봅니다. */
        static bool isMusicVoice( const VoiceSlot& slot, AudioPlayingID playingID, int32 segment );

    private:
        using LibraryMap   = unordered_map<hashed_string, shared_ptr<const AudioEventLibrary>, hashed_string::HashFunc>;
        using EventMap     = unordered_map<hashed_string, EventState, hashed_string::HashFunc>;
        using ParameterMap = unordered_map<hashed_string, ParameterState, hashed_string::HashFunc>;

        AudioClipCache _clipStore; /**< 경로에서 클립으로 가는 캐시입니다. */
        // 게임 스레드
        vector<Command> _listPendingCommand              SW_GUARDED_BY( _commandMutex ); /**< 쌓인 명령입니다(`_commandMutex`). */
        ThreadRole                                       _audioThread;                   ///< 오디오 스레드 역할 — 꺼내 온 명령 목록은 믹스를 도는 스레드만 만진다(잠금이 아니라 분석용 능력)
        vector<Command> _listApplyingCommand             SW_GUARDED_BY( _audioThread );  /**< 오디오 스레드가 꺼내 적용 중인 명령입니다. */
        LibraryMap _mapGameLibrary                       SW_GUARDED_BY( _libraryMutex ); /**< 게임 스레드가 아는 라이브러리입니다. */
        shared_ptr<const AudioMixerDesc> _pGameMixerDesc SW_GUARDED_BY( _libraryMutex ); /**< 게임 스레드가 아는 그래프입니다. 이름으로 대조합니다. */
        mutex                                            _commandMutex;                  /**< `_listPendingCommand` 를 지킵니다. */
        mutable mutex                                    _libraryMutex;                  /**< `_mapGameLibrary` 와 `_pGameMixerDesc` 를 지킵니다. 이벤트는 병렬 틱에서도 재생합니다. */
        atomic<uint32>                                   _libraryGeneration;             /**< `_mapGameLibrary` 를 바꿀 때마다 오릅니다(`getEventLibraryGeneration`). */
        atomic<uint64>                                   _nextPlayingID;                 /**< 다음 재생 id 입니다. */
        // 오디오 스레드
        shared_ptr<AudioMixer>                       _pMixer;                         /**< 지금 그래프입니다. */
        vector<VoiceSlot>                            _listVoice;                      /**< 보이스 슬롯입니다. */
        vector<uint32>                               _listVoiceOrder;                 /**< 보이스를 고르는 작업 목록입니다. */
        vector<EventInstance>                        _listInstance;                   /**< 이벤트 인스턴스 슬롯입니다. */
        AudioBlockBuffer                             _blockOutput;                    /**< 마지막 블록의 출력입니다(평면). */
        vector<AudioAttenuationDesc>                 _listAttenuation;                /**< 감쇠 테이블입니다. 그래프 프리셋과 라이브러리 프리셋을 합쳤습니다. */
        AudioSnapshotMixer                           _snapshotMixer;                  /**< 켜 둔 스냅샷과 버스 사용자 상태입니다(그래프를 바꿔도 이름으로 남는다). */
        AudioMusicPlayer                             _musicPlayer;                    /**< 적응형 음악의 재생 위치 · 전환 예약 · 레이어 게인입니다. */
        LibraryMap                                   _mapLibrary;                     /**< 로드한 라이브러리입니다. */
        EventMap                                     _mapEventState;                  /**< 이벤트 이름에서 상태로 가는 맵입니다. */
        ParameterMap                                 _mapParameter;                   /**< 파라미터 이름에서 상태로 가는 맵입니다. */
        unordered_map<AudioEmitterID, EmitterRecord> _mapEmitter;                     /**< 에미터 id 에서 상태로 가는 맵입니다. */
        AudioListenerState                           _arrListener[kMaxListenerCount]; /**< 리스너입니다. */
        DeterministicRandom64                        _random;                         /**< 컨테이너와 범위가 쓰는 난수입니다(시드는 `setRandomSeed`). */
        uint64                                       _renderedFrameCount;             /**< 지금까지 렌더한 프레임입니다. */
        uint64                                       _appliedPlayingID;               /**< 적용한 재생 명령의 가장 큰 id 입니다. */
        uint64                                       _playedEventCount;               /**< 재생한 이벤트 수입니다. 게시용입니다. */
        uint64                                       _droppedEventCount;              /**< 버린 이벤트 수입니다. 게시용입니다. */
        uint64                                       _lastPublishNanoseconds;         /**< 지난 블록의 게시 비용(ns)입니다. 다음 블록의 상태로 게시합니다(측정, E6). */
        uint64                                       _peakPublishNanoseconds;         /**< 게시 비용의 최댓값(ns)입니다. */
        uint32                                       _peakRealVoiceCount;             /**< 한 블록에 섞은 보이스의 최댓값입니다. 게시용입니다. */
        uint32                                       _blockCursor;                    /**< `_blockOutput` 에서 다음에 내보낼 프레임입니다. */
        AudioChannelLayout                           _layout;                         /**< 출력 채널 구성입니다. 초기화와 `setChannelLayout`(장치를 닫은 뒤)이 정합니다. */
        // 게시(오디오 스레드 → 게임 스레드)
        AudioPublishedState _published;    /**< 게임 스레드가 읽는 사본입니다(재생 목록, 상태, 음악 위치). */
        atomic<bool>        _bInitialized; /**< 초기화되었는지입니다. */
    };
} // namespace sw
