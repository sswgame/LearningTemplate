/**
 * @file GhostEncounter.h
 * @brief 방 하나의 유령 싸움 — 유령 상태(숨음 → 나타남 → 공격 → 기절 → 흡입 중 → 잡힘), 손전등 원뿔(각 · 거리)과 모아 쏘는 스트로브,
 *        청소기 흡입 줄다리기(유령 체력 · 도망 방향 · 반대로 당기면 보너스 피해와 서지 게이지 · 끌려감 · 강화 단계)입니다.
 * @details 유령의 자리는 게임이 움직여 `setGhostPosition` 으로 알립니다 — 이 클래스는 규칙만 듭니다. 원뿔 판정은 기반 `AiPerception::canSee`
 *          (주변 감지 0 · 가림 없음)입니다. 흡입 중 도망 방향은 씨앗 고정 `GameRandom` 이 정해 같은 씨앗 · 같은 입력이면 같은 싸움입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/AI/AiPerception.h"
#include "GameFramework/Base/GameRandom.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct GhostDef;

    class GhostCatalog;

    /** @brief 유령 상태입니다. */
    enum class GhostState : uint8
    {
        Hidden = 0, ///< 보이지 않는다 — 빛도 청소기도 닿지 않는다
        Visible,    ///< 나타났다 — 스트로브에 기절한다
        Attacking,  ///< 공격 동작 — 심장이 드러나 보통 손전등에도 기절한다
        Stunned,    ///< 기절 — 청소기로 빨아들일 수 있다
        Sucking,    ///< 흡입 중 — 줄다리기
        Caught
    };

    SW_GF_API const utf8* toString( GhostState state );

    /** @brief 싸움 중인 유령 하나입니다. */
    struct GhostInstance
    {
        const GhostDef* _pDef{ nullptr };
        float3          _position{};
        float3          _fleeDirection{}; ///< 흡입 중 도망치는 쪽(XZ 단위 벡터)
        float32         _hp{ 0.0f };
        float32         _timer{ 0.0f }; ///< 지금 상태의 남은 시간
        uint32          _id{ 0 };
        GhostState      _state{ GhostState::Hidden };
    };

    /** @brief 유령 알림 종류입니다. */
    enum class GhostEventType : uint8
    {
        Appeared = 0,
        AttackLanded, ///< `_amount` = 피해(맞았는지 — 거리 · 무적 — 는 게임이 본다)
        Stunned,
        SuctionStarted,
        Escaped, ///< 흡입이 끊겨 달아났다(체력은 남는다)
        Caught,  ///< `_coins` = 떨어뜨린 동전
        Surge    ///< `_amount` = 서지 피해
    };

    /** @brief 유령 알림 하나입니다. */
    struct GhostEvent
    {
        float32        _amount{ 0.0f };
        uint32         _ghostId{ 0 };
        int32          _coins{ 0 };
        GhostEventType _type{ GhostEventType::Appeared };
    };

    /** @brief 흡입 한 틱의 결과입니다. */
    struct GhostSuctionTick
    {
        float3  _drag{}; ///< 이번 틱에 플레이어가 끌려간 거리(게임이 플레이어를 옮긴다)
        float32 _damage{ 0.0f };
        float32 _alignment{ 0.0f }; ///< 당기는 방향 · 도망 반대 방향의 내적(−1..1)
        uint8   _bAligned{ SW_FALSE };
        uint8   _bCaught{ SW_FALSE };
    };

    /**
     * @class GhostEncounter
     * @brief 한 방의 유령들과 손전등 · 청소기입니다. 흡입은 한 번에 한 유령입니다.
     */
    class SW_GF_API GhostEncounter
    {
    public:
        GhostEncounter();

        void initialize( const GhostCatalog* pCatalog, uint32 seed );
        /** @brief 유령을 모두 치웁니다(방을 나갔다). 흡입도 끊습니다. */
        void clear();
        /** @brief 유령을 숨은 채로 둡니다. 유령 번호(1 부터)이고 모르는 종류면 0 입니다. */
        uint32 spawnGhost( const hashed_string& ghostId, const float3& position );
        void   setGhostPosition( uint32 ghostId, const float3& position );

        /** @brief 보통 손전등을 비춥니다 — 원뿔 안에서 공격 중인(심장이 드러난) 유령이 기절합니다. 기절시킨 수입니다. */
        int32 shineBeam( const float3& eye, const float3& forward );
        /** @brief 스트로브를 모읍니다(버튼을 누른 동안). */
        void chargeStrobe( float32 deltaTime );
        /** @brief 스트로브를 터뜨립니다. 다 모였으면 넓은 원뿔 안의 나타난 · 공격 중 유령이 기절합니다. 기절시킨 수(덜 모였으면 0)이고 모은 것은 비웁니다. */
        int32 releaseStrobe( const float3& eye, const float3& forward );
        /** @brief 이 자리가 손전등(@p bStrobe 면 스트로브) 원뿔 안인가입니다. */
        bool isInCone( const float3& eye, const float3& forward, const float3& position, bool bStrobe ) const;

        /** @brief 기절한 유령을 빨아들이기 시작합니다. 거리 밖 · 기절이 아님 · 이미 흡입 중이면 false 입니다. */
        [[nodiscard]] bool startSuction( uint32 ghostId, const float3& playerPosition );
        /** @brief 흡입을 그만둡니다(버튼을 뗐다) — 유령이 달아나 숨습니다. */
        void stopSuction();
        /**
         * @brief 흡입 한 틱입니다. @p pullDirection 은 플레이어가 당기는 쪽(XZ — 스틱)입니다. 도망 방향의 반대로 당기면 보너스 피해 · 서지 게이지 · 덜 끌림.
         */
        GhostSuctionTick updateSuction( const float3& pullDirection, float32 deltaTime );
        /** @brief 서지 게이지가 찼으면 한 번에 큰 피해를 줍니다. 흡입 중이 아니거나 덜 찼으면 false 입니다. */
        bool triggerSurge();
        /** @brief 청소기 강화 단계입니다(없는 단계는 마지막 단계로 자른다). */
        void setVacuumStage( int32 stage );
        /** @brief 시간을 흘립니다 — 숨음 · 나타남 · 공격 · 기절 시간과 흡입 중 도망 방향 바꾸기. */
        void update( float32 deltaTime );
        /** @brief 쌓인 알림을 @p outListEvent 뒤에 붙이고 비웁니다. */
        void drainEvents( vector<GhostEvent>& outListEvent );

        const GhostInstance*         findGhost( uint32 ghostId ) const;
        const vector<GhostInstance>& getGhosts() const { return _listGhost; }
        int32                        countRemaining() const;
        uint32                       getSuctionTarget() const { return _suctionTarget; }
        float32                      getStrobeCharge() const { return _strobeCharge; }
        float32                      getSurgeGauge() const { return _surgeGauge; }
        int32                        getVacuumStage() const { return _vacuumStage; }
        float32                      computeVacuumPower() const;

    private:
        GhostInstance* findGhostMutable( uint32 ghostId );
        void           enterState( GhostInstance& ghost, GhostState state );
        void           chooseFleeDirection( GhostInstance& ghost );
        void           applySuctionDamage( GhostInstance& ghost, float32 damage );
        void           pushEvent( GhostEventType type, uint32 ghostId, float32 amount = 0.0f, int32 coins = 0 );

        const GhostCatalog*   _pCatalog;
        GameRandom            _random;
        AiPerception          _beamCone;
        AiPerception          _strobeCone;
        vector<GhostInstance> _listGhost;
        vector<GhostEvent>    _listEvent;
        float32               _strobeCharge;
        float32               _surgeGauge; ///< 0..1
        uint32                _suctionTarget;
        uint32                _nextGhostId;
        int32                 _vacuumStage;
    };
} // namespace sw
