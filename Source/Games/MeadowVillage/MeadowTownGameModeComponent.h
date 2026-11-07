/**
 * @file MeadowTownGameModeComponent.h
 * @brief 마을 디렉터 — 밭 디렉터 뒤에서 생물 마을 키트를 공유 상태 위에 돌립니다. 생물이 찾아와 부탁을 하고, 공유 지갑으로 과수원을 심어 부탁을 끝냅니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/FourCcUtil.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Simulation/CreatureLife/CreatureTown.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"

namespace sw
{
    class CreatureLifeCatalog;
    class SessionStateComponent;

    /**
     * @struct MeadowTownView
     * @brief 마을 뷰 표 한 줄 — 오브젝트(서식지 키의 `object`) 또는 생물 종 id 와 그 모습 프리팹입니다(디렉터의 PROPERTY — 씬 데이터).
     *        카탈로그의 오브젝트 · 종마다 한 줄이 있어야 판을 연다(`MeadowTownGameModeComponent::validateViews`).
     */
    REFLECT()
    struct MeadowTownView
    {
        REFLECT_BODY();
        PROPERTY( Tooltip = "Town object id (a habitat key object) or creature species id" )
        hashed_string _id{};
        PROPERTY( Tooltip = "Prefab spawned for it; its root scene component is placed on the town grid" )
        AssetRef<PrefabAsset> _prefab{};
    };

    /**
     * @class MeadowTownGameModeComponent
     * @brief 마을(6 × 6) 한 판입니다. 같은 오브젝트에서 밭 디렉터 **뒤에** 붙어 밭이 이번 틱에 번 돈을 봅니다.
     * @details 상태는 마을 하나(구간 'CTWN'), 부탁 일지 · 호감도(공유 평판의 세력 `creature.<종>`) · 지갑은 공유 상태에 있다. 시작 배치(풀 두 칸)는
     *          공유 일지에 서식지 수를 알리므로 새 판일 때만 놓는다. `Interact`(E)로 생물과 대화한다(하루 한 번 — 키트가 막는다).
     */
    REFLECT( Category = "Meadow", DisplayName = "Meadow Town Game Mode", Tooltip = "Runs the creature town kit on the shared game state behind the farm game mode" )
    class MeadowTownGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCcUtil::make( "MVTN" );
        static constexpr uint32 kStateVersion = 1;
        static constexpr int32  kTownSize     = 6;

        MeadowTownGameModeComponent();
        virtual ~MeadowTownGameModeComponent() override;

        void serializeState( StateIO& io ) override;

        const CreatureTown& getTown() const { return _town; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        bool               hasPendingSpawn() const override { return _bViewsDirty == SW_TRUE; }

    private:
        /** @brief 마을을 열고 공유 상태(일지 · 평판 · 시계)를 빌립니다. 카탈로그가 없으면 false 입니다. */
        [[nodiscard]] static bool initializeTown( CreatureTown& outTown, SessionStateComponent& state );
        /** @brief 카탈로그의 서식지 오브젝트 · 종이 모두 뷰 표에 있는지 봅니다. 빠진 것마다 오류를 남기고 false 입니다(판을 열지 않는다). */
        [[nodiscard]] bool validateViews( const CreatureLifeCatalog& catalog ) const;
        void               spawnTown( GameObjectManager& manager );
        /** @brief @p listView 에서 @p id 의 프리팹을 @p position 에 놓습니다. 표에 없으면 오류(시작 검사가 막으니 오지 않는다). */
        void spawnView( GameObjectManager& manager, const vector<MeadowTownView>& listView, const hashed_string& id, const float3& position );

    private:
        PROPERTY( Category = "Prefabs", DisplayName = "Object Views", Tooltip = "Town object id -> prefab; every habitat key object of the creature catalog needs a row" )
        vector<MeadowTownView> _listObjectView;
        PROPERTY( Category = "Prefabs", DisplayName = "Creature Views", Tooltip = "Species id -> prefab; every species of the creature catalog needs a row" )
        vector<MeadowTownView> _listCreatureView;
        PROPERTY( Category = "Economy", DisplayName = "Orchard Price", Min = 0 )
        int32 _orchardPrice;
        PROPERTY( Category = "Layout", DisplayName = "Town Offset X", Tooltip = "Town grid starts this far right of the field", Units = m )
        float32 _townOffsetX;

        CreatureTown             _town;
        vector<GameObjectHandle> _listView;
        uint8                    _bViewsDirty : 1;
        uint8                    _reserved    : 7;
    };
} // namespace sw
