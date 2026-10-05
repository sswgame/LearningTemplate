/**
 * @file AudioListenerComponent.h
 * @brief 리스너(귀)를 카메라가 아닌 곳에 두는 컴포넌트입니다 — 3인칭에서 캐릭터 머리, 분할 화면의 둘째 플레이어, 2D 화면 팬의 반폭 지정.
 * @details 씬에 이것이 없으면 게임 카메라가 리스너입니다(`SceneAudio`). 칸 번호(`_listenerIndex`, 0..3)가 같은 것이 여럿이면 나중 것이 이깁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Audio/AudioSpatial.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class AudioListenerComponent
     * @brief 리스너 칸 하나입니다. 앞 · 위는 이 컴포넌트의 월드 회전(+Z 앞, +Y 위)입니다.
     */
    REFLECT( Category = "Audio", DisplayName = "Audio Listener", Tooltip = "Where the player hears from (overrides the game camera)" )
    class SW_API AudioListenerComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 지난 프레임의 자리입니다(속도). `SceneAudio` 만 씁니다. */
        float3 _lastPosition;
        /** @brief `_lastPosition` 이 유효한지입니다. `SceneAudio` 만 씁니다. */
        bool _bHasLastPosition;

        /** @brief 칸 0 · 3D 로 만듭니다. */
        AudioListenerComponent();
        /** @brief 기본 소멸자입니다. */
        virtual ~AudioListenerComponent() override = default;

        /** @brief 씬 오디오에 등록합니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 씬 오디오에서 뺍니다. */
        void onUnregister( GameObjectManager& manager ) override;

        /** @brief 이 리스너의 상태입니다(자리 · 앞 · 위 · 모드 · 반폭). 속도는 `SceneAudio` 가 넣습니다. */
        AudioListenerState makeListenerState() const;

        /** @brief 칸 번호입니다. */
        uint32 getListenerIndex() const { return _listenerIndex; }
        /** @brief 칸 번호를 정합니다(0..3). */
        void setListenerIndex( uint32 listenerIndex ) { _listenerIndex = listenerIndex; }
        /** @brief 모드와 2D 반폭을 정합니다. */
        void setMode( AudioSpatialMode mode, float32 screenHalfWidth );

    private:
        PROPERTY( Category = "Listener", DisplayName = "Screen Half Width", Min = 0.01, Tooltip = "Screen2D: horizontal distance where panning reaches full left/right", Units = m )
        float32 _screenHalfWidth;
        PROPERTY( Category = "Listener", DisplayName = "Listener Index", Min = 0, Max = 3, Tooltip = "Listener slot (split screen)" )
        uint32 _listenerIndex;
        PROPERTY( Category = "Listener", DisplayName = "Mode", Tooltip = "World3D (camera axes) or Screen2D (screen-plane panning, no elevation)" )
        AudioSpatialMode _mode;
    };
} // namespace sw
