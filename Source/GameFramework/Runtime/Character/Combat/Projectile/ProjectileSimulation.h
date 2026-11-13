/**
 * @file ProjectileSimulation.h
 * @brief 단일 발사체 시뮬레이션입니다. 탄을 게임 오브젝트로 만들지 않고 값 목록 하나로 돌려 탄 수가 많아도 비용이 오르지 않고, 같은 입력이면 같은 결과를 냅니다(락스텝 · 롤백 · 서버 권위).
 * @details 운동(중력 · 저항 · 유도) · 사거리 · 수명 · 관통 · 튕김 · 반사(패리)를 여기서 한 벌로 다룬다. 무엇에 맞는지는 쓰는 쪽이 `IProjectileWorld::sweep` 으로 답하고,
 *          맞은 뒤의 반응(피해 · 다운 · 넉백 · 폭발 범위 · 아이템 효과)은 쓰는 쪽이 사건(`ProjectileEvent`)을 받아 한다 — 키트는 데이터와 반응만 가진다.
 *
 *          **2D 는 평면 제약이다.** 2D 키트(액션 플랫포머 · 액션 룸)와 평면 위를 도는 카트도 같은 3D 계산에 평면 법선(`setPlaneNormal`)을 준다. 언리얼 ProjectileMovement 의
 *          평면 제약(`bConstrainToPlane`)과 같은 길이다. 유니티 · Godot 는 2D 물리를 따로 두지만, 발사체 규칙(관통 · 튕김 · 유도)을 차원마다 두 벌로 쓰게 되므로 택하지 않았다(기억에 의존한 비교).
 *          결정성: 이 TU 는 `sw_mark_deterministic_sources` 에 있고 삼각함수는 `DeterministicMathUtil` 이다. 중력은 쓰는 쪽이 값(가속의 크기)으로 넘긴다 — 물리 설정을 읽지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ResolvedShot;

    class Archive;

    /**
     * @brief 날아가는 탄 하나입니다. 운동 값까지 들고 있어 정의를 다시 찾지 않고 상태만으로 다시 돕니다(세이브 · 롤백 · 리플레이).
     * @details `_owner` · `_team` · `_target` · `_payload` 는 쓰는 쪽의 번호다(조종사 · 차 · 편 · 슬롯). 시뮬레이션은 `_target` 을 유도 대상으로 넘기고 나머지는 사건에 실어 돌려준다.
     */
    struct SW_GF_API ProjectileState
    {
        hashed_string _defID; ///< 탄 정의 id(`ProjectileDef`, 카트 아이템). 정의를 쓰지 않는 쓰는 쪽은 비운다
        float3        _position;
        float3        _previousPosition; ///< 지난 걸음의 위치. 이 선분으로 맞음을 판정해 빠른 탄이 얇은 벽을 건너뛰지 않는다
        float3        _velocity;
        float32       _damage;
        float32       _age;
        float32       _travelled;   ///< 날아간 거리(m)
        float32       _lifetime;    ///< 0 = 없음
        float32       _range;       ///< 0 = 없음
        float32       _gravity;     ///< 아래(−Y) 가속(m/s²). 배율을 곱한 값이다
        float32       _drag;        ///< 초당 속도 감소 비율
        float32       _turnRate;    ///< 유도 각속도(라디안/초)
        float32       _radius;      ///< 맞음 반지름
        float32       _blastRadius; ///< 끝날 때 터지는 반지름(쓰는 쪽이 범위를 찾는다)
        float32       _restitution; ///< 튕길 때 남는 속도 비율
        uint32        _id;
        uint32        _payload; ///< 쓰는 쪽의 반응 데이터 번호(슬롯 · 정의 인덱스)
        int32         _owner;
        int32         _team;
        int32         _target;     ///< 유도 대상(−1 = 없음)
        int32         _lastHit;    ///< 마지막으로 꿰뚫은 대상. 쓰는 쪽의 `sweep` 은 이 대상을 건너뛴다(같은 대상을 걸음마다 다시 맞히지 않는다)
        int32         _pierceLeft; ///< 남은 관통 수
        int32         _bounceLeft; ///< 남은 튕김 수
        uint8         _bAlive     : 1;
        uint8         _bReflected : 1; ///< 되받아친 탄이다
        uint8         _reserved   : 6;

        ProjectileState();

        /**
         * @brief @p shot 을 @p origin 에서 @p direction(단위) 쪽으로 쏜 탄입니다. 중력은 @p gravity(가속의 크기, 아래로) × 탄의 중력 배율입니다.
         * @details 광선 탄(속도 0)은 날아가지 않으므로 쓰는 쪽이 광선으로 판정한다. 이 함수는 날아가는 탄에만 부른다.
         */
        [[nodiscard]] static ProjectileState launch( const ResolvedShot& shot, const float3& origin, const float3& direction, float32 gravity );

        float32 getSpeed() const { return _velocity.getLength(); }
        /** @brief 탄을 되받아칩니다(패리 · 반사막). 속도는 반대로 @p speedScale 배, 피해는 @p damageScale 배가 되고 편이 @p team 이 됩니다. */
        void deflect( int32 team, float32 speedScale, float32 damageScale );
    };
} // namespace sw

namespace sw
{
    /** @brief 맞은 것의 종류입니다. */
    enum class ProjectileHitKind : uint8
    {
        Target = 0, ///< 쓰는 쪽의 대상(조종사 · 차 · 적). `_other` 가 그 번호다
        Surface     ///< 벽 · 바닥 · 판 밖. 튕길 수 있다
    };

