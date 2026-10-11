/**
 * @file NileDirectorComponent.h
 * @brief NileCity 의 규칙을 돌리는 컴포넌트입니다. 도시 시뮬레이션, 자동 계획, 마우스 건설과 철거, 속도, 월별 로그를 맡고 땅, 도로, 건물, 일꾼(프리팹) 스폰을 지시합니다.
 * @details 언리얼의 GameMode/GameState 에 해당합니다. 씬에 하나만 둡니다. 규칙(노동, 순회 일꾼, 물자 사슬, 집 진화, 범람)은 키트의 `CitySimulation` 이,
 *          땅 형태와 자동 계획은 `NileCityPlanner` 가 맡는다. 이 컴포넌트는 무엇을 언제 건설하는지와 그 모습(프리팹)을 어디에 스폰하는지를 안다.
 *          모습을 매 프레임 맞추는 일은 뷰 컴포넌트(`NileBuildingComponent`, `NileWalkerComponent`, `NileCursorComponent`)가 이 컴포넌트를 **읽기만** 해서 처리한다.
 *
 *          틱 규칙은 다음과 같다. 디렉터는 `TickGroup::PrePhysics` 에서 상태를 쓰고, 뷰와 카메라 리그는 `PostUpdate` 에서 읽는다(그룹은 차례로 돈다).
 *          바뀐 도로 타일과 건물 타일을 쌓아 두면 베이스 `GameDirectorComponent` 가 틱이 끝난 뒤 `onFlush` 로 스폰한다. 효과음도 그때 낸다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Framework/GameDirectorComponent.h"
#include "GameFramework/Base/Foundation/Framework/GameStateRefs.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/MaterialTintCache.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Base/World/Environment/WorldClock.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CityCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CitySimulation.h"

#include "Games/NileCity/NileCityPlanner.h"

namespace sw
{
    class Archive;
    class GameObject;
    class GameObjectManager;
    class InputManager;
    class Material;
    class MaterialInstance;
    class Mesh;
    class MeshComponent;

    /**
     * @class NileDirectorComponent
     * @brief 도시 한 판을 진행합니다. 플레이가 시작되면 도시 데이터를 읽고 땅을 칠한 새 도시를 엽니다.
     * @details 도시는 PROPERTY 가 아닌 런타임 상태이므로 핫 리로드 뒤 처음부터 다시 만든다. 스폰한 오브젝트는 핸들로 보관하고, 상태를 저장하기 전에 정리한다.
     */
    REFLECT( Category = "CityBuilder", DisplayName = "Nile Director", Tooltip = "Runs the Nile city simulation, the auto plan and mouse building, and spawns the city views" )
    class NileDirectorComponent : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        NileDirectorComponent();
        virtual ~NileDirectorComponent() override;

        /** @brief 도시 상태(시뮬레이션, 자동 계획 진행, 월 수, 속도, 선택한 도구)를 씁니다. `ComponentStateStore::capture` 가 부릅니다. */
        void writeState( Archive& outArchive ) const override;

        // ---- 뷰가 읽는 것(PostUpdate. 디렉터가 쓰지 않는 그룹) ----
        const CitySimulation& getCity() const { return _city; }
        /** @brief 커서가 땅 위에 있으면 true 를 반환하고, @p outTile 과 @p outSize 에 타일과 선택한 것의 크기(타일 수)를 채웁니다. */
        bool findCursor( int2& outTile, int32& outSize ) const;
        /** @brief 일꾼 종류와 서비스의 모습을 반환합니다. 아직 없으면 비어 있습니다. */
        const shared_ptr<MaterialInstance>& findWalkerLook( const CityWalker& walker ) const;

    protected:
        /** @brief 도시 데이터를 읽고 땅을 칠한 새 도시를 엽니다. */
        [[nodiscard]] bool startGame() override;
        /** @brief `writeState` 의 바이트를 읽어 한 번에 바꿉니다. 끝까지 맞지 않으면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive ) override;
        void               onStateRestored( bool bRestored ) override;
        void               onGameStarted() override;
        void               tickGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        bool               hasPendingSpawn() const override;

    private:
        /** @brief 시뮬레이션 건물 타일 하나의 모습입니다(같은 번호). 타일이 다른 건물로 다시 쓰이면 오브젝트를 바꿔 스폰합니다. */
        struct BuildingSlot
        {
            GameObjectHandle       _object{};
            const CityBuildingDef* _pShownDef{ nullptr }; ///< 스폰했거나 스폰할 건물(nullptr 이면 없다). 틱이 쓰고 틱이 끝난 뒤 맞춘다.
        };

    private:
        /** @brief 이 도시의 시뮬레이션 설정입니다(`startGame` 과 복원이 같은 설정으로 `initialize` 합니다). */
        CitySettings makeCitySettings() const;
        /** @brief 나일 달력입니다. 아케트, 페레트, 셰무가 각각 네 달씩(열두 달)이며, 계절 하루가 한 달입니다. */
        WorldClockSettings makeClockSettings() const;
        /** @brief 이번 틱의 달력 알림에서 달이 넘어갈 때마다 도시를 결산합니다(같은 시점에 해가 바뀌었으면 범람이 일어납니다). */
        void settleMonths();
        /** @brief 처음이거나 정리한 뒤에 땅, 모든 도로, 모든 건물, 일꾼 풀을 스폰합니다. */
        void spawnAll( GameObjectManager& manager );
        void spawnTerrain( GameObjectManager& manager );
        void spawnRoad( GameObjectManager& manager, int32 tileIndex );
        void spawnBuilding( GameObjectManager& manager, int32 buildingIndex );
        void spawnWalkers( GameObjectManager& manager, size_t count );
        /** @brief 일꾼 종류와 서비스마다 모습 인스턴스를 만듭니다(뷰가 워커에서 고릅니다). */
        void prepareWalkerLooks( Material* pMaterial );
        /** @brief 건물 모델을 미리 로드해 둡니다(집 단계가 바뀔 때 뷰가 워커에서 파일을 읽지 않게 합니다). */
        void preloadModels();

        void updateInput( float32 deltaTime, const InputManager& input );
        void updateCursor( const InputManager& input );
        void placeSelected();
        void drainEvents();
        /** @brief 시뮬레이션과 스폰한 모습을 비교해 바뀐 도로 타일, 건물 타일, 모자란 일꾼 수를 쌓습니다(PrePhysics). */
        void        collectViewChanges();
        void        logStatus() const;
        const utf8* getToolName() const;
        bool        isAutoPlanOn() const;
        /** @brief 도시가 빌려 쓸 공유 상태(디렉터가 보관하는 금고)를 반환합니다. */
        GameStateRefs makeRefs();

    private:
        PROPERTY( Category = "Data", DisplayName = "City Data", AssetPath, Tooltip = "City catalog XML" )
        string _cityDataPath;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _groundPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _roadPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab", Tooltip = "Building drawn with a kit model (palette material)" )
        string _modelBuildingPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab", Tooltip = "Building drawn as a coloured block (fields, statues)" )
        string _blockBuildingPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _walkerPrefab;
        PROPERTY( Category = "Scene", DisplayName = "Camera Rig", Tooltip = "Object with the OrthoCameraRigComponent the mouse cursor is picked through" )
        GameObjectHandle _cameraRig;
        PROPERTY( Category = "City", DisplayName = "Starting Money", Min = 0 )
        int32 _startingMoney;
        PROPERTY( Category = "City", DisplayName = "Service Duration", Tooltip = "Seconds a walker's service lasts at a house", Min = 1.0, Units = s )
        float32 _serviceDuration;
        PROPERTY( Category = "Look", DisplayName = "Road Tile Scale", Tooltip = "Scale of the path piece so one road tile fills a cell", Min = 0.0 )
        float32 _roadTileScale;
        PROPERTY( Category = "City", DisplayName = "Seconds Per Month", Tooltip = "Real seconds in one city month before the time scale", Min = 1.0, Units = s )
        float32 _secondsPerMonth;

        CityCatalog                          _catalog;
        CitySimulation                       _city;
        Wallet                               _wallet; ///< 도시 금고. 키트 하나만 쓰는 게임이라 디렉터가 보관하고 빌려 준다.
        WorldClock                           _clock;  ///< 달력. 하루가 한 달이다(계절 열둘 x 하루). 디렉터가 보관하고 시간을 흘린다.
        NileCityPlanner                      _planner;
        vector<CityEvent>                    _listEvent;
        vector<WorldClockEvent>              _listClockEvent; ///< 이번 틱의 달력 알림(스크래치).
        vector<const CityBuildingDef*>       _listTool;       ///< 0은 도로(nullptr)다.
        vector<GameObjectHandle>             _listRoadObject; ///< 타일마다 하나이며, 도로가 아니면 무효다. 틱이 끝난 뒤에만 바뀐다.
        vector<uint8>                        _listRoadShown;  ///< 타일마다 스폰했거나 스폰할 도로. 틱이 쓴다.
        vector<BuildingSlot>                 _listBuildingSlot;
        vector<GameObjectHandle>             _listWalkerObject;
        vector<int32>                        _listPendingRoad;     ///< 스폰하거나 지울 도로 타일(틱이 끝난 뒤 처리).
        vector<int32>                        _listPendingBuilding; ///< 다시 스폰할 건물 타일(틱이 끝난 뒤 처리).
        MaterialTintCache                    _tintCache;           ///< 땅, 건물, 일꾼 색(같은 색은 나눠 쓴다).
        vector<shared_ptr<MaterialInstance>> _listWalkerLook;      ///< 일꾼 종류 x 16 + 서비스.
        vector<shared_ptr<Mesh>>             _listModelMesh;       ///< 건물 모델을 계속 참조한다(집 단계가 바뀔 때 뷰가 워커에서 읽지 않게 한다).
        size_t                               _wantedWalkerCount;   ///< 스폰할 일꾼 풀 크기(틱이 쓴다).
        int2                                 _cursorTile;
        float32                              _timeScale;
        int32                                _selectedTool;
        int32                                _monthCount;
        int32                                _evolvedCount; ///< 이번 달 올라간 집 수(월말 로그).
        uint8                                _bCursorValid    : 1;
        uint8                                _bPaused         : 1;
        uint8                                _bAutoPlanToggle : 1; ///< P 로 켠 자동 계획(PROPERTY 와 전역 변수와는 별개다).
        uint8                                _reserved        : 5;
    };
} // namespace sw
