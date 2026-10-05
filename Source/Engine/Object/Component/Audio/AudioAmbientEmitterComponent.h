/**
 * @file AudioAmbientEmitterComponent.h
 * @brief 환경음(바람 · 강 · 숲 · 기계 웅웅거림)을 점이나 영역(상자 · 구)에서 루프로 내는 컴포넌트입니다.
 * @details 영역 에미터는 리스너 쪽 가장 가까운 점에서 소리를 냅니다 — 영역 안에 서면 거리 0(가운데 팬), 밖이면 가장자리까지의 거리로 감쇠합니다
 *          (언리얼 Audio Volume · Wwise 의 "가장 가까운 점" 앰비언트 기법). 멀어지면 엔진의 가상 보이스가 되어 섞는 비용이 들지 않습니다.
 *          이벤트는 루프(`_bLoop`)로 만들고 `ambient` 버스로 보냅니다. 레이캐스트 가림은 기본으로 끕니다(영역 소리는 벽을 넘어 번진다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Audio/AudioSpatial.h"
#include "Engine/Object/Component/Audio/AudioEmitterComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class AudioAmbientEmitterComponent
     * @brief 점 · 상자 · 구 모양의 앰비언트 에미터입니다. 모양은 이 컴포넌트의 월드 위치가 가운데이고 축 정렬입니다(회전은 무시, 크기는 데이터 값).
     */
    REFLECT( Category = "Audio", DisplayName = "Audio Ambient Emitter", Tooltip = "Looping ambience from a point, box or sphere (nearest point to the listener)" )
    class SW_API AudioAmbientEmitterComponent : public AudioEmitterComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 기본 값(상자 반 크기 5 m, 가림 끔)으로 만듭니다. */
        AudioAmbientEmitterComponent();
        /** @brief 기본 소멸자입니다. */
        virtual ~AudioAmbientEmitterComponent() override = default;

        /** @brief 리스너 쪽 가장 가까운 점입니다(점 모양은 월드 위치). */
        float3 computeAudioPosition( const float3& listenerPosition ) const override;

        /** @brief 모양을 정합니다. */
        void setShape( AudioVolumeShape shape, const float3& halfExtents, float32 radius );
        /** @brief 모양입니다. */
        AudioVolumeShape getShape() const { return _shape; }

    private:
        PROPERTY( Category = "Ambience", DisplayName = "Half Extents", Tooltip = "Box half size", Units = m )
        float3 _halfExtents;
        PROPERTY( Category = "Ambience", DisplayName = "Radius", Min = 0.0, Tooltip = "Sphere radius", Units = m )
        float32 _radius;
        PROPERTY( Category = "Ambience", DisplayName = "Shape", Tooltip = "Point, Box or Sphere" )
        AudioVolumeShape _shape;
    };
} // namespace sw
