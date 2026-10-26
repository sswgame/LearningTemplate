#include "pch.h"

#include "GameFramework/Base/Gameplay/Ability/AbilityCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/Ability/CombatAttributeSet.h"

namespace sw
{
    SW_LOG_CALLER( "AbilityCatalog" );

    namespace
    {
        struct AbilityCatalogInternal
        {
            /** @brief `<X tag="A.B"/>` 자식들을 컨테이너에 추가합니다. 태그 속성이 비어 있는 노드는 건너뜁니다. */
            static void readTagChildren( const XMLNode& parent, const utf8* pChildName, TagContainer& outTags )
            {
                for ( XMLNode child = parent.findChild( pChildName ); child; child = child.findNextSibling( pChildName ) )
                {
                    const utf8* pTag = child.findAttribute( "tag" );
                    if ( StringUtil::isNullOrEmpty( pTag ) )
                        continue;
                    outTags.addTag( TagID::request( string_view( pTag ) ) );
                }
            }

            /** @brief 이펙트의 태그 자식과 `<GrantAbility>` 를 읽고 즉시 이펙트에 줄 동안 항목이 없는지 확인합니다. 어긋나면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool readEffectTagsAndGrants( const XMLNode& node, const utf8* pID, string_view sourceName, GameplayEffectDef& outDef )
            {
                readTagChildren( node, "AssetTag", outDef._assetTags );
                readTagChildren( node, "GrantedTag", outDef._grantedTags );
                readTagChildren( node, "RequiredTag", outDef._applicationRequiredTags );
                readTagChildren( node, "BlockedTag", outDef._applicationBlockedTags );
                readTagChildren( node, "RemoveEffectsWithTag", outDef._removeEffectsWithTags );
                readTagChildren( node, "Cue", outDef._cueTags );
                readTagChildren( node, "OngoingRequiredTag", outDef._ongoingRequiredTags );
                for ( XMLNode grantNode = node.findChild( "GrantAbility" ); grantNode; grantNode = grantNode.findNextSibling( "GrantAbility" ) )
                {
                    hashed_string abilityID;
                    if ( XMLReadUtil::readRequiredName( grantNode, "id", sourceName, abilityID ) == false )
                    {
                        SW_LOG_ERROR( "%#: effect '%#' has a <GrantAbility> without an id - skipped", sourceName, pID );
                        return false;
                    }
                    outDef._listGrantedAbility.push_back( abilityID );
                }
                // 즉시 이펙트는 걸려 있지 않으므로 줄 동안이 없다 — 적은 것을 조용히 버리지 않고 오류로 둔다.
                const bool bInstantWithLasting = outDef._durationPolicy == EffectDurationPolicy::Instant &&
                                                 ( outDef._listGrantedAbility.empty() == false || outDef._ongoingRequiredTags.getTagCount() > 0 );
                if ( bInstantWithLasting )
                {
                    SW_LOG_ERROR( "%#: effect '%#' is Instant but has <GrantAbility> or <OngoingRequiredTag> - skipped", sourceName, pID );
                    return false;
                }
                return true;
            }

            /** @brief `<Modifier>` 하나를 읽습니다. 어트리뷰트가 없거나 열거형 이름을 알 수 없으면 경고를 남기고 false 를 반환합니다. */
            [[nodiscard]] static bool readModifier( const XMLNode& node, const utf8* pEffectID, string_view sourceName, GameplayEffectModifier& outModifier )
            {
                if ( XMLReadUtil::readRequiredName( node, "attribute", sourceName, outModifier._attribute ) == false )
                {
                    SW_LOG_WARNING( "%#: effect '%#' has a <Modifier> without an attribute - skipped", sourceName, pEffectID );
                    return false;
                }

                const utf8* pOp = node.findAttribute( "op" );
                if ( pOp != nullptr && game::getSubsystem<TypeRegistry>().enumFromString( string_view( pOp ), outModifier._op ) == false )
                {
                    SW_LOG_WARNING( "%#: effect '%#' has unknown modifier op '%#' - skipped", sourceName, pEffectID, pOp );
                    return false;
                }

                const utf8* pSource = node.findAttribute( "source" );
                if ( pSource != nullptr && game::getSubsystem<TypeRegistry>().enumFromString( string_view( pSource ), outModifier._magnitudeSource ) == false )
                {
                    SW_LOG_WARNING( "%#: effect '%#' has unknown magnitude source '%#' - skipped", sourceName, pEffectID, pSource );
                    return false;
                }

                // 적지 않은 칸은 모디파이어 구조체의 초기값이다. 깨진 값은 그 모디파이어를 넣지 않는 오류다.
                constexpr float32 kAny        = MathUtil::kMaxFloat;
                bool              bValid      = XMLReadUtil::readOptionalNumber( node, "magnitude", sourceName, -kAny, kAny, outModifier._scalableMagnitude._baseValue );
                bValid                        = XMLReadUtil::readOptionalNumber( node, "perLevel", sourceName, -kAny, kAny, outModifier._scalableMagnitude._perLevel ) && bValid;
                bValid                        = XMLReadUtil::readOptionalNumber( node, "coefficient", sourceName, -kAny, kAny, outModifier._coefficient ) && bValid;
                bValid                        = XMLReadUtil::readOptionalNumber( node, "pre", sourceName, -kAny, kAny, outModifier._preMultiplyAdditive ) && bValid;
                bValid                        = XMLReadUtil::readOptionalNumber( node, "post", sourceName, -kAny, kAny, outModifier._postMultiplyAdditive ) && bValid;
                outModifier._backingAttribute = XMLReadUtil::readOptionalName( node, "backing" ); // 아래에서 AttributeBased 일 때 필수로 확인한다
                outModifier._setByCallerName  = XMLReadUtil::readOptionalName( node, "name" );    // 아래에서 SetByCaller 일 때 필수로 확인한다
                if ( bValid == false )
                {
                    SW_LOG_ERROR( "%#: effect '%#' has a <Modifier> with invalid numbers - skipped", sourceName, pEffectID );
                    return false;
                }

                const utf8* pFrom        = node.findAttribute( "from" );
                const bool  bFromTarget  = pFrom != nullptr && StringUtil::equals( pFrom, "Target" );
                outModifier._bFromSource = bFromTarget ? SW_FALSE : SW_TRUE;

                const bool bMissingBacking = outModifier._magnitudeSource == EffectMagnitudeSource::AttributeBased && outModifier._backingAttribute.empty();
                if ( bMissingBacking )
                {
                    SW_LOG_WARNING( "%#: effect '%#' AttributeBased modifier on '%#' has no backing attribute - skipped", sourceName, pEffectID,
                                    outModifier._attribute.c_str() );
                    return false;
                }
                const bool bMissingCallerName = outModifier._magnitudeSource == EffectMagnitudeSource::SetByCaller && outModifier._setByCallerName.empty();
                if ( bMissingCallerName )
                {
                    SW_LOG_WARNING( "%#: effect '%#' SetByCaller modifier on '%#' has no name - skipped", sourceName, pEffectID,
                                    outModifier._attribute.c_str() );
                    return false;
                }
                return true;
            }

