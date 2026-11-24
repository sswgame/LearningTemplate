/**
 * @file ParkGameModeComponent.h
 * @brief 테마파크 키트(`GF_ThemePark`)의 디렉터 — 배치 데이터로 공원을 열고 시뮬레이션 · 코스터 열차 · 건설 · 자동 건설을 돌립니다.
 * @details 언리얼의 GameMode/GameState 에 해당합니다. 씬에 하나만 둡니다. 키트 세 갈래의 가운데다: 규칙(키트 루트, 엔진 시계 · 렌더를 모른다)을 들고 씬과 잇는다.
 *          손님, 대기줄, 티켓, 평점은 `ThemeParkSimulation` 이, 코스터 트랙, 물리, 평가는 `CoasterTrackBuilder`, `CoasterTrain`, `CoasterRideStats` 가 맡는다.
 *          손님 · 놀이기구 · 레일 모습과 입력, 코스터 탑승 시점은 클라이언트 모듈(`GF_Client_ThemePark` 의 `ParkViewComponent`)이 이 디렉터의 명령 · 읽기 API 로만
 *          다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          배치 데이터의 자리는 공원 원점 기준이고 원점은 이 오브젝트의 씬 컴포넌트다 — 하위 레벨로 얹은 공원(`SceneSubLevelSubsystem` 의 앵커)이 얹은 자리에서 열린다.
 *          시뮬레이션과 트랙은 월드 좌표로 돈다.
 *
 *          틱 규칙, 상태 바이트 보류는 베이스 `GameModeComponent` 가 맡는다. 디렉터는 `PrePhysics` 에서 상태를 쓰고, 뷰와 카메라 리그는 그 뒤에 읽는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrack.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrain.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/ParkLayout.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/ThemePark.h"
#include "GameFramework/Runtime/Character/Navigation/NavGrid.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class ParkGameModeComponent
     * @brief 공원 한 판을 진행합니다. 플레이가 시작되면 데이터를 읽고 시뮬레이션을 열어, 처음 두 개(가장 싼 평지 놀이기구와 코스터)를 건설합니다.
     * @details 시뮬레이션, 건설한 것, 열차 위치는 PROPERTY 가 아니라 `writeState` 로 게임 상태 스냅샷의 컴포넌트 섹션에 실려 핫 리로드와 세이브를 넘긴다.
     *          코스터 트랙은 배치 데이터에서 다시 만든다.
     */
    REFLECT( Version = 2, Category = "ThemePark", DisplayName = "Park Game Mode", Tooltip = "Runs the park simulation, the coaster trains and building; the park layout is placed at this object's scene component" )
    class SW_KIT_API ParkGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 건설할 수 있는 놀이기구 하나입니다. 키트의 배치 정의(자리는 월드로 옮겼다)에 건설한 뒤의 번호를 더합니다. */
        struct RidePlacement : ParkRidePlacement
        {
            int32 _rideIndex{ -1 }; ///< 건설했으면 시뮬레이션의 놀이기구 번호.
        };

        ParkGameModeComponent();
        virtual ~ParkGameModeComponent() override;

        /** @brief 공원 상태(시뮬레이션, 건설한 배치, 열차 위치, 금고)를 씁니다. `ComponentStateStore::capture` 가 부릅니다. */
        void serializeGameState( StateIO& io ) override;

        // ---- 명령(판이 열린 동안만 받는다, 게임 스레드) ----
        /** @brief 배치 하나를 건설합니다. 판이 열리지 않았거나 없는 번호 · 이미 지은 것 · 돈 부족 · 테스트 운행 실패면 false 입니다. */
        [[nodiscard]] bool submitBuild( int32 placementIndex );
        /** @brief 아직 건설하지 않은 것 중 가장 싼 것을 건설합니다. 남은 것이 없거나 돈이 모자라면 false 입니다. */
        [[nodiscard]] bool submitBuildCheapest();
        /** @brief 지은 놀이기구를 닫거나 엽니다. 없는 번호면 false 입니다. */
        [[nodiscard]] bool submitToggleRideOpen( int32 rideIndex );
        /** @brief 지은 놀이기구의 표값을 @p delta 만큼 바꿉니다. 없는 번호면 false 입니다. */
        [[nodiscard]] bool submitRidePriceChange( int32 rideIndex, int32 delta );
        /** @brief 입장료를 @p delta 만큼 바꿉니다(0 아래로는 내려가지 않는다). 판이 열리지 않았으면 false 입니다. */
        [[nodiscard]] bool submitEntryFeeChange( int32 delta );

        // ---- 읽기(클라이언트 모듈. 디렉터는 틱 뒤에만 쓴다) ----
        const ThemeParkSimulation&   getSimulation() const { return _simulation; }
        const vector<RidePlacement>& getPlacements() const { return _listPlacement; }
        /** @brief 공원 원점의 요(라디안)입니다 — 놀이기구 모습이 공원과 함께 돈다. */
        float32 getParkYaw() const { return _parkYaw; }
        /** @brief 코스터 번호의 열차를 반환합니다. 없으면 nullptr 입니다. */
        const CoasterTrain* findCoasterTrain( int32 coasterIndex ) const;
        /** @brief 배치 @p placementIndex 에 건설한 코스터의 번호입니다. 코스터가 아니거나 건설 전이면 nullopt 입니다. */
        [[nodiscard]] optional<int32> findCoasterIndex( int32 placementIndex ) const;
        /** @brief 공원이 새로 열리거나 복원될 때마다 오르는 번호입니다(열린 판은 1 이상) — 뷰는 이 값이 바뀌면 모습을 모두 다시 세운다. 건설은 올리지 않는다. */
        uint32 getParkRevision() const { return _parkRevision; }
        /** @brief 손님이 @p from 에서 @p to 로 걷는 길의 점(시작은 뺀다)입니다 — 뷰가 그 자리에 길을 깐다. */
        void findGuestRoute( const float3& from, const float3& to, vector<float3>& outListPoint ) const;
        /** @brief 공원과 놀이기구의 지금 상태를 로그로 남깁니다. */
        void logStatus() const override;

    protected:
        /** @brief 배치 데이터를 읽고 공원을 열어 처음 두 개(가장 싼 평지 놀이기구와 코스터)를 건설합니다. */
        [[nodiscard]] bool startGame() override;
        void               onStateRestored( bool bRestored ) override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 건설한 코스터 하나입니다. 트랙은 열차가 가리키므로 힙에 둡니다(목록이 자라도 주소가 그대로입니다). */
        struct CoasterRuntime
        {
            unique_ptr<CoasterTrack> _pTrack{};
            CoasterTrain             _train{};
            int32                    _placementIndex{ -1 };
        };

    private:
        /** @brief 배치 데이터(공원 원점 기준)를 월드 자리로 옮겨 놀이기구 목록을 만들고 정문을 정합니다. 이 오브젝트에 씬 컴포넌트가 없으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool placeLayout( const ParkLayout& layout );
        /** @brief 손님이 걷는 격자를 세우고 막는 오브젝트(정적 바디)와 평지 놀이기구의 땅을 막습니다. 막는 오브젝트에 정적 바디가 없으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool rebuildWalkGrid();
        /**
         * @brief 배치의 코스터(트랙, 열차)를 만들고 테스트 운행 결과를 @p outStats 에 냅니다. 배치나 설계가 없으면 nullptr 을 반환합니다.
         * @details 건설(`buildPlacement`)과 복원이 같은 트랙을 만든다. 열차는 트랙을 가리키므로 트랙째 힙에 둔다.
         */
        unique_ptr<CoasterRuntime> createCoaster( int32 placementIndex, CoasterRideStats& outStats ) const;
        /** @brief 설계도 하나를 건설합니다(돈이 모자라면 false). 모습은 클라이언트 뷰가 세웁니다. */
        bool buildPlacement( int32 placementIndex );
        /** @brief 아직 건설하지 않은 것 중 가장 싼 것을 건설합니다. */
        bool buildCheapestRemaining();
        /** @brief 판이 열렸고 지은 놀이기구 번호면 true 입니다. */
        [[nodiscard]] bool hasRide( int32 rideIndex ) const;

    private:
        PROPERTY( Category = "Data", DisplayName = "Park Layout", AssetPath, Tooltip = "Gate, cash and ride placement XML" )
        string _parkDataPath;
        PROPERTY( Category = "Scene", DisplayName = "Gate", Tooltip = "Object whose position is the park gate; empty uses the park layout" )
        GameObjectHandle _gate;
        PROPERTY( Category = "Park", DisplayName = "Walk Blockers", Tooltip = "Scene objects with a static body (stalls, benches, bins) that guests and paths go around" )
        vector<GameObjectHandle> _listWalkBlocker;
        PROPERTY( Category = "Park", DisplayName = "Walk Cell Size", Tooltip = "Edge of one cell of the guest walk grid", Min = 0.1, Units = m )
        float32 _walkCellSize;
        PROPERTY( Category = "Park", DisplayName = "Walk Clearance", Tooltip = "Guests keep this far from blockers and ride lots (half the path width)", Min = 0.0, Units = m )
        float32 _walkClearance;
        PROPERTY( Category = "Build", DisplayName = "Auto Build Interval", Tooltip = "Seconds between automatic builds when auto play is on", Min = 1.0, Units = s )
        float32 _autoBuildInterval;
        PROPERTY( Category = "Debug", DisplayName = "Status Log Interval", Tooltip = "Seconds between status log lines", Min = 0.1, Units = s )
        float32 _statusLogInterval;

        NavGrid                              _walkGrid; ///< 시뮬레이션이 빌린다 — 먼저 만들고 나중에 사라진다
        ThemeParkSimulation                  _simulation;
        Wallet                               _wallet;       ///< 공원 금고. 한 판 임시 상태라 디렉터가 보관하고 빌려 준다(디렉터 계약 ③).
        int32                                _startingCash; ///< 이 판을 연 돈(배치 데이터) — 나갈 때 번 돈을 센다
        SharedAssetRef<CoasterLayoutCatalog> _pLayoutCatalog;
        ThemeParkSettings                    _settings;
        vector<RidePlacement>                _listPlacement;
        vector<unique_ptr<CoasterRuntime>>   _listCoaster;
        PROPERTY( RuntimeState )
        float32 _autoBuildTimer;
        float32 _parkYaw;      ///< `getParkYaw`
        uint32  _parkRevision; ///< `getParkRevision`
    };
} // namespace sw
