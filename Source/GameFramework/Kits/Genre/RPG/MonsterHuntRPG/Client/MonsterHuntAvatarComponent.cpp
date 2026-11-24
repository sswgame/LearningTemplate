#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Client/MonsterHuntAvatarComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Client/MonsterHuntHUDViewModel.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Component/MonsterHuntGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/IHUDViewModelBinder.h"

namespace sw
{
    namespace
    {
        /** @brief 모습의 색 · 크기(m)입니다. */
        struct MonsterHuntAvatarComponentInternal
        {
            static constexpr float4 kHunterColor  = float4( 0.2f, 0.6f, 1.0f, 1.0f );
            static constexpr float4 kMonsterColor = float4( 0.85f, 0.15f, 0.2f, 1.0f );
            static constexpr float4 kClueColor    = float4( 1.0f, 0.85f, 0.2f, 1.0f );
            static constexpr float3 kHunterScale  = float3( 1.2f, 1.2f, 1.2f );
            static constexpr float3 kMonsterScale = float3( 2.4f, 2.0f, 2.4f );
            static constexpr float3 kClueSize     = float3( 0.4f, 0.3f, 0.4f );

            /** @brief 보이는 정수(소수는 올림)입니다. */
            static int32 toShown( float32 value ) { return static_cast<int32>( MathUtil::ceil( value ) ); }
        };
    } // namespace

    MonsterHuntAvatarComponent::MonsterHuntAvatarComponent()
        : _moveAction{ CommonInputActions::kMove }
        , _fastAction{}
        , _strongAction{}
        , _dodgeAction{}
        , _signAction{}
        , _oilAction{}
        , _potionAction{}
        , _investigateAction{}
        , _alternateSignAction{}
        , _learnAction{}
        , _brewAction{}
        , _mutagenAction{}
        , _bodyPrefab{}
        , _markerPrefab{}
        , _director{}
        , _bodies{}
        , _markers{}
        , _hudViewModel{}
        , _bInputLive{ SW_FALSE }
        , _bActionsValid{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    MonsterHuntAvatarComponent::~MonsterHuntAvatarComponent() = default;

    void MonsterHuntAvatarComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                         pOwner    = getOwner();
        const MonsterHuntGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<MonsterHuntGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "MonsterHunt.AvatarWithoutGameMode", "Monster hunt avatar on '%#': no MonsterHuntGameModeComponent in front of it on the same object - the avatar does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<MonsterHuntGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<MonsterHuntGameModeComponent>( pDirector );
    }

