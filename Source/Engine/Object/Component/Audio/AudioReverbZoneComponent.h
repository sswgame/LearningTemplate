/**
 * @file AudioReverbZoneComponent.h
 * @brief 리버브 존 — 리스너가 들어오면 믹스 스냅샷(동굴 · 홀 · 물속)을 세기로 겁니다. 경계에서 `_fadeDistance` 만큼 안쪽으로 0 → 1 블렌드합니다.
 * @details 언리얼 Audio Volume(Reverb Settings · Submix Send) · Wwise Room/Aux Bus · 유니티 Audio Reverb Zone 의 자리입니다. 스냅샷이 리버브 버스의
 *          파라미터(방 크기 · 프리딜레이)와 센드 레벨을 정하므로, 존은 "어떤 스냅샷을 얼마나" 만 말합니다. 같은 스냅샷을 거는 존이 여럿 겹치면 가장 큰 세기입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Audio/AudioSpatial.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class AudioReverbZoneComponent
     * @brief 상자 · 구 모양의 리버브 존입니다. 모양은 이 컴포넌트의 월드 위치가 가운데이고 축 정렬입니다.
     */
    REFLECT( Category = "Audio", DisplayName = "Audio Reverb Zone", Tooltip = "Applies a mix snapshot (reverb) while the listener is inside" )
    class SW_API AudioReverbZoneComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 기본 값(상자 반 크기 5 m, 블렌드 1 m)으로 만듭니다. */
        AudioReverbZoneComponent();
        /** @brief 기본 소멸자입니다. */
        virtual ~AudioReverbZoneComponent() override = default;

        /** @brief 씬 오디오에 등록합니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 씬 오디오에서 뺍니다(다음 프레임에 세기가 빠진다). */
        void onUnregister( GameObjectManager& manager ) override;

        /** @brief @p listenerPosition 에서의 세기(0..1)입니다. */
        float32 computeIntensity( const float3& listenerPosition ) const;

        /** @brief 거는 스냅샷입니다. */
        const hashed_string& getSnapshot() const { return _snapshot; }
        /** @brief 거는 스냅샷을 정합니다. */
        void setSnapshot( const hashed_string& snapshot ) { _snapshot = snapshot; }
        /** @brief 모양 · 블렌드 거리를 정합니다. */
        void setShape( AudioVolumeShape shape, const float3& halfExtents, float32 radius, float32 fadeDistance );

    private:
        PROPERTY( Category = "Reverb", DisplayName = "Snapshot", Tooltip = "Mix snapshot applied inside (Cave, Hall, Underwater ...)" )
        hashed_string _snapshot;
        PROPERTY( Category = "Reverb", DisplayName = "Half Extents", Tooltip = "Box half size", Meta = "Units=m" )
        float3 _halfExtents;
        PROPERTY( Category = "Reverb", DisplayName = "Radius", Min = 0.0, Tooltip = "Sphere radius", Meta = "Units=m" )
        float32 _radius;
        PROPERTY( Category = "Reverb", DisplayName = "Fade Distance", Min = 0.0, Tooltip = "Blend from 0 at the boundary to 1 this far inside", Meta = "Units=m" )
        float32 _fadeDistance;
        PROPERTY( Category = "Reverb", DisplayName = "Shape", Tooltip = "Box or Sphere" )
        AudioVolumeShape _shape;
    };
} // namespace sw
