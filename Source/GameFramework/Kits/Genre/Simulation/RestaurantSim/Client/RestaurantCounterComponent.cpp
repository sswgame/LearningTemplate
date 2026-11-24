#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/Client/RestaurantCounterComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/Client/RestaurantHUDViewModel.h"
#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/Component/RestaurantGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
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
        : _moveAction{ CommonInputActions::kMove }
        , _buyAction{}
        , _priceUpAction{}
        , _priceDownAction{}
        , _stationAction{}
        , _openAction{}
        , _seatOrigin{ 0.0f, 0.0f }
        , _seatSpacing{ 1.5f }
        , _bodyPrefab{}
        , _markerPrefab{}
        , _director{}
        , _bodies{}
        , _markers{}
        , _hudViewModel{}
        , _row{ 0 }
        , _lastStep{ 0 }
        , _bInputLive{ SW_FALSE }
        , _bActionsValid{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    RestaurantCounterComponent::~RestaurantCounterComponent() = default;

    void RestaurantCounterComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                        pOwner    = getOwner();
        const RestaurantGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<RestaurantGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "Restaurant.CounterWithoutGameMode", "Restaurant counter on '%#': no RestaurantGameModeComponent in front of it on the same object - the counter does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<RestaurantGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<RestaurantGameModeComponent>( pDirector );
    }

    void RestaurantCounterComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        const bool bMarketNamed = _moveAction.empty() == false && _buyAction.empty() == false && _stationAction.empty() == false && _openAction.empty() == false;
        const bool bPriceNamed  = _priceUpAction.empty() == false && _priceDownAction.empty() == false;
        _bActionsValid          = bMarketNamed && bPriceNamed ? SW_TRUE : SW_FALSE;
        if ( _bActionsValid == SW_FALSE )
            SW_LOG_ERROR_ID( "Restaurant.CounterActionMissing", "Restaurant counter: move '%#', buy '%#', station '%#', open '%#', price up '%#' and price down '%#' actions must all be named - the counter reads no input",
                             _moveAction.c_str(), _buyAction.c_str(), _stationAction.c_str(), _openAction.c_str(), _priceUpAction.c_str(), _priceDownAction.c_str() );
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
        Component::onEndPlay();
    }

    void RestaurantCounterComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( RestaurantCounterComponent& counter, GameObjectManager& manager )
        { counter.updateCounter( manager ); } );
    }

    void RestaurantCounterComponent::updateCounter( GameObjectManager& manager )
    {
        RestaurantGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        const bool                bReadInput = _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE;
        const InputManager* const pInput     = bReadInput ? game::findSubsystem<InputManager>() : nullptr;
        if ( pInput != nullptr )
            submitInput( *pDirector, GameInputView( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() ) );
        updateHUD( *pDirector );
        _bodies.begin();
        _markers.begin();
        draw( *pDirector, manager );
        _bodies.end( manager );
        _markers.end( manager );
    }

    void RestaurantCounterComponent::submitInput( RestaurantGameModeComponent& director, const GameInputView& input )
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
