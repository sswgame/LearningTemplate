#include "pch.h"

#include "GameFramework/Runtime/Gameplay/SessionState/PlayerProgressComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Console/DebugHUDSectionFormat.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Character/Control/Controller/ControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/GameplayDebugTarget.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/ComponentStateStore.h"
#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"
#include "GameFramework/Runtime/Gameplay/SessionState/StateSectionTableUtil.h"

namespace sw
{
    namespace
    {
        /** @brief 플레이어 진행을 모든 게임의 상태 스냅샷에 싣습니다(게임마다 올리지 않는다 — 형제 `PlayerStateComponent` 와 같다). */
        const StatefulComponentRegistrar<PlayerProgressComponent> s_stateRegistration{ "PlayerProgressComponent" };

        struct PlayerProgressComponentInternal
        {
            static constexpr uint32 kSectionCount     = 4;
            static constexpr int32  kQuestHUDOrder    = 200; ///< HUD 섹션 순서 — 게임플레이 섹션 무리
            static constexpr int32  kProgressHUDOrder = 210;

            /** @brief 활성 씬의 열린 플레이어 진행(씬에 하나)입니다. 없으면 답을 채우고 nullptr 입니다. */
            static PlayerProgressComponent* findProgress( string& outReply )
            {
                SceneManager* const      pSceneManager = game::findSubsystem<SceneManager>();
                const GameObjectManager* pManager      = pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
                if ( pManager == nullptr )
                {
                    outReply = "cheat: no active scene";
                    return nullptr;
                }
                PlayerProgressComponent* const pFound = PlayerProgressComponent::findOpen( pManager );
                if ( pFound == nullptr )
                    outReply = "cheat: no single open PlayerProgressComponent in the active scene";
                return pFound;
            }

            /** @brief `cheat.give <itemID> [count]` — 플레이어 가방에 아이템을 넣습니다(슬롯 · 무게가 모자라면 일부만). */
            static bool runGive( const vector<string>& listArgument, string& outReply )
            {
                int32      count                  = 1;
                const bool bInvalidCountArguments = listArgument.empty() || listArgument.size() > 2 || ( listArgument.size() == 2 && StringUtil::parseInt( listArgument[1], count ) == false ) || count < 1;
                if ( bInvalidCountArguments )
                    return false;
                PlayerProgressComponent* pProgress = findProgress( outReply );
                if ( pProgress == nullptr )
                    return true;
                Inventory& inventory = pProgress->getInventory();
                if ( inventory.getSlotCount() == 0 )
                {
                    outReply = "cheat.give: the player progress has no inventory (slot count 0)";
                    return true;
                }
                const int32                              addedCount = inventory.addItem( hashed_string( listArgument[0] ), count );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "cheat.give added %# of %# '%#'", addedCount, count, listArgument[0] );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `cheat.money <amount> [currency]` — 플레이어 지갑에 돈을 더합니다(양수만). 통화를 적지 않으면 기본 통화입니다. */
            static bool runMoney( const vector<string>& listArgument, string& outReply )
            {
                int64      amount                  = 0;
                const bool bInvalidAmountArguments = listArgument.empty() || listArgument.size() > 2 || StringUtil::parseInt64( listArgument[0], amount ) == false || amount <= 0;
                if ( bInvalidAmountArguments )
                    return false;
                PlayerProgressComponent* pProgress = findProgress( outReply );
                if ( pProgress == nullptr )
                    return true;
                const hashed_string currency = listArgument.size() == 2 ? hashed_string( listArgument[1] ) : Wallet::getDefaultCurrency();
                Wallet&             wallet   = pProgress->getWallet();
                wallet.add( currency, amount );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                if ( listArgument.size() == 2 )
                    replyBuilder.appendFormat( "cheat.money balance %# %#", wallet.getBalance( currency ), listArgument[1] );
                else
                    replyBuilder.appendFormat( "cheat.money balance %# (default currency)", wallet.getBalance( currency ) );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief 디버그 섹션의 대상(`debug.target`, 없으면 첫 플레이어 진행)입니다. 열리지 않았으면 그렇게 적고 nullptr 입니다. */
            static const PlayerProgressComponent* findDebugTarget( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* const pManager  = GameplayDebugTarget::findActiveObjectManager();
                const PlayerProgressComponent* pProgress = pManager != nullptr ? GameplayDebugTarget::findTargetOrLowestIDComponent<PlayerProgressComponent>( writer.getSceneIterationKey(), *pManager ) : nullptr;
                if ( pProgress == nullptr || pProgress->isInitialized() == false )
                {
                    writer.addLine( "target", "none (no open player progress in the active scene)" );
                    return nullptr;
                }
                writer.addLine( "target", pProgress->getOwner() != nullptr ? string( pProgress->getOwner()->getName().c_str() ) : string( "?" ) );
                return pProgress;
            }

            /** @brief 진행 중 퀘스트의 단계 · 목표 달성도, 받지 않은 퀘스트의 잠금 사유입니다(레벨 조건은 판정하지 않고 표시만 — 플레이어 진행은 레벨을 모른다). */
            static void writeQuest( DebugHUDSectionWriter& writer )
            {
                if ( const PlayerProgressComponent* pProgress = findDebugTarget( writer ) )
                    pProgress->getQuestLog().writeDebugLines( writer, MathUtil::kMaxInt32, GameplayDebugTarget::kMaxLines );
            }

            static void writeProgress( DebugHUDSectionWriter& writer )
            {
                const PlayerProgressComponent* pProgress = findDebugTarget( writer );
                if ( pProgress == nullptr )
                    return;
                SessionStateComponent* const pWorld = SessionStateComponent::findOpen( GameplayDebugTarget::findActiveObjectManager() );
                pProgress->writeProgressDebugLines( writer, pWorld != nullptr ? &pWorld->getFlags() : nullptr, GameplayDebugTarget::kMaxLines );
            }
        };
    } // namespace

