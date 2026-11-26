/**
 * @file AbilityDevTools.cpp
 * @brief `Ability` 의 개발 명령입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Character/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Ability/Ability.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityTask.h"

namespace sw
{
    namespace
    {
        struct AbilityDevCommandsInternalDev
        {
            /** @brief 활성 씬의 어빌리티 시스템을 모읍니다. 씬이 없거나 하나도 없으면 그 까닭(문장이 명령 답이다)입니다. */
            [[nodiscard]] static expected<vector<AbilitySystemComponent*>> collectAbilitySystems()
            {
                SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                GameObjectManager*  pManager      = pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
                if ( pManager == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "cheat: no active scene" } );
                vector<AbilitySystemComponent*> listComponent;
                for ( AbilitySystemComponent* pComponent : pManager->getComponentRegistry().getAll<AbilitySystemComponent>() )
                {
                    listComponent.push_back( pComponent );
                }
                if ( listComponent.empty() )
                    return unexpected( Error{ ErrorKind::NotFound, "cheat: no AbilitySystemComponent in the active scene" } );
                return listComponent;
            }

            /** @brief `cheat.heal` — 체력 어트리뷰트가 있는 어빌리티 시스템마다 체력을 최대 체력으로 채웁니다. */
            static bool runHeal( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                expected<vector<AbilitySystemComponent*>> collected = collectAbilitySystems();
                if ( collected.has_value() == false )
                {
                    outReply = string( collected.error().message() );
                    return true;
                }
                const vector<AbilitySystemComponent*> listComponent = std::move( *collected );
                int32                                 healedCount   = 0;
                for ( AbilitySystemComponent* pComponent : listComponent )
                {
                    const hashed_string&    health    = pComponent->getHealthAttribute();
                    const optional<float32> maxHealth = pComponent->getAttributeValue( pComponent->getMaxHealthAttribute() );
                    if ( pComponent->hasAttribute( health ) == false || maxHealth.has_value() == false )
                        continue;
                    if ( pComponent->setAttributeBaseValue( health, *maxHealth ) )
                        ++healedCount;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "cheat.heal healed %# of %# ability system(s)", healedCount, listComponent.size() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `cheat.attribute <name> <value>` — 그 어트리뷰트가 있는 어빌리티 시스템마다 base 값을 정합니다(세트의 클램프를 거친다). */
            static bool runAttribute( const vector<string>& listArgument, string& outReply )
            {
                float32 value = 0.0f;
                if ( listArgument.size() != 2 || StringUtil::parseFloat( listArgument[1], value ) == false )
                    return false;
                expected<vector<AbilitySystemComponent*>> collected = collectAbilitySystems();
                if ( collected.has_value() == false )
                {
                    outReply = string( collected.error().message() );
                    return true;
                }
                const vector<AbilitySystemComponent*> listComponent = std::move( *collected );
                const hashed_string                   name( listArgument[0] );
                int32                                 changedCount = 0;
                for ( AbilitySystemComponent* pComponent : listComponent )
                {
                    if ( pComponent->setAttributeBaseValue( name, value ) )
                        ++changedCount;
                }
                if ( changedCount == 0 )
                {
                    StringBuilder<constant::buffer::kMax256> failBuilder;
                    failBuilder.appendFormat( "cheat.attribute: no ability system has the attribute '%#'", listArgument[0] );
                    outReply = string( failBuilder.view() );
                    return true;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "cheat.attribute set %# on %# ability system(s)", listArgument[0], changedCount );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief @p pComponent 의 주인이 플레이어 조종자(`PlayerControllerComponent`)가 쥔 폰이면 true 입니다. */
            static bool isPlayerControlled( const GameObjectManager& manager, const AbilitySystemComponent& component )
            {
                const GameObject*    pOwner = component.getOwner();
                const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
                if ( pPawn == nullptr || pPawn->isPossessed() == false )
                    return false;
                const ControllerComponent* pController = manager.resolveComponent( pPawn->getController() );
                // 종류 거르기는 타입 정보로 한다(`castTo` 는 늘 성공한다).
                return pController != nullptr && pController->getTypeInfo()->isDerivedFrom( PlayerControllerComponent::StaticType() );
            }

            /**
             * @brief `cheat.god [on|off]` — 플레이어가 쥔 폰의 어빌리티 시스템에 무적 태그를 붙이거나 뗍니다. 인자가 없으면 뒤집습니다.
             * @details 무적은 태그(`AbilitySystemComponent::getInvulnerableTag`) 하나이고 이펙트 없이 붙인다 — 체력 base 가 내려가지 않는다.
             *          대상은 플레이어 조종 폰뿐이다(다른 치트처럼 모든 시스템에 걸면 적도 죽지 않는다).
             */
            static bool runGod( const vector<string>& listArgument, string& outReply )
            {
                const bool bExplicit = listArgument.size() == 1;
                bool       bArgumentOn{ false };
                if ( listArgument.size() > 1 || readOptionalBoolArgument( listArgument, 0, bArgumentOn ) == false )
                    return false;
                expected<vector<AbilitySystemComponent*>> collected = collectAbilitySystems();
                if ( collected.has_value() == false )
                {
                    outReply = string( collected.error().message() );
                    return true;
                }
                const vector<AbilitySystemComponent*> listComponent = std::move( *collected );
                SceneManager* const                   pSceneManager = game::findSubsystem<SceneManager>();
                const GameObjectManager*              pManager      = pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
                // 무적 태그의 하위 태그로 붙인다 — 무적 판정(`hasMatchingTag`)은 하위를 함께 보고, 치트의 몫은 이펙트가 준 무적과 따로 센다.
                const TagID tag         = "State.Invulnerable.Cheat"_tag;
                int32       playerCount = 0;
                int32       godCount    = 0;
                for ( AbilitySystemComponent* pComponent : listComponent )
                {
                    if ( isPlayerControlled( *pManager, *pComponent ) == false )
                        continue;
                    ++playerCount;
                    TagCountContainer& tags     = pComponent->getOwnedTagCounts();
                    const bool         bCheatOn = tags.getCount( tag ) > 0;
                    const bool         bWantOn  = bExplicit ? bArgumentOn : bCheatOn == false;
                    if ( bWantOn && bCheatOn == false )
                        tags.updateCount( tag, 1 );
                    else if ( bWantOn == false && bCheatOn )
                        tags.updateCount( tag, -1 );
                    godCount += pComponent->isInvulnerable() ? 1 : 0;
                }
                if ( playerCount == 0 )
                {
                    outReply = "cheat.god: no player-controlled ability system in the active scene";
                    return true;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "cheat.god invulnerable %# of %# player ability system(s)", godCount, playerCount );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `cheat.resetCooldowns` — 부여된 어빌리티마다 쿨다운 이펙트의 부여 태그와 일치하는 이펙트를 지웁니다. */
            static bool runResetCooldowns( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                expected<vector<AbilitySystemComponent*>> collected = collectAbilitySystems();
                if ( collected.has_value() == false )
                {
                    outReply = string( collected.error().message() );
                    return true;
                }
                const vector<AbilitySystemComponent*> listComponent = std::move( *collected );
                int32                                 removedCount  = 0;
                vector<AbilitySpecHandle>             listHandle;
                for ( AbilitySystemComponent* pComponent : listComponent )
                {
                    TagContainer cooldownTags;
                    listHandle.clear();
                    pComponent->getAbilitySpecHandles( listHandle );
                    for ( const AbilitySpecHandle& handle : listHandle )
                    {
                        const Ability* pAbility = pComponent->findAbility( handle );
                        if ( pAbility == nullptr || pAbility->getConfig()._pCooldownEffect == nullptr )
                            continue;
                        for ( const TagID tag : pAbility->getConfig()._pCooldownEffect->_grantedTags.getTags() )
                        {
                            cooldownTags.addTag( tag );
                        }
                    }
                    if ( cooldownTags.getTagCount() > 0 )
                        removedCount += pComponent->removeActiveEffectsWithTags( cooldownTags );
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "cheat.resetCooldowns removed %# cooldown effect(s)", removedCount );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    // 등록 줄은 기능 파일로 옮길 때(165e16647c) 빠졌다 — 함수만 남아 명령이 없었다.
    SW_DEV_COMMAND( CheatHeal, "cheat.heal", "cheat.heal", "Fill health to max health on every ability system in the active scene", &AbilityDevCommandsInternalDev::runHeal );
    SW_DEV_COMMAND( CheatAttribute, "cheat.attribute", "cheat.attribute <name> <value>", "Set an attribute's base value on every ability system in the active scene that has it",
                    &AbilityDevCommandsInternalDev::runAttribute );
    SW_DEV_COMMAND( CheatGod, "cheat.god", "cheat.god [on|off]", "Toggle invulnerability (health never goes down) on every player-controlled ability system",
                    &AbilityDevCommandsInternalDev::runGod );
    SW_DEV_COMMAND( CheatResetCooldowns, "cheat.resetCooldowns", "cheat.resetCooldowns", "Remove the cooldown effects of every granted ability in the active scene",
                    &AbilityDevCommandsInternalDev::runResetCooldowns );
} // namespace sw
