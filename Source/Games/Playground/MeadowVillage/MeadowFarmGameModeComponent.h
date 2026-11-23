/**
 * @file MeadowFarmGameModeComponent.h
 * @brief 마을 밭 디렉터 — 월드 상태(`GameStateComponent`)를 여는 첫 디렉터. 하루가 넘어가면 밭이 자라고, 다 자란 칸을 거둬 플레이어 지갑에 팔고 다시 심습니다.
 * @details 월드 상태의 설정(시계 · 계절 · 공유 땅)은 이 디렉터의 씬 데이터 하나다 — 뒤에 붙은 마을 디렉터(`CreatureTownGameModeComponent`)와 집 부지가 같은 판을 쓴다.
 *          부탁 일지 · 생물 호감도는 마을 디렉터가 연다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/CropCatalog.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/FarmField.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameSpeedControl.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/SimulationViewSync.h"
#include "GameFramework/Runtime/Gameplay/Appearance/MaterialTintCache.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"

namespace sw
{
    class WalletComponent;

    /**
     * @class MeadowFarmGameModeComponent
     * @brief 밭(4 × 2) 한 판입니다. 같은 오브젝트의 맨 앞 `GameStateComponent` 를 열고(새 판이면 시작 돈), 공유 시계의 하루 넘김에 밭을 돌립니다.
     * @details 상태는 밭 하나(구간 'FFLD')이고, 돈 · 시계 · 수확 수(`farm.harvested`)는 공유 상태에 있다. `FastForward`(Space)를 누르는 동안,
     *          또는 자동 플레이면 시계가 8 배로 흐른다. 카메라는 이 디렉터도 마을 디렉터도 만지지 않는다(씬의 고정 카메라).
     */
    REFLECT( Category = "Meadow", DisplayName = "Meadow Farm Game Mode", Tooltip = "Runs the farm kit on the shared game state and spawns the field tiles" )
    class MeadowFarmGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32      kStateTag      = FourCCUtil::make( "MVFM" );
        static constexpr uint32      kStateVersion  = 1;
        static constexpr int32       kFieldWidth    = 4;
        static constexpr int32       kFieldHeight   = 2;
        static constexpr const utf8* kHarvestedFlag = "farm.harvested"; ///< 밭이 거둔 수(공유 플래그)

        MeadowFarmGameModeComponent();
        virtual ~MeadowFarmGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        const FarmField& getField() const { return _field; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        bool               hasPendingSpawn() const override { return _bViewsDirty == SW_TRUE; }

    private:
        /** @brief 하루 넘김 — 밭이 자라고, 다 자란 칸을 거둬 팔고 다시 심고, 나머지는 물 준다. */
        void startNextDay( GameStateComponent& state, WalletComponent& wallet );
        void replant( int32 x, int32 y, const hashed_string& season );
        /** @brief 밭 칸마다 지금 모습(풀 · 갈림 · 물 줌 · 다 익음)을 동기화에 쌓습니다 — 바뀐 칸만 맞출 목록에 들어간다. */
        void             collectFieldViews();
        GameObjectHandle spawnTileView( GameObjectManager& manager, size_t tileIndex, uint8 look );

    private:
        PROPERTY( Category = "Data", DisplayName = "Crop Data", Tooltip = "Crop catalog XML" )
        AssetRef<CropCatalog> _cropDataPath;
        PROPERTY( Required, Category = "Data", DisplayName = "Crop", Tooltip = "Crop id planted on every field tile" )
        CatalogKey<CropDef> _crop;
        PROPERTY( Category = "World", DisplayName = "State Settings", Tooltip = "Settings the shared state opens with (clock, seasons, land); the land cell size must match the cell size of the housing part catalog" )
        GameStateSettings _stateSettings;
        PROPERTY( Category = "Prefabs", Tooltip = "One tile view (a cube)" )
        AssetRef<PrefabAsset> _tilePrefab;
        PROPERTY( Category = "Economy", DisplayName = "Starting Gold", Min = 0 )
        int32 _startingGold;
        PROPERTY( Category = "Time", DisplayName = "Fast Time Scale", Tooltip = "Clock speed while fast forward is held (up to the game speed limit)", Min = 1.0, Max = 8.0 )
        float32 _fastTimeScale;
        PROPERTY( Category = "Look", DisplayName = "Ready Color", Tooltip = "Tile tint when the crop is fully grown" )
        float4 _readyColor;
        PROPERTY( Category = "Look", DisplayName = "Watered Color" )
        float4 _wateredColor;
        PROPERTY( Category = "Look", DisplayName = "Tilled Color" )
        float4 _tilledColor;
        PROPERTY( Category = "Look", DisplayName = "Grass Color", Tooltip = "Tile tint before it is tilled" )
        float4 _grassColor;
        PROPERTY( Category = "Look", DisplayName = "Tile Spacing", Tooltip = "Distance between field tiles", Min = 0.01, Units = m )
        float32 _tileSpacing;

        SharedAssetRef<CropCatalog> _pCropCatalog;
        FarmField                   _field;
        MaterialTintCache           _tintCache;
        SimulationViewSync<uint8>   _tileViews; ///< 칸 번호(y × 너비 + x) → 모습 번호(.cpp 의 TileLook — 풀 · 갈림 · 물 줌 · 다 익음, 0 은 칸 없음)
        GameSpeedControl            _speed;     ///< 배속(빨리 감기를 누르는 동안 `_fastTimeScale`) — 공유 시계와 시뮬레이션이 같은 값을 받는다
        uint8                       _bViewsDirty : 1;
        [[maybe_unused]] uint8      _reserved    : 7;
    };
} // namespace sw
