/**
 * @file ProjectileDef.h
 * @brief 날아가는 것 한 종류(`ProjectileDef`), 탄 값 수정자(`ShotModifierDef`), 탄과 표면의 맞음 표(`ProjectileImpactDef`)와 그 카탈로그(projectiles.xml)를 정의합니다.
 * @details 무기(`WeaponDef`)는 쏘는 법만, 탄은 날아가는 법 · 피해 · 궤적 · 맞을 때의 동작만 가진다. 그래서 무기 N 종과 탄 M 종이 파일 N + M 줄로 조합된다.
 *          무기가 어떤 탄을 받는지는 조합 표가 아니라 탄의 태그에 대한 조건식이다(`WeaponDef::_accepts`). 표로 적으면 M × N 이 그대로 돌아온다.
 *          맞음 효과는 탄과 표면의 조합이라 탄이 들지 않고 맞음 표에서 찾는다(언리얼 Physical Material 의 표면 반응, 유니티 탄 정의 ScriptableObject 와 같은 방향 — 기억에 의존한 비교).
 *          탄 항목은 `_base` 로 다른 탄을 물려받는다(`Content/engine/settings/datacataloginheritance.xml` 의 `ProjectileCatalog` 줄).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/TagID.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Behavior/ActionRegistry.h"
#include "Engine/Behavior/ConditionExpr.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    class XMLNode;

    /** @brief 수정자가 바꾸는 탄 값입니다(`ResolvedShot` 의 칸 하나). */
    ENUM()
    enum class ShotField : uint8
    {
        Damage = 0,
        Speed,
        Gravity,
        Drag,
        Range,
        Lifetime,
        Radius,
        Homing,
        Pierce,
        Bounce,
        BlastRadius,
        HeadshotMultiplier
    };

    /** @brief 수정자가 값을 바꾸는 방법입니다. */
    ENUM()
    enum class ShotModifierOp : uint8
    {
        Add = 0,
        Multiply
    };

    /**
     * @brief 탄 값 하나를 바꾸는 수정자입니다(무기의 `<Modifier>`, 부착물 · 버프가 넘기는 목록). `<Modifier field="Damage" op="Multiply" value="1.2"/>`.
     * @details 합치는 순서는 한 벌이다 — 탄의 값 → 무기의 수정자 → 부착물의 수정자를 적힌 순서대로 얹는다(`ResolvedShot::resolve`). 정수 칸(관통 · 튕김)은 얹은 뒤 반올림한다.
     */
    REFLECT()
    struct SW_GF_API ShotModifierDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        ShotField _field{ ShotField::Damage };
        PROPERTY()
        ShotModifierOp _op{ ShotModifierOp::Multiply };
        PROPERTY()
        float32 _value{ 1.0f };
    };

    /** @brief 탄의 `<OnHit when="tag(Enemy.Armored)" call="flag.add(armorHits, 1)"/>` 한 줄의 글입니다. 읽을 때 `ProjectileHitAction` 으로 컴파일합니다. */
    REFLECT()
    struct SW_GF_API ProjectileHitActionDef
    {
        REFLECT_BODY();

        PROPERTY()
        string _when{}; ///< 맞은 쪽 문맥에 대한 조건식(비면 늘 부른다). 그냥 이름은 태그(`tag`)다
        PROPERTY( Required )
        string _call{}; ///< 동작 어휘의 호출(`flag.add(hits, 1)`)
    };

    /** @brief 컴파일한 맞음 동작 하나입니다. 부를 때 글을 보지 않습니다. */
    struct ProjectileHitAction
    {
        ConditionExpr  _when{};
        GameActionCall _call{};
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 날아가는 것 한 종류입니다. 각도는 도, 시간은 초, 거리는 m 입니다.
     * @details 속도 0 은 광선(히트스캔)이다. 사거리 · 수명 0 은 그 제한이 없다는 뜻이다(놓인 바나나처럼 맞기 전까지 남는 탄).
     *          관통 · 튕김 · 폭발 · 유도는 단일 발사체 시뮬레이션(`ProjectileSimulation`)이 읽고, 피해 감쇠 · 머리 배율은 피해 계산(`ResolvedShot::computeDamage`)이 읽는다.
     */
    REFLECT()
    struct SW_GF_API ProjectileDef
    {
        REFLECT_BODY();

        ProjectileDef();

        PROPERTY( Required )
        hashed_string _id;
        PROPERTY( Name = "tags" )
        vector<TagID> _listTag; ///< 무기의 받음 조건(`accepts`)과 맞음 표(`ProjectileImpactDef::_when`)가 묻는 태그(`Ammo.Rifle`, `Ammo.Incendiary`)
        PROPERTY( Min = 0.0 )
        float32 _damage; ///< 한 발(산탄이면 한 알)의 피해
        PROPERTY( Min = 0.0, Units = "m/s" )
        float32 _speed; ///< 0 이면 광선(히트스캔)
        PROPERTY()
        float32 _gravityScale; ///< 물리 중력에 곱하는 배율. 0 이면 곧게 간다
        PROPERTY( Min = 0.0 )
        float32 _drag; ///< 초당 속도 감소 비율(0 = 없음)
        PROPERTY( Min = 0.0, Units = m )
        float32 _range; ///< 날아간 거리 상한(0 = 없음)
        PROPERTY( Min = 0.0, Units = s )
        float32 _lifetime; ///< 수명(0 = 없음)
        PROPERTY( Min = 0.0, Units = m )
        float32 _radius; ///< 맞음 반지름(0 은 선분)
        PROPERTY( Min = 0.0 )
        float32 _homingRate; ///< 유도 대상 쪽으로 꺾는 최대 각속도(도/초, 0 = 곧게)
        PROPERTY( Min = 0 )
        int32 _pierceCount; ///< 사라지지 않고 더 꿰뚫는 대상 수
        PROPERTY( Min = 0 )
        int32 _bounceCount; ///< 사라지지 않고 표면에서 튕기는 횟수
        PROPERTY( Min = 0.0, Max = 1.0, Units = ratio )
        float32 _restitution; ///< 튕길 때 남는 속도 비율
        PROPERTY( Min = 0.0, Units = m )
        float32 _blastRadius; ///< 끝날 때 터지는 반지름(0 = 터지지 않음). 범위 안의 대상은 쓰는 쪽이 찾는다
        PROPERTY( Min = 0.0, Units = m )
        float32 _falloffStart; ///< 이 거리까지는 피해 그대로(0 = 감쇠 없음)
        PROPERTY( Min = "_falloffStart", Units = m )
        float32 _falloffEnd; ///< 이 거리부터 `_falloffMinScale`
        PROPERTY( Min = 0.0, Max = 1.0, Units = ratio )
        float32 _falloffMinScale;
        PROPERTY( Min = 0.0 )
        float32 _headshotMultiplier;
        PROPERTY()
        float4 _tracerColor; ///< 궤적(탄도선) 색. 1 을 넘으면 빛난다
        PROPERTY( Min = 0.0, Units = m )
        float32 _tracerWidth; ///< 궤적 두께(0 = 그리지 않음)

        TagContainer                _tags;          ///< `_listTag` 를 묻기 좋게 담은 것(읽을 때 채운다)
        vector<ProjectileHitAction> _listHitAction; ///< `<OnHit>` 줄(읽을 때 컴파일한다)

        /** @brief 맞은 쪽 문맥 @p context 로 맞음 동작을 차례로 부릅니다. 조건이 거짓인 줄은 건너뜁니다. */
        static void runHitActions( const vector<ProjectileHitAction>& listHitAction, ActionContext& context );
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 탄과 표면의 맞음 표 한 줄입니다. `<Impact id="body_fire" surface="Body" when="Ammo.Incendiary" color="4,1,0.2,1" size="0.3"/>`.
     * @details 위에서 아래로 처음 맞는 줄이 이긴다 — 특정한 줄(조건 있음)을 일반 줄(조건 없음)보다 위에 적는다. 표면 이름은 게임이 정한다(`Body`, `Cover`).
     */
    REFLECT()
    struct SW_GF_API ProjectileImpactDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        hashed_string _surface{};
        PROPERTY()
        string _when{}; ///< 탄 태그에 대한 조건식(비면 모든 탄). 그냥 이름은 태그다
        PROPERTY()
        float4 _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Min = 0.0, Units = m )
        float32 _size{ 0.1f };

        ConditionExpr _whenExpr{}; ///< `_when` 을 읽을 때 컴파일한 것
    };
} // namespace sw