            /** @brief 노드의 매개변수로 "Damage" 실행 계산을 만듭니다. 세 매개변수는 필수이고, 빠지거나 깨졌으면 오류를 남기고 nullptr 입니다. */
            static shared_ptr<const IGameplayEffectExecution> createDamageExecution( const XMLNode& node )
            {
                const std::optional<string>  damageName  = node.readRequiredAttribute<string>( "setByCaller" );
                const std::optional<float32> coefficient = node.readRequiredAttribute<float32>( "attackPowerCoefficient" );
                const std::optional<float32> baseDamage  = node.readRequiredAttribute<float32>( "base" );
                const bool                   bComplete   = damageName.has_value() && coefficient.has_value() && baseDamage.has_value();
                if ( bComplete == false )
                    return nullptr;
                shared_ptr<DamageExecution> pExecution = make_shared<DamageExecution>();
                pExecution->setParameters( hashed_string( *damageName ), *coefficient, *baseDamage );
                return pExecution;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AbilityCatalog::AbilityCatalog()
        : _mapEffect{}
        , _mapAbility{}
        , _mapAbilitySet{}
        , _mapCue{}
        , _mapAbilityFactory{}
        , _mapAttributeSetFactory{}
        , _mapExecutionFactory{}
    {
        registerAttributeSetClass<AttributeSet>( "Generic" );
        registerAttributeSetClass<CombatAttributeSet>( "Combat" );
        registerAbilityClass<ApplyEffectsAbility>( "ApplyEffects" );
        registerExecutionClass( "Damage", &AbilityCatalogInternal::createDamageExecution );
    }

    AbilityCatalog::~AbilityCatalog() = default;

    void AbilityCatalog::registerAbilityClass( const hashed_string& className, AbilityFactoryFunc factory )
    {
        if ( className.empty() || factory == nullptr )
            return;
        _mapAbilityFactory.insert_or_assign( className, factory );
    }

    void AbilityCatalog::registerAttributeSetClass( const hashed_string& className, AttributeSetFactoryFunc factory )
    {
        if ( className.empty() || factory == nullptr )
            return;
        _mapAttributeSetFactory.insert_or_assign( className, factory );
    }

    void AbilityCatalog::registerExecutionClass( const hashed_string& className, ExecutionFactoryFunc factory )
    {
        if ( className.empty() || factory == nullptr )
            return;
        _mapExecutionFactory.insert_or_assign( className, factory );
    }

    shared_ptr<const GameplayEffectDef> AbilityCatalog::addEffect( const GameplayEffectDef& def )
    {
        if ( def._id.empty() )
        {
            SW_LOG_WARNING( "addEffect: an effect without an id was ignored" );
            return nullptr;
        }
        shared_ptr<const GameplayEffectDef> pDef = make_shared<GameplayEffectDef>( def );
        _mapEffect.insert_or_assign( def._id, pDef );
        return pDef;
    }

    void AbilityCatalog::addAbility( const GameplayAbilityDef& def )
    {
        if ( def._id.empty() )
        {
            SW_LOG_WARNING( "addAbility: an ability without an id was ignored" );
            return;
        }
        GameplayAbilityDef stored = def;
        stored._config._id        = def._id;
        _mapAbility.insert_or_assign( def._id, std::move( stored ) );
    }

    void AbilityCatalog::addAbilitySet( const AbilitySetDef& def )
    {
        if ( def._id.empty() )
        {
            SW_LOG_WARNING( "addAbilitySet: an ability set without an id was ignored" );
            return;
        }
        _mapAbilitySet.insert_or_assign( def._id, def );
    }

    void AbilityCatalog::addCue( const GameplayCueDef& def )
    {
        if ( def._cueTag.isValid() == false )
        {
            SW_LOG_WARNING( "addCue: a gameplay cue without a tag was ignored" );
            return;
        }
        _mapCue.insert_or_assign( def._cueTag._id, def );
    }

    void AbilityCatalog::clearDefinitions()
    {
        _mapEffect.clear();
        _mapAbility.clear();
        _mapAbilitySet.clear();
        _mapCue.clear();
    }

    const GameplayCueDef* AbilityCatalog::findCue( TagID cueTag ) const
    {
        if ( cueTag.isValid() == false || _mapCue.empty() )
            return nullptr;
        // 같은 태그부터 조상 쪽으로 찾는다("GameplayCue.Hit.Fire", "GameplayCue.Hit", "GameplayCue"). 조상 id 는 intern 하지 않고 해시만 구한다.
        const utf8* pTagText = cueTag.getString();
        if ( pTagText == nullptr )
        {
            const auto mapIter = _mapCue.find( cueTag._id );
            return mapIter != _mapCue.end() ? &mapIter->second : nullptr;
        }
        const size_t tagLength = StringUtil::strlen( pTagText );
        for ( size_t charIndex = tagLength; charIndex > 0; --charIndex )
        {
            const bool bBoundary = charIndex == tagLength || pTagText[charIndex] == '.';
            if ( bBoundary == false )
                continue;
            const auto mapIter = _mapCue.find( TagID::computeID( pTagText, charIndex ) );
            if ( mapIter != _mapCue.end() )
                return &mapIter->second;
        }
        return nullptr;
    }

    shared_ptr<const GameplayEffectDef> AbilityCatalog::findEffect( const hashed_string& effectID ) const
    {
        const auto mapIter = _mapEffect.find( effectID );
        return mapIter != _mapEffect.end() ? mapIter->second : nullptr;
    }

    const GameplayAbilityDef* AbilityCatalog::findAbility( const hashed_string& abilityID ) const
    {
        const auto mapIter = _mapAbility.find( abilityID );
        return mapIter != _mapAbility.end() ? &mapIter->second : nullptr;
    }

    const AbilitySetDef* AbilityCatalog::findAbilitySet( const hashed_string& setID ) const
    {
        const auto mapIter = _mapAbilitySet.find( setID );
        return mapIter != _mapAbilitySet.end() ? &mapIter->second : nullptr;
    }

    unique_ptr<GameplayAbility> AbilityCatalog::createAbility( const hashed_string& abilityID ) const
    {
        const GameplayAbilityDef* pDef = findAbility( abilityID );
        if ( pDef == nullptr )
        {
            SW_LOG_WARNING( "createAbility: no ability '%#' in the catalog", abilityID.c_str() );
            return nullptr;
        }
        const auto factoryIter = _mapAbilityFactory.find( pDef->_className );
        if ( factoryIter == _mapAbilityFactory.end() )
        {
            SW_LOG_WARNING( "createAbility: ability '%#' uses class '%#' which is not registered (registerAbilityClass)", abilityID.c_str(),
                            pDef->_className.c_str() );
            return nullptr;
        }
        unique_ptr<GameplayAbility> pAbility = factoryIter->second();
        if ( pAbility != nullptr )
            pAbility->setConfig( pDef->_config );
        return pAbility;
    }

    unique_ptr<AttributeSet> AbilityCatalog::createAttributeSet( const hashed_string& className ) const
    {
        const auto factoryIter = _mapAttributeSetFactory.find( className );
        if ( factoryIter == _mapAttributeSetFactory.end() )
        {
            SW_LOG_WARNING( "createAttributeSet: attribute set class '%#' is not registered (registerAttributeSetClass)", className.c_str() );
            return nullptr;
        }
        return factoryIter->second();
    }

    bool AbilityCatalog::hasAbilityClass( const hashed_string& className ) const
    {
        return _mapAbilityFactory.find( className ) != _mapAbilityFactory.end();
    }

    uint32 AbilityCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;

        // 이펙트, 어빌리티, 세트 순으로 읽는다. 어빌리티가 비용 이펙트와 쿨다운 이펙트를 참조하므로, 파일 안의 순서와 상관없이 이펙트를 먼저 모두 읽는다.
        for ( XMLNode node = root.findChild( "GameplayEffect" ); node; node = node.findNextSibling( "GameplayEffect" ) )
        {
            GameplayEffectDef def;
            if ( readEffect( node, sourceName, def ) == false )
                continue;
            (void)addEffect( def ); // id 가 있으므로 실패하지 않는다
            ++loadedCount;
        }

        for ( XMLNode node = root.findChild( "Ability" ); node; node = node.findNextSibling( "Ability" ) )
        {
            GameplayAbilityDef def;
            if ( readAbility( node, sourceName, def ) == false )
                continue;
            addAbility( def );
            ++loadedCount;
        }

        for ( XMLNode node = root.findChild( "AbilitySet" ); node; node = node.findNextSibling( "AbilitySet" ) )
        {
            AbilitySetDef def;
            if ( readAbilitySet( node, sourceName, def ) == false )
                continue;
            addAbilitySet( def );
            ++loadedCount;
        }

        for ( XMLNode node = root.findChild( "GameplayCue" ); node; node = node.findNextSibling( "GameplayCue" ) )
        {
            GameplayCueDef def;
            if ( readCue( node, sourceName, def ) == false )
                continue;
            addCue( def );
            ++loadedCount;
        }

        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <GameplayEffect>, <Ability>, <AbilitySet> or <GameplayCue> entries", sourceName );
        return loadedCount;
    }

