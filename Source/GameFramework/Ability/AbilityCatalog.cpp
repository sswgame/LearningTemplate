#include "pch.h"

#include "GameFramework/Ability/AbilityCatalog.h"

#include "Core/String/StringUtil.h"

#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Ability/CombatAttributeSet.h"
#include "GameFramework/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "AbilityCatalog" );

    namespace
    {
        struct AbilityCatalogInternal
        {
            /** @brief `<X tag="A.B"/>` 자식들을 컨테이너에 더합니다(태그 속성이 빈 노드는 건너뛴다). */
            static void readTagChildren( const XmlNode& parent, const utf8* pChildName, TagContainer& outTags )
            {
                for ( XmlNode child = parent.findChild( pChildName ); child; child = child.findNextSibling( pChildName ) )
                {
                    const utf8* pTag = child.findAttribute( "tag" );
                    if ( StringUtil::isNullOrEmpty( pTag ) )
                        continue;
                    outTags.addTag( TagID::request( string_view( pTag ) ) );
                }
            }

            /** @brief 속성 @p pName 을 이름으로 읽습니다. 없거나 비었으면 빈 이름입니다. */
            static hashed_string readName( const XmlNode& node, const utf8* pName )
            {
                const utf8* pText = node.findAttribute( pName );
                if ( StringUtil::isNullOrEmpty( pText ) )
                    return hashed_string{};
                return hashed_string( pText );
            }

            /** @brief `<Modifier>` 하나를 읽습니다. 어트리뷰트가 없거나 열거 이름을 모르면 false 입니다(경고). */
            [[nodiscard]] static bool readModifier( const XmlNode& node, const utf8* pEffectId, string_view sourceName, GameplayEffectModifier& outModifier )
            {
                outModifier._attribute = readName( node, "attribute" );
                if ( outModifier._attribute.empty() )
                {
                    SW_LOG_WARNING( "%#: effect '%#' has a <Modifier> without an attribute - skipped", sourceName, pEffectId );
                    return false;
                }

                const utf8* pOp = node.findAttribute( "op" );
                if ( pOp != nullptr && engine::getTypeRegistry().enumFromString( string_view( pOp ), outModifier._op ) == false )
                {
                    SW_LOG_WARNING( "%#: effect '%#' has unknown modifier op '%#' - skipped", sourceName, pEffectId, pOp );
                    return false;
                }

                const utf8* pSource = node.findAttribute( "source" );
                if ( pSource != nullptr && engine::getTypeRegistry().enumFromString( string_view( pSource ), outModifier._magnitudeSource ) == false )
                {
                    SW_LOG_WARNING( "%#: effect '%#' has unknown magnitude source '%#' - skipped", sourceName, pEffectId, pSource );
                    return false;
                }

                outModifier._scalableMagnitude._baseValue = node.getAttributeFloat( "magnitude", 0.0f );
                outModifier._scalableMagnitude._perLevel  = node.getAttributeFloat( "perLevel", 0.0f );
                outModifier._backingAttribute             = readName( node, "backing" );
                outModifier._coefficient                  = node.getAttributeFloat( "coefficient", 1.0f );
                outModifier._preMultiplyAdditive          = node.getAttributeFloat( "pre", 0.0f );
                outModifier._postMultiplyAdditive         = node.getAttributeFloat( "post", 0.0f );
                outModifier._setByCallerName              = readName( node, "name" );

                const utf8* pFrom        = node.findAttribute( "from" );
                const bool  bFromTarget  = pFrom != nullptr && StringUtil::equals( pFrom, "Target", true );
                outModifier._bFromSource = bFromTarget ? SW_FALSE : SW_TRUE;

                const bool bMissingBacking = outModifier._magnitudeSource == EffectMagnitudeSource::AttributeBased && outModifier._backingAttribute.empty();
                if ( bMissingBacking )
                {
                    SW_LOG_WARNING( "%#: effect '%#' AttributeBased modifier on '%#' has no backing attribute - skipped", sourceName, pEffectId,
                                    outModifier._attribute.c_str() );
                    return false;
                }
                const bool bMissingCallerName = outModifier._magnitudeSource == EffectMagnitudeSource::SetByCaller && outModifier._setByCallerName.empty();
                if ( bMissingCallerName )
                {
                    SW_LOG_WARNING( "%#: effect '%#' SetByCaller modifier on '%#' has no name - skipped", sourceName, pEffectId,
                                    outModifier._attribute.c_str() );
                    return false;
                }
                return true;
            }

            /** @brief "Damage" 실행 계산을 노드의 매개변수로 만듭니다. */
            static shared_ptr<const IGameplayEffectExecution> createDamageExecution( const XmlNode& node )
            {
                shared_ptr<DamageExecution> pExecution = make_shared<DamageExecution>();
                const utf8*                 pName      = node.findAttribute( "setByCaller" );
                const hashed_string         damageName = pName != nullptr ? hashed_string( pName ) : hashed_string( "Damage" );
                pExecution->setParameters( damageName, node.getAttributeFloat( "attackPowerCoefficient", 0.0f ), node.getAttributeFloat( "base", 0.0f ) );
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

    void AbilityCatalog::clearDefinitions()
    {
        _mapEffect.clear();
        _mapAbility.clear();
        _mapAbilitySet.clear();
    }

    shared_ptr<const GameplayEffectDef> AbilityCatalog::findEffect( const hashed_string& effectId ) const
    {
        const auto mapIter = _mapEffect.find( effectId );
        return mapIter != _mapEffect.end() ? mapIter->second : nullptr;
    }

    const GameplayAbilityDef* AbilityCatalog::findAbility( const hashed_string& abilityId ) const
    {
        const auto mapIter = _mapAbility.find( abilityId );
        return mapIter != _mapAbility.end() ? &mapIter->second : nullptr;
    }

    const AbilitySetDef* AbilityCatalog::findAbilitySet( const hashed_string& setId ) const
    {
        const auto mapIter = _mapAbilitySet.find( setId );
        return mapIter != _mapAbilitySet.end() ? &mapIter->second : nullptr;
    }

    unique_ptr<GameplayAbility> AbilityCatalog::createAbility( const hashed_string& abilityId ) const
    {
        const GameplayAbilityDef* pDef = findAbility( abilityId );
        if ( pDef == nullptr )
        {
            SW_LOG_WARNING( "createAbility: no ability '%#' in the catalog", abilityId.c_str() );
            return nullptr;
        }
        const auto factoryIter = _mapAbilityFactory.find( pDef->_className );
        if ( factoryIter == _mapAbilityFactory.end() )
        {
            SW_LOG_WARNING( "createAbility: ability '%#' uses class '%#' which is not registered (registerAbilityClass)", abilityId.c_str(),
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

    uint32 AbilityCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;

        // 이펙트 → 어빌리티 → 세트. 어빌리티가 비용 · 쿨다운 이펙트를 가리키므로 파일 안의 순서와 상관없이 이펙트를 먼저 다 읽는다.
        for ( XmlNode node = root.findChild( "GameplayEffect" ); node; node = node.findNextSibling( "GameplayEffect" ) )
        {
            GameplayEffectDef def;
            if ( readEffect( node, sourceName, def ) == false )
                continue;
            (void)addEffect( def ); // id 가 있으므로 실패하지 않는다
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "Ability" ); node; node = node.findNextSibling( "Ability" ) )
        {
            GameplayAbilityDef def;
            if ( readAbility( node, sourceName, def ) == false )
                continue;
            addAbility( def );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "AbilitySet" ); node; node = node.findNextSibling( "AbilitySet" ) )
        {
            AbilitySetDef def;
            if ( readAbilitySet( node, sourceName, def ) == false )
                continue;
            addAbilitySet( def );
            ++loadedCount;
        }

        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <GameplayEffect>, <Ability> or <AbilitySet> entries", sourceName );
        return loadedCount;
    }

    bool AbilityCatalog::readEffect( const XmlNode& node, string_view sourceName, GameplayEffectDef& outDef ) const
    {
        const utf8* pId = node.findAttribute( "id" );
        if ( StringUtil::isNullOrEmpty( pId ) )
        {
            SW_LOG_WARNING( "%#: <GameplayEffect> without an id - skipped", sourceName );
            return false;
        }
        outDef._id = hashed_string( pId );

        const utf8* pDuration = node.findAttribute( "duration" );
        if ( pDuration != nullptr && engine::getTypeRegistry().enumFromString( string_view( pDuration ), outDef._durationPolicy ) == false )
        {
            SW_LOG_WARNING( "%#: effect '%#' has unknown duration policy '%#' - skipped", sourceName, pId, pDuration );
            return false;
        }
        outDef._duration._baseValue           = node.getAttributeFloat( "seconds", 0.0f );
        outDef._duration._perLevel            = node.getAttributeFloat( "secondsPerLevel", 0.0f );
        outDef._period                        = node.getAttributeFloat( "period", 0.0f );
        outDef._bExecutePeriodicOnApplication = node.getAttributeBool( "executeOnApplication", true ) ? SW_TRUE : SW_FALSE;
        outDef._bRefreshDurationOnStack       = node.getAttributeBool( "refreshOnStack", true ) ? SW_TRUE : SW_FALSE;
        outDef._stackLimit                    = node.getAttributeInt( "stackLimit", 1 );
        if ( outDef._stackLimit < 1 )
            outDef._stackLimit = 1;

        const utf8* pStacking = node.findAttribute( "stacking" );
        if ( pStacking != nullptr && engine::getTypeRegistry().enumFromString( string_view( pStacking ), outDef._stackingPolicy ) == false )
        {
            SW_LOG_WARNING( "%#: effect '%#' has unknown stacking policy '%#' - skipped", sourceName, pId, pStacking );
            return false;
        }
        const utf8* pExpiration = node.findAttribute( "stackExpiration" );
        if ( pExpiration != nullptr && engine::getTypeRegistry().enumFromString( string_view( pExpiration ), outDef._stackExpirationPolicy ) == false )
        {
            SW_LOG_WARNING( "%#: effect '%#' has unknown stack expiration '%#' - skipped", sourceName, pId, pExpiration );
            return false;
        }

        const bool bDurationWithoutLength = outDef._durationPolicy == EffectDurationPolicy::HasDuration && outDef._duration._baseValue <= 0.0f;
        if ( bDurationWithoutLength )
            SW_LOG_WARNING( "%#: effect '%#' is HasDuration but 'seconds' is not positive - it expires on the next update", sourceName, pId );

        for ( XmlNode modifierNode = node.findChild( "Modifier" ); modifierNode; modifierNode = modifierNode.findNextSibling( "Modifier" ) )
        {
            GameplayEffectModifier modifier;
            if ( AbilityCatalogInternal::readModifier( modifierNode, pId, sourceName, modifier ) )
                outDef._listModifier.push_back( modifier );
        }

        for ( XmlNode executionNode = node.findChild( "Execution" ); executionNode; executionNode = executionNode.findNextSibling( "Execution" ) )
        {
            const hashed_string className   = AbilityCatalogInternal::readName( executionNode, "class" );
            const auto          factoryIter = _mapExecutionFactory.find( className );
            if ( factoryIter == _mapExecutionFactory.end() )
            {
                SW_LOG_WARNING( "%#: effect '%#' uses execution class '%#' which is not registered - skipped", sourceName, pId, className.c_str() );
                continue;
            }
            shared_ptr<const IGameplayEffectExecution> pExecution = factoryIter->second( executionNode );
            if ( pExecution != nullptr )
                outDef._listExecution.push_back( std::move( pExecution ) );
        }

        AbilityCatalogInternal::readTagChildren( node, "AssetTag", outDef._assetTags );
        AbilityCatalogInternal::readTagChildren( node, "GrantedTag", outDef._grantedTags );
        AbilityCatalogInternal::readTagChildren( node, "RequiredTag", outDef._applicationRequiredTags );
        AbilityCatalogInternal::readTagChildren( node, "BlockedTag", outDef._applicationBlockedTags );
        AbilityCatalogInternal::readTagChildren( node, "RemoveEffectsWithTag", outDef._removeEffectsWithTags );
        AbilityCatalogInternal::readTagChildren( node, "Cue", outDef._cueTags );
        return true;
    }

    bool AbilityCatalog::readAbility( const XmlNode& node, string_view sourceName, GameplayAbilityDef& outDef ) const
    {
        const utf8* pId = node.findAttribute( "id" );
        if ( StringUtil::isNullOrEmpty( pId ) )
        {
            SW_LOG_WARNING( "%#: <Ability> without an id - skipped", sourceName );
            return false;
        }
        outDef._id        = hashed_string( pId );
        outDef._className = AbilityCatalogInternal::readName( node, "class" );
        if ( outDef._className.empty() )
        {
            SW_LOG_WARNING( "%#: ability '%#' has no class - skipped", sourceName, pId );
            return false;
        }
        // 클래스 이름은 여기서 풀지 않는다 — 클래스는 게임 모듈이 등록하므로 데이터를 읽는 순서와 상관없어야 한다. 없는 클래스는 만들 때(`createAbility`) 알린다.

        GameplayAbilityConfig& config      = outDef._config;
        config._id                         = outDef._id;
        config._pCostEffect                = resolveEffectReference( node.findAttribute( "cost" ), pId, sourceName );
        config._pCooldownEffect            = resolveEffectReference( node.findAttribute( "cooldown" ), pId, sourceName );
        config._bRetriggerInstancedAbility = node.getAttributeBool( "retrigger", false ) ? SW_TRUE : SW_FALSE;
        config._bActivateOnGranted         = node.getAttributeBool( "activateOnGranted", false ) ? SW_TRUE : SW_FALSE;

        AbilityCatalogInternal::readTagChildren( node, "AbilityTag", config._abilityTags );
        AbilityCatalogInternal::readTagChildren( node, "CancelTag", config._cancelAbilitiesWithTags );
        AbilityCatalogInternal::readTagChildren( node, "BlockTag", config._blockAbilitiesWithTags );
        AbilityCatalogInternal::readTagChildren( node, "OwnedTag", config._activationOwnedTags );
        AbilityCatalogInternal::readTagChildren( node, "RequiredTag", config._activationRequiredTags );
        AbilityCatalogInternal::readTagChildren( node, "BlockedTag", config._activationBlockedTags );
        AbilityCatalogInternal::readTagChildren( node, "TriggerTag", config._triggerEventTags );

        for ( XmlNode paramNode = node.findChild( "Param" ); paramNode; paramNode = paramNode.findNextSibling( "Param" ) )
        {
            const hashed_string paramName = AbilityCatalogInternal::readName( paramNode, "name" );
            if ( paramName.empty() )
                continue;
            const utf8* pText = paramNode.findAttribute( "text" );
            if ( pText != nullptr )
                config._mapNameParameter.insert_or_assign( paramName, hashed_string( pText ) );
            else
                config._mapParameter.insert_or_assign( paramName, paramNode.getAttributeFloat( "value", 0.0f ) );
        }
        return true;
    }

    bool AbilityCatalog::readAbilitySet( const XmlNode& node, string_view sourceName, AbilitySetDef& outDef ) const
    {
        const utf8* pId = node.findAttribute( "id" );
        if ( StringUtil::isNullOrEmpty( pId ) )
        {
            SW_LOG_WARNING( "%#: <AbilitySet> without an id - skipped", sourceName );
            return false;
        }
        outDef._id = hashed_string( pId );

        for ( XmlNode setNode = node.findChild( "AttributeSet" ); setNode; setNode = setNode.findNextSibling( "AttributeSet" ) )
        {
            AbilitySetAttributeSetEntry setEntry;
            setEntry._className = AbilityCatalogInternal::readName( setNode, "class" );
            if ( setEntry._className.empty() )
                setEntry._className = hashed_string( "Generic" );

            for ( XmlNode attributeNode = setNode.findChild( "Attribute" ); attributeNode; attributeNode = attributeNode.findNextSibling( "Attribute" ) )
            {
                AbilitySetAttributeEntry attributeEntry;
                attributeEntry._attribute = AbilityCatalogInternal::readName( attributeNode, "name" );
                if ( attributeEntry._attribute.empty() )
                    continue;
                attributeEntry._baseValue = attributeNode.getAttributeFloat( "base", 0.0f );
                const bool bHasRange      = attributeNode.findAttribute( "min" ) != nullptr || attributeNode.findAttribute( "max" ) != nullptr;
                attributeEntry._bHasRange = bHasRange ? SW_TRUE : SW_FALSE;
                attributeEntry._minValue  = attributeNode.getAttributeFloat( "min", -3.0e38f );
                attributeEntry._maxValue  = attributeNode.getAttributeFloat( "max", 3.0e38f );
                setEntry._listAttribute.push_back( attributeEntry );
            }
            outDef._listAttributeSet.push_back( std::move( setEntry ) );
        }

        for ( XmlNode abilityNode = node.findChild( "Ability" ); abilityNode; abilityNode = abilityNode.findNextSibling( "Ability" ) )
        {
            AbilitySetAbilityEntry abilityEntry;
            abilityEntry._abilityId = AbilityCatalogInternal::readName( abilityNode, "id" );
            if ( abilityEntry._abilityId.empty() )
                continue;
            abilityEntry._level   = abilityNode.getAttributeInt( "level", 1 );
            abilityEntry._inputId = abilityNode.getAttributeInt( "input", -1 );
            outDef._listAbility.push_back( abilityEntry );
        }

        for ( XmlNode effectNode = node.findChild( "Effect" ); effectNode; effectNode = effectNode.findNextSibling( "Effect" ) )
        {
            AbilitySetEffectEntry effectEntry;
            effectEntry._effectId = AbilityCatalogInternal::readName( effectNode, "id" );
            if ( effectEntry._effectId.empty() )
                continue;
            effectEntry._level = effectNode.getAttributeInt( "level", 1 );
            outDef._listEffect.push_back( effectEntry );
        }

        AbilityCatalogInternal::readTagChildren( node, "Tag", outDef._looseTags );
        return true;
    }

    shared_ptr<const GameplayEffectDef> AbilityCatalog::resolveEffectReference( const utf8* pEffectId, const utf8* pOwnerId, string_view sourceName ) const
    {
        if ( StringUtil::isNullOrEmpty( pEffectId ) )
            return nullptr;
        shared_ptr<const GameplayEffectDef> pDef = findEffect( hashed_string( pEffectId ) );
        if ( pDef == nullptr )
            SW_LOG_WARNING( "%#: ability '%#' refers to effect '%#' which is not in the catalog", sourceName, pOwnerId, pEffectId );
        return pDef;
    }
} // namespace sw
