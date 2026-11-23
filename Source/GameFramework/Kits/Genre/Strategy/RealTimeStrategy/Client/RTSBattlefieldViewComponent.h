/**
 * @file RTSBattlefieldViewComponent.h
 * @brief RTS 키트의 클라이언트 쪽 전장 — 디렉터(`RTSGameModeComponent`)의 월드에서 보이는 유닛마다 모습 하나(풀)를 세우고, 막힌 지형 칸마다 절벽 프리팹을 놓으며, 팀 · 자원 색 인스턴스를 냅니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_RealTimeStrategy`, 전용 서버는 짓지 않는다). 디렉터의 읽기 API(`getWorld` · `findCatalog` · `getLocalPlayer` · `isLocalPlayerHuman`)만 쓴다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. 1 대 1 판과 도시 건설 미니게임의 RTS 가 같은 뷰를 쓴다.
 *          틱 뒤 게임 스레드에서 안개 · 죽음으로 바뀐 슬롯만 지우고 세운다(`SimulationViewSync`). 자리 · 크기 · 색은 유닛 뷰(`RTSUnitViewComponent`)가 저마다 병렬 틱에서 맞춘다.
 *          세운 것은 상태를 싣기 전에 걷는다(`despawnViews` — .cpp 의 `StatefulComponentRegistrar`). 다음 틱이 지금 월드대로 다시 세운다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetHandle.h"
#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"
#include "GameFramework/Runtime/Foundation/Framework/PrefabPool.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/RuleViewCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/SimulationViewSync.h"
#include "GameFramework/Runtime/Gameplay/Appearance/MaterialTintCache.h"

namespace sw
{
    class GameObjectManager;
    class Material;
    class MaterialInstance;
    class RTSGameModeComponent;

    /**
     * @class RTSBattlefieldViewComponent
     * @brief 디렉터와 같은 오브젝트에 붙이는 유닛 · 절벽 모습의 주인입니다.
     * @details 모델 표(`_unitModelPath`)와 유닛 카탈로그는 같은 id 를 가져야 한다 — 어긋나면 오류를 남기고 아무것도 세우지 않는다(모델 없는 유닛이 조용히 보이지 않는 일을 막는다).
     */
    REFLECT( Category = "RealTimeStrategy", DisplayName = "RTS Battlefield View",
             Tooltip = "Client side of an RTS world: spawns the unit views and the cliffs of the RTS game mode on the same object" )
    class SW_KIT_CLIENT_API RTSBattlefieldViewComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief 색 인스턴스는 틱 뒤 게임 스레드에서만 바뀝니다 — 유닛 뷰가 병렬 틱에서 읽어도 됩니다. */
        static constexpr bool kTickReadSafe = true;

        RTSBattlefieldViewComponent();
        virtual ~RTSBattlefieldViewComponent() override;

        /** @brief 같은 오브젝트의 디렉터를 찾아 핸들로 듭니다. 없으면 오류를 남기고 아무것도 하지 않습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 틱 그룹은 뷰의 `PostUpdate` 입니다. */
        void onBeginPlay() override;
        /** @brief 세운 모습을 걷습니다. */
        void onEndPlay() override;
        /** @brief 모습 맞추기를 틱 뒤 게임 스레드로 미룹니다. */
        void onTick( TickContext& context ) override;

        /** @brief 세운 모습(유닛 · 절벽)을 모두 지웁니다(상태를 싣기 전 · 플레이 끝). 월드는 그대로이고 다음 틱이 다시 세웁니다. */
        void despawnViews();

        /** @brief 유닛 주인(또는 자원 종류)과 선택 여부의 색 인스턴스입니다. 아직 없으면 비어 있습니다. */
        const shared_ptr<MaterialInstance>& findUnitLook( const RTSUnit& unit, bool bSelected ) const;

    private:
        /** @brief 틱 뒤 게임 스레드: 보일 유닛과 세운 모습을 비교해 바뀐 슬롯만 다시 세웁니다. 처음이면 절벽도 놓습니다. */
        void updateViews( GameObjectManager& manager );
        /** @brief 모델 표를 받아 유닛 카탈로그와 서로 같은 id 를 가지는지 보고, 모델을 워커로 미리 읽게 합니다. 어긋난 것은 모두 오류로 남기고 false 입니다. */
        [[nodiscard]] bool acquireUnitModels( const RTSGameModeComponent& director );
        /** @brief 막힌 지형 칸마다 절벽 프리팹 하나를 놓습니다 — 고르기 · 돌리기는 칸 자리의 해시라 판마다 같습니다. */
        void             spawnCliffs( GameObjectManager& manager, const RTSWorld& world );
        GameObjectHandle spawnUnit( GameObjectManager& manager, const RTSWorld& world, RTSUnitID unitID );
        /** @brief 유닛 모습 인스턴스(팀 또는 자원과 선택 여부 조합)를 만듭니다(유닛 뷰가 워커에서 고릅니다). */
        void prepareUnitLooks( Material* pMaterial );

    private:
        PROPERTY( Category = "Data", DisplayName = "Unit Models", Tooltip = "Unit model table XML (unit id -> kit model and width); every unit of the unit catalog needs a row" )
        AssetRef<RuleViewCatalog> _unitModelPath;
        PROPERTY( Category = "Prefabs", DisplayName = "Cliff Prefabs", Tooltip = "One is placed on every blocked terrain tile, picked and turned by the tile position; required when the map has blocked terrain" )
        vector<AssetRef<PrefabAsset>> _listCliffPrefab;
        PROPERTY( Category = "Prefabs", DisplayName = "Unit Prefab", Tooltip = "One unit view (a mesh and an RTS Unit View)" )
        AssetRef<PrefabAsset> _unitPrefab;
        PROPERTY( Category = "Prefabs", DisplayName = "Unit Pool", Tooltip = "Pool of the unit views that come and go with deaths and the fog (0 spawns every time)" )
        PrefabPoolPolicy _unitPool;
        PROPERTY( Category = "Look", DisplayName = "Player 0 Color", Tooltip = "Tint of player 0's units (multiplies the kit's white body)" )
        float4 _playerZeroColor;
        PROPERTY( Category = "Look", DisplayName = "Player 1 Color" )
        float4 _playerOneColor;
        PROPERTY( Category = "Look", DisplayName = "Neutral Color", Tooltip = "Tint of units no player owns" )
        float4 _neutralColor;
        PROPERTY( Category = "Look", DisplayName = "Mineral Color" )
        float4 _mineralColor;
        PROPERTY( Category = "Look", DisplayName = "Gas Color" )
        float4 _gasColor;

        TComponentHandle<RTSGameModeComponent> _director;
        SharedAssetRef<RuleViewCatalog>        _pUnitModelCatalog;
        shared_ptr<const RuleViewCatalog>      _shownUnitModels; ///< 세운 모습이 쓴 모델 표의 판 — 다시 읽히면 모습을 새 판으로 다시 세운다
        AssetReadySet                          _unitModels;      ///< 모든 유닛 모델 — 판이 열리면 워커가 읽어 둔다(처음 짓는 건물 · 유닛이 게임 스레드에서 디스크를 읽지 않게). 집합이 핸들을 쥐어 약한 참조 캐시에 남긴다.
        optional<PrefabPool>                   _unitViewPool;
        SimulationViewSync<RTSUnitID>          _unitViews; ///< 유닛 슬롯 번호 → 보일 유닛(죽음 · 안개면 없다)
        vector<GameObjectHandle>               _listCliff;
        MaterialTintCache                      _tintCache;    ///< 유닛 색(같은 색은 나눠 쓴다).
        vector<shared_ptr<MaterialInstance>>   _listUnitLook; ///< (팀 또는 자원 종류) x 2 + 선택 여부로 만든 유닛 모습 인스턴스.
        uint8                                  _bModelsChecked : 1;
        uint8                                  _bModelsValid   : 1;
        uint8                                  _bCliffsSpawned : 1;
        [[maybe_unused]] uint8                 _reserved       : 5;
    };
} // namespace sw
