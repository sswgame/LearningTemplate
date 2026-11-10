/**
 * @file KillcamShot.h
 * @brief 킬캠의 기록과 연출 시간표입니다 — 판정이 끝낸 한 발(출발점 · 방향 · 차례대로의 명중 · 쓰러짐)을 값으로 남기고, 그 값만으로 카메라가 탄을 따라가 명중마다 멈추는 길을 셉니다.
 * @details 기록은 판정의 결과다 — 다시 판정하지 않는다(피해 · 물리 · 네트워크를 두 번 돌리지 않는다). 서버 권위 게임은 서버가 정한 명중을 클라이언트가 이 모양으로 받아 연출한다.
 *          히트스캔은 쏜 쪽이 광선의 명중을 차례로 넣고(`addHit`), 날아가는 탄은 시뮬레이션의 사건(`ProjectileEvent`)을 그대로 붙인다(`addProjectileEvent` — 관통탄은 한 걸음의 명중이 여럿이다).
 *          시간표는 순수 함수다(`KillcamShot`) — 언리얼 리플레이 킬캠 · 스나이퍼 엘리트의 총알 카메라처럼 "탄 따라가기 → 명중 클로즈업 → 다음 명중" 이고, 길이는 상한에서 자른다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct KillcamSettings;
    struct KillcamPose;

    struct ProjectileEvent;

    /** @brief 한 발이 맞은 것 하나입니다. 출발점에서의 거리 순으로 쌓인다. */
    struct SW_GF_API KillcamHit
    {
        GameObjectHandle       _target; ///< 맞은 오브젝트(없으면 무효 — 벽 · 바닥)
        hashed_string          _zone;   ///< 맞은 부위(히트 존, 없으면 빈 이름)
        float3                 _position;
        float32                _distance;     ///< 출발점에서의 거리(m)
        uint8                  _bKilled  : 1; ///< 이 명중으로 쓰러졌다
        uint8                  _bSevered : 1; ///< 이 명중이 부위를 잘랐다
        [[maybe_unused]] uint8 _reserved : 6;

        KillcamHit();
    };

    /**
     * @class KillcamShot
     * @brief 판정이 끝낸 한 발의 기록입니다. 명중 수는 `kMaxHits` 에서 자른다(고정 상한 — 연출 길이와 메모리를 묶는다).
     */
    class SW_GF_API KillcamShot
    {
    public:
        /** @brief 한 발에 담는 명중 수의 상한입니다. 관통 수가 이보다 많아도 연출은 앞의 명중만 보인다. */
        static constexpr uint32 kMaxHits = 8;

        KillcamShot();

        /** @brief 새 발을 시작합니다(앞 기록은 지운다). @p direction 은 단위 벡터로 맞춘다. */
        void begin( const float3& origin, const float3& direction, float64 time );
        /** @brief 명중 하나를 거리 순 자리에 넣습니다. 상한을 넘으면 false 입니다(넣지 않는다). */
        bool addHit( const KillcamHit& hit );
        /** @brief 날아가는 탄의 맞음 사건 하나를 명중으로 붙입니다(맞음이 아닌 사건은 무시하고 false). 쓰러짐 · 대상은 쓰는 쪽이 안다. */
        bool addProjectileEvent( const ProjectileEvent& event, GameObjectHandle target, bool bKilled );
        /** @brief 탄이 끝난 자리(마지막 명중 뒤의 벽 · 사거리 끝)입니다. */
        void finish( const float3& end );

        const float3&             getOrigin() const { return _origin; }
        const float3&             getDirection() const { return _direction; }
        const float3&             getEnd() const { return _end; }
        float64                   getTime() const { return _time; }
        const vector<KillcamHit>& getHits() const { return _listHit; }
        /** @brief 쓰러뜨린 명중이 하나라도 있으면 true 입니다. */
        bool hasKill() const;

    private:
        vector<KillcamHit> _listHit;
        float64            _time;
        float3             _origin;
        float3             _direction;
        float3             _end;

    public:
        // 기록 한 발의 연출 길이와 순간의 카메라입니다(순수 함수 — 테스트가 그대로 부른다).
        // 구간: 출발점 → 명중 0 (따라가기) → 명중 0 멈춤 → 명중 1 (따라가기) → ... → 마지막 명중 멈춤. 명중이 없으면 끝 자리까지 따라간다. 길이는 `_maxSeconds` 에서 자른다.
        /** @brief 연출 길이(초)입니다. */
        static float32 computeDuration( const KillcamShot& shot, const KillcamSettings& settings );
        /** @brief @p seconds 의 카메라입니다(0 이하는 시작, 길이 이상은 끝 — `Done`). */
        static KillcamPose sample( const KillcamShot& shot, const KillcamSettings& settings, float32 seconds );

    private:
        /** @brief 길이 안(@p seconds 는 0..길이)의 카메라입니다. */
        static KillcamPose sampleClamped( const KillcamShot& shot, const KillcamSettings& settings, float32 seconds );
    };
} // namespace sw

namespace sw
{
    /** @brief 킬캠 연출의 값입니다(데이터 — 킬캠 컴포넌트의 PROPERTY). */
    REFLECT()
    struct SW_GF_API KillcamSettings
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.5, Units = "m/s", Tooltip = "Speed the camera follows the bullet at (presentation, not the shot's speed)" )
        float32 _bulletSpeed{ 12.0f };
        PROPERTY( Min = 0.0, Units = m, Tooltip = "Distance behind the bullet while following" )
        float32 _followDistance{ 1.2f };
        PROPERTY( Min = 0.0, Units = m, Tooltip = "Height above the bullet line while following and holding" )
        float32 _followHeight{ 0.25f };
        PROPERTY( Min = 0.0, Units = s, Tooltip = "Hold on each hit" )
        float32 _hitHoldSeconds{ 0.6f };
        PROPERTY( Min = 0.1, Units = m, Tooltip = "Close-up distance from a hit" )
        float32 _closeUpDistance{ 2.0f };
        PROPERTY( Min = 0.1, Units = s, Tooltip = "Longest killcam; longer shots are cut here" )
        float32 _maxSeconds{ 4.0f };
    };

    /** @brief 킬캠 시간표의 단계입니다. */
    enum class KillcamPhase : uint8
    {
        Follow = 0, ///< 탄을 따라간다
        HitHold,    ///< 명중에서 멈춰 클로즈업한다
        Done,       ///< 끝났다(마지막 자리를 유지)
    };

    /** @brief 시간표의 한 순간입니다. */
    struct KillcamPose
    {
        float3       _position{};
        float3       _lookAt{};
        float3       _bullet{}; ///< 연출 속 탄의 자리
        int32        _hitIndex; ///< 멈춘(또는 다음) 명중 번호. 자세를 만들 때 채운다(`KillcamShot::sample`)
        KillcamPhase _phase{ KillcamPhase::Follow };
    };

} // namespace sw
