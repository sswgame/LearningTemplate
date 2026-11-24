#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/Client/CardBattleViewComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/Client/CardBattleHUDViewModel.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/Component/CardBattleGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/IHUDViewModelBinder.h"

namespace sw
{
    namespace
    {
        struct CardBattleViewComponentInternal
        {
            static constexpr float32 kPickThreshold = 0.5f;  ///< 축이 이보다 기울어야 한 장 옮긴다(스틱 떨림을 고르기로 읽지 않게)
            static constexpr float32 kCardWidth     = 1.0f;  ///< 카드 폭(m)
            static constexpr float32 kCardLength    = 1.5f;  ///< 카드 길이(m)
            static constexpr float32 kCardThickness = 0.05f; ///< 카드 두께(m)
            static constexpr float32 kCardSpacing   = 1.2f;  ///< 손패 사이 간격(m)
            static constexpr float32 kPickedLift    = 0.4f;  ///< 고른 카드를 들어 올리는 높이(m)
            static constexpr float32 kPileX         = -5.0f; ///< 뽑을 더미 자리(m)
            static constexpr float32 kDiscardX      = 5.0f;  ///< 버린 더미 자리(m)
            static constexpr float32 kEnemyZ        = 5.0f;  ///< 상대 자리(m)

            /** @brief 축 값의 방향(-1 · 0 · 1)입니다. */
            static int32 computeStep( float32 axis )
            {
                return static_cast<int32>( axis > kPickThreshold ) - static_cast<int32>( axis < -kPickThreshold );
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( cardBattleSelected, "CardBattle.SelectedCard", "Picked hand slot (0 = leftmost)", CardBattleViewComponent, &CardBattleViewComponent::getSelectedCard );

    CardBattleViewComponent::CardBattleViewComponent()
        : _moveAction{ CommonInputActions::kMove }
        , _confirmAction{}
        , _endTurnAction{}
        , _cardPrefab{}
        , _enemyPrefab{}
        , _playableColor{ 0.95f, 0.95f, 0.85f, 1.0f }
        , _disabledColor{ 0.4f, 0.4f, 0.42f, 1.0f }
        , _pickedColor{ 1.0f, 0.85f, 0.25f, 1.0f }
        , _pileColor{ 0.25f, 0.35f, 0.75f, 1.0f }
        , _enemyColor{ 0.85f, 0.25f, 0.3f, 1.0f }
        , _director{}
        , _cardViews{}
        , _enemyViews{}
        , _hudViewModel{ nullptr }
        , _selectedCard{ 0 }
        , _lastStep{ 0 }
        , _bInputLive{ SW_FALSE }
        , _bActionsValid{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    CardBattleViewComponent::~CardBattleViewComponent() = default;

    void CardBattleViewComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                        pOwner    = getOwner();
        const CardBattleGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<CardBattleGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "CardBattle.ViewWithoutGameMode", "Card battle view on '%#': no CardBattleGameModeComponent in front of it on the same object - the view does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<CardBattleGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<CardBattleGameModeComponent>( pDirector );
    }

    void CardBattleViewComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostUpdate );
        _bActionsValid = _moveAction.empty() == false && _confirmAction.empty() == false && _endTurnAction.empty() == false ? SW_TRUE : SW_FALSE;
        if ( _bActionsValid == SW_FALSE )
            SW_LOG_ERROR_ID( "CardBattle.ViewActionMissing", "Card battle view: move '%#', confirm '%#' and end turn '%#' actions must all be named - the view reads no input", _moveAction.c_str(),
                             _confirmAction.c_str(), _endTurnAction.c_str() );
        _selectedCard = 0;
        _lastStep     = 0;
    }

