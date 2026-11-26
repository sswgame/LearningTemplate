/**
 * @file PhysicsAudioOcclusionQuery.h
 * @brief 물리 씬 레이캐스트(`IPhysicsQueryScene3D::raycast` · `IPhysicsQueryScene2D::raycast`)로 측정하는 소리 가림입니다 — 리스너 → 에미터 레이 셋(가운데 · 좌우 0.4 m) 중 막힌 비율.
 * @details 레이 셋이라 모서리 뒤로 반쯤 가린 소리는 1/3 · 2/3 처럼 중간 값이 나와 엔진의 추종과 함께 부드럽게 바뀝니다. 출발점 0.3 m 안(리스너가 서 있는
 *          캐릭터 캡슐)과 에미터 0.5 m 안(소리 내는 물체 자신의 콜라이더)의 닿음은 세지 않습니다. 트리거는 보지 않습니다.
 *          2D 물리 세계는 월드의 XY 평면이라(Z 는 레이어) 2D 씬이면 리스너와 에미터의 XY 만 쓰고 좌우는 평면 안에서 벌립니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Audio/AudioSpatial.h"
#include "Engine/Physics/IPhysicsQueryScene.h"

namespace sw
{
    /**
     * @class PhysicsAudioOcclusionQuery
     * @brief 물리 씬 레이캐스트 가림 질의입니다. 씬(3D 또는 2D)은 프레임마다 정합니다(`setScene`). 씬이 없으면 0 입니다.
     */
    class SW_API PhysicsAudioOcclusionQuery final : public IAudioOcclusionQuery
    {
    public:
        /** @brief 레이 하나가 좌우로 벌어지는 거리(m)입니다. */
        static constexpr float32 kSideOffset = 0.4f;
        /** @brief 리스너 쪽에서 세지 않는 거리(m)입니다. */
        static constexpr float32 kListenerSkin = 0.3f;
        /** @brief 에미터 쪽에서 세지 않는 거리(m)입니다. */
        static constexpr float32 kEmitterSkin = 0.5f;

        PhysicsAudioOcclusionQuery();
        ~PhysicsAudioOcclusionQuery() override = default;

        /** @brief 쏠 3D 씬과 레이어 마스크입니다(2D 씬은 놓습니다). */
        void setScene( const IPhysicsQueryScene3D* pScene, uint32 layerMask );
        /** @brief 쏠 2D 씬과 레이어 마스크입니다(3D 씬은 놓습니다). */
        void setScene( const IPhysicsQueryScene2D* pScene, uint32 layerMask );
        /** @brief 가림은 막힌 레이의 비율(0, 1/3, 2/3, 1)입니다. 레이로는 길을 모르므로 회절과 겉보기 위치는 없습니다. */
        AudioPropagation computePropagation( const float3& listener, const float3& emitter ) const override;

    private:
        /** @brief 레이 하나가 에미터 앞에서 막히는지입니다. */
        bool isBlocked( const float3& from, const float3& to ) const;
        bool isBlocked( const float2& from, const float2& to ) const;

    private:
        const IPhysicsQueryScene3D* _pScene3D;  /**< 쏠 3D 씬입니다. */
        const IPhysicsQueryScene2D* _pScene2D;  /**< 쏠 2D 씬입니다(3D 씬이 없을 때만). */
        uint32                      _layerMask; /**< 볼 레이어입니다. */
    };
} // namespace sw
