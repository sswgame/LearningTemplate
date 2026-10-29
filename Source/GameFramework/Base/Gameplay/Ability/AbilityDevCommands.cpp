/**
 * @file AbilityDevCommands.cpp
 * @brief 어빌리티 시스템의 치트 개발 명령(`cheat.heal` · `cheat.attribute` · `cheat.god` · `cheat.resetCooldowns`)입니다. Shipping 에는 없습니다.
 * @details 대상은 활성 씬의 모든 `AbilityComponent` 입니다(플레이어를 따로 가리지 않는다). `cheat.god` 만 플레이어 조종 폰으로 거른다. 명령은 게임 스레드에서 돈다.
 *          순회 콜백 안(매니저 잠금 안)에서는 어트리뷰트를 바꾸지 않는다 — 알림이 오브젝트를 건드릴 수 있어서 포인터를 모은 뒤 바꾼다.
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Base/Character/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Base/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/Ability/Ability.h"
#include "GameFramework/Base/Gameplay/Ability/AbilityComponent.h"

namespace sw
{
    namespace
    {
        struct AbilityDevCommandsInternal
        {
            /** @brief 활성 씬의 어빌리티 시스템을 모읍니다. 씬이 없거나 하나도 없으면 답을 채우고 false 입니다. */
            [[nodiscard]] static bool collectAbilitySystems( vector<AbilityComponent*>& outListComponent, string& outReply )
            {
                SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                GameObjectManager*  pManager      = pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
                if ( pManager == nullptr )
                {
                    outReply = "cheat: no active scene";
                    return false;
                }
                pManager->forEachComponentOfType<AbilityComponent>( [&outListComponent]( AbilityComponent* pComponent )
                { outListComponent.push_back( pComponent ); } );
                if ( outListComponent.empty() )
                {
                    outReply = "cheat: no AbilityComponent in the active scene";
                    return false;
                }
                return true;
            }

            /** @brief `cheat.heal` — 체력 어트리뷰트가 있는 어빌리티 시스템마다 체력을 최대 체력으로 채웁니다. */
            static bool runHeal( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                vector<AbilityComponent*> listComponent;
                if ( collectAbilitySystems( listComponent, outReply ) == false )
                    return true;
                int32 healedCount = 0;
                for ( AbilityComponent* pComponent : listComponent )
                {
                    const hashed_string&         health    = pComponent->getHealthAttribute();
                    const std::optional<float32> maxHealth = pComponent->getAttributeValue( pComponent->getMaxHealthAttribute() );
                    if ( pComponent->hasAttribute( health ) == false || maxHealth.has_value() == false )
                        continue;
                    if ( pComponent->setAttributeBaseValue( health, *maxHealth ) )
                        ++healedCount;
                }
                outReply = formatToString( "cheat.heal healed %# of %# ability system(s)", healedCount, listComponent.size() );
                return true;
            }

            /** @brief `cheat.attribute <name> <value>` — 그 어트리뷰트가 있는 어빌리티 시스템마다 base 값을 정합니다(세트의 클램프를 거친다). */
            static bool runAttribute( const vector<string>& listArgument, string& outReply )
            {
                float32 value = 0.0f;
                if ( listArgument.size() != 2 || StringUtil::parseFloat( listArgument[1], value ) == false )
                    return false;
                vector<AbilityComponent*> listComponent;
                if ( collectAbilitySystems( listComponent, outReply ) == false )
                    return true;
                const hashed_string name( listArgument[0] );
                int32               changedCount = 0;
                for ( AbilityComponent* pComponent : listComponent )
                {
                    if ( pComponent->setAttributeBaseValue( name, value ) )
                        ++changedCount;
                }
                if ( changedCount == 0 )
                {
                    outReply = "cheat.attribute: no ability system has the attribute '" + listArgument[0] + "'";
                    return true;
                }
                outReply = formatToString( "cheat.attribute set %# on %# ability system(s)", listArgument[0], changedCount );
                return true;
            }

            /** @brief @p pComponent 의 주인이 플레이어 조종자(`PlayerControllerComponent`)가 쥔 폰이면 true 입니다. */
            static bool isPlayerControlled( const GameObjectManager& manager, const AbilityComponent& component )
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
             * @details 무적은 태그(`AbilityComponent::getInvulnerableTag`) 하나이고 이펙트 없이 붙인다 — 체력 base 가 내려가지 않는다.
             *          대상은 플레이어 조종 폰뿐이다(다른 치트처럼 모든 시스템에 걸면 적도 죽지 않는다).
             */
            static bool runGod( const vector<string>& listArgument, string& outReply )
            {
                const bool bExplicit = listArgument.size() == 1;
                bool       bArgumentOn{ false };
                if ( listArgument.size() > 1 || readOptionalBoolArgument( listArgument, 0, bArgumentOn ) == false )
                    return false;
                vector<AbilityComponent*> listComponent;
                if ( collectAbilitySystems( listComponent, outReply ) == false )
                    return true;
                SceneManager* const      pSceneManager = game::findSubsystem<SceneManager>();
                const GameObjectManager* pManager      = pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
                // 무적 태그의 하위 태그로 붙인다 — 무적 판정(`hasMatchingTag`)은 하위를 함께 보고, 치트의 몫은 이펙트가 준 무적과 따로 센다.
                const TagID tag         = "State.Invulnerable.Cheat"_tag;
                int32       playerCount = 0;
                int32       godCount    = 0;
                for ( AbilityComponent* pComponent : listComponent )
                {
                    if ( isPlayerControlled( *pManager, *pComponent ) == false )
                        continue;
                    ++playerCount;
                    TagCountTable& tags     = pComponent->getOwnedTagCounts();
                    const bool     bCheatOn = tags.getCount( tag ) > 0;
                    const bool     bWantOn  = bExplicit ? bArgumentOn : bCheatOn == false;
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
                outReply = formatToString( "cheat.god invulnerable %# of %# player ability system(s)", godCount, playerCount );
                return true;
            }

            /** @brief `cheat.resetCooldowns` — 부여된 어빌리티마다 쿨다운 이펙트의 부여 태그와 일치하는 이펙트를 지웁니다. */
            static bool runResetCooldowns( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                vector<AbilityComponent*> listComponent;
                if ( collectAbilitySystems( listComponent, outReply ) == false )
                    return true;
                int32                     removedCount = 0;
                vector<AbilitySpecHandle> listHandle;
                for ( AbilityComponent* pComponent : listComponent )
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
                outReply = formatToString( "cheat.resetCooldowns removed %# cooldown effect(s)", removedCount );
                return true;
            }
        };
    } // namespace
} // namespace sw