    /** @brief `IProjectileWorld::sweep` 의 답입니다. */
    struct ProjectileHit
    {
        float3            _position{};
        float3            _normal{ 0.0f, 1.0f, 0.0f }; ///< 표면의 바깥 법선(튕김에 쓴다)
        hashed_string     _surface{};                  ///< 표면 이름(맞음 표를 찾는다). 비어도 된다
        int32             _other{ constant::invalid_index::kInt32 };
        ProjectileHitKind _kind{ ProjectileHitKind::Target };
    };

    /** @brief 걸음 하나에서 탄에 생긴 일입니다. `_projectile` 은 그 순간의 탄(맞은 뒤의 위치 · 남은 관통 수)입니다. */
    struct ProjectileEvent
    {
        enum class Kind : uint8
        {
            Hit = 0, ///< 맞았다(`_hit`). `_projectile._bAlive` 가 거짓이면 여기서 끝났다
            Bounced, ///< 표면에서 튕겼다
            Expired  ///< 사거리 · 수명이 다했다
        };
        ProjectileState _projectile{};
        ProjectileHit   _hit{};
        Kind            _kind{ Kind::Hit };
    };

    /**
     * @class IProjectileWorld
     * @brief 탄이 묻는 세계입니다. 쓰는 쪽이 자기 대상과 지형으로 답합니다. 걸음 안에서 세계를 바꾸지 않습니다(답은 사건으로 받아 걸음 뒤에 반응한다).
     */
    class IProjectileWorld
    {
    public:
        virtual ~IProjectileWorld() = default;
        /** @brief @p projectile 의 유도 대상(`_target`)의 겨눌 점입니다. 대상이 없거나 맞힐 수 없으면 false 입니다(그 걸음은 곧게 간다). */
        virtual bool findTargetPoint( const ProjectileState& projectile, float3& outPoint ) const = 0;
        /** @brief @p from → @p to 선분에서 가장 먼저 닿는 것입니다. `_lastHit` 과 쏜 쪽은 쓰는 쪽의 규칙대로 건너뜁니다. 닿은 것이 없으면 false 입니다. */
        virtual bool sweep( const ProjectileState& projectile, const float3& from, const float3& to, ProjectileHit& outHit ) const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ProjectileSimulation
     * @brief 탄 목록과 걸음입니다. 걸음 순서는 수명 → 유도 → 저항 → 중력 → 평면 제약 → 사거리로 자른 선분 → 맞음(관통 · 튕김) → 사거리 끝입니다.
     * @details 관통하면 같은 걸음의 남은 선분을 다시 `sweep` 해 한 걸음에 지나는 대상을 모두 가까운 순서로 맞힌다(사건도 그 순서다). 튕기면 방향이 바뀌므로 그 걸음은 거기서 끝나고
     *          남은 길은 다음 걸음에 간다. 다시 묻는 선분은 맞은 자리에서 시작하고 `_lastHit` 이 방금 꿰뚫은 대상을 건너뛰게 한다.
     */
    class SW_GF_API ProjectileSimulation
    {
    public:
        ProjectileSimulation();

        /** @brief 탄이 머무는 평면의 법선입니다(2D · 지면 위). 0 벡터면 제약이 없습니다. 상태가 아니라 설정이라 저장하지 않습니다. */
        void setPlaneNormal( const float3& normal );
        /** @brief 탄을 넣고 id 를 돌려줍니다. 살아 있는 탄으로 넣습니다. */
        uint32 spawn( const ProjectileState& projectile );
        /**
         * @brief 한 걸음 진행하고 끝난 탄을 목록에서 뺍니다.
         * @return 이번 걸음에 생긴 일입니다. 버퍼는 시뮬레이션이 들고 다음 `step` 까지 유효합니다 — 걸음마다 쓰는 쪽이 칸을 두고 비우던 것을 한 곳으로 모았다. 쓰지 않으면 받지 않아도 된다.
         */
        const vector<ProjectileEvent>& step( float32 deltaTime, const IProjectileWorld& world );
        /** @brief 쓰는 쪽이 끝낸 탄(`_bAlive` 거짓)을 뺍니다. */
        void removeDead();
        /** @brief 모든 탄을 지웁니다. id 는 이어서 셉니다(지운 탄과 새 탄이 겹치지 않게). */
        void clear();

        const vector<ProjectileState>& getProjectiles() const { return _listProjectile; }
        /** @brief 쓰는 쪽이 탄을 고칩니다(유도 대상 바꾸기 · 반사 · 맞아서 끝내기). 끝낸 탄은 `removeDead` 나 다음 `step` 이 뺍니다. */
        vector<ProjectileState>& accessProjectiles() { return _listProjectile; }
        uint32                   getNextID() const { return _nextID; }

        /**
         * @brief 다음 id 와 살아 있는 탄을 씁니다. 평면 법선은 설정이라 싣지 않습니다.
         *        `writeState` 의 바이트로 탄을 바꿉니다. 깨진 바이트 · 범위 밖 값(음수 수명 · 거리, 0..1 밖의 반발, 다음 id 이상의 id, 유한하지 않은 값)이면 false 이고 그대로입니다.
         *        쓰는 쪽 번호(쏜 쪽 · 대상 · 반응 번호)의 범위는 쓰는 쪽이 읽은 뒤 본다.
         */
        SW_DECLARE_STATE_IO();

    private:
        vector<ProjectileState> _listProjectile;
        vector<ProjectileEvent> _listEventScratch; ///< 이번 걸음의 사건(`step`). 상태가 아니라 저장하지 않는다
        float3                  _planeNormal;
        uint32                  _nextID;
    };
} // namespace sw
