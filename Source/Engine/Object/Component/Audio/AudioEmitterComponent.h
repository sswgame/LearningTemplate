/**
 * @file AudioEmitterComponent.h
 * @brief 오브젝트 자리에서 사운드 이벤트를 내는 컴포넌트입니다(언리얼 `UAudioComponent` · Wwise `AkGameObj` · 유니티 `AudioSource` 의 자리).
 * @details 에미터 id 는 컴포넌트 id 입니다(프로세스 전체에서 겹치지 않는다). 자리 · 속도(도플러) · 가림은 씬의 `SceneAudio` 가 틱 뒤에 프레임마다 엔진에 넣고,
 *          이 컴포넌트는 틱하지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Audio/AudioTypes.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class AudioEngine;
    class GameObjectManager;

    /**
     * @class AudioEmitterComponent
     * @brief 사운드 이벤트를 내는 자리입니다. `_event` 를 시작할 때 내고(`_bPlayOnBeginPlay`), 끝날 때 멈춥니다(`_bStopOnEndPlay`).
     */
    REFLECT( Category = "Audio", DisplayName = "Audio Emitter", Tooltip = "Posts sound events from this object's position" )
    class SW_API AudioEmitterComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 지난 프레임에 엔진에 넣은 자리입니다(속도 = 차이 / 시간). `SceneAudio` 만 씁니다. */
        float3 _lastAudioPosition;
        /** @brief `_lastAudioPosition` 이 유효한지입니다. `SceneAudio` 만 씁니다. */
        bool _bHasLastAudioPosition;

        /** @brief 기본 값으로 만듭니다. */
        AudioEmitterComponent();
        /** @brief 기본 소멸자입니다. */
        virtual ~AudioEmitterComponent() override = default;

        /** @brief 씬 오디오에 등록합니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 씬 오디오에서 뺍니다. 이 자리의 소리는 마지막 자리에서 끝까지 갑니다(`_bStopOnEndPlay` 면 끝날 때 멈춤). */
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief `_bPlayOnBeginPlay` 면 `_event` 를 냅니다. */
        void onBeginPlay() override;
        /** @brief `_bStopOnEndPlay` 면 이 자리의 소리를 이벤트의 페이드로 멈춥니다. */
        void onEndPlay() override;

        /** @brief 이 자리에서 이벤트를 냅니다. 엔진이 없으면(헤드리스) 0 입니다. 어느 스레드에서 불러도 됩니다. */
        AudioPlayingId post( const hashed_string& eventName );
        /** @brief 이 자리의 소리를 모두 멈춥니다(음수 페이드는 이벤트의 값). */
        void stopAll( float32 fadeSeconds );
        /** @brief 이 자리에만 걸리는 파라미터 값입니다(엔진 `setEmitterParameter`). */
        void setParameter( const hashed_string& name, float32 value );

        /** @brief 엔진의 에미터 id 입니다(컴포넌트 id). */
        AudioEmitterId getEmitterId() const { return getComponentId(); }
        /** @brief 시작할 때 내는 이벤트입니다. */
        const hashed_string& getEvent() const { return _event; }
        /** @brief 시작할 때 낼 이벤트를 정합니다. */
        void setEvent( const hashed_string& eventName ) { _event = eventName; }
        /** @brief 레이캐스트 가림을 재는지입니다. */
        bool usesOcclusion() const { return _bOcclusion; }
        /** @brief 레이캐스트 가림을 켜고 끕니다. */
        void setOcclusion( bool bOcclusion ) { _bOcclusion = bOcclusion; }
        /** @brief 시작할 때 낼지 정합니다. */
        void setPlayOnBeginPlay( bool bPlay ) { _bPlayOnBeginPlay = bPlay; }

        /** @brief 엔진에 넣을 소리의 자리입니다. 점 에미터는 월드 위치이고, 넓은 앰비언트는 리스너 쪽 가장 가까운 점입니다. */
        virtual float3 computeAudioPosition( const float3& listenerPosition ) const;

    protected:
        /** @brief 이 컴포넌트가 쓰는 엔진입니다(씬 오디오가 정한다). 없으면 nullptr 입니다. */
        AudioEngine* findAudioEngine() const;

    private:
        PROPERTY( Category = "Audio", DisplayName = "Event", Tooltip = "Event posted on begin play" )
        hashed_string _event;
        PROPERTY( Category = "Audio", DisplayName = "Play On Begin Play", Tooltip = "Post the event when play begins" )
        bool _bPlayOnBeginPlay;
        PROPERTY( Category = "Audio", DisplayName = "Stop On End Play", Tooltip = "Stop this emitter's sounds when play ends" )
        bool _bStopOnEndPlay;
        PROPERTY( Category = "Audio", DisplayName = "Occlusion", Tooltip = "Raycast between listener and emitter to muffle sound behind walls" )
        bool _bOcclusion;
    };
} // namespace sw
