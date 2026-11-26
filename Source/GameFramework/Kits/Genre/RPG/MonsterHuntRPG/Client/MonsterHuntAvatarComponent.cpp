#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Client/MonsterHuntHUDViewModel.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Component/MonsterHuntGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    struct MonsterHuntClueDef;

    class GameInputView;
    class GameObjectManager;
    class MonsterHuntGameModeComponent;
    class MonsterHuntHUDViewModel;

    /**
     * @class MonsterHuntAvatarComponent
     * @brief 흥정 · 걷기 · 조사 · 공격 · 회피 · 표식 · 오일 · 물약 액션을 디렉터 명령으로 넘기고 판을 그립니다.
     */
    REFLECT( Category = "MonsterHuntRPG", DisplayName = "Monster Hunt Avatar",
             Tooltip = "Client side of a MonsterHuntRPG contract: sends commands to the Monster Hunt Game Mode on the same object, draws clues and the monster and fills the HUD" )
    class MonsterHuntAvatarComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        MonsterHuntAvatarComponent();
        virtual ~MonsterHuntAvatarComponent() override;

        /** @brief 세운 모습을 지웁니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 HUD · 그리기를 합니다. */
        void updateAvatar( GameObjectManager& manager, MonsterHuntGameModeComponent& director );
        void submitInput( const GameModeInput& input, MonsterHuntGameModeComponent& director ) const;
        void updateHUD( const MonsterHuntGameModeComponent& director, bool bClueNearby );
        /** @brief 감각 안의 단서(@p listClue) · 사냥꾼 · 괴물을 그립니다. */
        void draw( const MonsterHuntGameModeComponent& director, const vector<const MonsterHuntClueDef*>& listClue, GameObjectManager& manager );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that walks the hunter (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Fast Action", Tooltip = "InputMap action of the fast attack; accepts the offer while haggling (required)" )
        hashed_string _fastAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Strong Action", Tooltip = "InputMap action of the strong attack; asks for more while haggling (required)" )
        hashed_string _strongAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Dodge Action", Tooltip = "InputMap action that dodges (required)" )
        hashed_string _dodgeAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Sign Action", Tooltip = "InputMap action that casts the sign (required)" )
        hashed_string _signAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Oil Action", Tooltip = "InputMap action that oils the blade (required)" )
        hashed_string _oilAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Potion Action", Tooltip = "InputMap action that drinks the potion (required)" )
        hashed_string _potionAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Investigate Action", Tooltip = "InputMap action that investigates the nearest open clue (required)" )
        hashed_string _investigateAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Alternate Sign Action", Tooltip = "InputMap action that casts the alternate sign (required)" )
        hashed_string _alternateSignAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Learn Action", Tooltip = "InputMap action that learns the next skill (required)" )
        hashed_string _learnAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Brew Action", Tooltip = "InputMap action that brews from the bag (required)" )
        hashed_string _brewAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Mutagen Action", Tooltip = "InputMap action that slots the next mutagen (required)" )
        hashed_string _mutagenAction;

        PROPERTY( Required, Category = "View", DisplayName = "Body Prefab", Tooltip = "Prefab with one mesh, feet at its origin and one metre tall, spawned for every body the view shows" )
        AssetRef<PrefabAsset> _bodyPrefab;
        PROPERTY( Required, Category = "View", DisplayName = "Marker Prefab", Tooltip = "One-metre cube prefab, spawned for every marker the view shows" )
        AssetRef<PrefabAsset> _markerPrefab;

        SW_TRANSIENT PrefabViewBatch _bodies;
        SW_TRANSIENT PrefabViewBatch _markers;
        SW_TRANSIENT unique_ptr<MonsterHuntHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다)
    };

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
        : GameModeViewComponent( GameModeViewBinding::of<&MonsterHuntAvatarComponent::updateAvatar, &MonsterHuntAvatarComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
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
        , _bodies{}
        , _markers{}
        , _hudViewModel{}
    {
    }

    MonsterHuntAvatarComponent::~MonsterHuntAvatarComponent() = default;

    void MonsterHuntAvatarComponent::onEndPlay()
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

    void MonsterHuntAvatarComponent::updateAvatar( GameObjectManager& manager, MonsterHuntGameModeComponent& director )
    {
        dispatchInput( manager, director );
        vector<const MonsterHuntClueDef*> listClue; // 카탈로그의 것이라 프레임을 넘겨 들지 않는다
        if ( director.getGamePhase() == MonsterHuntPhase::Investigate )
            director.collectNearbyClues( listClue );
        updateHUD( director, listClue.empty() == false );
        _bodies.begin();
        _markers.begin();
        draw( director, listClue, manager );
        _bodies.end( manager );
        _markers.end( manager );
    }

    void MonsterHuntAvatarComponent::submitInput( const GameModeInput& input, MonsterHuntGameModeComponent& director ) const
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

#include "MonsterHuntAvatarComponent.cpp.gen.inl"
