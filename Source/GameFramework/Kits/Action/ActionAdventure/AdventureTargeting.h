/**
 * @file AdventureTargeting.h
 * @brief 젤다의 주목(Z 주목) — 버튼을 누르면 대상을 잡고(없으면 카메라만 앞으로 맞추는 평행 주목), 잡은 동안 옆 입력은 대상을 도는 옆걸음,
 *        점프 + 뒤 입력은 뒤로 공중제비, 점프 + 옆 입력은 옆 뛰기 회피입니다.
 * @details 대상 고르기 · 넘기기 · 놓치기는 기반 `LockOnSelector` 입니다. 이 파일은 그 위의 "주목 중 몸놀림" 상태 기계만 듭니다.
 *          좌표는 +Y 위, 오른쪽은 앞 × 위의 반대(앞이 +Z 면 오른쪽이 +X)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/Combat/LockOnSelector.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 주목 상태입니다. */
    enum class AdventureTargetingState : uint8
    {
        Free = 0,    ///< 주목하지 않는다
        Parallel,    ///< 버튼은 누르고 있지만 대상이 없다 — 카메라가 앞을 본다(옆걸음은 된다)
        Locked,      ///< 대상을 보고 선다
        StrafeLeft,  ///< 대상을 왼쪽으로 돈다
        StrafeRight, ///< 대상을 오른쪽으로 돈다
        Backflip,    ///< 뒤로 공중제비(회피)
        SideHopLeft, ///< 왼쪽 옆 뛰기(회피)
        SideHopRight ///< 오른쪽 옆 뛰기(회피)
    };

    SW_GF_API const utf8* toString( AdventureTargetingState state );

    /** @brief 주목 설정입니다. 시간은 초입니다. */
    struct AdventureTargetingSettings
    {
        LockOnSettings _lockOn{};
        float32        _deadZone{ 0.3f };         ///< 이 아래 입력은 없는 것으로 본다
        float32        _backflipDuration{ 0.6f }; ///< 회피 동작 동안은 다른 입력을 받지 않는다
        float32        _sideHopDuration{ 0.4f };
        float32        _evadeInvulnerable{ 0.25f }; ///< 회피 시작부터 이만큼은 무적(게임이 `isInvulnerable` 로 본다)
    };

    /**
     * @class AdventureTargeting
     * @brief 플레이어 한 명의 주목입니다. 매 틱 `update` 에 대상 후보 · 이동 입력(x 오른쪽 · y 앞, −1..1) · 점프를 넘깁니다.
     */
    class SW_GF_API AdventureTargeting
    {
    public:
        AdventureTargeting();

        void initialize( const AdventureTargetingSettings& settings );

        /** @brief 주목 버튼을 눌렀습니다. 대상을 잡았으면 true(아니면 평행 주목)입니다. */
        bool press( const float3& eye, const float3& forward, const vector<LockOnCandidate>& listCandidate );
        /** @brief 주목 버튼을 뗐습니다. */
        void release();
        /** @brief 잡은 채로 다음 대상으로 넘깁니다(@p direction 1 오른쪽 · −1 왼쪽). */
        uint64 cycleTarget( const float3& eye, const float3& forward, const vector<LockOnCandidate>& listCandidate, int32 direction );
        /** @brief 한 틱 — 대상 유지 판정과 몸놀림 상태입니다. */
        AdventureTargetingState update( const float3& eye, const vector<LockOnCandidate>& listCandidate, const float2& moveInput, bool bJumpPressed, float32 deltaTime );
        /**
         * @brief 이동 입력을 월드 방향(XZ, 길이는 입력 크기)으로 바꿉니다. 대상을 잡았으면 대상 쪽이 앞이라 옆 입력이 대상을 도는 원의 접선이 되고,
         *        아니면 @p cameraForward 가 앞입니다.
         */
        float3 computeMoveDirection( const float3& position, const float3& cameraForward, const float2& moveInput ) const;

        AdventureTargetingState getState() const { return _state; }
        uint64                  getTarget() const { return _selector.getTarget(); }
        bool                    hasTarget() const { return _selector.hasTarget(); }
        const float3&           getTargetPosition() const { return _targetPosition; }
        bool                    isHeld() const { return _bHeld == SW_TRUE; }
        bool                    isEvading() const { return _evadeRemaining > 0.0f; }
        bool                    isInvulnerable() const { return _invulnerableRemaining > 0.0f; }

    private:
        void refreshTargetPosition( const vector<LockOnCandidate>& listCandidate );

        AdventureTargetingSettings _settings;
        LockOnSelector             _selector;
        float3                     _targetPosition;
        float32                    _evadeRemaining;
        float32                    _invulnerableRemaining;
        AdventureTargetingState    _state;
        uint8                      _bHeld;
    };
} // namespace sw
