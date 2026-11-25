/**
 * @file CityViewComponent.h
 * @brief 도시 건설 키트의 클라이언트 쪽 모습 — 디렉터(`CityBuilderGameModeComponent`)의 땅 · 도로 · 건물 · 일꾼을 프리팹으로 세우고, 건물 · 일꾼 뷰가 쓸 모델 표와 색을 듭니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_CityBuilder`, 전용 서버는 짓지 않는다). 디렉터의 읽기 API(`getCity` · `getLayoutRevision`)만 쓰고 도시(규칙)를 고치지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다.
 *          스폰은 틱 뒤 게임 스레드에서 한다. 이 컴포넌트는 `PostUpdate` 라 디렉터 걸음 뒤에 돌아 이번 걸음의 결과를 보인다. 건물 단계 · 일꾼 자리는 세운 뷰(`CityBuildingViewComponent` ·
 *          `CityWalkerViewComponent`)가 맞춘다.
 *          세운 모습은 상태를 싣기 전에 걷는다(`despawnViews` — .cpp 의 `StatefulComponentRegistrar`). 다음 틱이 지금 도시대로 다시 세운다.
 */
#pragma once
#include "Core/Container/vector.h"
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
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CityCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CitySimulation.h"
#include "GameFramework/Runtime/Foundation/Presentation/RuleViewCatalog.h"
#include "GameFramework/Runtime/Foundation/Presentation/SimulationViewSync.h"
#include "GameFramework/Runtime/Gameplay/Appearance/MaterialTintCache.h"

namespace sw
{
    class CityBuilderGameModeComponent;
    class GameObject;
    class GameObjectManager;
    class Material;
    class MaterialInstance;

    /**
     * @class CityViewComponent
     * @brief 디렉터와 같은 오브젝트에 붙이는 도시 모습의 주인입니다. 도시 배치가 바뀐 때(`getLayoutRevision`)에 바뀐 슬롯만 지우고 다시 세웁니다.
     * @details 모델 표(`_buildingViewPath`)에 줄이 있는 건물은 모델 프리팹, 없는 건물(밭 · 조각상)은 색 상자 프리팹이다. 표를 적지 않은 도시는 모두 색 상자다.
     *          땅(`_groundPrefab`) · 일꾼(`_walkerPrefab`)은 프리팹을 적은 도시만 세운다 — 다른 것이 땅을 그리는 도시(공유 땅)가 있다.
     */
    REFLECT( Category = "CityBuilder", DisplayName = "City View", Tooltip = "Client side of a city: spawns the ground, roads, buildings and walkers of the city game mode on the same object" )
    class SW_KIT_CLIENT_API CityViewComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        /** @brief 모델 표 · 색 인스턴스는 틱 뒤 게임 스레드에서만 바뀝니다 — 건물 · 일꾼 뷰가 병렬 틱에서 읽어도 됩니다. */
        static constexpr bool kTickReadSafe = true;

        CityViewComponent();
        virtual ~CityViewComponent() override;

        /** @brief 같은 오브젝트의 디렉터를 찾아 핸들로 듭니다. 없으면 오류를 남기고 아무것도 하지 않습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 틱 그룹은 뷰의 `PostUpdate` 입니다. */
        void onBeginPlay() override;
        /** @brief 세운 모습을 걷습니다. */
        void onEndPlay() override;
        /** @brief 모습 맞추기를 틱 뒤 게임 스레드로 미룹니다. */
        void onTick( TickContext& context ) override;

        /** @brief 세운 모습을 모두 지웁니다(상태를 싣기 전 · 플레이 끝). 도시는 그대로이고 다음 틱이 다시 세웁니다. */
        void despawnViews();

        // ---- 건물 · 일꾼 뷰가 읽는 것(병렬 틱) ----
        /** @brief 건물 id → 모델 표입니다. 표를 적지 않았거나 아직 읽지 않았으면 nullptr 입니다. */
        const RuleViewCatalog* getBuildingViews() const { return _shownBuildingViews.get(); }
        /** @brief 일꾼 종류와 서비스의 모습을 반환합니다. 아직 없으면 비어 있습니다. */
        const shared_ptr<MaterialInstance>& findWalkerLook( const CityWalker& walker ) const;

    private:
        /** @brief 건물 슬롯 하나가 지금 보이는 것 — 정의와 자리입니다. 같은 정의라도 자리가 바뀌면(복원 · 다시 짓기) 다시 세운다. */
        struct BuildingView
        {
            const CityBuildingDef* _pDef{ nullptr };
            int2                   _origin{};

            bool operator==( const BuildingView& other ) const { return _pDef == other._pDef && _origin == other._origin; }
        };