    bool AbilityCatalog::readEffect( const XMLNode& node, string_view sourceName, GameplayEffectDef& outDef ) const
    {
        const utf8* pID = node.findAttribute( "id" );
        if ( StringUtil::isNullOrEmpty( pID ) )
        {
            SW_LOG_WARNING( "%#: <GameplayEffect> without an id - skipped", sourceName );
            return false;
        }
        outDef._id = hashed_string( pID );

        const utf8* pDuration = node.findAttribute( "duration" );
        if ( pDuration != nullptr && game::getSubsystem<TypeRegistry>().enumFromString( string_view( pDuration ), outDef._durationPolicy ) == false )
        {
            SW_LOG_WARNING( "%#: effect '%#' has unknown duration policy '%#' - skipped", sourceName, pID, pDuration );
            return false;
        }
        // 적지 않은 칸은 GameplayEffectDef 의 초기값이다. 깨진 값 · 범위 밖 값은 그 이펙트를 넣지 않는 오류다(자르지 않는다).
        constexpr float32 kAny   = MathUtil::kMaxFloat;
        bool              bValid = XMLReadUtil::readOptionalNumber( node, "seconds", sourceName, -kAny, kAny, outDef._duration._baseValue );
        bValid                   = XMLReadUtil::readOptionalNumber( node, "secondsPerLevel", sourceName, -kAny, kAny, outDef._duration._perLevel ) && bValid;
        bValid                   = XMLReadUtil::readOptionalNumber( node, "period", sourceName, 0.0f, kAny, outDef._period ) && bValid;
        bValid                   = XMLReadUtil::readOptionalFlag( node, "executeOnApplication", outDef._bExecutePeriodicOnApplication ) && bValid;
        bValid                   = XMLReadUtil::readOptionalFlag( node, "refreshOnStack", outDef._bRefreshDurationOnStack ) && bValid;
        bValid                   = XMLReadUtil::readOptionalNumber( node, "stackLimit", sourceName, 1, MathUtil::kMaxInt32, outDef._stackLimit ) && bValid;
        if ( bValid == false )
        {
            SW_LOG_ERROR( "%#: effect '%#' has invalid fields - skipped", sourceName, pID );
            return false;
        }

        const utf8* pStacking = node.findAttribute( "stacking" );
        if ( pStacking != nullptr && game::getSubsystem<TypeRegistry>().enumFromString( string_view( pStacking ), outDef._stackingPolicy ) == false )
        {
            SW_LOG_WARNING( "%#: effect '%#' has unknown stacking policy '%#' - skipped", sourceName, pID, pStacking );
            return false;
        }
        const utf8* pExpiration = node.findAttribute( "stackExpiration" );
        if ( pExpiration != nullptr && game::getSubsystem<TypeRegistry>().enumFromString( string_view( pExpiration ), outDef._stackExpirationPolicy ) == false )
        {
            SW_LOG_WARNING( "%#: effect '%#' has unknown stack expiration '%#' - skipped", sourceName, pID, pExpiration );
            return false;
        }

        const bool bDurationWithoutLength = outDef._durationPolicy == EffectDurationPolicy::HasDuration && outDef._duration._baseValue <= 0.0f;
        if ( bDurationWithoutLength )
            SW_LOG_WARNING( "%#: effect '%#' is HasDuration but 'seconds' is not positive - it expires on the next update", sourceName, pID );

        for ( XMLNode modifierNode = node.findChild( "Modifier" ); modifierNode; modifierNode = modifierNode.findNextSibling( "Modifier" ) )
        {
            GameplayEffectModifier modifier;
            if ( AbilityCatalogInternal::readModifier( modifierNode, pID, sourceName, modifier ) )
                outDef._listModifier.push_back( modifier );
        }

        for ( XMLNode executionNode = node.findChild( "Execution" ); executionNode; executionNode = executionNode.findNextSibling( "Execution" ) )
        {
            hashed_string className;
            if ( XMLReadUtil::readRequiredName( executionNode, "class", sourceName, className ) == false )
            {
                SW_LOG_ERROR( "%#: effect '%#' has an <Execution> without a class - skipped", sourceName, pID );
                return false;
            }
            const auto factoryIter = _mapExecutionFactory.find( className );
            if ( factoryIter == _mapExecutionFactory.end() )
            {
                SW_LOG_WARNING( "%#: effect '%#' uses execution class '%#' which is not registered - skipped", sourceName, pID, className.c_str() );
                continue;
            }
            shared_ptr<const IGameplayEffectExecution> pExecution = factoryIter->second( executionNode );
            if ( pExecution == nullptr )
            {
                SW_LOG_ERROR( "%#: effect '%#' has an <Execution class=\"%#\"> with invalid parameters - skipped", sourceName, pID, className.c_str() );
                return false;
            }
            outDef._listExecution.push_back( std::move( pExecution ) );
        }

        return AbilityCatalogInternal::readEffectTagsAndGrants( node, pID, sourceName, outDef );
    }

