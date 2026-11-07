/**
 * @file Engine/Audio/AudioSoundPlayer.h
 * @brief 소리 창구(`ISoundPlayer`)를 오디오 서비스로 잇는 구현입니다. 오디오 서비스가 멤버로 하나 들고 기동 단계가 엔진 서비스 칸으로 겁니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Audio/AudioTypes.h"
#include "Engine/Presentation/ISoundPlayer.h"

namespace sw
{
    class AudioEngine;
    class IAudioService;

    /**
     * @class AudioSoundPlayer
     * @brief 엔진 · 게임 코드의 소리 요청을 오디오 서비스와 엔진에 넘깁니다. 상태는 한 번 쓰는 에미터 번호뿐입니다.
     * @details 서비스가 서지 않았으면(초기화 전 · 내린 뒤) 내지 않고 0 · false 를 돌려준다. 이벤트 라이브러리를 올리는 일은 할 것이 없으므로 true 다.
     */
    class SW_API AudioSoundPlayer final : public ISoundPlayer
    {
    public:
        explicit AudioSoundPlayer( IAudioService& audio );

        AudioSoundPlayer( const AudioSoundPlayer& )            = delete;
        AudioSoundPlayer& operator=( const AudioSoundPlayer& ) = delete;

        bool               play( const AssetID& assetID, const hashed_string& bus ) override;
        bool               playMusic( const AssetID& assetID ) override;
        [[nodiscard]] bool loadEvents( const AssetID& assetID ) override;
        void               unloadEvents( string_view path ) override;
        SoundPlayingID     postEvent( const hashed_string& eventName ) override;
        SoundPlayingID     postEventAt( const hashed_string& eventName, const float3& position ) override;
        SoundPlayingID     postEventOn( GameObject& target, const hashed_string& eventName ) override;
        void               setObjectParameter( const GameObject& object, const hashed_string& name, float32 value ) override;
        void               stop( SoundPlayingID playingID, float32 fadeSeconds ) override;

    private:
        /** @brief 오디오 엔진입니다. 서비스가 서지 않았으면 nullptr 입니다. */
        AudioEngine* findEngine() const;

    private:
        /** @brief 한 번 쓰는 에미터 id 의 시작입니다 — 컴포넌트 id(작은 수부터 센다)와 겹치지 않는 높은 자리. */
        static constexpr AudioEmitterID kOneShotEmitterBase = AudioEmitterID{ 1 } << 62;

        IAudioService& _audio;              ///< 이 창구를 든 오디오 서비스입니다(수명이 같다).
        atomic<uint64> _nextOneShotEmitter; ///< 다음 한 번 쓰는 에미터의 번호입니다.
    };
} // namespace sw
