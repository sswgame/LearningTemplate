/**
 * @file WesternDeedComponent.h
 * @brief 세상에 놓인 서부극 행동 자리(금고 · 사슴 · 교역소 · 보안관 사무소)와 그 범죄를 볼 수 있는 사람입니다. 게임은 데이터로 놓고 버튼 하나로 가장 가까운 자리를 씁니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Rule/WesternOutlaw.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;

    /**
     * @class WesternWitnessComponent
     * @brief 범죄를 볼 수 있는 사람입니다. 눈은 오브젝트 자리와 앞(+Z)입니다. 보안관이면 보는 즉시 신고되고, 보는 동안 수배가 식지 않습니다.
     */
    REFLECT( Category = "Western", DisplayName = "Western Witness", Tooltip = "A person who can see crimes; a lawman reports at once and keeps the wanted level from cooling" )
    class SW_KIT_API WesternWitnessComponent : public Component
    {
    public:
        REFLECT_BODY();

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;

        WesternWitnessComponent();
        ~WesternWitnessComponent() override = default;

        /** @brief 씬의 목격자 후보를 오브젝트 id 순서로 모읍니다(방문 순서를 결과에 내지 않는다). */
        static void collect( const GameObjectManager& manager, vector<WesternWitness>& outListWitness );

    private:
        PROPERTY( Category = "Witness", DisplayName = "Lawman", Tooltip = "Reports what he sees at once and keeps the wanted level from cooling while he sees the player" )
        bool _bLawman;
    };
} // namespace sw

namespace sw
{
    /**
     * @class WesternDeedComponent
     * @brief 플레이어가 곁에서 할 수 있는 일 하나입니다. 무엇을 하는지는 데이터(`_kind` · `_id`)이고, 규칙은 `WesternOutlaw` 가 돌립니다. 언리얼 Smart Object 처럼 "세상 쪽에 놓인 행동" 의 자리입니다(기억에 의존한 비교).
     * @details `useNearest` 가 거리(`_reach`)로 고른다(`InteractionView` — 상호작용과 같은 계산). `_bOnce` 면 한 번 쓴 뒤 다시 고르지 않고, `_bHideWhenUsed` 면 오브젝트를 끈다(잡은 사슴).
     *          사용 횟수는 씬 상태에 실린다.
     */
    REFLECT( Category = "Western", DisplayName = "Western Deed", Tooltip = "A spot where the player can do something: a crime, a hunt, a sale, a meal" )
    class SW_KIT_API WesternDeedComponent : public Component
    {
    public:
        REFLECT_BODY();

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;

        WesternDeedComponent();
        ~WesternDeedComponent() override = default;

        /** @brief @p position 곁에서 쓸 수 있는 가장 가까운 자리를 @p outlaw 에게 행하게 합니다. 게임 스레드에서 부릅니다. 행했으면 그 자리이고, 없거나 거절되면 nullptr 입니다. */
        static WesternDeedComponent* useNearest( GameObjectManager& manager, const float3& position, WesternOutlaw& outlaw );

        bool  canUse() const { return _bOnce == false || _useCount == 0; }
        int32 getUseCount() const { return _useCount; }

    private:
        /** @brief 오브젝트 자리에서 일어난 행동으로 만듭니다. */
        WesternDeed makeDeed( const float3& position ) const;
        /** @brief 썼음을 적고, 정한 대로 오브젝트를 끕니다. */
        void markUsed();

    private:
        PROPERTY( Category = "Deed", DisplayName = "Kind", Tooltip = "What the player does here" )
        WesternDeedKind _kind;
        PROPERTY( Category = "Deed", DisplayName = "Id", Tooltip = "Crime, honor action, animal, food or clothing id in the western catalog (by kind)" )
        hashed_string _id;
        PROPERTY( Category = "Hunt", DisplayName = "Weapon", Tooltip = "Hunt: <HuntWeapon id> of the killing shot" )
        hashed_string _weapon;
        PROPERTY( Category = "Hunt", DisplayName = "Hit Zone", Tooltip = "Hunt: <HitZone id> of the killing shot" )
        hashed_string _zone;
        PROPERTY( Category = "Hunt", DisplayName = "Hit Count", Min = 1, Tooltip = "Hunt: shots that hit; more than one lowers the pelt" )
        int32 _hitCount;
        PROPERTY( Category = "Deed", DisplayName = "Reach", Min = 0.1, Units = m )
        float32 _reach;
        PROPERTY( RuntimeState, Category = "Deed", DisplayName = "Use Count", Tooltip = "Runtime" )
        int32 _useCount;
        PROPERTY( Category = "Deed", DisplayName = "Once", Tooltip = "Usable only the first time" )
        bool _bOnce;
        PROPERTY( Category = "Deed", DisplayName = "Hide When Used", Tooltip = "Turn the object off after it is used (a hunted animal)" )
        bool _bHideWhenUsed;
    };
} // namespace sw