    bool AbilityCatalog::readCue( const XMLNode& node, string_view sourceName, GameplayCueDef& outDef )
    {
        const utf8* pTag = node.findAttribute( "tag" );
        if ( StringUtil::isNullOrEmpty( pTag ) )
        {
            SW_LOG_ERROR( "%#: <GameplayCue> without a tag - skipped", sourceName );
            return false;
        }
        outDef._cueTag     = TagID::request( string_view( pTag ) );
        outDef._prefab     = XMLReadUtil::readOptionalName( node, "prefab" ); // 둘 중 하나는 아래에서 필수로 확인한다
        outDef._soundEvent = XMLReadUtil::readOptionalName( node, "sound" );
        bool bValid        = XMLReadUtil::readOptionalFloat3( node, "offset", sourceName, outDef._offset );
        bValid             = XMLReadUtil::readOptionalFlag( node, "onRemoved", outDef._bOnRemoved ) && bValid;
        bValid             = XMLReadUtil::readOptionalFlag( node, "positional", outDef._bPositional ) && bValid;
        if ( bValid == false )
        {
            SW_LOG_ERROR( "%#: gameplay cue '%#' has invalid fields - skipped", sourceName, pTag );
            return false;
        }
        // 보여 줄 것이 없는 정의는 적은 사람의 실수다 — 조용히 아무것도 하지 않는 큐로 두지 않는다.
        if ( outDef._prefab.empty() && outDef._soundEvent.empty() )
        {
            SW_LOG_ERROR( "%#: gameplay cue '%#' has neither prefab nor sound - skipped", sourceName, pTag );
            return false;
        }
        return true;
    }

