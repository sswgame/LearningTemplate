/**
 * @file MeadowFarmDirectorComponent.h
 * @brief 마을 밭 디렉터 — 공유 상태(`GameStateComponent`)를 여는 첫 디렉터. 하루가 넘어가면 밭이 자라고, 다 자란 칸을 거둬 공유 지갑에 팔고 다시 심습니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Framework/GameDirectorComponent.h"
#include "GameFramework/Base/Framework/MaterialTintCache.h"
#include "GameFramework/Kits/Simulation/Farming/FarmField.h"

namespace sw
{
    class GameStateComponent;

    /**
     * @class MeadowFarmDirectorComponent
     * @brief 밭(4 × 2) 한 판입니다. 같은 오브젝트의 맨 앞 `GameStateComponent` 를 열고(새 판이면 시작 돈), 공유 시계의 하루 넘김에 밭을 돌립니다.
     * @details 상태는 밭 하나(구간 'FFLD')이고, 돈 · 시계 · 수확 수(`farm.harvested`)는 공유 상태에 있다. `Village.FastForward`(Space)를 누르는 동안,
     *          또는 자동 플레이면 시계가 8 배로 흐른다. 카메라는 이 디렉터도 마을 디렉터도 만지지 않는다(씬의 고정 카메라).
     */
    REFLECT( Category = "Meadow", DisplayName = "Meadow Farm Director", Tooltip = "Runs the farm kit on the shared game state and spawns the field tiles" )
    class MeadowFarmDirectorComponent : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32  kStateTag     = FourCcUtil::make( "MVFM" );
        static constexpr uint32  kStateVersion = 1;
        static constexpr int32   kFieldWidth   = 4;
        static constexpr int32   kFieldHeight  = 2;
        static constexpr float32 kTileSpacing  = 1.0f; ///< 칸 사이(m)

        MeadowFarmDirectorComponent();
        virtual ~MeadowFarmDirectorComponent() override;

        void writeState( Archive& outArchive ) const override;

        const FarmField& getField() const { return _field; }

    protected:
        [[nodiscard]] bool startGame() override;
        [[nodiscard]] bool readState( Archive& archive ) override;
        void               onGameStarted() override;
        void               tickGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        bool               hasPendingSpawn() const override { return _bViewsDirty == SW_TRUE; }

    private:
        /** @brief 하루 넘김 — 밭이 자라고, 다 자란 칸을 거둬 팔고 다시 심고, 나머지는 물 준다. */
        void startNextDay( GameStateComponent& state );
        void replant( int32 x, int32 y, const hashed_string& season );
        void spawnField( GameObjectManager& manager );

    private:
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab", Tooltip = "One tile view (a cube)" )
        string _tilePrefab;
        PROPERTY( Category = "Economy", DisplayName = "Starting Gold", Min = 0 )
        int32 _startingGold;
        PROPERTY( Category = "Time", DisplayName = "Fast Time Scale", Tooltip = "Clock speed while fast forward is held", Min = 1.0 )
        float32 _fastTimeScale;

        FarmField                _field;
        MaterialTintCache        _tintCache;
        vector<GameObjectHandle> _listTileView;
        uint8                    _bViewsDirty : 1;
        uint8                    _reserved    : 7;
    };
} // namespace sw
