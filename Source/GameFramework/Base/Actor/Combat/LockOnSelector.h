/**
 * @file LockOnSelector.h
 * @brief 록온 — 시야 안 후보를 거리 · 화면 가운데에서 벗어난 각 · 우선도로 점수 매겨 고르고, 좌우로 다음 대상으로 넘기고, 멀어지거나 가려지면 풉니다.
 * @details 젤다의 주목(Z 주목), 소울라이크 · 위쳐의 대상 고정, 기체 대전의 록온, 카트 유도 아이템의 목표 고르기가 같은 계산입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;

    /** @brief 후보 하나입니다. id 는 게임의 것(오브젝트 핸들의 묶은 값 등)입니다. */
    struct LockOnCandidate
    {
        float3  _position{};
        uint64  _id{ 0 };
        float32 _priority{ 0.0f };    ///< 클수록 먼저(보스 · 위협)
        uint8   _bVisible{ SW_TRUE }; ///< 가려지지 않았다(시선 판정은 게임이)
    };
} // namespace sw

namespace sw
{
    /** @brief 록온 설정입니다. 각도는 도입니다. */
    struct LockOnSettings
    {
        float32 _maxDistance{ 25.0f };   ///< 눈에서 대상까지의 3D 거리(높이 차도 든다 — 공중 대상)
        float32 _breakDistance{ 32.0f }; ///< 잡은 뒤에는 이만큼까지 유지
        float32 _maxAngle{ 60.0f };      ///< 앞에서 이 각 안만 새로 잡는다
        float32 _angleWeight{ 1.0f };    ///< 점수 = 거리 비 + 각 비 × 이 값 − 우선도
        float32 _lostSightGrace{ 1.0f }; ///< 가려진 뒤 이 초까지는 유지
    };
} // namespace sw

namespace sw
{
    /**
     * @class LockOnSelector
     * @brief 대상 하나를 쥡니다. 매 틱 `update` 로 유지 여부를 보고, `pickBest` · `cycle` 로 고릅니다. 좌표는 +Y 위입니다.
     */
    class SW_GF_API LockOnSelector
    {
    public:
        void setSettings( const LockOnSettings& settings ) { _settings = settings; }

        /** @brief 가장 좋은 후보를 잡습니다. 없으면 0 이고 풀립니다. */
        uint64 pickBest( const float3& eye, const float3& forward, const vector<LockOnCandidate>& listCandidate );
        /** @brief 지금 대상에서 오른쪽(@p direction 1) · 왼쪽(−1)으로 가장 가까운 각의 후보로 넘깁니다. 없으면 그대로입니다. */
        uint64 cycle( const float3& eye, const float3& forward, const vector<LockOnCandidate>& listCandidate, int32 direction );
        /** @brief 유지 판정 — 사라졌거나 너무 멀거나 오래 가려졌으면 풉니다. 쥐고 있으면 true 입니다. */
        bool update( const float3& eye, const vector<LockOnCandidate>& listCandidate, float32 deltaTime );
        void release();

        uint64 getTarget() const { return _target; }
        bool   hasTarget() const { return _target != 0; }

        /** @brief 표적 · 숨은 시간을 씁니다. 설정은 싣지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 앞 방향 기준 가로 각(도, 오른쪽 +)입니다. */
        static float32 computeYawOffset( const float3& eye, const float3& forward, const float3& position );

        LockOnSettings _settings{};
        uint64         _target{ 0 };
        float32        _hiddenTime{ 0.0f };
    };
} // namespace sw