    bool AbilityCatalog::readAbility( const XMLNode& node, string_view sourceName, GameplayAbilityDef& outDef ) const
    {
        const utf8* pID = node.findAttribute( "id" );
        if ( StringUtil::isNullOrEmpty( pID ) )
        {
            SW_LOG_WARNING( "%#: <Ability> without an id - skipped", sourceName );
            return false;
        }
        outDef._id = hashed_string( pID );
        if ( XMLReadUtil::readRequiredName( node, "class", sourceName, outDef._className ) == false )
        {
            SW_LOG_WARNING( "%#: ability '%#' has no class - skipped", sourceName, pID );
            return false;
        }
        // 클래스 이름은 여기서 찾지 않는다. 클래스는 게임 모듈이 등록하므로 데이터를 읽는 순서에 영향받으면 안 된다. 없는 클래스는 만들 때(`createAbility`) 경고한다.

        GameplayAbilityConfig& config = outDef._config;
        config._id                    = outDef._id;
        config._pCostEffect           = resolveEffectReference( node.findAttribute( "cost" ), pID, sourceName );
        config._pCooldownEffect       = resolveEffectReference( node.findAttribute( "cooldown" ), pID, sourceName );
        bool bValid                   = XMLReadUtil::readOptionalFlag( node, "retrigger", config._bRetriggerInstancedAbility );
        bValid                        = XMLReadUtil::readOptionalFlag( node, "activateOnGranted", config._bActivateOnGranted ) && bValid;

        AbilityCatalogInternal::readTagChildren( node, "AbilityTag", config._abilityTags );
        AbilityCatalogInternal::readTagChildren( node, "CancelTag", config._cancelAbilitiesWithTags );
        AbilityCatalogInternal::readTagChildren( node, "BlockTag", config._blockAbilitiesWithTags );
        AbilityCatalogInternal::readTagChildren( node, "OwnedTag", config._activationOwnedTags );
        AbilityCatalogInternal::readTagChildren( node, "RequiredTag", config._activationRequiredTags );
        AbilityCatalogInternal::readTagChildren( node, "BlockedTag", config._activationBlockedTags );
        AbilityCatalogInternal::readTagChildren( node, "TriggerTag", config._triggerEventTags );
        AbilityCatalogInternal::readTagChildren( node, "OwnedTagTrigger", config._ownedTagTriggerTags );

        for ( XMLNode paramNode = node.findChild( "Param" ); paramNode; paramNode = paramNode.findNextSibling( "Param" ) )
        {
            hashed_string paramName;
            if ( XMLReadUtil::readRequiredName( paramNode, "name", sourceName, paramName ) == false )
            {
                bValid = false; // 이름 없는 매개변수를 조용히 건너뛰지 않는다
                continue;
            }
            const utf8* pText = paramNode.findAttribute( "text" );
            if ( pText != nullptr )
            {
                config._mapNameParameter.insert_or_assign( paramName, hashed_string( pText ) );
                continue;
            }
            // 글이 아닌 매개변수는 값을 반드시 적는다 — 빠진 값을 0 으로 두지 않는다.
            const std::optional<float32> value = paramNode.readRequiredAttribute<float32>( "value" );
            if ( value.has_value() )
                config._mapParameter.insert_or_assign( paramName, *value );
            else
                bValid = false;
        }
        if ( bValid == false )
            SW_LOG_ERROR( "%#: ability '%#' has invalid fields - skipped", sourceName, pID );
        return bValid;
    }

