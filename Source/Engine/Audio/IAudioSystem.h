/**
 * @file IAudioSystem.h
 * @brief 오디오 서비스 — 엔진(`AudioEngine`: 믹서 그래프 · 보이스 · 이벤트 · 음악)을 하나 들고, 플랫폼 백엔드는 출력 장치만 맡습니다.
 *
 * @note **볼륨과 음소거는 이 서비스가 들고 있습니다.** 값 범위(0~1 클램프)와 "음소거는 master 버스 한 자리에만 건다" 는 규칙은 여기
 *       한 번만 적고, 값은 같은 이름의 믹서 버스 사용자 볼륨으로 엔진에 갑니다.
 * @note **장치가 없으면**(Null 백엔드 · 장치를 못 연 Windows · 시험) `update` 가 흐른 시간만큼 엔진을 버퍼에 렌더합니다 — 재생 상태
 *       (끝남 · 음악 박자 · 가상 보이스)는 장치가 있을 때와 같이 진행합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Audio/AudioTypes.h"

namespace sw
{
    class AudioEngine;

    /**
     * @brief 이름 붙인 볼륨 버스입니다. 세 버스(`master` · `music` · `sfx`)는 기존 볼륨 칸으로 가고, 나머지 이름은 `IAudioSystem` 의 표에 남습니다.
     * @details 어느 이름이든 같은 이름의 믹서 버스 사용자 볼륨으로도 갑니다(`AudioEngine::setBusUserVolume`). 그래프에 없는 이름은 값만 남습니다.
     */
    struct AudioBusVolume
    {
        hashed_string _bus{};          ///< 버스 이름
        float32       _volume{ 1.0f }; ///< 버스 볼륨 [0,1]
    };
} // namespace sw

namespace sw
{
    /**
     * @class IAudioSystem
     * @brief 오디오 재생과 관리를 위한 서비스입니다. 백엔드(XAudio2 · Null)는 출력 장치를 열고 닫는 일만 재정의합니다.
     */
    class SW_API IAudioSystem
    {
    public:
        /** @brief 엔진의 기본 믹서 그래프 경로입니다. 없으면 코드의 기본 그래프(`AudioMixerDesc::makeDefault`)를 씁니다. */
        static constexpr utf8 kDefaultMixerPath[] = "engine/audio/default.audiomixer.xml";

        /** @brief 현재 플랫폼에 맞는 오디오 시스템 인스턴스를 생성합니다. */
        static unique_ptr<IAudioSystem> create();

        IAudioSystem();
        virtual ~IAudioSystem();

        IAudioSystem( const IAudioSystem& )            = delete;
        IAudioSystem& operator=( const IAudioSystem& ) = delete;
        IAudioSystem( IAudioSystem&& )                 = delete;
        IAudioSystem& operator=( IAudioSystem&& )      = delete;

        /** @brief 엔진을 만들고(기본 믹서 그래프) 출력 장치를 엽니다. 장치를 못 열어도 성공입니다(오프라인 진행). */
        bool initialize();
        /** @brief 출력 장치를 닫고 엔진을 내립니다. */
        void shutdown();
        /** @brief 초기화가 끝났는지 반환합니다. */
        bool isInitialized() const { return _bInitialized; }
        /** @brief 출력 장치가 엔진을 당기고 있는지입니다. false 면 `update` 가 렌더합니다. */
        bool isOutputOpen() const { return _bOutputOpen; }

        /** @brief 프레임마다 부릅니다. 장치가 없으면 흐른 시간만큼 엔진을 렌더해 재생을 진행합니다. */
        void update( float32 deltaSeconds );

        /**
         * @brief 클립 하나를 비동기로 재생합니다(디코드는 워커).
         * @param bus 보낼 버스입니다. 비었으면 `sfx` 입니다(`voice` · `ambient` · `ui` …).
         * @return 경로가 비었거나 리소스가 없으면 false 입니다.
         */
        bool play( string_view path, const hashed_string& bus = hashed_string{} );

        /**
         * @brief 소리를 **지금** 디코드해 캐시에 올립니다(언리얼 사운드 프리캐시 · 유니티 `AudioClip.LoadAudioData`).
         * @details 디코드할 수 없는 형식 · 없는 파일이면 false 입니다.
         */
        bool preload( string_view path );

        /** @brief 배경음악(BGM)을 `music` 버스에서 루프로 재생합니다. 같은 곡이 재생 중이면 다시 시작하지 않고, 다른 곡이면 짧게 크로스페이드합니다. */
        bool playMusic( string_view path );
        /** @brief 현재 재생 중인 배경음악(BGM)을 중지합니다. */
        void stopMusic();
        /** @brief 배경음악을 일시정지합니다. */
        void pauseMusic();
        /** @brief 일시정지된 배경음악을 재개합니다. */
        void resumeMusic();
        /** @brief 마지막으로 요청된 배경음악 경로입니다. 재생 중이 아니면 비어 있습니다. 요청 시점에 정해집니다. */
        string getMusicPath() const { return _musicPath; }

