#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/Fighting/Client/FightingHUDViewModel.h"
#include "GameFramework/Kits/Genre/Action/Fighting/Component/FightingGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    class FightingGameModeComponent;
    class FightingHUDViewModel;
    class GameObjectManager;

    /**
     * @class FightingViewComponent
     * @brief `Move` 축을 넘패드 방향으로, 버튼 액션 누름을 카탈로그 버튼 비트로 바꿔 디렉터에 넘기고 두 몸 · HUD 를 맞춥니다.
     */
    REFLECT( Category = "Fighting", DisplayName = "Fighting View",
             Tooltip = "Client side of a Fighting match: sends input commands to the Fighting Game Mode on the same object and shows both fighters and the HUD" )
    class FightingViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        FightingViewComponent();
        virtual ~FightingViewComponent() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 몸 · HUD 를 맞춥니다. */
        void updateView( GameObjectManager& manager, FightingGameModeComponent& director );
        void submitInput( const GameModeInput& input, FightingGameModeComponent& director ) const;
        /** @brief 캐릭터 @p player 의 몸 오브젝트를 대전의 자리 · 자세 높이에 둡니다. */
        void updateBody( GameObjectManager& manager, const FightingGameModeComponent& director, int32 player, GameObjectHandle body ) const;
        void updateHUD( const FightingGameModeComponent& director );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action turned into a numpad direction (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Button Actions", Tooltip = "InputMap actions of the attack buttons, in the catalog's button order (required)" )
        vector<hashed_string> _listButtonAction;
        PROPERTY( Category = "Input", DisplayName = "Dead Zone", Tooltip = "An axis below this is neutral", Min = 0.0, Max = 1.0 )
        float32 _deadZone;
        PROPERTY( Category = "Scene", DisplayName = "Player Body", Tooltip = "Object shown as player 0" )
        GameObjectHandle _playerBody;
        PROPERTY( Category = "Scene", DisplayName = "Opponent Body", Tooltip = "Object shown as player 1" )
        GameObjectHandle _opponentBody;

        SW_TRANSIENT unique_ptr<FightingHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다 — 먼저 지워지면 HUD 가 스스로 뗀다)
    };

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
        : GameModeViewComponent( GameModeViewBinding::of<&FightingViewComponent::updateView, &FightingViewComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _listButtonAction{}
        , _deadZone{ 0.4f }
        , _playerBody{}
        , _opponentBody{}
        , _hudViewModel{}
    {
    }

    FightingViewComponent::~FightingViewComponent() = default;

    void FightingViewComponent::updateView( GameObjectManager& manager, FightingGameModeComponent& director )
    {
        dispatchInput( manager, director );
        updateBody( manager, director, FightingGameModeComponent::kPlayer, _playerBody );
        updateBody( manager, director, FightingGameModeComponent::kOpponent, _opponentBody );
        updateHUD( director );
    }

    void FightingViewComponent::submitInput( const GameModeInput& input, FightingGameModeComponent& director ) const
    {
        uint16 buttons = 0;
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

#include "FightingViewComponent.cpp.gen.inl"
