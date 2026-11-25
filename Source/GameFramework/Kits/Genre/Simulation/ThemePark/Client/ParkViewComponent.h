/**
 * @file ParkViewComponent.h
 * @brief 테마파크 키트의 클라이언트 쪽 — 손님 · 놀이기구 · 코스터 레일과 차량 모습(프리팹)을 세우고, 입력을 디렉터 명령으로 바꾸며, 코스터 탑승 시점을 카메라에 건다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_ThemePark`, 전용 서버는 짓지 않는다). 디렉터(`ParkGameModeComponent`)의 명령(`submit…`)과 읽기 API 만 쓰고 공원(규칙)을 직접
 *          고치지 않는다. 디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. 모습 하나를 매 프레임 맞추는 일은 뷰 컴포넌트
 *          (`ParkGuestComponent`, `CoasterCarComponent`, `FlatRideComponent`)가 디렉터를 읽어 한다.
 *          하는 일은 모두 틱 뒤 게임 스레드에서 한다. 디렉터와 같은 `PrePhysics` 그룹이라 카메라 디렉터(`CameraDirectorComponent`, `PostPhysics` 틱 뒤)가 이번 프레임의 탑승 시점을 읽는다.
 *          세운 모습은 파생이라 상태에 들지 않는다(`SpawnLifetime::Derived`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/Radians.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Appearance/MaterialTintCache.h"

namespace sw
{
    class CoasterTrain;
    class GameInputView;
    class GameObject;
    class GameObjectManager;
    class MaterialInstance;
    class MeshComponent;
    class ParkGameModeComponent;

    /**
     * @class ParkViewComponent
     * @brief 공원의 모습과 조작입니다. 공원이 열리거나 복원되면(`getParkRevision`) 손님 풀과 지은 것을 모두 다시 세우고, 새로 지은 놀이기구는 그때 하나씩 세웁니다.
     * @details 손님은 최대 손님 수만큼 미리 세워 숨긴다(오브젝트를 매 프레임 만들고 지우지 않는다). 고른 배치는 이 컴포넌트가 들고(RuntimeState), 탑승 중인 코스터는 싣지 않는다.
     */
    REFLECT( Category = "ThemePark", DisplayName = "Park View",
             Tooltip = "Client side of a park: spawns the guest, ride and coaster views of the Park Game Mode on the same object, turns input into its commands and drives the coaster ride camera" )
    class SW_KIT_CLIENT_API ParkViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PrePhysics; ///< 디렉터와 같은 그룹 — 카메라 디렉터(PostPhysics 틱 뒤)가 이번 프레임의 탑승 시점을 읽는다.

        /** @brief 손님 뷰(병렬 틱)가 남 읽기 길로 읽습니다 — 모습은 틱 뒤 게임 스레드(`updateView`)에서만 바뀌고 틱은 자기 칸만 쓴다. */
        static constexpr bool kTickReadSafe = true;

        ParkViewComponent();
        virtual ~ParkViewComponent() override;

        /** @brief 세운 모습을 걷습니다. */
        void onEndPlay() override;

        /** @brief 행복도 슬롯(0 초록, 1 노랑, 2 빨강)의 손님 모습을 반환합니다. 아직 없으면 비어 있습니다. */
        const shared_ptr<MaterialInstance>& getGuestLook( int32 bucket ) const;
        /** @brief 행복도에서 색 슬롯으로 바꿉니다. */
        static int32 computeHappinessBucket( float32 happiness );

    private:
        /** @brief 세운 모습을 모두 지웁니다(플레이 끝 · 다시 세울 때). 모습은 파생이라 상태를 실을 때 걷지 않는다(`SpawnLifetime::Derived`). */
        void despawnViews();
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고, 모습을 맞추고, 탑승 시점을 카메라에 겁니다. */
        void updateView( GameObjectManager& manager, ParkGameModeComponent& director );
        void updateInput( const GameInputView& input, ParkGameModeComponent& director );
        /** @brief 고른 배치를 다음 것으로 옮깁니다. */
        void selectNextRide( const ParkGameModeComponent& director );
        /** @brief 타고 있으면 내립니다. 아니면 고른 코스터(고른 것이 코스터가 아니면 첫 코스터)에 탑니다. */
        void        toggleCoasterRide( const ParkGameModeComponent& director );
        static void logThoughts( const ParkGameModeComponent& director );
        /** @brief 공원이 바뀌었으면(`getParkRevision`) 모두 다시 세우고, 새로 지은 놀이기구를 세웁니다. */
        void syncViews( GameObjectManager& manager, const ParkGameModeComponent& director );
        void spawnGuestPool( GameObjectManager& manager, const ParkGameModeComponent& director );
        void spawnRideView( GameObjectManager& manager, const ParkGameModeComponent& director, int32 placementIndex );
        void spawnCoasterView( GameObjectManager& manager, const CoasterTrain& train, int32 coasterIndex, float32 groundHeight );
        void spawnPath( GameObjectManager& manager, const float3& from, const float3& to );
        /** @brief 프리팹 하나를 세우고 걷을 목록에 넣습니다. 프리팹을 읽지 못하면 nullptr 입니다. */
        GameObject* spawnView( GameObjectManager& manager, const AssetRef<PrefabAsset>& prefab, const utf8* pName );
        /** @brief 코스터 탑승 시점을 카메라 리그에 겁니다(내리면 풉니다). */
        void updateCameraOverride( GameObjectManager& manager, const ParkGameModeComponent& director );

    private:
        PROPERTY( Category = "Prefabs" )
        AssetRef<PrefabAsset> _guestPrefab;
        PROPERTY( Category = "Prefabs" )
        AssetRef<PrefabAsset> _carFrontPrefab;
        PROPERTY( Category = "Prefabs" )
        AssetRef<PrefabAsset> _carPrefab;
        PROPERTY( Category = "Prefabs" )
        AssetRef<PrefabAsset> _railPrefab;
        PROPERTY( Category = "Prefabs" )
        AssetRef<PrefabAsset> _supportPrefab;
        PROPERTY( Category = "Prefabs" )
        AssetRef<PrefabAsset> _stationPrefab;
        PROPERTY( Category = "Prefabs" )
        AssetRef<PrefabAsset> _rideEntrancePrefab;
        PROPERTY( Category = "Prefabs" )
        AssetRef<PrefabAsset> _pathPrefab;
        PROPERTY( Category = "Prefabs" )
        AssetRef<PrefabAsset> _flatRidePrefab;
        PROPERTY( Category = "Look", DisplayName = "Happy Guest", Tooltip = "Guest colour at happiness 0.6 and above" )
        float4 _happyColor;
        PROPERTY( Category = "Look", DisplayName = "Content Guest", Tooltip = "Guest colour at happiness 0.35 to 0.6" )
        float4 _contentColor;
        PROPERTY( Category = "Look", DisplayName = "Unhappy Guest", Tooltip = "Guest colour below happiness 0.35" )
        float4 _unhappyColor;
        PROPERTY( Category = "Scene", DisplayName = "Camera Rig", Tooltip = "Object with the CameraDirectorComponent the coaster ride view overrides" )
        GameObjectHandle _cameraRig;
        PROPERTY( Required, Category = "Input", DisplayName = "Next Ride Action", Tooltip = "InputMap action that selects the next ride (all actions are required)" )
        hashed_string _nextRideAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Toggle Open Action", Tooltip = "InputMap action that opens or closes the selected ride" )
        hashed_string _toggleOpenAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Price Down Action", Tooltip = "InputMap action that lowers the selected ride's ticket price" )
        hashed_string _priceDownAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Price Up Action", Tooltip = "InputMap action that raises the selected ride's ticket price" )
        hashed_string _priceUpAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Fee Down Action", Tooltip = "InputMap action that lowers the entry fee" )
        hashed_string _feeDownAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Fee Up Action", Tooltip = "InputMap action that raises the entry fee" )
        hashed_string _feeUpAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Build Action", Tooltip = "InputMap action that builds the selected ride (the cheapest remaining one when it is already built)" )
        hashed_string _buildAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Ride Action", Tooltip = "InputMap action that boards or leaves the selected coaster" )
        hashed_string _rideAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Thoughts Action", Tooltip = "InputMap action that logs what the guests think" )
        hashed_string _thoughtsAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Status Action", Tooltip = "InputMap action that logs the park status" )
        hashed_string _statusAction;
        PROPERTY( Category = "Input", DisplayName = "Price Step", Tooltip = "Ticket price change per press", Min = 1 )
        int32 _priceStep;
        PROPERTY( Category = "Input", DisplayName = "Entry Fee Step", Tooltip = "Entry fee change per press", Min = 1 )
        int32 _entryFeeStep;
        PROPERTY( Category = "Coaster", DisplayName = "Car Count", Tooltip = "Cars in the coaster train", Min = 1 )
        uint32 _carCount;
        PROPERTY( Category = "Coaster", DisplayName = "Model Scale", Tooltip = "Scale of the rail, support and car models", Min = 0.0 )
        float32 _modelScale;
        PROPERTY( Category = "Ride Camera", DisplayName = "Eye Height", Min = 0.0, Units = m )
        float32 _rideEyeHeight;
        PROPERTY( Category = "Ride Camera", DisplayName = "Field Of View", Min = 5.7 )
        Radians _rideFieldOfView;
        PROPERTY( Category = "Ride Camera", DisplayName = "Far Plane", Min = 1.0, Units = m )
        float32 _rideFarPlane;
        PROPERTY( Category = "Coaster", DisplayName = "Rail Spacing", Tooltip = "Distance between rail pieces along the track", Min = 0.1, Units = m )
        float32 _railSpacing;
        PROPERTY( Category = "Coaster", DisplayName = "Support Spacing", Tooltip = "Distance between supports along the track", Min = 0.1, Units = m )
        float32 _supportSpacing;
        /**
         * @brief Kenney Coaster Kit 레일 모델의 모양입니다(키트 단위, Model Scale 을 곱하기 전). 키트는 열차 폭 0.7 이고 이 공원의 차 폭은 1.4 다.
         * @details 레일 조각은 시작점에서 +Z 로 Rail Piece Length 만큼 뻗고, 레일 윗면이 원점보다 Rail Top Depth 아래다(열차 바닥이 원점).
         *          그래서 레일은 트랙 점에서 `Model Scale × Rail Top Depth` 만큼 올려 윗면을 트랙 점에 맞추고, 차는 바닥을 트랙 점에 둔다.
         */
        PROPERTY( Category = "Coaster", DisplayName = "Rail Piece Length", Tooltip = "Length of one rail model along +Z, in kit units", Min = 0.1 )
        float32 _railPieceLength;
        PROPERTY( Category = "Coaster", DisplayName = "Rail Top Depth", Tooltip = "How far the rail model's top sits below its origin, in kit units", Min = 0.0 )
        float32 _railTopDepth;
        PROPERTY( Category = "Coaster", DisplayName = "Rail Bottom Depth", Tooltip = "How far the rail model's bottom sits below its origin, in kit units", Min = 0.0 )
        float32 _railBottomDepth;
        PROPERTY( RuntimeState )
        int32 _selectedPlacement;

        SW_TRANSIENT vector<GameObjectHandle> _listViewObject; ///< 세운 모습(걷을 목록)
        SW_TRANSIENT vector<uint8>     _listRideShown;         ///< 배치 번호 → 모습을 세웠으면 SW_TRUE
        SW_TRANSIENT MaterialTintCache _tintCache;             ///< 손님 · 평지 놀이기구 색(같은 색은 나눠 쓴다. 배치 키가 인스턴스다)
        SW_TRANSIENT shared_ptr<MaterialInstance> _arrGuestLook[3];
        SW_TRANSIENT uint32                       _shownRevision; ///< 모습을 세운 공원의 번호(0 이면 아직 — 열린 디렉터는 1 부터다)
        SW_TRANSIENT int32                        _ridingCoaster; ///< 0 이상이면 그 코스터에 타고 있다
    };
} // namespace sw