        /** @brief 믹서 그래프를 리소스에서 읽어 바꿉니다(게임이 자기 그래프를 쓸 때). 읽기 · 검사에 실패하면 false 이고 지금 그래프를 그대로 씁니다. */
        [[nodiscard]] bool loadMixer( string_view resourcePath );

        /** @brief 엔진입니다 — 이벤트 · 파라미터 · 스냅샷 · 음악 · 공간화는 여기서 씁니다. */
        AudioEngine& getEngine() { return *_pEngine; }
        /** @brief 엔진입니다. */
        const AudioEngine& getEngine() const { return *_pEngine; }

        /** @brief 전체 마스터 볼륨을 설정합니다. 0~1 밖의 값은 잘립니다. */
        void setMasterVolume( float32 volume );
        /** @brief 현재 마스터 볼륨입니다. */
        float32 getMasterVolume() const { return _masterVolume; }

        /** @brief 배경음악(BGM) 볼륨을 설정합니다. 0~1 밖의 값은 잘립니다. */
        void setMusicVolume( float32 volume );
        /** @brief 현재 배경음악 볼륨입니다. */
        float32 getMusicVolume() const { return _musicVolume; }

        /** @brief 효과음(SFX) 볼륨을 설정합니다. 0~1 밖의 값은 잘립니다. */
        void setSfxVolume( float32 volume );
        /** @brief 현재 효과음 볼륨입니다. */
        float32 getSfxVolume() const { return _sfxVolume; }

        /**
         * @brief 전체 음소거 여부를 설정합니다.
         * @details 음소거는 **master 버스 한 자리에만** 겁니다. 다른 버스는 자기 볼륨만 들고 있으므로, 음소거 중에 볼륨을 바꿔도 음소거를 풀면
         *          그대로 돌아옵니다.
         */
        void setMute( bool bMute );
        /** @brief 현재 음소거 상태인지 반환합니다. */
        bool isMuted() const { return _bMuted; }

        /** @brief master 에 실제로 걸리는 볼륨입니다. 음소거면 0 입니다. */
        float32 getEffectiveMasterVolume() const { return _bMuted ? 0.0f : _masterVolume; }

        /** @brief 이름 붙인 버스의 볼륨을 설정합니다. 0~1 밖의 값은 잘립니다. 같은 이름의 믹서 버스에 사용자 볼륨으로 겁니다. */
        void setBusVolume( const hashed_string& bus, float32 volume );
        /** @brief 버스 볼륨입니다. 설정한 적 없는 버스는 1 입니다. */
        float32 getBusVolume( const hashed_string& bus ) const;

    protected:
        /**
         * @brief 출력 장치를 열어 엔진의 `render` 를 장치 스레드에서 당기기 시작합니다.
         * @return 장치가 없으면 false 입니다 — 그때는 `update` 가 렌더합니다.
         */
        [[nodiscard]] virtual bool openOutput() { return false; }
        /** @brief 출력 장치를 닫습니다. 돌아온 뒤에는 장치 스레드가 `render` 를 부르지 않습니다. */
        virtual void closeOutput() {}

    private:
        unique_ptr<AudioEngine> _pEngine;          /**< 오디오 엔진입니다. */
        vector<AudioBusVolume>  _listBusVolume;    /**< 세 기본 버스 밖의 버스 볼륨입니다. */
        vector<float32>         _listOfflineBlock; /**< 장치가 없을 때 렌더하는 버퍼입니다. */
        string                  _musicPath;        /**< 마지막으로 요청된 배경음악 경로입니다. */
        AudioPlayingId          _musicPlayingId;   /**< 배경음악 재생 id 입니다. */
        float64                 _offlineFrameDebt; /**< 장치가 없을 때 아직 렌더하지 않은 프레임(소수)입니다. */
        float32                 _masterVolume;     /**< 마스터 볼륨 [0,1] 입니다. */
        float32                 _musicVolume;      /**< 배경음악 볼륨 [0,1] 입니다. */
        float32                 _sfxVolume;        /**< 효과음 볼륨 [0,1] 입니다. */
        bool                    _bMuted;           /**< 음소거 상태입니다. */
        bool                    _bOutputOpen;      /**< 장치가 엔진을 당기고 있는지입니다. */
        bool                    _bInitialized;     /**< initialize 가 끝났는지입니다. */
    };
} // namespace sw
