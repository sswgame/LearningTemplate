#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/Client/CardBattleHUDViewModel.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/Component/CardBattleGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    class CardBattleGameModeComponent;
    class CardBattleHUDViewModel;
    class GameObjectManager;

    /**
     * @class CardBattleViewComponent
     * @brief `Move` 축을 좌우로 한 번 기울일 때 고른 카드가 한 장 옮겨 가고, 확정 액션은 카드 내기, 턴 끝 액션은 턴 끝으로 디렉터에 넘깁니다.
     */
    REFLECT( Category = "CardGame", DisplayName = "Card Battle View", Tooltip = "Client side of a deck battle: picks hand cards from input, shows the hand and the enemy and fills the HUD" )
    class CardBattleViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        CardBattleViewComponent();
        virtual ~CardBattleViewComponent() override;

        void onBeginPlay() override;
        /** @brief 세운 모습을 지우고 HUD 에서 뷰모델을 뗍니다. */
        void onEndPlay() override;

        /** @brief 고른 손패 자리입니다(자동화 감시 값). */
        int32 getSelectedCard() const { return _selectedCard; }

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 내고 모습과 HUD 를 맞춥니다. */
        void updateView( GameObjectManager& manager, CardBattleGameModeComponent& director );
        /** @brief 고르기를 옮기고 확정 · 턴 끝을 디렉터에 냅니다. */
        void submitInput( const GameModeInput& input, CardBattleGameModeComponent& director );
        /** @brief 손패 · 더미 · 상대를 보입니다. */
        void showBattle( GameObjectManager& manager, const CardBattleGameModeComponent& director );
        /** @brief HUD 뷰모델을 채웁니다. HUD 가 없는 씬이면 값만 듭니다. */
        void updateHUD( const CardBattleGameModeComponent& director );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action: left/right steps the picked card (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Confirm Action", Tooltip = "InputMap action that plays the picked card (required)" )
        hashed_string _confirmAction;
        PROPERTY( Required, Category = "Input", DisplayName = "End Turn Action", Tooltip = "InputMap action that ends the turn (required)" )
        hashed_string _endTurnAction;
        PROPERTY( Category = "View", DisplayName = "Card Prefab", Required, Tooltip = "Unit piece stretched to a card" )
        AssetRef<PrefabAsset> _cardPrefab;
        PROPERTY( Category = "View", DisplayName = "Enemy Prefab", Required )
        AssetRef<PrefabAsset> _enemyPrefab;
        PROPERTY( Category = "View", DisplayName = "Playable Color" )
        float4 _playableColor;
        PROPERTY( Category = "View", DisplayName = "Disabled Color" )
        float4 _disabledColor;
        PROPERTY( Category = "View", DisplayName = "Picked Color" )
        float4 _pickedColor;
        PROPERTY( Category = "View", DisplayName = "Pile Color" )
        float4 _pileColor;
        PROPERTY( Category = "View", DisplayName = "Enemy Color" )
        float4 _enemyColor;

        SW_TRANSIENT PrefabViewBatch _cardViews;
        SW_TRANSIENT PrefabViewBatch _enemyViews;
        SW_TRANSIENT unique_ptr<CardBattleHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다)
        SW_TRANSIENT int32                              _selectedCard; ///< 고른 손패 자리
        SW_TRANSIENT int32                              _lastStep;     ///< 지난 프레임의 축 방향(-1 · 0 · 1) — 놓아야 다음 장으로 옮긴다
    };

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
        : GameModeViewComponent( GameModeViewBinding::of<&CardBattleViewComponent::updateView, &CardBattleViewComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _confirmAction{}
        , _endTurnAction{}
        , _cardPrefab{}
        , _enemyPrefab{}
        , _playableColor{ 0.95f, 0.95f, 0.85f, 1.0f }
        , _disabledColor{ 0.4f, 0.4f, 0.42f, 1.0f }
        , _pickedColor{ 1.0f, 0.85f, 0.25f, 1.0f }
        , _pileColor{ 0.25f, 0.35f, 0.75f, 1.0f }
        , _enemyColor{ 0.85f, 0.25f, 0.3f, 1.0f }
        , _cardViews{}
        , _enemyViews{}
        , _hudViewModel{ nullptr }
        , _selectedCard{ 0 }
        , _lastStep{ 0 }
    {
        setCanEverTick( true );
    }

    CardBattleViewComponent::~CardBattleViewComponent() = default;

    void CardBattleViewComponent::onBeginPlay()
    {
        GameModeViewComponent::onBeginPlay();
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
        GameModeViewComponent::onEndPlay();
    }

    void CardBattleViewComponent::updateView( GameObjectManager& manager, CardBattleGameModeComponent& director )
    {
        dispatchInput( manager, director );
        _selectedCard = MathUtil::clamp( _selectedCard, 0, MathUtil::max( 0, director.getHandCount() - 1 ) ); // 낸 카드만큼 손이 줄었다
        showBattle( manager, director );
        updateHUD( director );
    }

    void CardBattleViewComponent::submitInput( const GameModeInput& input, CardBattleGameModeComponent& director )
    {
        const int32 step  = CardBattleViewComponentInternal::computeStep( input.getVector2D( _moveAction )._x );
        const int32 fresh = step != _lastStep ? step : 0;
        _lastStep         = step;
        _selectedCard     = MathUtil::clamp( _selectedCard + fresh, 0, MathUtil::max( 0, director.getHandCount() - 1 ) );
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

#include "CardBattleViewComponent.cpp.gen.inl"
