#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/Client/AsymmetricHorrorAvatarComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/Client/AsymmetricHorrorHUDViewModel.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/Component/AsymmetricHorrorGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    namespace
    {
        /** @brief 표식의 색 · 크기(m)입니다. */
        struct AsymmetricHorrorAvatarComponentInternal
        {
            static constexpr float4 kIdleGeneratorColor = float4( 0.55f, 0.35f, 0.15f, 1.0f );
            static constexpr float4 kDoneGeneratorColor = float4( 1.0f, 0.85f, 0.35f, 1.0f );
            static constexpr float4 kClosedGateColor    = float4( 0.3f, 0.3f, 0.35f, 1.0f );
            static constexpr float4 kPoweredGateColor   = float4( 0.85f, 0.3f, 0.2f, 1.0f );
            static constexpr float4 kOpenGateColor      = float4( 0.3f, 0.9f, 0.4f, 1.0f );
            static constexpr float4 kHookColor          = float4( 0.6f, 0.1f, 0.1f, 1.0f );
            static constexpr float4 kPropColor          = float4( 0.4f, 0.3f, 0.2f, 1.0f ); ///< 판자 · 창틀 · 사물함
            static constexpr float4 kOpenHatchColor     = float4( 0.7f, 0.95f, 1.0f, 1.0f );
            static constexpr float4 kHealthyColor       = float4( 0.2f, 0.6f, 1.0f, 1.0f );
            static constexpr float4 kInjuredColor       = float4( 1.0f, 0.6f, 0.2f, 1.0f );
            static constexpr float4 kDownColor          = float4( 0.9f, 0.15f, 0.15f, 1.0f ); ///< 빈사 · 들림 · 걸림
            static constexpr float4 kKillerColor        = float4( 0.1f, 0.05f, 0.08f, 1.0f );
            static constexpr float4 kAbilityColor       = float4( 0.8f, 0.05f, 0.4f, 1.0f ); ///< 능력을 쓰는 살인마
            static constexpr float3 kGeneratorSize      = float3( 1.2f, 1.0f, 0.8f );
            static constexpr float3 kGateSize           = float3( 2.4f, 2.4f, 0.3f );
            static constexpr float3 kHookSize           = float3( 0.2f, 2.2f, 0.2f );
            static constexpr float3 kPalletSize         = float3( 0.3f, 1.6f, 0.8f );
            static constexpr float3 kDroppedSize        = float3( 1.6f, 0.2f, 0.8f );
            static constexpr float3 kWindowSize         = float3( 1.6f, 0.9f, 0.2f );
            static constexpr float3 kLockerSize         = float3( 0.8f, 1.9f, 0.6f );
            static constexpr float3 kHatchSize          = float3( 1.0f, 0.1f, 1.0f );
            static constexpr float3 kBodySize           = float3( 0.6f, 1.7f, 0.6f );
            static constexpr float3 kDownSize           = float3( 0.6f, 0.4f, 1.4f );
            static constexpr float3 kKillerSize         = float3( 0.8f, 2.1f, 0.8f );
        };
    } // namespace

    AsymmetricHorrorAvatarComponent::AsymmetricHorrorAvatarComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&AsymmetricHorrorAvatarComponent::updateAvatar, &AsymmetricHorrorAvatarComponent::submitInput>( GameModeInputUser::EachLocal ) )
        , _moveAction{ CommonInputActions::kMove }
        , _useAction{}
        , _markerPrefab{}
        , _markers{}
        , _hudViewModel{}
    {
        setCanEverTick( true );
    }

    AsymmetricHorrorAvatarComponent::~AsymmetricHorrorAvatarComponent() = default;

    void AsymmetricHorrorAvatarComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _markers.destroyAll( *pManager );
        GameModeViewComponent::onEndPlay();
    }

    void AsymmetricHorrorAvatarComponent::updateAvatar( GameObjectManager& manager, AsymmetricHorrorGameModeComponent& director )
    {
        GameObject* const pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        dispatchInput( manager, director );
        _markers.begin();
        drawGame( manager, director );
        _markers.end( manager );
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<AsymmetricHorrorHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        if ( IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>() )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        fillHUD( *_hudViewModel, director );
    }

    void AsymmetricHorrorAvatarComponent::submitInput( const GameModeInput& input, AsymmetricHorrorGameModeComponent& director ) const
    {
        const uint32            user = input.getLocalUser(); // 로컬 사용자 n 이 생존자 n 을 잡는다
        AsymmetricHorrorCommand command;
        command._move          = input.getVector2D( _moveAction );
        command._bUseTriggered = input.wasActionTriggered( _useAction ) ? SW_TRUE : SW_FALSE;
        command._bUseHeld      = input.isActionDown( _useAction ) ? SW_TRUE : SW_FALSE;
        (void)director.submitSurvivorInput( static_cast<int32>( user ), command ); // 생존자보다 많은 사용자의 입력은 쓸 곳이 없다
    }

    void AsymmetricHorrorAvatarComponent::drawGame( GameObjectManager& manager, const AsymmetricHorrorGameModeComponent& director )
    {
        using Internal                     = AsymmetricHorrorAvatarComponentInternal;
        using Director                     = AsymmetricHorrorGameModeComponent;
        const AsymmetricHorrorMatch& match = director.getMatch();
        const AsymmetricHorrorStage& stage = director.getStage();
        for ( int32 index = 0; index < match.getGeneratorCount(); ++index )
        {
            const AsymmetricHorrorGenerator& generator = *match.findGenerator( index );
            _markers.showStanding( manager, _markerPrefab, Director::toAbsolute( generator._position ), Internal::kGeneratorSize,
                                   generator._progress.isCompleted() ? Internal::kDoneGeneratorColor : Internal::kIdleGeneratorColor );
        }
        for ( int32 index = 0; index < match.getGateCount(); ++index )
        {
            const AsymmetricHorrorGate& gate   = *match.findGate( index );
            const float4&               closed = match.areGatesPowered() ? Internal::kPoweredGateColor : Internal::kClosedGateColor;
            _markers.showStanding( manager, _markerPrefab, Director::toAbsolute( gate._position ), Internal::kGateSize, gate._progress.isCompleted() ? Internal::kOpenGateColor : closed );
        }
        for ( const float3& hook : stage._listHook )
        {
            _markers.showStanding( manager, _markerPrefab, Director::toAbsolute( hook ), Internal::kHookSize, Internal::kHookColor );
        }
        for ( size_t index = 0; index < stage._listPallet.size(); ++index )
        {
            const PalletState state = match.getPalletState( static_cast<int32>( index ) );
            if ( state != PalletState::Broken )
                _markers.showStanding( manager, _markerPrefab, Director::toAbsolute( stage._listPallet[index] ), state == PalletState::Upright ? Internal::kPalletSize : Internal::kDroppedSize,
                                       Internal::kPropColor );
        }
        for ( const float3& window : stage._listWindow )
        {
            _markers.showStanding( manager, _markerPrefab, Director::toAbsolute( window ), Internal::kWindowSize, Internal::kPropColor );
        }
        for ( const float3& locker : stage._listLocker )
        {
            _markers.showStanding( manager, _markerPrefab, Director::toAbsolute( locker ), Internal::kLockerSize, Internal::kPropColor );
        }
        if ( match.isHatchOpen() )
            _markers.showStanding( manager, _markerPrefab, Director::toAbsolute( stage._hatch ), Internal::kHatchSize, Internal::kOpenHatchColor );
        for ( int32 index = 0; index < match.getSurvivorCount(); ++index )
        {
            const AsymmetricHorrorSurvivor& survivor = *match.findSurvivor( index );
            if ( survivor.isStanding() == false || survivor._activity == SurvivorActivity::InLocker )
                continue;
            const bool    bUpright = survivor._state == SurvivorState::Healthy || survivor._state == SurvivorState::Injured;
            const float4& upright  = survivor._state == SurvivorState::Healthy ? Internal::kHealthyColor : Internal::kInjuredColor;
            _markers.showStanding( manager, _markerPrefab, Director::toAbsolute( survivor._position ), bUpright ? Internal::kBodySize : Internal::kDownSize, bUpright ? upright : Internal::kDownColor );
        }
        _markers.showStanding( manager, _markerPrefab, Director::toAbsolute( match.getKiller()._position ), Internal::kKillerSize,
                               match.getKiller()._abilityRemaining.isActive() ? Internal::kAbilityColor : Internal::kKillerColor );
    }

    void AsymmetricHorrorAvatarComponent::fillHUD( AsymmetricHorrorHUDViewModel& outViewModel, const AsymmetricHorrorGameModeComponent& director )
    {
        constexpr int32                  kPlayer   = 0; // 이 화면의 HUD 는 생존자 0 을 보인다
        const AsymmetricHorrorMatch&     match     = director.getMatch();
        const InteractionProgress* const pProgress = match.findActivityProgress( kPlayer );
        const bool                       bCheck    = pProgress != nullptr && pProgress->hasPendingSkillCheck() && pProgress->getSkillCheckActor() == static_cast<uint32>( kPlayer );
        const AsymmetricHorrorResult     result    = director.getMatchResult();
        outViewModel.setRemainingGenerators( director.countRemainingGenerators() );
        outViewModel.setActivityRatio( pProgress != nullptr ? pProgress->getProgress() : 0.0f );
        outViewModel.setHeartbeat( match.computeHeartbeat( kPlayer ) );
        outViewModel.setSkillCheckShown( bCheck );
        outViewModel.setWinShown( result == AsymmetricHorrorResult::Win );
        outViewModel.setDrawShown( result == AsymmetricHorrorResult::Draw );
        outViewModel.setLoseShown( result == AsymmetricHorrorResult::Lose );
    }
} // namespace sw
