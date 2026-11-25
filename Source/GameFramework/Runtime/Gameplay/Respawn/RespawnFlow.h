/**
 * @file RespawnFlow.h
 * @brief 캐릭터 하나의 사망 → 부활 흐름입니다. 대기 → 자리 고르기(규칙의 정책을 차례로) → 자리 준비(스트리밍 셀) → 스폰(체력 되돌림) → 무적 → 벌칙을 돕니다.
 * @details 언리얼 `AGameModeBase::RestartPlayer`(→ `FindPlayerStart` · `ChoosePlayerStart`)와 Lyra 의 리스폰 타이머 · 스폰 관리자를 캐릭터 하나의 상태 기계로 둔 것이다(기억에 의존).
 *          멀티플레이에서는 서버가 이 흐름을 돌리고(클라이언트 요청은 `request` 가 서버 상태 · 서버 세계 질의로 판정한다), 상태 바이트(`writeState`)가 복제 · 세이브에 실린다.
 *          세계는 `IRespawnWorld` 하나로만 본다 — 안전 지점(내비 메시 · 충돌 질의)과 자리 준비(스트리밍 셀)는 게임 · 월드가 구현한다.
 *          시간은 `update` 로만 흐르고(고정 걸음이면 결정적이다), 몸을 옮기는 일은 게임이 `update` 의 결과(`RespawnSpawn`)로 한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/Time/Countdown.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Respawn/RespawnRules.h"

namespace sw
{
    class Archive;
    class CheckpointState;
    class HealthStateMachine;
    class Wallet;

    /** @brief 스폰 지점 하나입니다(언리얼 `APlayerStart`). 팀이 −1 이면 모든 팀의 지점입니다. 씬 · 카탈로그 데이터에 그대로 둘 수 있다. */
    REFLECT()
    struct SW_GF_API RespawnSpawnPoint
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Name of the point (respawn choices and logs)" )
        hashed_string _id{};
        PROPERTY()
        float3 _position{};
        PROPERTY( Tooltip = "Facing (radians)" )
        float32 _yaw{ 0.0f };
        PROPERTY( Min = -1, Tooltip = "Team that uses the point; -1 = every team" )
        int32 _team{ constant::invalid_index::kInt32 };
    };

    /**
     * @class IRespawnWorld
     * @brief 부활 흐름이 세계에 묻는 두 가지입니다. 서버에서는 서버의 세계가 답합니다(클라이언트가 준 자리를 믿지 않는다).
     */
    class IRespawnWorld
    {
    public:
        virtual ~IRespawnWorld() = default;

        /**
         * @brief @p position 이 설 수 있는 자리(위험 · 막힘 · 낭떠러지가 아님)면 그 자리를, 아니면 @p radius 안의 가장 가까운 안전 지점을 반환합니다. 없으면 nullopt 입니다.
         * @details 3D 게임은 내비 메시 위 가장 가까운 점 · 캡슐 겹침 질의로, 2D 게임은 발밑 칸으로 답한다.
         */
        [[nodiscard]] virtual optional<float3> findSafeLocation( const float3& position, float32 radius ) const = 0;
        /** @brief @p position 의 세계(스트리밍 셀 · 충돌)가 올라 그 자리에 스폰할 수 있으면 true 입니다. 스트리밍하지 않는 세계는 늘 true 입니다. */
        [[nodiscard]] virtual bool isLocationReady( const float3& position ) const = 0;
    };

    /** @brief `update` · `request` 가 한 번 빌려 쓰는 세계입니다. 없는 칸은 그 정책을 건너뜁니다(체크포인트 없음 → `LastCheckpoint` 건너뜀). */
    struct RespawnContext
    {
        const CheckpointState*           _pCheckpoints{ nullptr };
        const vector<RespawnSpawnPoint>* _pSpawnPoints{ nullptr };
        const IRespawnWorld*             _pWorld{ nullptr };  ///< 없으면 `InPlace` 를 건너뛰고 자리 준비는 늘 됐다고 본다
        HealthStateMachine*              _pHealth{ nullptr }; ///< 있으면 스폰 때 `respawn` · 무적을 건다
        Wallet*                          _pWallet{ nullptr }; ///< 있으면 비용 · 통화 벌칙을 뺀다. 없으면 비용 있는 정책을 건너뛴다
        int32                            _team{ constant::invalid_index::kInt32 };
    };

    /** @brief 흐름의 단계입니다. 리플렉션 열거(상태 칸)입니다. */
    ENUM()
    enum class RespawnPhase : uint8
    {
        Alive = 0, ///< 살아 있다(흐름이 쉰다)
        Waiting,   ///< 죽었고 대기 중이다
        Selection, ///< 플레이어 고르기(`PlayerChoice`) — 요청이나 시한을 기다린다
        Search     ///< 자리를 고르는 중이거나 고른 자리가 준비되기를 기다린다
    };

    /** @brief 부활 요청(`request`)의 판정입니다. 값은 원격 호출 결과(`RespawnRemoteTarget::receiveRespawnResult`)이기도 합니다. */
    ENUM( Count = Count )
    enum class RespawnRequestResult : uint8
    {
        Accepted = 0,
        NotInSelection,   ///< 살아 있거나 고르기 단계가 아니다
        PolicyNotAllowed, ///< 규칙에 그 정책이 없다
        UnknownPoint,     ///< 켠 체크포인트 · 고를 수 있는 스폰 지점이 아니다
        CannotAfford,     ///< 비용을 낼 통화가 모자라다(지갑이 없다)
        UsesExhausted,    ///< 그 정책의 횟수를 다 썼다
        NoLocation,       ///< 그 정책이 자리를 내지 못했다(체크포인트 없음 · 안전 지점 없음 · 팀 거점 없음)
        Count
    };

    /** @brief 스폰 한 번의 결과입니다. 게임은 몸을 `_position` · `_yaw` 에 세우고, 경험치 · 내구도 벌칙은 그 체계를 가진 게임이 적용합니다. */
    struct RespawnSpawn
    {
        hashed_string     _pointID{}; ///< 체크포인트 · 스폰 지점 id(제자리면 빈 이름)
        float3            _position{};
        float32           _yaw{ 0.0f };
        int64             _costCharged{ 0 };     ///< 정책 비용으로 뺀 통화
        int64             _currencyPenalty{ 0 }; ///< 벌칙으로 뺀 통화
        float32           _experiencePenaltyRatio{ 0.0f };
        float32           _durabilityPenaltyRatio{ 0.0f };
        RespawnPolicyKind _policy{ RespawnPolicyKind::LastCheckpoint };
    };

    /** @brief 정책 항목 하나가 낸 자리입니다(`RespawnFlow` 안에서만 쓴다). */
    struct RespawnCandidate
    {
        hashed_string _pointID{};
        float3        _position{};
        float32       _yaw{ 0.0f };
    };

    /**
     * @class RespawnFlow
     * @brief 캐릭터 하나의 부활 흐름입니다. 규칙은 `initialize` 가 받고(상태 바이트에 싣지 않는다), 나머지는 상태입니다.
     */
    REFLECT()
    class SW_GF_API RespawnFlow
    {
    public:
        REFLECT_BODY();

        RespawnFlow();

        /** @brief 규칙을 두고 살아 있는 상태로 시작합니다. 규칙이 틀리면(`RespawnRules::describeErrors`) 오류를 남기고 false 이며 상태는 그대로입니다. */
        [[nodiscard]] bool initialize( const RespawnRules& rules );
        /** @brief 살아 있는 상태로 되돌리고 정책 횟수를 비웁니다(판을 처음부터). 규칙은 그대로입니다. */
        void reset();

        /** @brief 죽었음을 알립니다. 살아 있을 때만 받고 대기를 시작합니다(죽은 자리 · 요는 제자리 부활의 기준). 받았으면 true 입니다. */
        bool notifyDied( const float3& position, float32 yaw );
        /**
         * @brief 시간을 흘리고 자리를 고릅니다. 이번 걸음에 스폰했으면 그 결과입니다.
         * @details 대기가 끝난 걸음에 바로 정책을 차례로 시도한다. 저절로 시도하지 않는 정책(`_bRequestOnly`)은 건너뛰고, `PlayerChoice` 에 닿으면 고르기 단계로 멈춘다.
         *          어떤 정책도 자리를 내지 못하면 다음 걸음에 같은 자리부터 다시 본다(거점이 생길 때까지). 고른 자리가 준비되지 않았으면(`isLocationReady`) 기다린다.
         *          스폰 때 그 정책의 횟수를 세고, 비용 · 통화 벌칙을 지갑에서 빼며, 체력을 `respawn` 하고 무적을 건다.
         */
        optional<RespawnSpawn> update( float32 deltaTime, const RespawnContext& context );
        /**
         * @brief 플레이어의 고르기를 판정합니다(서버에서 돈다). 고르기 단계에서만 받습니다.
         * @param kind `PlayerChoice` 면 @p pointHash 의 체크포인트 · 스폰 지점을 고르고, 다른 정책이면 규칙에 있는 그 정책(제자리 · 마지막 체크포인트 …)으로 부활한다.
         * @param pointHash 고른 지점 id 의 해시(`hashed_string::getHash`). 정책을 고를 때는 보지 않는다.
         * @details 자리 · 비용 · 잔액은 받지 않는다 — 제자리 자리는 이 흐름이 아는 죽은 자리에서 @p context 의 세계로, 비용은 그 지갑으로, 횟수는 이 상태로 판정한다.
         *          받으면 고른 자리를 두고 다음 `update` 가 (자리가 준비되면) 스폰한다.
         */
        RespawnRequestResult request( RespawnPolicyKind kind, uint32 pointHash, const RespawnContext& context );
        /** @brief 고르기 단계에서 고를 수 있는 지점 id 들(켠 체크포인트, 내 팀 · 모든 팀 스폰 지점)을 @p outListPointID 뒤에 붙입니다. */
        void collectChoices( const RespawnContext& context, vector<hashed_string>& outListPointID ) const;

        RespawnPhase        getPhase() const { return _phase; }
        bool                isDead() const { return _phase != RespawnPhase::Alive; }
        float32             getDelayRemaining() const { return _delay._remaining > 0.0f ? _delay._remaining : 0.0f; }
        float32             getChoiceRemaining() const { return _choice._remaining > 0.0f ? _choice._remaining : 0.0f; }
        const float3&       getDeathPosition() const { return _deathPosition; }
        const RespawnRules& getRules() const { return _rules; }
        /** @brief 그 정책으로 부활한 횟수입니다(규칙에 없으면 0). */
        int32 getPolicyUses( RespawnPolicyKind kind ) const;

        /**
         * @brief 단계, 죽은 자리 · 요, 정책 횟수, 시도할 자리, 고른 자리, 대기 · 고르기 남은 시간을 씁니다. 규칙은 `initialize` 의 것이라 싣지 않습니다.
         *        읽기 전에 같은 규칙으로 `initialize` 해야 합니다. 정책 수가 다르거나 자리 · 단계가 어긋난 바이트는 거절하고 상태는 그대로입니다.
         */
        SW_DECLARE_STATE_IO();

    private:
        /** @brief 항목 @p entryIndex 의 횟수 · 비용 · 자리를 봅니다. 자리를 내면 `Accepted` 와 @p outCandidate 입니다. */
        RespawnRequestResult resolveEntry( int32 entryIndex, const RespawnContext& context, RespawnCandidate& outCandidate ) const;
        /** @brief 저절로 시도할 정책을 `_cursor` 부터 차례로 봅니다. `PlayerChoice` 에 닿으면 고르기 단계로 바꿉니다. */
        void searchAutomatic( const RespawnContext& context );
        void setPending( int32 entryIndex, const RespawnCandidate& candidate );
        void clearPending();
        /** @brief 고른 자리에 스폰합니다. 비용을 낼 수 없게 됐으면 고른 자리를 버리고 nullopt 입니다. */
        optional<RespawnSpawn> spawn( const RespawnContext& context );
        bool                   isStateConsistent() const;

    private:
        RespawnRules _rules;
        PROPERTY( RuntimeState )
        vector<int32> _listUses; ///< 정책 항목마다 부활한 횟수(규칙 목록과 같은 길이)
        PROPERTY( RuntimeState )
        float3 _deathPosition;
        PROPERTY( RuntimeState )
        float3 _pendingPosition;
        PROPERTY( RuntimeState )
        hashed_string _pendingPointID;
        PROPERTY( RuntimeState )
        float32 _deathYaw;
        PROPERTY( RuntimeState )
        float32   _pendingYaw;
        Countdown _delay;  ///< 손 본문
        Countdown _choice; ///< 손 본문
        PROPERTY( RuntimeState, Min = 0 )
        int32 _cursor; ///< 다음에 시도할 정책 항목 자리
        PROPERTY( RuntimeState, Min = -1 )
        int32 _choiceEntry; ///< 고르기 단계를 연 `PlayerChoice` 항목(−1 이면 없음)
        PROPERTY( RuntimeState, Min = -1 )
        int32 _pendingEntry; ///< 고른 자리를 낸 정책 항목(−1 이면 아직 고르지 않음)
        PROPERTY( RuntimeState )
        RespawnPhase _phase;
    };
} // namespace sw
