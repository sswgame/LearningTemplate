/**
 * @file MeadowTownDirectorComponent.h
 * @brief 마을 디렉터 — 밭 디렉터 뒤에서 생물 마을 키트를 공유 상태 위에 돌립니다. 생물이 찾아와 부탁을 하고, 공유 지갑으로 과수원을 심어 부탁을 끝냅니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Framework/GameDirectorComponent.h"
#include "GameFramework/Base/Framework/MaterialTintCache.h"
#include "GameFramework/Kits/Simulation/CreatureLife/CreatureTown.h"

namespace sw
{
    class GameStateComponent;

    /**
     * @class MeadowTownDirectorComponent
     * @brief 마을(6 × 6) 한 판입니다. 같은 오브젝트에서 밭 디렉터 **뒤에** 붙어 밭이 이번 틱에 번 돈을 봅니다.
     * @details 상태는 마을 하나(구간 'CTWN'), 부탁 일지 · 호감도(공유 평판의 세력 `creature.<종>`) · 지갑은 공유 상태에 있다. 시작 배치(풀 두 칸)는
     *          공유 일지에 서식지 수를 알리므로 새 판일 때만 놓는다. `Town.Talk`(E)로 생물과 대화한다(하루 한 번 — 키트가 막는다).
     */
    REFLECT( Category = "Meadow", DisplayName = "Meadow Town Director", Tooltip = "Runs the creature town kit on the shared game state behind the farm director" )
    class MeadowTownDirectorComponent : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32  kStateTag     = 0x4E54564Du; ///< 'MVTN'
        static constexpr uint32  kStateVersion = 1;
        static constexpr int32   kTownSize     = 6;
        static constexpr int64   kOrchardPrice = 50;
        static constexpr float32 kTownOffsetX  = 6.0f; ///< 밭 오른쪽(m)

        MeadowTownDirectorComponent();
        virtual ~MeadowTownDirectorComponent() override;

        void writeState( Archive& outArchive ) const override;

        const CreatureTown& getTown() const { return _town; }

    protected:
        [[nodiscard]] bool startGame() override;
        [[nodiscard]] bool readState( Archive& archive ) override;
        void               tickGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        bool               hasPendingSpawn() const override { return _bViewsDirty == SW_TRUE; }

    private:
        /** @brief 마을을 열고 공유 상태(일지 · 평판 · 시계)를 빌립니다. 카탈로그가 없으면 false 입니다. */
        static bool initializeTown( CreatureTown& outTown, GameStateComponent& state );
        void        spawnTown( GameObjectManager& manager );
        void        spawnCube( GameObjectManager& manager, const float3& position, const float4& color );

    private:
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab", Tooltip = "One object or creature view (a cube)" )
        string _tilePrefab;

        CreatureTown             _town;
        MaterialTintCache        _tintCache;
        vector<GameObjectHandle> _listView;
        uint8                    _bViewsDirty : 1;
        uint8                    _reserved    : 7;
    };
} // namespace sw
