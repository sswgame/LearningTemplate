/**
 * @file RestaurantCounterComponent.h
 * @brief 식당 경영 키트의 클라이언트 쪽 — 입력 맵 액션을 디렉터(`RestaurantGameModeComponent`)의 명령으로 넘기고, 자리 · 주방 · 줄을 프리팹 인스턴스(`PrefabViewBatch`)로 그리며 HUD 뷰모델을 채웁니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_RestaurantSim`, 전용 서버는 짓지 않는다). 디렉터의 명령과 읽기 API 만 쓰고 규칙을 직접 고치지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. HUD 는 같은 오브젝트의 HUD 컨트롤러(`HUDControllerComponent`)다.
 *          `Move` 축은 고른 줄(재료 · 요리)을 바꾼다(한 번 기울일 때 하나). 명령 · 그리기 · HUD 는 틱 뒤 게임 스레드에서 한다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameInputView;
    class GameObjectManager;
    class RestaurantGameModeComponent;
    class RestaurantHUDViewModel;

    /**
     * @class RestaurantCounterComponent
     * @brief 줄 고르기 · 사기 · 가격 올리기 · 내리기 · 열기와 다음 날을 디렉터 명령으로 넘기고 식당을 그립니다.
     */
    REFLECT( Category = "RestaurantSim", DisplayName = "Restaurant Counter",
             Tooltip = "Client side of a RestaurantSim run: sends commands to the Restaurant Game Mode on the same object, draws the seats and the kitchen and fills the HUD" )
    class SW_KIT_CLIENT_API RestaurantCounterComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        RestaurantCounterComponent();
        virtual ~RestaurantCounterComponent() override;

        /** @brief 같은 오브젝트의 디렉터를 찾아 핸들로 듭니다. 없으면 오류를 남기고 아무것도 하지 않습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 액션 이름이 비었으면 오류를 남깁니다(폴백 없음). 틱 그룹은 뷰와 같은 `PostUpdate` 입니다. */
        void onBeginPlay() override;
        /** @brief 세운 모습을 지웁니다. */
        void onEndPlay() override;
        /** @brief 입력 · 명령 · 그리기를 틱 뒤 게임 스레드로 미룹니다. */
        void onTick( TickContext& context ) override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 HUD · 그리기를 합니다. */
        void updateCounter( GameObjectManager& manager );
        void submitInput( RestaurantGameModeComponent& director, const GameInputView& input );
        void updateHUD( const RestaurantGameModeComponent& director );
        /** @brief 자리 · 손님 · 주문을 그립니다. */
        void draw( const RestaurantGameModeComponent& director, GameObjectManager& manager );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that changes the chosen row (required)" )
        hashed_string _moveAction;
        PROPERTY( Category = "Input", DisplayName = "Buy Action", Tooltip = "InputMap action that buys the chosen ingredient (required)" )
        hashed_string _buyAction;
        PROPERTY( Category = "Input", DisplayName = "Price Up Action", Tooltip = "InputMap action that raises the price of the chosen dish (required)" )
        hashed_string _priceUpAction;
        PROPERTY( Category = "Input", DisplayName = "Price Down Action", Tooltip = "InputMap action that lowers the price of the chosen dish (required)" )
        hashed_string _priceDownAction;
        PROPERTY( Category = "Input", DisplayName = "Station Action", Tooltip = "InputMap action that adds a cooking station for the chosen dish (required)" )
        hashed_string _stationAction;
        PROPERTY( Category = "Input", DisplayName = "Open Action", Tooltip = "InputMap action that opens the day, or starts the next one after closing (required)" )
        hashed_string _openAction;
        PROPERTY( Category = "View", DisplayName = "Seat Origin", Tooltip = "Absolute ground-plane position (X, Z) of the first seat" )
        float2 _seatOrigin;
        PROPERTY( Category = "View", DisplayName = "Seat Spacing", Min = 0.1, Units = m )
        float32 _seatSpacing;

        PROPERTY( Required, Category = "View", DisplayName = "Body Prefab", Tooltip = "Prefab with one mesh, feet at its origin and one metre tall, spawned for every body the view shows" )
        AssetRef<PrefabAsset> _bodyPrefab;
        PROPERTY( Required, Category = "View", DisplayName = "Marker Prefab", Tooltip = "One-metre cube prefab, spawned for every marker the view shows" )
        AssetRef<PrefabAsset> _markerPrefab;

        TComponentHandle<RestaurantGameModeComponent> _director;
        PrefabViewBatch                               _bodies;
        PrefabViewBatch                               _markers;
        unique_ptr<RestaurantHUDViewModel>            _hudViewModel;      ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다)
        int32                                         _row;               ///< 고른 줄(재료 · 요리)
        int32                                         _lastStep;          ///< 지난 프레임의 축 방향(-1 · 0 · 1) — 놓아야 다음 줄을 고른다
        uint8                                         _bInputLive    : 1; ///< 이번 틱의 시간이 흘렀다(멈춤 중에는 입력을 읽지 않고 그리기만 한다)
        uint8                                         _bActionsValid : 1; ///< 액션 이름이 모두 있다(`onBeginPlay`)
        [[maybe_unused]] uint8                        _reserved      : 6;
    };
} // namespace sw