    void MonsterHuntAvatarComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostUpdate );
        _bActionsValid = SW_TRUE;
        for ( const hashed_string* pAction : { &_moveAction, &_fastAction, &_strongAction, &_dodgeAction, &_signAction, &_oilAction, &_potionAction, &_investigateAction, &_alternateSignAction, &_learnAction, &_brewAction, &_mutagenAction } )
        {
            if ( pAction->empty() )
                _bActionsValid = SW_FALSE;
        }
        if ( _bActionsValid == SW_FALSE )
            SW_LOG_ERROR_ID( "MonsterHunt.AvatarActionMissing",
                             "Monster hunt avatar: move '%#', fast '%#', strong '%#', dodge '%#', sign '%#', alternate sign '%#', oil '%#', potion '%#', learn '%#', brew '%#', mutagen '%#' and investigate '%#' actions must all be named - the avatar reads no input",
                             _moveAction.c_str(), _fastAction.c_str(), _strongAction.c_str(), _dodgeAction.c_str(), _signAction.c_str(), _alternateSignAction.c_str(), _oilAction.c_str(), _potionAction.c_str(),
                             _learnAction.c_str(), _brewAction.c_str(), _mutagenAction.c_str(), _investigateAction.c_str() );
    }

    void MonsterHuntAvatarComponent::onEndPlay()
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

    void MonsterHuntAvatarComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( MonsterHuntAvatarComponent& avatar, GameObjectManager& manager )
        { avatar.updateAvatar( manager ); } );
    }

    void MonsterHuntAvatarComponent::updateAvatar( GameObjectManager& manager )
    {
        MonsterHuntGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        const bool                bReadInput = _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE;
        const InputManager* const pInput     = bReadInput ? game::findSubsystem<InputManager>() : nullptr;
        if ( pInput != nullptr )
            submitInput( *pDirector, GameInputView( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() ) );
        vector<const MonsterHuntClueDef*> listClue; // 카탈로그의 것이라 프레임을 넘겨 들지 않는다
        if ( pDirector->getGamePhase() == MonsterHuntPhase::Investigate )
            pDirector->collectNearbyClues( listClue );
        updateHUD( *pDirector, listClue.empty() == false );
        _bodies.begin();
        _markers.begin();
        draw( *pDirector, listClue, manager );
        _bodies.end( manager );
        _markers.end( manager );
    }

    void MonsterHuntAvatarComponent::submitInput( MonsterHuntGameModeComponent& director, const GameInputView& input ) const
    {
        // 지금 단계에 맞지 않거나 쓸 수 없는 명령(쿨다운 · 스태미나 · 빈 물약)은 디렉터가 거절하고 판은 그대로다.
        (void)director.submitMove( input.getVector2D( _moveAction ) );
        const bool bHaggle = director.getGamePhase() == MonsterHuntPhase::Haggle;
        if ( input.wasActionTriggered( _fastAction ) )
            (void)( bHaggle ? director.submitAccept() : director.submitAttack( false ) );
        if ( input.wasActionTriggered( _strongAction ) )
            (void)( bHaggle ? director.submitRaise() : director.submitAttack( true ) );
        if ( input.wasActionTriggered( _dodgeAction ) )
            (void)director.submitDodge();
        if ( input.wasActionTriggered( _signAction ) )
            (void)director.submitSign( false );
        if ( input.wasActionTriggered( _alternateSignAction ) )
            (void)director.submitSign( true );
        if ( input.wasActionTriggered( _learnAction ) )
            (void)director.submitLearnSkill();
        if ( input.wasActionTriggered( _brewAction ) )
            (void)director.submitBrew();
        if ( input.wasActionTriggered( _mutagenAction ) )
            (void)director.submitMutagen();
        if ( input.wasActionTriggered( _oilAction ) )
            (void)director.submitOil();
        if ( input.wasActionTriggered( _potionAction ) )
            (void)director.submitPotion();
        if ( input.wasActionTriggered( _investigateAction ) )
            (void)director.submitInvestigate();
    }

    void MonsterHuntAvatarComponent::updateHUD( const MonsterHuntGameModeComponent& director, bool bClueNearby )
    {
        GameObject* const pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<MonsterHuntHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        if ( IHUDViewModelBinder* pBinder = game::findSubsystem<IHUDViewModelBinder>() )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        using Internal = MonsterHuntAvatarComponentInternal;
        _hudViewModel->setPhase( director.getGamePhase() );
        _hudViewModel->setReward( director.getReward() );
        _hudViewModel->setHunter( director.getPlayerHP(), Internal::toShown( director.getCombat().getStamina().getValue() ) );
        _hudViewModel->setMonster( director.getMonsterHP(), director.isWeaknessKnown() );
        _hudViewModel->setAlchemy( director.getOilHits(), Internal::toShown( director.getToxicity() ) );
        _hudViewModel->setSkills( director.getSkills().getPoints(), director.getSignIntensity() );
        _hudViewModel->setClues( director.getFoundClueCount(), bClueNearby );
    }

    void MonsterHuntAvatarComponent::draw( const MonsterHuntGameModeComponent& director, const vector<const MonsterHuntClueDef*>& listClue, GameObjectManager& manager )
    {
        using Internal = MonsterHuntAvatarComponentInternal;
        for ( const MonsterHuntClueDef* pClue : listClue )
        {
            _markers.showStanding( manager, _markerPrefab, AbsolutePosition{ pClue->_position }, Internal::kClueSize, Internal::kClueColor );
        }
        (void)_bodies.show( manager, _bodyPrefab, director.getPlayerPosition(), Internal::kHunterScale, 0.0f, Internal::kHunterColor ); // 실패는 배치가 한 번 알린다
        if ( director.getGamePhase() == MonsterHuntPhase::Hunt )
            (void)_bodies.show( manager, _bodyPrefab, director.getMonsterPosition(), Internal::kMonsterScale, 0.0f, Internal::kMonsterColor );
    }
} // namespace sw
