/**
 * @file ParkDirectorComponent.h
 * @brief ThemeParkTycoon 의 규칙을 돌리는 컴포넌트 — 시뮬레이션 · 짓기 · 입력 · 자동 짓기 · 로그, 그리고 런타임 오브젝트(프리팹) 스폰 지시입니다.
 *
 * @details 언리얼 GameMode/GameState 의 자리입니다. 씬에 하나 둡니다. 손님 · 줄 · 표 · 평점은 키트의 `ThemeParkSimulation`, 코스터 트랙 · 물리 · 평가는
 *          `CoasterTrackBuilder` · `CoasterTrain` · `CoasterRideAnalyzer` 가 맡고, 여기는 무엇을 언제 짓는지와 그 모습(프리팹)을 어디에 세우는지를 압니다.
 *          모습을 매 프레임 맞추는 일은 뷰 컴포넌트(`ParkGuestComponent` · `CoasterCarComponent` · `FlatRideComponent`)가 이 컴포넌트를 **읽기만** 해서 합니다.
 *
 *          틱 규칙 · 틱 뒤 스폰 · 상태 바이트 보류 · 걷기는 베이스 `GameDirectorComponent` 가 맡습니다 — 디렉터는 `PrePhysics` 에서 상태를 쓰고,
 *          뷰 · 카메라 리그는 `PostUpdate` 에서 읽습니다(그룹은 차례로 돈다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Framework/GameDirectorComponent.h"
#include "GameFramework/Base/Framework/GameStateRefs.h"
#include "GameFramework/Base/Framework/MaterialTintCache.h"
#include "GameFramework/Base/Inventory/Shop.h"
#include "GameFramework/Kits/Simulation/ThemePark/CoasterTrack.h"
#include "GameFramework/Kits/Simulation/ThemePark/CoasterTrain.h"
#include "GameFramework/Kits/Simulation/ThemePark/ParkLayout.h"
#include "GameFramework/Kits/Simulation/ThemePark/ThemePark.h"

namespace sw
{
    class Archive;
    class GameObject;
    class GameObjectManager;
    class InputManager;
    class MaterialInstance;
    class MeshComponent;
    class OrthoCameraRigComponent;

    /**
     * @class ParkDirectorComponent
     * @brief 공원 한 판입니다. 플레이가 시작되면 데이터를 읽고 시뮬레이션을 열어 처음 둘(가장 싼 평지 · 코스터)을 짓습니다.
     * @details 시뮬레이션 · 지은 것 · 열차 자리는 PROPERTY 가 아니라 `writeState` 로 게임 상태 스냅샷의 컴포넌트 섹션에 실려 핫 리로드 · 세이브를
     *          넘깁니다(`ThemeParkTycoonGame`). 코스터 트랙은 배치 데이터에서 다시 짓습니다. 세운 오브젝트는 핸들로 들고, 상태 저장 전에 걷습니다.
     */
    REFLECT( Category = "ThemePark", DisplayName = "Park Director", Tooltip = "Runs the park simulation, building, input and the runtime spawns" )
    class ParkDirectorComponent : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        ParkDirectorComponent();
        virtual ~ParkDirectorComponent() override;

        /** @brief 공원 상태(시뮬레이션 · 지은 배치 · 열차 자리 · 타이머 · 고른 배치)를 씁니다 — `ComponentStateStore::capture` 가 부릅니다. */
        void writeState( Archive& outArchive ) const override;

        // ---- 뷰가 읽는 것(PostUpdate — 디렉터가 쓰지 않는 그룹) ----
        const ThemeParkSimulation& getSimulation() const { return _simulation; }
        /** @brief 행복도 칸(0 초록 · 1 노랑 · 2 빨강)의 손님 모습입니다. 아직 없으면 비어 있다. */
        const shared_ptr<MaterialInstance>& getGuestLook( int32 bucket ) const;
        /** @brief 코스터 번호의 열차입니다. 없으면 nullptr 입니다. */
        const CoasterTrain* findCoasterTrain( int32 coasterIndex ) const;
        /** @brief 행복도 → 색 칸입니다. */
        static int32 computeHappinessBucket( float32 happiness );

    protected:
        /** @brief 배치 데이터를 읽고 공원을 열어 처음 둘(가장 싼 평지 · 코스터)을 짓습니다. */
        [[nodiscard]] bool startGame() override;
        /** @brief `writeState` 의 바이트를 읽어 한 번에 바꿉니다. 끝까지 맞지 않으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive ) override;
        void               onStateRestored( bool bRestored ) override;
        void               onGameStarted() override;
        void               tickGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        bool               hasPendingSpawn() const override { return _listPendingRideView.empty() == false; }

    private:
        /** @brief 지을 수 있는 놀이기구 하나 — 키트의 배치 정의에 지은 뒤의 번호를 더한다. */
        struct RidePlacement : ParkRidePlacement
        {
            int32 _rideIndex{ -1 }; ///< 지었으면 시뮬레이션의 놀이기구 번호
        };

        /** @brief 지은 코스터 하나 — 트랙은 열차가 가리키므로 힙에 둔다(목록이 자라도 주소가 그대로). */
        struct CoasterRuntime
        {
            unique_ptr<CoasterTrack> _pTrack{};
            CoasterTrain             _train{};
            int32                    _placementIndex{ -1 };
        };

    private:
        /**
         * @brief 배치의 코스터(트랙 · 열차)를 짓고 시험 운행 결과를 @p outStats 에 냅니다. 배치 · 설계가 없으면 nullptr 입니다.
         * @details 짓기(`buildPlacement`)와 복원이 같은 트랙을 짓습니다 — 열차는 트랙을 가리키므로 트랙째 힙에 있다.
         */
        unique_ptr<CoasterRuntime> createCoaster( int32 placementIndex, CoasterRideStats& outStats ) const;
        /** @brief 설계도 하나를 짓습니다(돈이 모자라면 false). 모습은 틱 뒤에 세운다. */
        bool buildPlacement( int32 placementIndex );
        /** @brief 아직 안 지은 것 중 가장 싼 것을 짓습니다. */
        bool buildCheapestRemaining();
        /** @brief 공원이 빌릴 공유 상태(디렉터가 든 금고)입니다. */
        GameStateRefs makeRefs();
        void          spawnGuestPool( GameObjectManager& manager );
        void          spawnRideView( GameObjectManager& manager, int32 placementIndex );
        void          spawnCoasterView( GameObjectManager& manager, int32 coasterIndex );
        void          spawnPath( GameObjectManager& manager, const float3& from, const float3& to );

        void  updateInput( const InputManager& input );
        void  updateRides( float32 deltaTime );
        void  updateCameraOverride();
        void  logStatus( float32 deltaTime, bool bForce );
        void  logThoughts() const;
        int32 findSelectedRideIndex() const;

    private:
        PROPERTY( Category = "Data", DisplayName = "Coaster Layouts", AssetPath, Tooltip = "Coaster layout XML" )
        string _coasterDataPath;
        PROPERTY( Category = "Data", DisplayName = "Park Layout", AssetPath, Tooltip = "Gate, cash and ride placement XML" )
        string _parkDataPath;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _guestPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _carFrontPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _carPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _railPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _supportPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _stationPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _rideEntrancePrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _pathPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _flatRidePrefab;
        PROPERTY( Category = "Look", DisplayName = "Happy Guest", Tooltip = "Guest colour at happiness 0.6 and above" )
        float4 _happyColor;
        PROPERTY( Category = "Look", DisplayName = "Content Guest", Tooltip = "Guest colour at happiness 0.35 to 0.6" )
        float4 _contentColor;
        PROPERTY( Category = "Look", DisplayName = "Unhappy Guest", Tooltip = "Guest colour below happiness 0.35" )
        float4 _unhappyColor;
        PROPERTY( Category = "Scene", DisplayName = "Camera Rig", Tooltip = "Object with the OrthoCameraRigComponent the coaster ride view overrides" )
        GameObjectHandle _cameraRig;
        PROPERTY( Category = "Scene", DisplayName = "Gate", Tooltip = "Object whose position is the park gate; empty uses the park layout" )
        GameObjectHandle _gate;
        PROPERTY( Category = "Build", DisplayName = "Auto Build Interval", Tooltip = "Seconds between automatic builds when auto play is on", Min = 1.0, Units = s )
        float32 _autoBuildInterval;

        ThemeParkSimulation                _simulation;
        Wallet                             _wallet; ///< 공원 금고 — 키트 하나만 쓰는 게임이라 디렉터가 들고 빌려 준다
        CoasterLayoutCatalog               _layoutCatalog;
        ThemeParkSettings                  _settings;
        vector<RidePlacement>              _listPlacement;
        vector<unique_ptr<CoasterRuntime>> _listCoaster;
        vector<int32>                      _listPendingRideView; ///< 모습을 세울 배치 번호(틱 뒤)
        MaterialTintCache                  _tintCache;           ///< 평지 놀이기구 색(같은 색은 나눠 쓴다 — 배치 키가 인스턴스다)
        shared_ptr<MaterialInstance>       _arrGuestLook[3];
        float32                            _statusTimer;
        float32                            _autoBuildTimer;
        int32                              _startingCash;
        int32                              _selectedPlacement;
        int32                              _ridingCoaster; ///< 0 이상이면 그 코스터에 타고 있다
        uint8                              _bWasRiding : 1;
        uint8                              _reserved   : 7;
    };
} // namespace sw
