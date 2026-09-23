/**
 * @file IAudioSystem.h
 * @brief 오디오 시스템의 추상 인터페이스입니다.
 *
 * @note **볼륨과 음소거는 이 인터페이스가 들고 있습니다.** 값 범위(0~1 클램프)와 "음소거는 마스터
 *       한 자리에만 건다" 는 규칙은 백엔드마다 다시 적을 것이 아니라 여기 한 번만 적습니다.
 *       백엔드는 값이 바뀐 뒤 `applyVolume()` 으로 통보만 받아 실제 보이스에 반영합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @class IAudioSystem
     * @brief 오디오 재생과 관리를 위한 추상 인터페이스입니다.
     */
    class SW_API IAudioSystem
    {
    public:
        /** @brief 현재 플랫폼에 맞는 오디오 시스템 인스턴스를 생성합니다. */
        static unique_ptr<IAudioSystem> create();

        virtual ~IAudioSystem() = default;

        IAudioSystem()                                 = default;
        IAudioSystem( const IAudioSystem& )            = delete;
        IAudioSystem& operator=( const IAudioSystem& ) = delete;
        IAudioSystem( IAudioSystem&& )                 = delete;
        IAudioSystem& operator=( IAudioSystem&& )      = delete;

        /** @brief 오디오 시스템을 초기화합니다. */
        virtual bool initialize() = 0;

        /** @brief 오디오 시스템을 해제합니다. */
        virtual void shutdown() = 0;

        /** @brief 초기화가 끝났는지 반환합니다. */
        virtual bool isInitialized() const = 0;

        /** @brief 프레임마다 오디오 시스템을 갱신합니다. */
        virtual void update( float32 deltaSeconds ) = 0;

        /** @brief 단발성 효과음(SFX)을 비동기로 재생합니다. */
        virtual bool play( string_view path ) = 0;

        /** @brief 배경음악(BGM)을 루프로 재생합니다. */
        virtual bool playMusic( string_view path ) = 0;

        /** @brief 현재 재생 중인 배경음악(BGM)을 중지합니다. */
        virtual void stopMusic() = 0;

        /** @brief 배경음악을 일시정지합니다. */
        virtual void pauseMusic() = 0;

        /** @brief 일시정지된 배경음악을 재개합니다. */
        virtual void resumeMusic() = 0;

        /**
         * @brief 마지막으로 요청된 배경음악 경로입니다. 재생 중이 아니면 비어 있습니다.
         * @details 요청 시점에 정해지므로 비동기 디코드가 끝나기 전에도 이미 이 값입니다.
         *          **사본을 반환합니다.** 백엔드가 이 문자열을 다른 스레드에서 바꾸므로
         *          안을 가리키는 뷰를 내주면 잠금 밖에서 흔들립니다.
         */
        virtual string getMusicPath() const = 0;

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
         * @details 음소거는 **마스터 볼륨 한 자리에만** 겁니다. 음악 · 효과음 보이스는 자기 볼륨만
         *          들고 있으므로, 음소거 중에 볼륨을 바꿔도 음소거를 풀면 그대로 돌아옵니다.
         *          예전에는 세 자리에 걸려 있어서 "음소거 → 볼륨 조절 → 음소거 해제" 뒤에
         *          효과음이 영영 들리지 않았습니다.
         */
        void setMute( bool bMute );
        /** @brief 현재 음소거 상태인지 반환합니다. */
        bool isMuted() const { return _bMuted; }

        /** @brief 마스터 보이스에 실제로 걸 볼륨입니다. 음소거면 0 입니다. */
        float32 getEffectiveMasterVolume() const { return _bMuted ? 0.0f : _masterVolume; }

    protected:
        /** @brief 볼륨이나 음소거가 바뀐 뒤 불립니다. 백엔드가 살아 있는 보이스에 반영합니다. */
        virtual void applyVolume() {}

    private:
        float32 _masterVolume{ 1.0f }; /**< 마스터 볼륨 [0,1] 입니다. */
        float32 _musicVolume{ 1.0f };  /**< 배경음악 볼륨 [0,1] 입니다. */
        float32 _sfxVolume{ 1.0f };    /**< 효과음 볼륨 [0,1] 입니다. */
        bool    _bMuted{ false };      /**< 음소거 상태입니다. */
    };
} // namespace sw
