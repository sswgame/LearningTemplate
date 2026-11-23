/**
 * @file CreatureTownViewComponent.h
 * @brief 생물 생활 키트의 클라이언트 쪽 — 디렉터(`CreatureTownGameModeComponent`)의 마을 오브젝트 · 생물을 프리팹 하나씩으로 세우고, 대화 입력을 디렉터 명령으로 넘깁니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_CreatureLife`, 전용 서버는 짓지 않는다). 디렉터의 명령(`submitTalk`)과 읽기 API(`getTown` · `getCatalog` · `getTownRevision`)만 쓰고
 *          마을(규칙)을 직접 고치지 않는다. 디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다.
 *          명령과 스폰은 틱 뒤 게임 스레드에서 한다. 이 컴포넌트는 `PostUpdate` 라 디렉터 걸음 뒤에 돌아, 넘긴 입력은 다음 걸음이 쓰고 모습은 이번 걸음의 결과를 보인다.
 *          세운 모습은 상태를 싣기 전에 걷는다(`despawnViews` — .cpp 의 `StatefulComponentRegistrar`). 다음 틱이 지금 마을대로 다시 세운다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/SimulationViewSync.h"

namespace sw
{
    class CreatureLifeCatalog;
    class CreatureTown;
    class CreatureTownGameModeComponent;
    class GameObjectManager;
    struct CreatureSpeciesDef;
    struct HabitatCell;
    struct HabitatInstance;

    /**
     * @struct CreatureTownObjectView
     * @brief 마을 오브젝트 뷰 표 한 줄 — 서식지 키의 오브젝트(`<Habitat><Key object>`)와 그 모습 프리팹입니다(뷰의 PROPERTY — 씬 데이터).
     *        카탈로그의 오브젝트마다 한 줄이 있어야 모습을 세운다(`CreatureTownViewComponent::validateViews`). 생물 종 줄(`CreatureTownCreatureView`)과 형식을 나눈 까닭은
     *        가리키는 표가 달라서다 — 한 칸이 두 표를 가리키면 쿠커 · 인스펙터가 어느 표인지 모른다.
     */
    REFLECT()
    struct CreatureTownObjectView
    {
        REFLECT_BODY();
        PROPERTY( Tooltip = "Town object (the object of a habitat key in the creature catalog)" )
        CatalogKey<HabitatCell> _object{};
        PROPERTY( Tooltip = "Prefab spawned for it; its root scene component is placed on the town grid" )
        AssetRef<PrefabAsset> _prefab{};
    };

    /**
     * @struct CreatureTownCreatureView
     * @brief 마을 생물 뷰 표 한 줄 — 생물 종(`<Species id>`)과 그 모습 프리팹입니다. 카탈로그의 종마다 한 줄이 있어야 모습을 세운다.
     */
    REFLECT()
    struct CreatureTownCreatureView
    {
        REFLECT_BODY();
        PROPERTY( Tooltip = "Creature species id in the creature catalog" )
        CatalogKey<CreatureSpeciesDef> _species{};
        PROPERTY( Tooltip = "Prefab spawned for it; its root scene component is placed on the town grid" )
        AssetRef<PrefabAsset> _prefab{};
    };

    /**
     * @class CreatureTownViewComponent
     * @brief 셀의 오브젝트마다 프리팹 하나, 사는 생물마다 프리팹 하나를 세웁니다. 마을이 바뀐 때(`getTownRevision`)에 바뀐 슬롯만 지우고 다시 세운다.
     * @details 생물은 머무는 서식지가 차지한 셀들의 가운데에 선다(서식지가 사라졌으면 마을 모서리). `_talkAction` 을 누르면 `_talkSpecies` 와 대화한다.
     */
    REFLECT( Category = "CreatureLife", DisplayName = "Creature Town View",
             Tooltip = "Client side of a creature town: spawns the objects and creatures of the Creature Town Game Mode on the same object and sends the talk input to it" )
    class SW_KIT_CLIENT_API CreatureTownViewComponent : public Component
    {
    public:
        REFLECT_BODY();

        CreatureTownViewComponent();
        virtual ~CreatureTownViewComponent() override;

        /** @brief 같은 오브젝트의 디렉터를 찾아 핸들로 듭니다. 없으면 오류를 남기고 아무것도 하지 않습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 액션 이름 · 대화할 종이 비었으면 오류를 남깁니다(폴백 없음). 틱 그룹은 뷰의 `PostUpdate` 입니다. */
        void onBeginPlay() override;
        /** @brief 세운 모습을 걷습니다. */
        void onEndPlay() override;
        /** @brief 입력 명령과 모습 맞추기를 틱 뒤 게임 스레드로 미룹니다. */
        void onTick( TickContext& context ) override;

        /** @brief 세운 모습을 모두 지웁니다(상태를 싣기 전 · 플레이 끝). 마을은 그대로이고 다음 틱이 다시 세웁니다. */
        void despawnViews();

    private:
        /** @brief 생물 슬롯 하나가 지금 보이는 것 — 종과 머무는 서식지입니다. 서식지가 바뀌면 자리가 달라 다시 세운다. */
        struct CreatureView
        {
            hashed_string _speciesID{};
            int32         _habitat{ constant::invalid_index::kInt32 };

            bool operator==( const CreatureView& other ) const { return _speciesID == other._speciesID && _habitat == other._habitat; }
        };

    private:
        /** @brief 틱 뒤 게임 스레드: 대화 입력을 넘기고, 마을이 바뀌었으면 모습을 맞춥니다. */
        void updateView( GameObjectManager& manager );
        /** @brief 카탈로그의 서식지 오브젝트 · 종이 모두 뷰 표에 있는지 봅니다. 빠진 것마다 오류를 남기고 false 입니다(모습을 세우지 않는다). */
        [[nodiscard]] bool validateViews( const CreatureLifeCatalog& catalog ) const;
        /** @brief 지금 마을의 오브젝트 · 생물과 세운 모습을 견줘 바뀐 슬롯만 다시 세웁니다. */
        void syncTown( GameObjectManager& manager, const CreatureTown& town );
        /** @brief 생물이 설 자리(마을 격자 좌표)입니다. @p pHabitat 이 없으면 마을 모서리다. */
        [[nodiscard]] static float2 computeCreatureCenter( const CreatureTown& town, const HabitatInstance* pHabitat );
        /** @brief 프리팹을 마을 격자의 @p center 에 세웁니다. 프리팹을 읽지 못했거나 루트가 없으면 빈 핸들이거나 원점에 선 오브젝트의 핸들입니다. */
        [[nodiscard]] GameObjectHandle spawnView( GameObjectManager& manager, const AssetRef<PrefabAsset>& prefab, const float2& center ) const;

    private:
        PROPERTY( Category = "Prefabs", DisplayName = "Object Views", Tooltip = "Town object id -> prefab; every habitat key object of the creature catalog needs a row" )
        vector<CreatureTownObjectView> _listObjectView;
        PROPERTY( Category = "Prefabs", DisplayName = "Creature Views", Tooltip = "Species id -> prefab; every species of the creature catalog needs a row" )
        vector<CreatureTownCreatureView> _listCreatureView;
        PROPERTY( Category = "Layout", DisplayName = "Town Origin", Tooltip = "World position of the corner of town cell (0, 0)", Units = m )
        float3 _townOrigin;
        PROPERTY( Category = "Input", DisplayName = "Talk Action", Tooltip = "InputMap action that talks to the creature (required)" )
        hashed_string _talkAction;
        PROPERTY( Category = "Input", DisplayName = "Talk Species", Tooltip = "Creature the talk action talks to (required)" )
        CatalogKey<CreatureSpeciesDef> _talkSpecies;

        TComponentHandle<CreatureTownGameModeComponent> _director;
        SimulationViewSync<hashed_string>               _objectViews;         ///< 타일 번호(y × 너비 + x) → 오브젝트 id(빈 셀이면 빈 id)
        SimulationViewSync<CreatureView>                _creatureViews;       ///< 온 순서 → 생물
        uint32                                          _shownRevision;       ///< 맞춘 마을 판(0 이면 아직 — 열린 디렉터의 판은 1 부터다)
        uint8                                           _bInputLive      : 1; ///< 이번 틱의 시간이 흘렀다(멈춤 중에는 입력을 넘기지 않는다)
        uint8                                           _bActionsValid   : 1; ///< 액션 이름 · 종이 있다(`onBeginPlay`)
        uint8                                           _bViewsValidated : 1; ///< 뷰 표를 카탈로그와 견줬다(한 번)
        uint8                                           _bViewsRejected  : 1; ///< 뷰 표에 빠진 줄이 있어 모습을 세우지 않는다
        [[maybe_unused]] uint8                          _reserved        : 4;
    };
} // namespace sw