    private:
        /** @brief 틱 뒤 게임 스레드: 처음이면 표를 읽고 땅을 세우고, 도시가 바뀌었으면 도로 · 건물을, 일꾼이 늘었으면 일꾼을 맞춥니다. */
        void updateView( GameObjectManager& manager );
        /** @brief 모델 표를 받아 집 단계마다 줄이 있는지 보고 모델을 워커로 미리 읽게 합니다. 표를 적지 않았으면 그냥 true 입니다. 어긋나면 오류를 남기고 false 다 — 아무것도 세우지 않는다. */
        [[nodiscard]] bool acquireBuildingViews( const CityCatalog& catalog );
        /** @brief 지금 도시의 건물 · 도로와 세운 모습을 견줘 바뀐 슬롯만 다시 세웁니다. */
        void syncTiles( GameObjectManager& manager, const CitySimulation& city );
        /** @brief 줄마다 같은 땅이 이어지는 구간을 상자 하나로, 솟은 돌이 있는 땅은 타일마다 상자로 세웁니다(땅은 바뀌지 않는다). 색 · 높이는 땅의 행이 정한다. */
        void             spawnTerrain( GameObjectManager& manager, const CitySimulation& city, const CityCatalog& catalog );
        void             spawnGroundBox( GameObjectManager& manager, const float3& center, const float3& scale, const float4& color );
        GameObjectHandle spawnRoad( GameObjectManager& manager, const int2& tile );
        GameObjectHandle spawnBuilding( GameObjectManager& manager, size_t buildingIndex, const CityBuildingDef& def );
        /** @brief 일꾼 수만큼 미리 세우고 남으면 뷰가 숨긴다(오브젝트를 매 프레임 만들고 지우지 않는다). */
        void spawnWalkers( GameObjectManager& manager, const CityCatalog& catalog, size_t count );
        /** @brief 일꾼 종류와 서비스 행마다 모습 인스턴스를 만듭니다(뷰가 워커에서 고릅니다). */
        void prepareWalkerLooks( const CityCatalog& catalog, Material* pMaterial );
        /** @brief 프리팹 하나를 세웁니다. 읽지 못하면 nullptr 입니다(까닭은 프리팹 캐시가 남긴다). */
        [[nodiscard]] static GameObject* spawnView( GameObjectManager& manager, const AssetRef<PrefabAsset>& prefab, const utf8* pName );

        /** @brief 서비스 행의 색입니다. @p serviceIndex 가 -1(서비스 없음)이면 회색이다. */
        static float4 computeServiceColor( const CityCatalog& catalog, int32 serviceIndex );
        float4        computeWalkerColor( const CityCatalog& catalog, CityWalkerKind kind, int32 serviceIndex ) const;

    private:
        PROPERTY( Category = "Data", DisplayName = "Building Views", Tooltip = "Building id to kit model table (*.views.xml); a building without a row (or a city without a table) is drawn as a coloured block" )
        AssetRef<RuleViewCatalog> _buildingViewPath;
        PROPERTY( Category = "Prefabs", Tooltip = "Unit box stretched under the tiles; empty when something else draws the ground" )
        AssetRef<PrefabAsset> _groundPrefab;
        PROPERTY( Category = "Prefabs", Tooltip = "One road tile; its own position and scale are kept relative to the tile centre" )
        AssetRef<PrefabAsset> _roadPrefab;
        PROPERTY( Category = "Prefabs", Tooltip = "Building drawn with a kit model (a mesh and a City Building View)" )
        AssetRef<PrefabAsset> _modelBuildingPrefab;
        PROPERTY( Category = "Prefabs", Tooltip = "Building drawn as a coloured block (a mesh and a City Building View)" )
        AssetRef<PrefabAsset> _blockBuildingPrefab;
        PROPERTY( Category = "Prefabs", Tooltip = "One walker (a mesh and a City Walker View); empty when walkers are not drawn" )
        AssetRef<PrefabAsset> _walkerPrefab;

        PROPERTY( Category = "Look", DisplayName = "Road Color", Tooltip = "Tint multiplied into the road tile (white keeps the prefab material)" )
        float4 _roadColor;
        PROPERTY( Category = "Look", DisplayName = "Trader Color", Tooltip = "Colour of a trader walker" )
        float4 _traderColor;
        PROPERTY( Category = "Look", DisplayName = "Cart Color", Tooltip = "Colour of a cart walker" )
        float4 _cartColor;

        SW_TRANSIENT TComponentHandle<CityBuilderGameModeComponent> _director;
        SharedAssetRef<RuleViewCatalog>                             _pBuildingViewCatalog;
        shared_ptr<const RuleViewCatalog>                           _shownBuildingViews; ///< 세운 모습이 쓴 모델 표의 판 — 다시 읽히면 모습을 새 판으로 다시 세운다
        SW_TRANSIENT AssetReadySet                                  _buildingModels;     ///< 모든 건물 모델 — 판이 열리면 워커가 읽어 둔다(집 단계가 바뀔 때 뷰가 게임 스레드에서 디스크를 읽지 않게)
        SW_TRANSIENT MaterialTintCache                              _tintCache;          ///< 땅, 도로, 건물, 일꾼 색(같은 색은 나눠 쓴다)
        SW_TRANSIENT SimulationViewSync<BuildingView> _buildingViews;                    ///< 건물 번호 → 지금 건물(허물렸으면 없다)
        SW_TRANSIENT SimulationViewSync<uint8> _roadViews;                               ///< 타일 번호(y × 너비 + x) → 도로(1)
        SW_TRANSIENT vector<GameObjectHandle> _listGroundObject;
        SW_TRANSIENT vector<GameObjectHandle> _listWalkerObject;
        SW_TRANSIENT vector<shared_ptr<MaterialInstance>> _listWalkerLook;      ///< 일꾼 종류 × `_walkerLookStride` + 서비스 행 번호 + 1
        shared_ptr<const CityCatalog>                     _shownCatalog;        ///< 세운 모습이 색을 읽은 카탈로그의 판 — 도시가 새 판에 묶이면 모습을 다시 세운다
        SW_TRANSIENT int32                                _walkerLookStride;    ///< 서비스 행 수 + 1(서비스 없음)
        SW_TRANSIENT uint32                               _shownRevision;       ///< 맞춘 도시 배치 판(0 이면 아직 — 열린 디렉터의 판은 1 부터다)
        SW_TRANSIENT uint8                                _bViewsValid     : 1; ///< 모델 표가 맞다 — 아니면 아무것도 세우지 않는다
        SW_TRANSIENT uint8                                _bTerrainSpawned : 1;
        [[maybe_unused]] uint8                            _reserved        : 6;
    };
} // namespace sw
