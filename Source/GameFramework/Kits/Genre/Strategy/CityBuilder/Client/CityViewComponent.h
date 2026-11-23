/**
 * @file CityViewComponent.h
 * @brief 도시 건설 키트의 클라이언트 쪽 — 디렉터(`CityGameModeComponent`)의 건물 · 도로를 프리팹 하나씩으로 세우고, 빨리 감기 입력을 디렉터 명령으로 넘깁니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_CityBuilder`, 전용 서버는 짓지 않는다). 디렉터의 명령(`submitFastForward`)과 읽기 API(`getCity` · `getLayoutRevision`)만 쓰고
 *          도시(규칙)를 직접 고치지 않는다. 디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다.
 *          명령과 스폰은 틱 뒤 게임 스레드에서 한다. 이 컴포넌트는 `PostUpdate` 라 디렉터 걸음 뒤에 돌아, 넘긴 입력은 다음 걸음이 쓰고 모습은 이번 걸음의 결과를 보인다.
 *          세운 모습은 상태를 싣기 전에 걷는다(`despawnViews` — .cpp 의 `StatefulComponentRegistrar`). 다음 틱이 지금 도시대로 다시 세운다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/SimulationViewSync.h"
#include "GameFramework/Runtime/Gameplay/Appearance/MaterialTintCache.h"

namespace sw
{
    class CityGameModeComponent;
    class GameObjectManager;
    class MeshComponent;
    struct CityBuildingDef;

    /**
     * @class CityViewComponent
     * @brief 건물은 크기만큼 늘린 판 하나, 도로는 칸마다 판 하나로 세웁니다(위에서 보는 직교 카메라라 높이는 얕다). 도시가 바뀐 때(`getLayoutRevision`)에 바뀐 슬롯만 지우고 다시 세웁니다.
     */
    REFLECT( Category = "CityBuilder", DisplayName = "City View",
             Tooltip = "Client side of a city: spawns the building and road tiles of the City Game Mode on the same object and sends the fast forward input to it" )
    class SW_KIT_CLIENT_API CityViewComponent : public Component
    {
    public:
        REFLECT_BODY();

        CityViewComponent();
        virtual ~CityViewComponent() override;

        /** @brief 같은 오브젝트의 디렉터를 찾아 핸들로 듭니다. 없으면 오류를 남기고 아무것도 하지 않습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 액션 이름이 비었으면 오류를 남깁니다(폴백 없음). 틱 그룹은 뷰의 `PostUpdate` 입니다. */
        void onBeginPlay() override;
        /** @brief 세운 모습을 걷습니다. */
        void onEndPlay() override;
        /** @brief 입력 명령과 모습 맞추기를 틱 뒤 게임 스레드로 미룹니다. */
        void onTick( TickContext& context ) override;

        /** @brief 세운 모습을 모두 지웁니다(상태를 싣기 전 · 플레이 끝). 도시는 그대로이고 다음 틱이 다시 세웁니다. */
        void despawnViews();

    private:
        /** @brief 건물 슬롯 하나가 지금 보이는 것 — 정의와 자리입니다. 같은 정의라도 자리가 바뀌면(복원 · 다시 짓기) 다시 세운다. */
        struct BuildingView
        {
            const CityBuildingDef* _pDef{ nullptr };
            int2                   _origin{};

            bool operator==( const BuildingView& other ) const { return _pDef == other._pDef && _origin == other._origin; }
        };

    private:
        /** @brief 틱 뒤 게임 스레드: 빨리 감기 입력을 넘기고, 도시가 바뀌었으면 모습을 맞춥니다. */
        void updateView( GameObjectManager& manager );
        /** @brief 지금 도시의 건물 · 도로와 세운 모습을 견줘 바뀐 슬롯만 다시 세웁니다. */
        void syncTiles( GameObjectManager& manager, const CityGameModeComponent& director );
        /** @brief 타일 프리팹 하나를 세우고 핸들을 @p outHandle 에 둡니다. 프리팹이 없거나 메시가 없으면 nullptr 입니다. */
        [[nodiscard]] MeshComponent* spawnTile( GameObjectManager& manager, const utf8* pName, GameObjectHandle& outHandle ) const;

    private:
        PROPERTY( Category = "Prefabs", DisplayName = "Tile Prefab", Tooltip = "One tile view (a cube)" )
        AssetRef<PrefabAsset> _tilePrefab;
        PROPERTY( Category = "Input", DisplayName = "Fast Forward Action", Tooltip = "InputMap action held to fast-forward the city (required)" )
        hashed_string _fastForwardAction;
        PROPERTY( Category = "Look", DisplayName = "Building Color" )
        float4 _buildingColor;
        PROPERTY( Category = "Look", DisplayName = "Road Color" )
        float4 _roadColor;

        TComponentHandle<CityGameModeComponent> _director;
        MaterialTintCache                       _tintCache;
        SimulationViewSync<BuildingView>        _buildingViews;     ///< 건물 번호 → 지금 건물(허물렸으면 없다)
        SimulationViewSync<uint8>               _roadViews;         ///< 타일 번호(y × 너비 + x) → 도로(1)
        uint32                                  _shownRevision;     ///< 맞춘 도시 배치 판(0 이면 아직 — 열린 디렉터의 판은 1 부터다)
        uint8                                   _bInputLive    : 1; ///< 이번 틱의 시간이 흘렀다(멈춤 중에는 입력을 넘기지 않는다)
        uint8                                   _bActionsValid : 1; ///< 액션 이름이 있다(`onBeginPlay`)
        [[maybe_unused]] uint8                  _reserved      : 6;
    };
} // namespace sw
