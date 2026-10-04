/**
 * @file ParkDirectorComponent.h
 * @brief ThemeParkTycoon 의 규칙을 돌리는 컴포넌트 — 시뮬레이션 · 짓기 · 입력 · 자동 짓기 · 로그, 그리고 런타임 오브젝트(프리팹) 스폰 지시입니다.
 *
 * @details 언리얼 GameMode/GameState 의 자리입니다. 씬에 하나 둡니다. 손님 · 줄 · 표 · 평점은 키트의 `ThemeParkSimulation`, 코스터 트랙 · 물리 · 평가는
 *          `CoasterTrackBuilder` · `CoasterTrain` · `CoasterRideAnalyzer` 가 맡고, 여기는 무엇을 언제 짓는지와 그 모습(프리팹)을 어디에 세우는지를 압니다.
 *          모습을 매 프레임 맞추는 일은 뷰 컴포넌트(`ParkGuestComponent` · `CoasterCarComponent` · `FlatRideComponent`)가 이 컴포넌트를 **읽기만** 해서 합니다.
 *
 *          틱 규칙: 디렉터는 `TickGroup::PrePhysics` 에서 상태를 쓰고, 뷰 · 카메라 리그는 `PostUpdate` 에서 읽습니다(그룹은 차례로 돈다).
 *          스폰은 틱 안에서 할 수 없으므로 요청을 쌓아 두고 `executeOrDeferPostTick` 한 번으로 틱 뒤에 세웁니다. 효과음도 그때 냅니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Simulation/ThemePark/CoasterTrack.h"
#include "GameFramework/Kits/Simulation/ThemePark/CoasterTrain.h"
#include "GameFramework/Kits/Simulation/ThemePark/ParkLayout.h"
#include "GameFramework/Kits/Simulation/ThemePark/ThemePark.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;
    class InputManager;
    class MaterialInstance;
    class MeshComponent;
    class OrthoCameraRigComponent;

    /**
     * @class ParkDirectorComponent
     * @brief 공원 한 판입니다. 플레이가 시작되면 데이터를 읽고 시뮬레이션을 열어 처음 둘(가장 싼 평지 · 코스터)을 짓습니다.
     * @details 시뮬레이션은 핫 리로드에서 처음부터 다시 섭니다(PROPERTY 가 아닌 런타임 상태). 세운 오브젝트는 핸들로 들고, 상태 저장 전에 걷습니다.
     */
    REFLECT( Category = "ThemePark", DisplayName = "Park Director", Tooltip = "Runs the park simulation, building, input and the runtime spawns" )
    class ParkDirectorComponent : public Component
    {
    public:
        REFLECT_BODY();

        ParkDirectorComponent();
        virtual ~ParkDirectorComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 세운 런타임 오브젝트를 모두 지웁니다(상태 저장 전). 시뮬레이션은 그대로이고 다음 틱이 그 상태대로 다시 세운다. */
        void despawnViews();

        // ---- 뷰가 읽는 것(PostUpdate — 디렉터가 쓰지 않는 그룹) ----
        const ThemeParkSimulation& getSimulation() const { return _simulation; }
        /** @brief 행복도 칸(0 초록 · 1 노랑 · 2 빨강)의 손님 모습입니다. 아직 없으면 비어 있다. */
        const shared_ptr<MaterialInstance>& getGuestLook( int32 bucket ) const;
        /** @brief 코스터 번호의 열차입니다. 없으면 nullptr 입니다. */
        const CoasterTrain* findCoasterTrain( int32 coasterIndex ) const;
        /** @brief 행복도 → 색 칸입니다. */
        static int32 computeHappinessBucket( float32 happiness );
        /**
         * @brief 핸들의 오브젝트에 붙은 디렉터입니다. 없으면 nullptr 입니다.
         * @details 뷰는 이것을 매 프레임 부르고 포인터를 들지 않습니다. 매니저 조회는 잠그지 않습니다(틱 중 여러 워커가 불러도 된다).
         */
        static const ParkDirectorComponent* resolveDirector( const GameObjectManager& manager, GameObjectHandle director );

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

        /** @brief 평지 놀이기구 색 하나의 머티리얼 인스턴스입니다(같은 색은 나눠 쓴다 — 배치 키가 인스턴스다). */
        struct ColorLook
        {
            shared_ptr<MaterialInstance> _instance{};
            float4                       _color{};
        };

    private:
        [[nodiscard]] bool loadData();
        /** @brief 설계도 하나를 짓습니다(돈이 모자라면 false). 모습은 틱 뒤에 세운다. */
        bool buildPlacement( int32 placementIndex );
        /** @brief 아직 안 지은 것 중 가장 싼 것을 짓습니다. */
        bool buildCheapestRemaining();
        /** @brief 쌓인 스폰 · 효과음을 틱 뒤 한 번으로 미룹니다(틱 밖이면 바로). */
        void scheduleFlush();
        /** @brief 쌓인 요청을 세웁니다. 틱 밖(게임 스레드)에서만 불린다. */
        void flushPending();
        void spawnGuestPool( GameObjectManager& manager );
        void spawnRideView( GameObjectManager& manager, int32 placementIndex );
        void spawnCoasterView( GameObjectManager& manager, int32 coasterIndex );
        void spawnPath( GameObjectManager& manager, const float3& from, const float3& to );
        /** @brief 프리팹을 세우고 핸들을 듭니다. 읽지 못하면 nullptr 입니다. */
        GameObject* spawnPrefab( GameObjectManager& manager, const string& prefabPath, const utf8* pName );
        /** @brief 색 하나의 인스턴스를 찾거나 만듭니다(게임 스레드). */
        shared_ptr<MaterialInstance> acquireColorLook( MeshComponent& mesh, const float4& color );
        void                         playSound( const utf8* pPath );

        void               updateInput( const InputManager& input );
        void               updateRides( float32 deltaTime );
        void               updateCameraOverride();
        void               logStatus( float32 deltaTime, bool bForce );
        void               logThoughts() const;
        int32              findSelectedRideIndex() const;
        bool               isAutoBuildOn() const;
        GameObjectManager* getObjectManager() const;

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
        PROPERTY( Category = "Build", DisplayName = "Auto Build Interval", Tooltip = "Seconds between automatic builds", Min = 1.0, Meta = "Units=s" )
        float32 _autoBuildInterval;
        PROPERTY( Category = "Build", DisplayName = "Auto Build", Tooltip = "Build the cheapest remaining ride whenever cash allows (-gv_parkAutoBuild=1 also turns it on)" )
        bool _bAutoBuild;

        ThemeParkSimulation                _simulation;
        CoasterLayoutCatalog               _layoutCatalog;
        ThemeParkSettings                  _settings;
        vector<RidePlacement>              _listPlacement;
        vector<unique_ptr<CoasterRuntime>> _listCoaster;
        vector<GameObjectHandle>           _listSpawned;
        vector<int32>                      _listPendingRideView; ///< 모습을 세울 배치 번호(틱 뒤)
        vector<const utf8*>                _listPendingSound;    ///< 낼 효과음(틱 뒤 — 오디오는 게임 스레드에서)
        vector<ColorLook>                  _listColorLook;
        shared_ptr<MaterialInstance>       _arrGuestLook[3];
        float32                            _statusTimer;
        float32                            _autoBuildTimer;
        int32                              _startingCash;
        int32                              _selectedPlacement;
        int32                              _ridingCoaster; ///< 0 이상이면 그 코스터에 타고 있다
        uint8                              _bLoaded         : 1;
        uint8                              _bViewsSpawned   : 1; ///< 손님 풀과 지은 것의 모습이 서 있다(걷으면 다음 틱이 다시 세운다)
        uint8                              _bFlushScheduled : 1;
        uint8                              _bWasRiding      : 1;
        uint8                              _reserved        : 4;
    };
} // namespace sw