    void CardBattleViewComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            _cardViews.destroyAll( *pManager );
            _enemyViews.destroyAll( *pManager );
        }
        _hudViewModel.reset(); // 지워지는 뷰모델은 HUD 가 스스로 뗀다
        Component::onEndPlay();
    }

    void CardBattleViewComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( CardBattleViewComponent& view, GameObjectManager& manager )
        { view.updateView( manager ); } );
    }

    void CardBattleViewComponent::updateView( GameObjectManager& manager )
    {
        CardBattleGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        if ( _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE && pDirector->isAwaitingCommand() )
            submitInput( *pDirector );
        _selectedCard = MathUtil::clamp( _selectedCard, 0, MathUtil::max( 0, pDirector->getHandCount() - 1 ) ); // 낸 카드만큼 손이 줄었다
        showBattle( manager, *pDirector );
        updateHUD( *pDirector );
    }

    void CardBattleViewComponent::submitInput( CardBattleGameModeComponent& director )
    {
        const InputManager* const pInput = game::findSubsystem<InputManager>();
        if ( pInput == nullptr )
            return;
        const GameInputView input( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
        const int32         step  = CardBattleViewComponentInternal::computeStep( input.getVector2D( _moveAction )._x );
        const int32         fresh = step != _lastStep ? step : 0;
        _lastStep                 = step;
        _selectedCard             = MathUtil::clamp( _selectedCard + fresh, 0, MathUtil::max( 0, director.getHandCount() - 1 ) );
        // 낼 수 없는 카드는 디렉터가 거절한다 — 고른 자리는 결과로 바뀌지 않는다.
        if ( input.wasActionTriggered( _confirmAction ) )
            (void)director.playCard( _selectedCard );
        if ( input.wasActionTriggered( _endTurnAction ) )
            (void)director.endTurn(); // 카드를 내어 이겼으면 받지 않는다
    }

    void CardBattleViewComponent::showBattle( GameObjectManager& manager, const CardBattleGameModeComponent& director )
    {
        using Internal           = CardBattleViewComponentInternal;
        const CardBattle& battle = director.getBattle();
        const int32       count  = director.getHandCount();
        const float32     firstX = -0.5f * Internal::kCardSpacing * static_cast<float32>( count - 1 );
        const float3      cardScale{ Internal::kCardWidth, Internal::kCardThickness, Internal::kCardLength };
        _cardViews.begin();
        for ( int32 handIndex = 0; handIndex < count; ++handIndex )
        {
            const bool    bPicked = handIndex == _selectedCard;
            const float4& color   = bPicked ? _pickedColor : director.isPlayable( handIndex ) ? _playableColor
                                                                                              : _disabledColor;
            (void)_cardViews.show( manager, _cardPrefab, AbsolutePosition{
                                                             float3{ firstX + Internal::kCardSpacing * static_cast<float32>( handIndex ), bPicked ? Internal::kPickedLift : 0.0f, 0.0f }
            },
                                   cardScale, 0.0f, color );
        }
        // 더미는 장수만큼 두꺼운 한 덩이로 보인다.
        const auto showPile = [&]( float32 x, const CardPile& pile )
        {
            if ( pile.isEmpty() )
                return;
            const float32 thickness = Internal::kCardThickness * static_cast<float32>( pile.getCount() );
            (void)_cardViews.show( manager, _cardPrefab, AbsolutePosition{
                                                             float3{ x, thickness * 0.5f, 0.0f }
            },
                                   float3{ Internal::kCardWidth, thickness, Internal::kCardLength }, 0.0f, _pileColor );
        };
        showPile( Internal::kPileX, battle.getDrawPile() );
        showPile( Internal::kDiscardX, battle.getDiscardPile() );
        _cardViews.end( manager );

        _enemyViews.begin();
        if ( battle.getPhase() != CardBattlePhase::Won )
            (void)_enemyViews.show( manager, _enemyPrefab, AbsolutePosition{
                                                               float3{ 0.0f, 0.0f, Internal::kEnemyZ }
            },
                                    float3{ 1.0f, 1.0f, 1.0f }, MathUtil::kPi, _enemyColor );
        _enemyViews.end( manager );
    }

    void CardBattleViewComponent::updateHUD( const CardBattleGameModeComponent& director )
    {
        GameObject* const pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<CardBattleHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        if ( IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>() )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        const CardBattle&              battle  = director.getBattle();
        const CardBattleEffect* const  pIntent = director.findEnemyIntent();
        const CardBattleCardDef* const pPicked = _selectedCard < director.getHandCount() ? battle.getCardDef( battle.getHand().getAt( _selectedCard ) ) : nullptr;
        _hudViewModel->setPlayerHP( battle.getPlayerHP() );
        _hudViewModel->setPlayerBlock( battle.getPlayerBlock() );
        _hudViewModel->setEnemyHP( battle.getEnemy()._hp );
        _hudViewModel->setEnemyIntent( pIntent != nullptr ? pIntent->_amount : 0 );
        _hudViewModel->setEnergy( battle.getEnergy() );
        _hudViewModel->setTurn( battle.getTurn() );
        _hudViewModel->setCost( pPicked != nullptr ? pPicked->_cost : 0 );
        _hudViewModel->setDrawCount( battle.getDrawPile().getCount() );
        _hudViewModel->setWonShown( battle.getPhase() == CardBattlePhase::Won );
        _hudViewModel->setLostShown( battle.getPhase() == CardBattlePhase::Lost );
    }
} // namespace sw
