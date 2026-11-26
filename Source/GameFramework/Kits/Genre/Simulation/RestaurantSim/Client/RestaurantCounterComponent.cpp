#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/Client/RestaurantHUDViewModel.h"
#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/Component/RestaurantGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

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
    class RestaurantCounterComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        RestaurantCounterComponent();
        virtual ~RestaurantCounterComponent() override;

        void onBeginPlay() override;
        /** @brief 세운 모습을 지웁니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 HUD · 그리기를 합니다. */
        void updateCounter( GameObjectManager& manager, RestaurantGameModeComponent& director );
        void submitInput( const GameModeInput& input, RestaurantGameModeComponent& director );
        void updateHUD( const RestaurantGameModeComponent& director );
        /** @brief 자리 · 손님 · 주문을 그립니다. */
        void draw( const RestaurantGameModeComponent& director, GameObjectManager& manager );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that changes the chosen row (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Buy Action", Tooltip = "InputMap action that buys the chosen ingredient (required)" )
        hashed_string _buyAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Price Up Action", Tooltip = "InputMap action that raises the price of the chosen dish (required)" )
        hashed_string _priceUpAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Price Down Action", Tooltip = "InputMap action that lowers the price of the chosen dish (required)" )
        hashed_string _priceDownAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Station Action", Tooltip = "InputMap action that adds a cooking station for the chosen dish (required)" )
        hashed_string _stationAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Open Action", Tooltip = "InputMap action that opens the day, or starts the next one after closing (required)" )
        hashed_string _openAction;
        PROPERTY( Category = "View", DisplayName = "Seat Origin", Tooltip = "Absolute ground-plane position (X, Z) of the first seat" )
        float2 _seatOrigin;
        PROPERTY( Category = "View", DisplayName = "Seat Spacing", Min = 0.1, Units = m )
        float32 _seatSpacing;

        PROPERTY( Required, Category = "View", DisplayName = "Body Prefab", Tooltip = "Prefab with one mesh, feet at its origin and one metre tall, spawned for every body the view shows" )
        AssetRef<PrefabAsset> _bodyPrefab;
        PROPERTY( Required, Category = "View", DisplayName = "Marker Prefab", Tooltip = "One-metre cube prefab, spawned for every marker the view shows" )
        AssetRef<PrefabAsset> _markerPrefab;

        SW_TRANSIENT PrefabViewBatch _bodies;
        SW_TRANSIENT PrefabViewBatch _markers;
        SW_TRANSIENT unique_ptr<RestaurantHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다)
        SW_TRANSIENT int32                              _row;          ///< 고른 줄(재료 · 요리)
        SW_TRANSIENT int32                              _lastStep;     ///< 지난 프레임의 축 방향(-1 · 0 · 1) — 놓아야 다음 줄을 고른다
    };

    namespace
    {
        /** @brief 모습의 색 · 크기(m)와 입력 문턱입니다. */
        struct RestaurantCounterComponentInternal
        {
            static constexpr float4  kSeatColor      = float4( 0.5f, 0.35f, 0.2f, 1.0f );
            static constexpr float4  kWaitingColor   = float4( 1.0f, 0.75f, 0.2f, 1.0f );
            static constexpr float4  kEatingColor    = float4( 0.35f, 0.85f, 0.4f, 1.0f );
            static constexpr float4  kQueueColor     = float4( 0.6f, 0.6f, 0.65f, 1.0f );
            static constexpr float4  kOrderColor     = float4( 0.9f, 0.4f, 0.2f, 1.0f );
            static constexpr float3  kSeatSize       = float3( 0.7f, 0.4f, 0.7f );
            static constexpr float3  kBodyScale      = float3( 1.0f, 1.0f, 1.0f );
            static constexpr float3  kOrderSize      = float3( 0.3f, 1.1f, 0.3f ); ///< 주방 선반 위로 솟는 높이
            static constexpr float3  kReadySize      = float3( 0.6f, 1.3f, 0.6f );
            static constexpr float32 kRowOffset      = 2.0f; ///< 줄(문 앞) · 주방을 자리 줄에서 띄우는 거리
            static constexpr float32 kRowThreshold   = 0.5f; ///< 축이 이보다 기울어야 고른 줄을 옮긴다(스틱 떨림을 고르기로 읽지 않게)
            static constexpr float32 kMinutesPerHour = 60.0f;
            static constexpr int32   kPriceChange    = 1; ///< 한 번 누를 때 바꾸는 가격 단계

            /** @brief 축 값의 방향(-1 · 0 · 1)입니다. */
            static int32 computeStep( float32 axis )
            {
                if ( axis > kRowThreshold )
                    return 1;
                return axis < -kRowThreshold ? -1 : 0;
            }
        };
    } // namespace

    RestaurantCounterComponent::RestaurantCounterComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&RestaurantCounterComponent::updateCounter, &RestaurantCounterComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _buyAction{}
        , _priceUpAction{}
        , _priceDownAction{}
        , _stationAction{}
        , _openAction{}
        , _seatOrigin{ 0.0f, 0.0f }
        , _seatSpacing{ 1.5f }
        , _bodyPrefab{}
        , _markerPrefab{}
        , _bodies{}
        , _markers{}
        , _hudViewModel{}
        , _row{ 0 }
        , _lastStep{ 0 }
    {
    }

    RestaurantCounterComponent::~RestaurantCounterComponent() = default;

    void RestaurantCounterComponent::onBeginPlay()
    {
        GameModeViewComponent::onBeginPlay();
        _row      = 0;
        _lastStep = 0;
    }

    void RestaurantCounterComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            _bodies.destroyAll( *pManager );
            _markers.destroyAll( *pManager );
        }
        GameModeViewComponent::onEndPlay();
    }

    void RestaurantCounterComponent::updateCounter( GameObjectManager& manager, RestaurantGameModeComponent& director )
    {
        dispatchInput( manager, director );
        updateHUD( director );
        _bodies.begin();
        _markers.begin();
        draw( director, manager );
        _bodies.end( manager );
        _markers.end( manager );
    }

    void RestaurantCounterComponent::submitInput( const GameModeInput& input, RestaurantGameModeComponent& director )
    {
        const int32 step     = RestaurantCounterComponentInternal::computeStep( input.getVector2D( _moveAction )._x );
        const int32 fresh    = step != _lastStep ? step : 0;
        _lastStep            = step;
        const int32 rowCount = static_cast<int32>( director.getIngredients().size() ); // 열린 판은 재료가 하나 이상이다
        _row                 = ( ( _row + fresh ) % rowCount + rowCount ) % rowCount;
        // 지금 단계에 맞지 않거나 돈이 모자란 명령은 디렉터가 거절하고 판은 그대로다.
        if ( input.wasActionTriggered( _buyAction ) )
            (void)director.submitBuy( _row );
        if ( input.wasActionTriggered( _priceUpAction ) )
            (void)director.submitPriceChange( _row, RestaurantCounterComponentInternal::kPriceChange );
        if ( input.wasActionTriggered( _priceDownAction ) )
            (void)director.submitPriceChange( _row, -RestaurantCounterComponentInternal::kPriceChange );
        if ( input.wasActionTriggered( _stationAction ) )
            (void)director.submitAddStation( _row );
        if ( input.wasActionTriggered( _openAction ) )
            (void)( director.getGamePhase() == RestaurantPhase::Closed ? director.submitNextDay() : director.submitOpen() );
    }

    void RestaurantCounterComponent::updateHUD( const RestaurantGameModeComponent& director )
    {
        GameObject* const              pOwner   = getOwner();
        const RestaurantCatalog* const pCatalog = director.findCatalog();
        if ( pOwner == nullptr || pCatalog == nullptr )
            return;
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<RestaurantHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        if ( IHUDViewModelBinder* pBinder = game::findSubsystem<IHUDViewModelBinder>() )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        const RestaurantSimulation& simulation = director.getSimulation();
        const int32                 hour       = pCatalog->getOpenHour() + static_cast<int32>( simulation.getMinutes() / RestaurantCounterComponentInternal::kMinutesPerHour );
        _hudViewModel->setPhase( director.getGamePhase() );
        _hudViewModel->setDay( director.getDay(), director.getDayCount() );
        _hudViewModel->setMoney( director.getGold(), director.getTotalProfit(), director.getTargetProfit() );
        _hudViewModel->setHouse( director.getReputation(), director.getBestCookLevel() );
        _hudViewModel->setCustomers( hour, director.getServed(), simulation.countOccupiedSeats() );
        const vector<DishDef>& listDish        = pCatalog->getDishes();
        const size_t           row             = static_cast<size_t>( _row );
        const int32            ingredientCount = row < director.getIngredients().size() ? director.getPantry().getItemCount( director.getIngredients()[row] ) : 0;
        const bool             bDishRow        = row < listDish.size();
        _hudViewModel->setSelection( bDishRow ? string_view{ listDish[row]._name } : string_view{}, bDishRow ? static_cast<int32>( simulation.getMenuPrice( listDish[row]._id ) ) : 0, ingredientCount );
    }

    void RestaurantCounterComponent::draw( const RestaurantGameModeComponent& director, GameObjectManager& manager )
    {
        using Internal = RestaurantCounterComponentInternal;
        const AbsolutePosition first{
            float3{ _seatOrigin._x, 0.0f, _seatOrigin._y }
        };
        for ( int32 seat = 0; seat < director.getSettings()._seatCount; ++seat )
        {
            _markers.showStanding( manager, _markerPrefab, first + float3( static_cast<float32>( seat ) * _seatSpacing, 0.0f, 0.0f ), Internal::kSeatSize, Internal::kSeatColor );
        }
        float32 queueX = 0.0f;
        for ( const RestaurantCustomer& customer : director.getSimulation().getCustomers() )
        {
            if ( customer._seat >= 0 )
            {
                const float3 offset( static_cast<float32>( customer._seat ) * _seatSpacing, Internal::kSeatSize._y, 0.0f ); // 의자에 앉는다
                (void)_bodies.show( manager, _bodyPrefab, first + offset, Internal::kBodyScale, 0.0f, customer._state == CustomerState::Eating ? Internal::kEatingColor : Internal::kWaitingColor );
            }
            else if ( customer._state == CustomerState::Queued )
            {
                (void)_bodies.show( manager, _bodyPrefab, first + float3( queueX, 0.0f, -Internal::kRowOffset ), Internal::kBodyScale, 0.0f, Internal::kQueueColor ); // 실패는 배치가 한 번 알린다
                queueX += _seatSpacing;
            }
        }
        float32 orderX = 0.0f;
        for ( const KitchenOrder& order : director.getSimulation().getOrders() )
        {
            const float3 size = order._bReady == SW_TRUE ? Internal::kReadySize : Internal::kOrderSize; // 다 된 요리는 크게
            _markers.showStanding( manager, _markerPrefab, first + float3( orderX, 0.0f, Internal::kRowOffset ), size, Internal::kOrderColor );
            orderX += _seatSpacing;
        }
    }
} // namespace sw

#include "RestaurantCounterComponent.cpp.gen.inl"
