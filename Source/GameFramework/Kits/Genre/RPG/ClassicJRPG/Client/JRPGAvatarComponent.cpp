#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/Client/JRPGBattleHUDViewModel.h"
#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/Component/JRPGGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    class GameInputView;
    class GameObjectManager;
    class JRPGGameModeComponent;
    class JRPGBattleHUDViewModel;

    /**
     * @class JRPGAvatarComponent
     * @brief 걷는 동안은 `Move` · 쉬기, 전투 중에는 적 고르기 · 공격 · 주문 · 방어 · 도망을 디렉터 명령으로 넘기고 판을 그립니다.
     */
    REFLECT( Category = "ClassicJRPG", DisplayName = "JRPG Avatar",
             Tooltip = "Client side of a ClassicJRPG trail: sends commands to the JRPG Game Mode on the same object, draws the trail and the enemies and fills the HUD" )
    class JRPGAvatarComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        JRPGAvatarComponent();
        virtual ~JRPGAvatarComponent() override;

        void onBeginPlay() override;
        /** @brief 세운 모습을 지웁니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 HUD · 그리기를 합니다. */
        void updateAvatar( GameObjectManager& manager, JRPGGameModeComponent& director );
        void submitInput( const GameModeInput& input, JRPGGameModeComponent& director );
        void submitExploreInput( JRPGGameModeComponent& director, const GameInputView& input );
        void submitBattleInput( JRPGGameModeComponent& director, const GameInputView& input );
        /** @brief 고른 적을 살아 있는 적으로 맞추고, `Move` 축을 한 번 기울였으면 다음 산 적으로 옮깁니다. */
        void updateTarget( const JRPGGameModeComponent& director, float32 axis );
        /** @brief 명령 멤버가 지금 쓸 수 있는 첫 주문을 명령으로 냅니다(피해는 고른 적, 회복은 HP 가 가장 낮은 멤버, 부활은 쓰러진 멤버). 쓸 주문이 없으면 아무 일도 없습니다. */
        void castFirstSpell( JRPGGameModeComponent& director, int32 memberIndex ) const;
        void updateHUD( const JRPGGameModeComponent& director );
        /** @brief 길 · 파티 · 적을 그립니다. */
        void draw( const JRPGGameModeComponent& director, GameObjectManager& manager );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that walks the party and cycles the battle target (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Attack Action", Tooltip = "InputMap action that attacks the target (required)" )
        hashed_string _attackAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Spell Action", Tooltip = "InputMap action that casts the first usable spell (required)" )
        hashed_string _spellAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Defend Action", Tooltip = "InputMap action that defends this round (required)" )
        hashed_string _defendAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Flee Action", Tooltip = "InputMap action that flees the battle (required)" )
        hashed_string _fleeAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Rest Action", Tooltip = "InputMap action that rests at the inn (required)" )
        hashed_string _restAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Equip Action", Tooltip = "InputMap action that wears the next gear from the bag (required)" )
        hashed_string _equipAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Class Change Action", Tooltip = "InputMap action that changes the class of a member at the inn (required)" )
        hashed_string _classChangeAction;
        PROPERTY( Category = "Input", DisplayName = "Class Change Member", Tooltip = "Party member whose class the action changes", Min = 0 )
        int32 _classChangeMember;

        PROPERTY( Required, Category = "View", DisplayName = "Body Prefab", Tooltip = "Prefab with one mesh, feet at its origin and one metre tall, spawned for every body the view shows" )
        AssetRef<PrefabAsset> _bodyPrefab;
        PROPERTY( Required, Category = "View", DisplayName = "Marker Prefab", Tooltip = "One-metre cube prefab, spawned for every marker the view shows" )
        AssetRef<PrefabAsset> _markerPrefab;

        SW_TRANSIENT PrefabViewBatch _bodies;
        SW_TRANSIENT PrefabViewBatch _markers;
        SW_TRANSIENT unique_ptr<JRPGBattleHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다)
        SW_TRANSIENT int32                              _target;       ///< 고른 적
        SW_TRANSIENT int32                              _lastStep;     ///< 지난 프레임의 축 방향(-1 · 0 · 1) — 놓아야 다음 적을 고른다
    };

    namespace
    {
        /** @brief 모습의 색 · 크기(m)와 입력 문턱입니다. */
        struct JRPGAvatarComponentInternal
        {
            static constexpr float4  kPartyColor      = float4( 0.2f, 0.6f, 1.0f, 1.0f );
            static constexpr float4  kFallenColor     = float4( 0.35f, 0.35f, 0.4f, 1.0f );
            static constexpr float4  kEnemyColor      = float4( 1.0f, 0.3f, 0.2f, 1.0f );
            static constexpr float4  kTargetColor     = float4( 1.0f, 0.85f, 0.2f, 1.0f );
            static constexpr float4  kGoalColor       = float4( 0.75f, 0.2f, 0.75f, 1.0f );
            static constexpr float4  kRestColor       = float4( 0.95f, 0.6f, 0.15f, 1.0f );
            static constexpr float3  kGoalSize        = float3( 1.0f, 1.6f, 1.0f );
            static constexpr float3  kRestSize        = float3( 1.2f, 0.3f, 1.2f );
            static constexpr float3  kBodyScale       = float3( 1.2f, 1.2f, 1.2f );
            static constexpr float32 kRowSpacing      = 1.0f; ///< 한 줄에 선 몸 사이
            static constexpr float32 kBattleDistance  = 3.0f; ///< 전투 중 파티와 적 줄 사이
            static constexpr float32 kTargetThreshold = 0.5f; ///< 축이 이보다 기울어야 고른 적을 옮긴다(스틱 떨림을 고르기로 읽지 않게)

            /** @brief 줄 가운데가 0 이 되는 @p index 번째의 어긋남입니다. */
            static float32 computeRowOffset( size_t index, size_t count )
            {
                return ( static_cast<float32>( index ) - static_cast<float32>( count - 1 ) * 0.5f ) * kRowSpacing;
            }

            /** @brief 축 값의 방향(-1 · 0 · 1)입니다. */
            static int32 computeStep( float32 axis )
            {
                if ( axis > kTargetThreshold )
                    return 1;
                return axis < -kTargetThreshold ? -1 : 0;
            }

            /** @brief HP 가 가장 낮은 산 멤버입니다. */
            static int32 findWeakestMember( const JRPGParty& party )
            {
                int32 weakest = 0;
                for ( int32 memberIndex = 1; memberIndex < party.getMemberCount(); ++memberIndex )
                {
                    const JRPGMember& member  = party.getMember( memberIndex );
                    const JRPGMember& current = party.getMember( weakest );
                    if ( member.isAlive() && ( current.isAlive() == false || member._hp < current._hp ) )
                        weakest = memberIndex;
                }
                return weakest;
            }

            /** @brief 쓰러진 첫 멤버입니다. 없으면 비어 있습니다. */
            static optional<int32> findFallenMember( const JRPGParty& party )
            {
                for ( int32 memberIndex = 0; memberIndex < party.getMemberCount(); ++memberIndex )
                {
                    if ( party.getMember( memberIndex ).isAlive() == false )
                        return memberIndex;
                }
                return nullopt;
            }
        };
    } // namespace

    JRPGAvatarComponent::JRPGAvatarComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&JRPGAvatarComponent::updateAvatar, &JRPGAvatarComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _attackAction{}
        , _spellAction{}
        , _defendAction{}
        , _fleeAction{}
        , _restAction{}
        , _equipAction{}
        , _classChangeAction{}
        , _classChangeMember{ 0 }
        , _bodyPrefab{}
        , _markerPrefab{}
        , _bodies{}
        , _markers{}
        , _hudViewModel{}
        , _target{ 0 }
        , _lastStep{ 0 }
    {
    }

    JRPGAvatarComponent::~JRPGAvatarComponent() = default;

    void JRPGAvatarComponent::onBeginPlay()
    {
        GameModeViewComponent::onBeginPlay();
        _target   = 0;
        _lastStep = 0;
    }

    void JRPGAvatarComponent::onEndPlay()
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

    void JRPGAvatarComponent::updateAvatar( GameObjectManager& manager, JRPGGameModeComponent& director )
    {
        dispatchInput( manager, director );
        updateHUD( director );
        _bodies.begin();
        _markers.begin();
        draw( director, manager );
        _bodies.end( manager );
        _markers.end( manager );
    }

    void JRPGAvatarComponent::submitInput( const GameModeInput& input, JRPGGameModeComponent& director )
    {
        if ( director.getGamePhase() == JRPGGamePhase::Explore )
            submitExploreInput( director, input );
        else if ( director.getGamePhase() == JRPGGamePhase::Battle )
            submitBattleInput( director, input );
        else if ( director.getGamePhase() == JRPGGamePhase::Timing && input.wasActionTriggered( _attackAction ) )
            (void)director.submitTiming(); // 타이밍 단계인 것은 위에서 보았다
    }

    void JRPGAvatarComponent::submitExploreInput( JRPGGameModeComponent& director, const GameInputView& input )
    {
        // 쉼터 밖 · 모자란 돈 · 낄 것 없음 · 전직 조건은 디렉터가 거절한다 — 거절은 할 일이 없다는 뜻이라 버린다.
        const float2 move = input.getVector2D( _moveAction );
        _lastStep         = JRPGAvatarComponentInternal::computeStep( move._x ); // 걷던 축을 누른 채 전투가 열려도 적을 고른 것으로 읽지 않는다
        (void)director.submitMove( move );
        if ( input.wasActionTriggered( _restAction ) )
            (void)director.submitRest();
        if ( input.wasActionTriggered( _equipAction ) )
            (void)director.submitEquip();
        if ( input.wasActionTriggered( _classChangeAction ) )
            (void)director.submitClassChange( _classChangeMember );
    }

    void JRPGAvatarComponent::updateTarget( const JRPGGameModeComponent& director, float32 axis )
    {
        const int32 step                        = JRPGAvatarComponentInternal::computeStep( axis );
        const int32 freshStep                   = step != _lastStep ? step : 0;
        _lastStep                               = step;
        const vector<JRPGEnemyState>& listEnemy = director.getBattle().getEnemies();
        const int32                   count     = static_cast<int32>( listEnemy.size() );
        const int32                   direction = freshStep != 0 ? freshStep : 1;
        int32                         target    = freshStep != 0 ? _target + freshStep : _target;
        for ( int32 visited = 0; visited < count; ++visited )
        {
            const int32 candidate = ( ( target + visited * direction ) % count + count ) % count;
            if ( listEnemy[static_cast<size_t>( candidate )].isAlive() == false )
                continue;
            _target = candidate;
            return;
        }
    }

    void JRPGAvatarComponent::castFirstSpell( JRPGGameModeComponent& director, int32 memberIndex ) const
    {
        const JRPGCatalog* const pCatalog = director.findCatalog();
        if ( pCatalog == nullptr )
            return;
        for ( const hashed_string& spellID : director.getParty().getMember( memberIndex )._listSpell )
        {
            const JRPGSpellDef* const pSpell = pCatalog->findSpell( spellID );
            if ( pSpell == nullptr )
                continue;
            optional<int32> target = _target;
            if ( pSpell->_kind == JRPGSpellKind::Heal )
                target = JRPGAvatarComponentInternal::findWeakestMember( director.getParty() );
            else if ( pSpell->_kind == JRPGSpellKind::Revive )
                target = JRPGAvatarComponentInternal::findFallenMember( director.getParty() );
            if ( target.has_value() && director.submitCommand( JRPGCommand::makeSpell( spellID, *target ) ) )
                return;
        }
    }

    void JRPGAvatarComponent::submitBattleInput( JRPGGameModeComponent& director, const GameInputView& input )
    {
        updateTarget( director, input.getVector2D( _moveAction )._x );
        const optional<int32> member = director.findCommandMember();
        if ( member.has_value() == false )
            return;
        // 쓸 수 없는 명령(MP 부족 · 보스전 도망)은 디렉터가 거절하고 판은 그대로다.
        if ( input.wasActionTriggered( _attackAction ) )
            (void)director.submitCommand( JRPGCommand::makeAttack( _target ) );
        else if ( input.wasActionTriggered( _spellAction ) )
            castFirstSpell( director, *member );
        else if ( input.wasActionTriggered( _defendAction ) )
            (void)director.submitCommand( JRPGCommand::makeDefend() );
        else if ( input.wasActionTriggered( _fleeAction ) )
            (void)director.submitFlee();
    }

    void JRPGAvatarComponent::updateHUD( const JRPGGameModeComponent& director )
    {
        GameObject* const        pOwner   = getOwner();
        const JRPGCatalog* const pCatalog = director.findCatalog();
        if ( pOwner == nullptr || pCatalog == nullptr )
            return;
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<JRPGBattleHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        if ( IHUDViewModelBinder* pBinder = game::findSubsystem<IHUDViewModelBinder>() )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        const JRPGParty& party = director.getParty();
        _hudViewModel->setPhase( director.getGamePhase() );
        _hudViewModel->setGold( director.getGold() );
        _hudViewModel->setTiming( director.computeTimingRatio(), director.getTimingStep() == party.getMemberCount() );
        _hudViewModel->setLeaderAttack( director.getLeaderAttack() );
        _hudViewModel->setRestShown( director.getGamePhase() == JRPGGamePhase::Explore && director.isNearRest() );
        const JRPGMember&   member = party.getMember( director.findCommandMember().value_or( 0 ) ); // 열린 판의 파티는 비지 않는다 — 걷는 동안은 맨 앞 멤버를 보인다
        const JRPGClassDef* pClass = pCatalog->findClass( member._classID );
        _hudViewModel->setMember( pClass != nullptr ? string_view{ pClass->_name } : string_view{}, member._hp, member.getStat( JRPGStat::MaxHP ), member._mp );
        const vector<JRPGEnemyState>& listEnemy = director.getBattle().getEnemies();
        if ( static_cast<size_t>( _target ) < listEnemy.size() )
        {
            const JRPGEnemyState& enemy  = listEnemy[static_cast<size_t>( _target )];
            const JRPGEnemyDef*   pEnemy = pCatalog->findEnemy( enemy._enemyID );
            _hudViewModel->setTarget( pEnemy != nullptr ? string_view{ pEnemy->_name } : string_view{}, enemy._hp );
        }
    }

    void JRPGAvatarComponent::draw( const JRPGGameModeComponent& director, GameObjectManager& manager )
    {
        using Internal = JRPGAvatarComponentInternal;
        _markers.showStanding( manager, _markerPrefab, director.getGoalPosition(), Internal::kGoalSize, Internal::kGoalColor );
        _markers.showStanding( manager, _markerPrefab, director.getRestPosition(), Internal::kRestSize, Internal::kRestColor );
        const AbsolutePosition player      = director.getPlayerPosition();
        const JRPGParty&       party       = director.getParty();
        const size_t           memberCount = static_cast<size_t>( party.getMemberCount() );
        for ( size_t memberIndex = 0; memberIndex < memberCount; ++memberIndex )
        {
            const bool   bAlive = party.getMember( static_cast<int32>( memberIndex ) ).isAlive();
            const float3 offset( 0.0f, 0.0f, Internal::computeRowOffset( memberIndex, memberCount ) );
            (void)_bodies.show( manager, _bodyPrefab, player + offset, Internal::kBodyScale, 0.0f, bAlive ? Internal::kPartyColor : Internal::kFallenColor ); // 실패는 배치가 한 번 알린다
        }
        if ( director.isInBattle() == false )
            return;
        const vector<JRPGEnemyState>& listEnemy = director.getBattle().getEnemies();
        for ( size_t enemyIndex = 0; enemyIndex < listEnemy.size(); ++enemyIndex )
        {
            if ( listEnemy[enemyIndex].isAlive() == false )
                continue;
            const float3 offset( Internal::kBattleDistance, 0.0f, Internal::computeRowOffset( enemyIndex, listEnemy.size() ) );
            (void)_bodies.show( manager, _bodyPrefab, player + offset, Internal::kBodyScale, 0.0f, static_cast<int32>( enemyIndex ) == _target ? Internal::kTargetColor : Internal::kEnemyColor );
        }
    }
} // namespace sw

#include "JRPGAvatarComponent.cpp.gen.inl"