    bool AbilityCatalog::readAbilitySet( const XMLNode& node, string_view sourceName, AbilitySetDef& outDef ) const
    {
        const utf8* pID = node.findAttribute( "id" );
        if ( StringUtil::isNullOrEmpty( pID ) )
        {
            SW_LOG_WARNING( "%#: <AbilitySet> without an id - skipped", sourceName );
            return false;
        }
        outDef._id = hashed_string( pID );

        for ( XMLNode setNode = node.findChild( "AttributeSet" ); setNode; setNode = setNode.findNextSibling( "AttributeSet" ) )
        {
            AbilitySetAttributeSetEntry setEntry;
            if ( XMLReadUtil::readRequiredName( setNode, "class", sourceName, setEntry._className ) == false )
            {
                SW_LOG_ERROR( "%#: ability set '%#' has an <AttributeSet> without a class - skipped", sourceName, pID );
                return false;
            }

            for ( XMLNode attributeNode = setNode.findChild( "Attribute" ); attributeNode; attributeNode = attributeNode.findNextSibling( "Attribute" ) )
            {
                AbilitySetAttributeEntry attributeEntry;
                if ( XMLReadUtil::readRequiredName( attributeNode, "name", sourceName, attributeEntry._attribute ) == false )
                {
                    SW_LOG_ERROR( "%#: ability set '%#' has an <Attribute> without a name - skipped", sourceName, pID );
                    return false;
                }
                // 범위는 적은 쪽만 막는다 — 적지 않은 쪽은 구조체 초기값(실수의 끝)이라 "제한 없음" 이다.
                constexpr float32 kAny      = MathUtil::kMaxFloat;
                const bool        bHasRange = attributeNode.findAttribute( "min" ) != nullptr || attributeNode.findAttribute( "max" ) != nullptr;
                attributeEntry._bHasRange   = bHasRange ? SW_TRUE : SW_FALSE;
                bool bAttributeValid        = XMLReadUtil::readOptionalNumber( attributeNode, "base", sourceName, -kAny, kAny, attributeEntry._baseValue );
                bAttributeValid             = XMLReadUtil::readOptionalNumber( attributeNode, "min", sourceName, -kAny, kAny, attributeEntry._minValue ) && bAttributeValid;
                bAttributeValid             = XMLReadUtil::readOptionalNumber( attributeNode, "max", sourceName, -kAny, kAny, attributeEntry._maxValue ) && bAttributeValid;
                if ( bAttributeValid == false )
                {
                    SW_LOG_ERROR( "%#: ability set '%#' attribute '%#' has invalid numbers - skipped", sourceName, pID, attributeEntry._attribute.c_str() );
                    return false;
                }
                setEntry._listAttribute.push_back( attributeEntry );
            }
            outDef._listAttributeSet.push_back( std::move( setEntry ) );
        }

        for ( XMLNode abilityNode = node.findChild( "Ability" ); abilityNode; abilityNode = abilityNode.findNextSibling( "Ability" ) )
        {
            AbilitySetAbilityEntry abilityEntry;
            if ( XMLReadUtil::readRequiredName( abilityNode, "id", sourceName, abilityEntry._abilityID ) == false )
            {
                SW_LOG_ERROR( "%#: ability set '%#' has an <Ability> without an id - skipped", sourceName, pID );
                return false;
            }
            bool bEntryValid = XMLReadUtil::readOptionalNumber( abilityNode, "level", sourceName, 1, MathUtil::kMaxInt32, abilityEntry._level );
            bEntryValid      = XMLReadUtil::readOptionalNumber( abilityNode, "input", sourceName, -1, MathUtil::kMaxInt32, abilityEntry._inputID ) && bEntryValid;
            if ( bEntryValid == false )
            {
                SW_LOG_ERROR( "%#: ability set '%#' grants ability '%#' with invalid fields - skipped", sourceName, pID, abilityEntry._abilityID.c_str() );
                return false;
            }
            outDef._listAbility.push_back( abilityEntry );
        }

        for ( XMLNode effectNode = node.findChild( "Effect" ); effectNode; effectNode = effectNode.findNextSibling( "Effect" ) )
        {
            AbilitySetEffectEntry effectEntry;
            if ( XMLReadUtil::readRequiredName( effectNode, "id", sourceName, effectEntry._effectID ) == false )
            {
                SW_LOG_ERROR( "%#: ability set '%#' has an <Effect> without an id - skipped", sourceName, pID );
                return false;
            }
            if ( XMLReadUtil::readOptionalNumber( effectNode, "level", sourceName, 1, MathUtil::kMaxInt32, effectEntry._level ) == false )
            {
                SW_LOG_ERROR( "%#: ability set '%#' applies effect '%#' with an invalid level - skipped", sourceName, pID, effectEntry._effectID.c_str() );
                return false;
            }
            outDef._listEffect.push_back( effectEntry );
        }

        AbilityCatalogInternal::readTagChildren( node, "Tag", outDef._looseTags );
        return true;
    }

    shared_ptr<const GameplayEffectDef> AbilityCatalog::resolveEffectReference( const utf8* pEffectID, const utf8* pOwnerID, string_view sourceName ) const
    {
        if ( StringUtil::isNullOrEmpty( pEffectID ) )
            return nullptr;
        shared_ptr<const GameplayEffectDef> pDef = findEffect( hashed_string( pEffectID ) );
        if ( pDef == nullptr )
            SW_LOG_WARNING( "%#: ability '%#' refers to effect '%#' which is not in the catalog", sourceName, pOwnerID, pEffectID );
        return pDef;
    }
} // namespace sw