    // 등록 줄은 기능 파일로 옮길 때(165e16647c) 빠졌다 — 함수만 남아 명령이 없었다.
    SW_DEV_COMMAND( CheatGive, "cheat.give", "cheat.give <itemID> [count]", "Add items to the player progress's inventory (partly when slots or weight run out)",
                    &PlayerProgressComponentInternal::runGive );
    SW_DEV_COMMAND( CheatMoney, "cheat.money", "cheat.money <amount> [currency]", "Add money to the player progress's wallet (default currency when none is given)",
                    &PlayerProgressComponentInternal::runMoney );
    SW_DEBUG_HUD_SECTION( quest, "quest", "Quests", PlayerProgressComponentInternal::kQuestHUDOrder, DebugHUDSectionFlag::kNone, &PlayerProgressComponentInternal::writeQuest );
    SW_DEBUG_HUD_SECTION( progress, "progress", "Progress (flags, wallet, bag)", PlayerProgressComponentInternal::kProgressHUDOrder, DebugHUDSectionFlag::kNone, &PlayerProgressComponentInternal::writeProgress );
} // namespace sw

namespace sw
{
    PlayerProgressComponent::PlayerProgressComponent()
        : _wallet{}
        , _inventory{}
        , _questLog{}
        , _reputation{}
        , _pendingStateBytes{}
        , _bInitialized{ SW_FALSE }
        , _bFreshGame{ SW_FALSE }
        , _bOpenAtBeginPlay{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    PlayerProgressComponent::~PlayerProgressComponent() = default;

    SessionStateInitResult PlayerProgressComponent::initialize( const PlayerProgressSettings& settings )
    {
        if ( _bInitialized == SW_TRUE )
            return SessionStateInitResult::AlreadyInitialized;
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "PlayerProgress.RefusedOpenSettings", "PlayerProgress: refused to open - settings '%#'", rangeError.c_str() );
            return SessionStateInitResult::Refused;
        }
        _wallet.clear();
        _questLog.initialize( settings._pQuestCatalog );
        _reputation.initialize( settings._pReputationCatalog );
        if ( _inventory.initialize( settings._pItemCatalog, settings._inventorySlotCount ) == false )
            return SessionStateInitResult::Refused;
        _bInitialized = SW_TRUE;
        _bFreshGame   = SW_TRUE;
        if ( _pendingStateBytes.empty() )
            return SessionStateInitResult::Fresh;
        if ( applyPendingState() == false )
            return SessionStateInitResult::Fresh;
        _bFreshGame = SW_FALSE;
        return SessionStateInitResult::Restored;
    }