namespace sw
{
    /**
     * @class ProjectileCatalog
     * @brief `<ProjectileCatalog><Projectile id="rifle_round" tags="Ammo.Rifle" damage="14" range="90"/><Impact id="body" surface="Body" .../></ProjectileCatalog>` 를 읽습니다.
     * @details 틀린 칸 · 틀린 식 · 모르는 동작 이름은 그 줄을 넣지 않는 오류다(자르지 않는다).
     */
    class SW_GF_API ProjectileCatalog : public GameCatalog<ProjectileDef, XMLCatalog<ProjectileCatalog>>
    {
        friend class XMLCatalog<ProjectileCatalog>;

    public:
        /** @brief 데이터 종류 이름입니다(공유 캐시 등록 줄과 팩 경로가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "projectiles";

        ProjectileCatalog();

        /** @brief 표면 @p surface 에 탄 태그 @p projectileTags 가 맞을 때의 줄입니다. 위에서 처음 맞는 줄이고, 없으면 nullptr 입니다. */
        const ProjectileImpactDef* findImpact( const hashed_string& surface, const TagContainer& projectileTags ) const;
        /** @brief 맞음 표 전체입니다. */
        const vector<ProjectileImpactDef>& getImpacts() const { return _listImpact; }

    private:
        static constexpr const utf8* kXMLRootName = "ProjectileCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLCatalogRoot& root, string_view sourceName );
        [[nodiscard]] bool           loadProjectile( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadImpact( const XMLNode& node, string_view sourceName );

    private:
        vector<ProjectileImpactDef> _listImpact;
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<ProjectileCatalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_GF_API_EXTERN_TEMPLATE, ProjectileCatalog );
} // namespace sw
