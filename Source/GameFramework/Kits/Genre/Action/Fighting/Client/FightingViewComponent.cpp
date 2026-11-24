#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Fighting/Client/FightingViewComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Genre/Action/Fighting/Client/FightingHUDViewModel.h"
#include "GameFramework/Kits/Genre/Action/Fighting/Component/FightingGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/IHUDViewModelBinder.h"

namespace sw
{
    namespace
    {
        struct FightingViewComponentInternal
        {
            /** @brief 축 하나를 −1 · 0 · 1 로 바꿉니다. */
            static int32 toStep( float32 value, float32 deadZone )
            {
                if ( value > deadZone )
                    return 1;
                return value < -deadZone ? -1 : 0;
            }
            /** @brief 2D 축을 넘패드 방향으로 바꿉니다(가운데 5, 오른쪽 +1, 위 +3). */
            static uint8 toDirection( const float2& move, float32 deadZone )
            {
                constexpr int32 kCenter = 5;
                constexpr int32 kRowLen = 3;
                return static_cast<uint8>( kCenter + toStep( move._x, deadZone ) + kRowLen * toStep( move._y, deadZone ) );
            }
            /** @brief 지금 자세의 몸 높이입니다(대전의 허트 높이와 같은 데이터). */
            static float32 findBodyHeight( const FighterRuntime& fighter, const FightingSettings& rules )
            {
                const bool bDown = fighter._state == FighterState::Down || fighter._state == FighterState::Knockout;
                if ( bDown )
                    return rules._downBodyHeight;
                if ( fighter._state == FighterState::Juggle )
                    return rules._juggleBodyHeight;
                return fighter._posture == FighterPosture::Crouching ? fighter._pDef->_crouchHeight : fighter._pDef->_standHeight;
            }
        };
    } // namespace

    FightingViewComponent::FightingViewComponent()
        : _moveAction{ CommonInputActions::kMove }
        , _listButtonAction{}
        , _deadZone{ 0.4f }
        , _playerBody{}
        , _opponentBody{}
        , _director{}
        , _hudViewModel{}
        , _bInputLive{ SW_FALSE }
        , _bActionsValid{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    FightingViewComponent::~FightingViewComponent() = default;

    void FightingViewComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                      pOwner    = getOwner();
        const FightingGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<FightingGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "Fighting.ViewWithoutGameMode", "Fighting view on '%#': no FightingGameModeComponent in front of it on the same object - the view does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<FightingGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<FightingGameModeComponent>( pDirector );
    }

    void FightingViewComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        bool bActionsValid = _moveAction.empty() == false && _listButtonAction.empty() == false;
        for ( const hashed_string& action : _listButtonAction )
        {
            bActionsValid = bActionsValid && action.empty() == false;
        }
        _bActionsValid = bActionsValid ? SW_TRUE : SW_FALSE;
        if ( bActionsValid == false )
            SW_LOG_ERROR_ID( "Fighting.ViewActionMissing", "Fighting view: the move action '%#' and every button action (%# listed) must be named - the view reads no input", _moveAction.c_str(),
                             _listButtonAction.size() );
    }

    void FightingViewComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( FightingViewComponent& view, GameObjectManager& manager )
        { view.updateView( manager ); } );
    }

    void FightingViewComponent::updateView( GameObjectManager& manager )
    {
        FightingGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        const bool bReadInput = _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE && pDirector->isAcceptingInput();
        if ( bReadInput )
            submitInput( *pDirector );
        updateBody( manager, *pDirector, FightingGameModeComponent::kPlayer, _playerBody );
        updateBody( manager, *pDirector, FightingGameModeComponent::kOpponent, _opponentBody );
        updateHUD( *pDirector );
    }

    void FightingViewComponent::submitInput( FightingGameModeComponent& director ) const
    {
        const InputManager* const pInput = game::findSubsystem<InputManager>();
        if ( pInput == nullptr )
            return;
        const GameInputView input( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
        uint16              buttons = 0;
        for ( size_t index = 0; index < _listButtonAction.size(); ++index )
        {
            if ( input.wasActionTriggered( _listButtonAction[index] ) )
                buttons = static_cast<uint16>( buttons | ( 1u << index ) );
        }
        // 대전이 끝났으면 디렉터가 거절한다 — 거절은 할 일이 없다는 뜻이라 버린다.
        (void)director.submitPlayerInput( FightingViewComponentInternal::toDirection( input.getVector2D( _moveAction ), _deadZone ), buttons );
    }

    void FightingViewComponent::updateBody( GameObjectManager& manager, const FightingGameModeComponent& director, int32 player, GameObjectHandle body ) const
    {
        GameObject* const     pObject = body.isValid() ? manager.resolveGameObject( body ) : nullptr;
        SceneComponent* const pRoot   = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
        const FighterRuntime& fighter = director.getMatch().getFighter( player );
        if ( pRoot == nullptr || fighter._pDef == nullptr )
            return;
        const float32 height = FightingViewComponentInternal::findBodyHeight( fighter, director.getMatch().getSettings() );
        const float32 width  = fighter._pDef->_hurtRadius * 2.0f;
        pRoot->setLocalScale( float3( width, height, width ) );
        pRoot->setWorldPosition( manager.getWorldOrigin().toLocal( director.getFighterPosition( player ) + float3( 0.0f, height * 0.5f, 0.0f ) ) );
    }

    void FightingViewComponent::updateHUD( const FightingGameModeComponent& director )
    {
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<FightingHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>();
        GameObject* const          pOwner  = getOwner();
        if ( pBinder != nullptr && pOwner != nullptr )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        _hudViewModel->setHealth( director.computeHealthRatio( FightingGameModeComponent::kPlayer ), director.computeHealthRatio( FightingGameModeComponent::kOpponent ) );
        _hudViewModel->setRounds( director.getPlayerRounds(), director.getOpponentRounds() );
        _hudViewModel->setSecondsRemaining( director.computeSecondsRemaining() );
        _hudViewModel->setMatchWinner( director.getMatchWinner() );
    }
} // namespace sw