    void PlayerProgressComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<PlayerProgressComponent>( this );
    }

    void PlayerProgressComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<PlayerProgressComponent>( this );
        Component::onUnregister( manager );
    }

    void PlayerProgressComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 월드 상태와 같은 오브젝트면 붙은 순서대로 그 뒤에 돈다(디렉터 앞에 두면 예전처럼 디렉터가 식힌 평판을 읽는다). 다른 오브젝트면 다음 그룹에서 읽어 경쟁을 피한다.
        const GameObject* const pOwner = getOwner();
        setTickGroup( pOwner != nullptr && pOwner->getComponent<SessionStateComponent>() != nullptr ? TickGroup::PrePhysics : TickGroup::PostPhysics );
        if ( _bOpenAtBeginPlay == SW_TRUE && initialize( PlayerProgressSettings{} ) == SessionStateInitResult::Refused )
            SW_LOG_ERROR_ID( "PlayerProgress.OpenAtBeginPlayRefused", "PlayerProgress: could not open with default settings at begin play" );
    }

    void PlayerProgressComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _bInitialized == SW_FALSE )
            return;
        const GameObject* const            pOwner = getOwner();
        const SessionStateComponent* const pWorld = pOwner != nullptr ? SessionStateComponent::findOpen( pOwner->getManager() ) : nullptr;
        if ( pWorld == nullptr )
            return; // 월드 시계가 없는 씬(허브)은 날이 넘어가지 않는다
        // 평판은 날이 넘어갈 때 플레이어마다 한 번만 식힌다. 참조해서 쓰는 키트는 식히지 않는다 — 키트 수만큼 반복해서 식지 않게 하기 위해서다.
        for ( const WorldClockEvent& clockEvent : pWorld->getClockEvents() )
        {
            if ( clockEvent._kind == WorldClockEvent::Kind::DayChanged )
                _reputation.advanceDay();
        }
    }

    void PlayerProgressComponent::serializeState( StateIO& io )
    {
        // 구간 표는 여기 한 곳이다. 쓰기는 이 순서로 싣고, 읽기는 표로 찾아 넘긴다.
        const auto forEachSection = [this]( auto&& visit )
        {
            visit( _wallet, "wallet" );
            visit( _questLog, "quest log" );
            visit( _reputation, "reputation" );
            visit( _inventory, "inventory" );
        };
        if ( io.isWriting() )
        {
            Archive& archive = io.getArchive();
            // 열리기 전(복원한 직후의 두 번째 캡처)이면 받은 바이트가 곧 상태이다. 빈 판을 쓰면 세이브를 잃는다.
            if ( _bInitialized == SW_FALSE && _pendingStateBytes.empty() == false )
            {
                archive.writeBytes( _pendingStateBytes.data(), _pendingStateBytes.size() );
                return;
            }
            StateSectionTableUtil::write( archive, kStateTag, kStateVersion, PlayerProgressComponentInternal::kSectionCount, forEachSection );
            return;
        }
        (void)StateSectionTableUtil::read( io, kStateTag, kStateVersion, PlayerProgressComponentInternal::kSectionCount, forEachSection, "the player progress" );
    }

    void PlayerProgressComponent::restoreState( vector<uint8>&& bytes )
    {
        _pendingStateBytes = std::move( bytes );
        if ( _bInitialized == SW_TRUE && applyPendingState() )
            _bFreshGame = SW_FALSE;
    }

    bool PlayerProgressComponent::applyPendingState()
    {
        Archive    archive( _pendingStateBytes.data(), _pendingStateBytes.size() );
        const bool bRestored = readState( archive );
        _pendingStateBytes.clear();
        if ( bRestored == false )
            SW_LOG_ERROR_ID( "PlayerProgress.SavedStateUnread", "PlayerProgress on '%#': the saved player progress could not be read - it starts fresh", getOwner() != nullptr ? getOwner()->getName().c_str() : "?" );
        return bRestored;
    }

    PlayerProgressComponent* PlayerProgressComponent::findOnOwner( const Component& component )
    {
        GameObject* pOwner = component.getOwner();
        return pOwner != nullptr ? pOwner->getComponent<PlayerProgressComponent>() : nullptr;
    }

    PlayerProgressComponent* PlayerProgressComponent::openOnOwner( const Component& component, const PlayerProgressSettings& settings, SessionStateInitResult& outResult )
    {
        outResult                          = SessionStateInitResult::Refused;
        PlayerProgressComponent* pProgress = findOnOwner( component );
        if ( pProgress == nullptr )
        {
            SW_LOG_ERROR_ID( "PlayerProgress.MissingOnOwner", "no PlayerProgressComponent next to the game mode on this object - the game cannot start" );
            return nullptr;
        }
        outResult = pProgress->initialize( settings );
        return outResult != SessionStateInitResult::Refused ? pProgress : nullptr; // 거절한 까닭은 `initialize` 가 남겼다
    }

    PlayerProgressComponent* PlayerProgressComponent::openOnOwner( const Component& component, const PlayerProgressSettings& settings )
    {
        SessionStateInitResult result = SessionStateInitResult::Refused;
        return openOnOwner( component, settings, result );
    }

    PlayerProgressComponent* PlayerProgressComponent::findForPawnObject( GameObjectManager& manager, const GameObject& pawnObject )
    {
        const PawnComponent*       pPawn            = pawnObject.getComponent<PawnComponent>();
        const ControllerComponent* pController      = pPawn != nullptr ? manager.resolveComponent( pPawn->getController() ) : nullptr;
        GameObject*                pControllerOwner = pController != nullptr ? pController->getOwner() : nullptr;
        return pControllerOwner != nullptr ? pControllerOwner->getComponent<PlayerProgressComponent>() : nullptr;
    }

    PlayerProgressComponent* PlayerProgressComponent::findOpen( const GameObjectManager* pManager )
    {
        if ( pManager == nullptr )
            return nullptr;
        PlayerProgressComponent* pFound = nullptr;
        for ( PlayerProgressComponent* pProgress : pManager->getComponentRegistry().getAll<PlayerProgressComponent>() )
        {
            if ( pProgress->isInitialized() == false )
                continue;
            if ( pFound != nullptr )
            {
                SW_LOG_ERROR_ID( "PlayerProgress.SeveralOpen", "PlayerProgress: the scene has more than one open player progress ('%#', '%#') - a single-player reader cannot pick one",
                                 pFound->getOwner() != nullptr ? pFound->getOwner()->getName().c_str() : "?", pProgress->getOwner() != nullptr ? pProgress->getOwner()->getName().c_str() : "?" );
                return nullptr;
            }
            pFound = pProgress;
        }
        return pFound;
    }

    void PlayerProgressComponent::writeProgressDebugLines( DebugHUDSectionWriter& writer, const GameFlags* pWorldFlags, uint32 maxLines ) const
    {
        uint32     lineCount = 0;
        uint32     skipped   = 0;
        const auto add       = [&]( string_view label, string_view value )
        {
            if ( lineCount >= maxLines )
            {
                ++skipped;
                return;
            }
            writer.addLine( label, value );
            ++lineCount;
        };
        if ( pWorldFlags != nullptr )
        {
            vector<GameFlagEntry> listFlag;
            pWorldFlags->fillEntries( listFlag );
            for ( const GameFlagEntry& flag : listFlag )
            {
                fixed_string<constant::buffer::kMax256> label;
                formatstring( label.data(), label.capacity(), "flag %#", flag._name.c_str() );
                fixed_string<constant::buffer::kMax32> value;
                formatstring( value.data(), value.capacity(), "%#", flag._value );
                add( label.view(), value.view() );
            }
        }
        for ( const WalletBalance& balance : _wallet.getBalances() )
        {
            fixed_string<constant::buffer::kMax256> label;
            formatstring( label.data(), label.capacity(), "wallet %#", balance._currency.c_str() );
            fixed_string<constant::buffer::kMax32> value;
            formatstring( value.data(), value.capacity(), "%#", balance._amount );
            add( label.view(), value.view() );
        }
        for ( int32 slot = 0; slot < _inventory.getSlotCount(); ++slot )
        {
            const InventorySlot& content = _inventory.getSlot( slot );
            if ( content.isEmpty() )
                continue;
            fixed_string<constant::buffer::kMax32> label;
            formatstring( label.data(), label.capacity(), "slot %#", slot );
            fixed_string<constant::buffer::kMax256> value;
            formatstring( value.data(), value.capacity(), "%# x%#", content._itemID.c_str(), content._count );
            add( label.view(), value.view() );
        }
        if ( skipped > 0 )
            writer.addLineFormat( "...", "%# more", skipped );
    }
} // namespace sw
