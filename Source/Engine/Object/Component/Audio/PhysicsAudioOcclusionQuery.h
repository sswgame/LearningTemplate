/**
 * @file PhysicsAudioOcclusionQuery.h
 * @brief 3D 물리 씬(`IPhysicsScene3D::raycast`)으로 재는 소리 가림입니다 — 리스너 → 에미터 레이 셋(가운데 · 좌우 0.4 m) 중 막힌 비율.
 * @details 레이 셋이라 모서리 뒤로 반쯤 가린 소리는 1/3 · 2/3 처럼 중간 값이 나와 엔진의 추종과 함께 부드럽게 바뀝니다. 출발점 0.3 m 안(리스너가 서 있는
 *          캐릭터 캡슐)과 에미터 0.5 m 안(소리 내는 물체 자신의 콜라이더)의 닿음은 세지 않습니다. 트리거는 보지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Audio/AudioSpatial.h"
#include "Engine/Physics/IPhysicsScene.h"

namespace sw
{
    /**
     * @class PhysicsAudioOcclusionQuery
     * @brief 물리 씬 레이캐스트 가림 질의입니다. 씬은 프레임마다 정합니다(`setScene`). 씬이 없으면 0 입니다.
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

        /** @brief 쏠 씬과 레이어 마스크입니다. */
        void setScene( const IPhysicsScene3D* pScene, uint32 layerMask );
        /** @brief 막힌 레이의 비율(0, 1/3, 2/3, 1)입니다. */
        float32 computeOcclusion( const float3& listener, const float3& emitter ) const override;

    private:
        /** @brief 레이 하나가 에미터 앞에서 막히는지입니다. */
        bool isBlocked( const float3& from, const float3& to ) const;

    private:
        const IPhysicsScene3D* _pScene;    /**< 쏠 씬입니다. */
        uint32                 _layerMask; /**< 볼 레이어입니다. */
    };
} // namespace sw
