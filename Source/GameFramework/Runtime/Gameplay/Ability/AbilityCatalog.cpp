#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"
#include "GameFramework/Runtime/Gameplay/Ability/CombatAttributeSet.h"

namespace sw
{
    namespace
    {
        struct AbilityCatalogInternal
        {
            /** @brief `<X tag="A.B"/>` 자식들을 컨테이너에 추가합니다. 태그 속성이 비어 있는 노드는 건너뜁니다. */
            static void readTagChildren( const XMLNode& parent, const utf8* pChildName, TagContainer& outTags )
            {
                for ( XMLNode child = parent.findChild( pChildName ); child; child = child.findNextSibling( pChildName ) )
                {
                    AbilityTagParams params;
                    if ( XMLFields::readProperties( child, params, "AbilityCatalog" ) == false || params._tag.empty() )
                        continue;
                    outTags.addTag( TagID::request( string_view( params._tag ) ) );
                }
            }

            /** @brief 이펙트의 태그 자식과 `<GrantAbility>` 를 읽고 즉시 이펙트에 줄 동안 항목이 없는지 확인합니다. 어긋나면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool readEffectTagsAndGrants( const XMLNode& node, const utf8* pID, string_view sourceName, AbilityEffectDef& outDef )
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
                    AbilityIDParams grant;
                    if ( XMLFields::readProperties( grantNode, grant, sourceName ) == false )
                    {
                        SW_LOG_ERROR_ID( "AbilityCatalog.EffectGrantAbilityWithoutId", "%#: effect '%#' has a <GrantAbility> without an id - skipped", sourceName, pID );
                        return false;
                    }
                    outDef._listGrantedAbility.push_back( grant._id );
                }
                // 즉시 이펙트는 걸려 있지 않으므로 줄 동안이 없다 — 적은 것을 조용히 버리지 않고 오류로 둔다.
                const bool bInstantWithLasting = outDef._durationPolicy == EffectDurationPolicy::Instant &&
                                                 ( outDef._listGrantedAbility.empty() == false || outDef._ongoingRequiredTags.getTagCount() > 0 );
                if ( bInstantWithLasting )
                {
                    SW_LOG_ERROR_ID( "AbilityCatalog.EffectInstantButGrantAbility", "%#: effect '%#' is Instant but has <GrantAbility> or <OngoingRequiredTag> - skipped", sourceName, pID );
                    return false;
                }
                return true;
            }

            /** @brief `<Modifier>` 하나를 읽습니다. 어트리뷰트가 없거나 열거형 이름을 알 수 없으면 경고를 남기고 false 를 반환합니다. */
            [[nodiscard]] static bool readModifier( const XMLNode& node, const utf8* pEffectID, string_view sourceName, AbilityEffectModifier& outModifier )
            {
                // 칸 · 열거 이름은 AbilityModifierParams 의 PROPERTY 가 본다. 깨진 칸은 그 모디파이어를 넣지 않는 오류다.
                AbilityModifierParams params;
                if ( XMLFields::readProperties( node, params, sourceName ) == false )
                {
                    SW_LOG_ERROR_ID( "AbilityCatalog.EffectModifierInvalidParams", "%#: effect '%#' has a <Modifier> with invalid fields - skipped", sourceName, pEffectID );
                    return false;
                }
                outModifier._attribute                    = params._attribute;
                outModifier._op                           = params._op;
                outModifier._magnitudeSource              = params._source;
                outModifier._scalableMagnitude._baseValue = params._magnitude;
                outModifier._scalableMagnitude._perLevel  = params._perLevel;
                outModifier._coefficient                  = params._coefficient;
                outModifier._preMultiplyAdditive          = params._pre;
                outModifier._postMultiplyAdditive         = params._post;
                outModifier._backingAttribute             = params._backing; // 아래에서 AttributeBased 일 때 필수로 확인한다
                outModifier._setByCallerName              = params._name;    // 아래에서 SetByCaller 일 때 필수로 확인한다
                outModifier._bFromSource                  = StringUtil::equals( params._from, "Target" ) ? SW_FALSE : SW_TRUE;

                const bool bMissingBacking = outModifier._magnitudeSource == EffectMagnitudeSource::AttributeBased && outModifier._backingAttribute.empty();
                if ( bMissingBacking )
                {
                    SW_LOG_WARNING_ID( "AbilityCatalog.EffectAttributeBasedModifierNo", "%#: effect '%#' AttributeBased modifier on '%#' has no backing attribute - skipped", sourceName, pEffectID,
                                       outModifier._attribute.c_str() );
                    return false;
                }
                const bool bMissingCallerName = outModifier._magnitudeSource == EffectMagnitudeSource::SetByCaller && outModifier._setByCallerName.empty();
                if ( bMissingCallerName )
                {
                    SW_LOG_WARNING_ID( "AbilityCatalog.EffectSetByCallerModifierNo", "%#: effect '%#' SetByCaller modifier on '%#' has no name - skipped", sourceName, pEffectID,
                                       outModifier._attribute.c_str() );
                    return false;
                }
                return true;
            }

            /** @brief 노드의 매개변수로 "Damage" 실행 계산을 만듭니다. 세 매개변수는 필수이고, 빠지거나 깨졌으면 오류를 남기고 nullptr 입니다. */
            static shared_ptr<const IGameplayEffectExecution> createDamageExecution( const XMLNode& node )
            {
                DamageExecutionParams params; // 세 인자 모두 필수
                if ( XMLFields::readProperties( node, params, "AbilityCatalog" ) == false )
                    return nullptr;
                shared_ptr<DamageExecution> pExecution = make_shared<DamageExecution>();
                pExecution->setParameters( params._setByCaller, params._attackPowerCoefficient, params._base );
                return pExecution;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AbilityClassRegistry::AbilityClassRegistry()
        : _mapAbilityFactory{}
        , _mapAttributeSetFactory{}
    {
        registerAttributeSetClass<AttributeSet>( "Generic" );
        registerAttributeSetClass<CombatAttributeSet>( "Combat" );
        registerAbilityClass<ApplyEffectsAbility>( "ApplyEffects" );
    }

    AbilityClassRegistry::~AbilityClassRegistry() = default;

    void AbilityClassRegistry::registerAbilityClass( const hashed_string& className, AbilityFactoryFunc factory )
    {
        if ( className.empty() || factory == nullptr )
            return;
        _mapAbilityFactory.insert_or_assign( className, factory );
    }

    void AbilityClassRegistry::registerAttributeSetClass( const hashed_string& className, AttributeSetFactoryFunc factory )
    {
        if ( className.empty() || factory == nullptr )
            return;
        _mapAttributeSetFactory.insert_or_assign( className, factory );
    }

    unique_ptr<Ability> AbilityClassRegistry::createAbility( const AbilityCatalog& catalog, const hashed_string& abilityID ) const
    {
        const AbilityDef* pDef = catalog.findAbility( abilityID );
        if ( pDef == nullptr )
        {
            SW_LOG_WARNING_ID( "AbilityCatalog.CreateAbilityNoAbilityCatalog", "createAbility: no ability '%#' in the catalog", abilityID.c_str() );
            return nullptr;
        }
        const auto factoryIter = _mapAbilityFactory.find( pDef->_className );
        if ( factoryIter == _mapAbilityFactory.end() )
        {
            SW_LOG_WARNING_ID( "AbilityCatalog.CreateAbilityAbilityUsesClass", "createAbility: ability '%#' uses class '%#' which is not registered (registerAbilityClass)", abilityID.c_str(),
                               pDef->_className.c_str() );
            return nullptr;
        }
        unique_ptr<Ability> pAbility = factoryIter->second();
        if ( pAbility != nullptr )
            pAbility->setConfig( pDef->_config );
        return pAbility;
    }

    unique_ptr<AttributeSet> AbilityClassRegistry::createAttributeSet( const hashed_string& className ) const
    {
        const auto factoryIter = _mapAttributeSetFactory.find( className );
        if ( factoryIter == _mapAttributeSetFactory.end() )
        {
            SW_LOG_WARNING_ID( "AbilityCatalog.CreateAttributeSetAttributeSetClass", "createAttributeSet: attribute set class '%#' is not registered (registerAttributeSetClass)", className.c_str() );
            return nullptr;
        }
        return factoryIter->second();
    }

    bool AbilityClassRegistry::hasAbilityClass( const hashed_string& className ) const
    {
        return _mapAbilityFactory.find( className ) != _mapAbilityFactory.end();
    }
} // namespace sw

namespace sw
{
    AbilityCatalog::AbilityCatalog()
        : _mapEffect{}
        , _mapAbility{}
        , _mapAbilitySet{}
        , _mapCue{}
        , _mapExecutionFactory{}
    {
        registerExecutionClass( "Damage", &AbilityCatalogInternal::createDamageExecution );
    }

    AbilityCatalog::~AbilityCatalog() = default;

    void AbilityCatalog::registerExecutionClass( const hashed_string& className, ExecutionFactoryFunc factory )
    {
        if ( className.empty() || factory == nullptr )
            return;
        _mapExecutionFactory.insert_or_assign( className, factory );
    }

    shared_ptr<const AbilityEffectDef> AbilityCatalog::addEffect( const AbilityEffectDef& def )
    {
        if ( def._id.empty() )
        {
            SW_LOG_WARNING_ID( "AbilityCatalog.AddEffectEffectWithoutId", "addEffect: an effect without an id was ignored" );
            return nullptr;
        }
        shared_ptr<const AbilityEffectDef> pDef = make_shared<AbilityEffectDef>( def );
        _mapEffect.insert_or_assign( def._id, pDef );
        return pDef;
    }

    void AbilityCatalog::addAbility( const AbilityDef& def )
    {
        if ( def._id.empty() )
        {
            SW_LOG_WARNING_ID( "AbilityCatalog.AddAbilityAbilityWithoutId", "addAbility: an ability without an id was ignored" );
            return;
        }
        AbilityDef stored  = def;
        stored._config._id = def._id;
        _mapAbility.insert_or_assign( def._id, std::move( stored ) );
    }

    void AbilityCatalog::addAbilitySet( const AbilitySetDef& def )
    {
        if ( def._id.empty() )
        {
            SW_LOG_WARNING_ID( "AbilityCatalog.AddAbilitySetAbilitySetWithout", "addAbilitySet: an ability set without an id was ignored" );
            return;
        }
        _mapAbilitySet.insert_or_assign( def._id, def );
    }

    void AbilityCatalog::addCue( const AbilityPresentationDef& def )
    {
        if ( def._cueTag.isValid() == false )
        {
            SW_LOG_WARNING_ID( "AbilityCatalog.AddCueGameplayCueWithout", "addCue: a gameplay cue without a tag was ignored" );
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

    const AbilityPresentationDef* AbilityCatalog::findCue( TagID cueTag ) const
    {
        if ( cueTag.isValid() == false || _mapCue.empty() )
            return nullptr;
        // 같은 태그부터 조상 쪽으로 찾는다("AbilityPresentation.Hit.Fire", "AbilityPresentation.Hit", "AbilityPresentation"). 조상 id 는 intern 하지 않고 해시만 구한다.
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

    shared_ptr<const AbilityEffectDef> AbilityCatalog::findEffect( const hashed_string& effectID ) const
    {
        const auto mapIter = _mapEffect.find( effectID );
        return mapIter != _mapEffect.end() ? mapIter->second : nullptr;
    }

    const AbilityDef* AbilityCatalog::findAbility( const hashed_string& abilityID ) const
    {
        const auto mapIter = _mapAbility.find( abilityID );
        return mapIter != _mapAbility.end() ? &mapIter->second : nullptr;
    }

    const AbilitySetDef* AbilityCatalog::findAbilitySet( const hashed_string& setID ) const
    {
        const auto mapIter = _mapAbilitySet.find( setID );
        return mapIter != _mapAbilitySet.end() ? &mapIter->second : nullptr;
    }

    uint32 AbilityCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 어빌리티가 비용 이펙트와 쿨다운 이펙트를 참조하므로, 파일 안의 순서와 상관없이 이펙트를 먼저 모두 읽는다.
        using Element                          = XMLCatalogElement<AbilityCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&AbilityCatalog::loadEffect>( "AbilityEffect" ),
            Element::of<&AbilityCatalog::loadAbility>( "Ability" ),
            Element::of<&AbilityCatalog::loadAbilitySet>( "AbilitySet" ),
            Element::of<&AbilityCatalog::loadCue>( "AbilityPresentation" ),
        };
        return XMLCatalogReadResult::readInTableOrder( *this, root, sourceName, kArrElement )._loadedCount;
    }

    bool AbilityCatalog::loadEffect( const XMLNode& node, string_view sourceName )
    {

        AbilityEffectDef def;
        if ( readEffect( node, sourceName, def ) == false )
            return false;
        (void)addEffect( def ); // id 가 있으므로 실패하지 않는다
        return true;
    }

    bool AbilityCatalog::loadAbility( const XMLNode& node, string_view sourceName )
    {

        AbilityDef def;
        if ( readAbility( node, sourceName, def ) == false )
            return false;
        addAbility( def );
        return true;
    }

    bool AbilityCatalog::loadAbilitySet( const XMLNode& node, string_view sourceName )
    {

        AbilitySetDef def;
        if ( readAbilitySet( node, sourceName, def ) == false )
            return false;
        addAbilitySet( def );
        return true;
    }

    bool AbilityCatalog::loadCue( const XMLNode& node, string_view sourceName )
    {

        AbilityPresentationDef def;
        if ( readCue( node, sourceName, def ) == false )
            return false;
        addCue( def );
        return true;
    }

    bool AbilityCatalog::readEffect( const XMLNode& node, string_view sourceName, AbilityEffectDef& outDef ) const
    {
        // 칸 · 열거 이름 · 범위(주기 >= 0 · 겹침 상한 >= 1)는 AbilityEffectParams 의 PROPERTY 가 본다. 깨진 칸은 그 이펙트를 넣지 않는 오류다(자르지 않는다).
        AbilityEffectParams params;
        if ( XMLFields::readProperties( node, params, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "AbilityCatalog.EffectInvalidFieldsSkipped", "%#: <AbilityEffect id=\"%#\"> has invalid fields - skipped", sourceName, params._id.c_str() );
            return false;
        }
        const utf8* const pID                 = params._id.c_str();
        outDef._id                            = params._id;
        outDef._durationPolicy                = params._duration;
        outDef._duration._baseValue           = params._seconds;
        outDef._duration._perLevel            = params._secondsPerLevel;
        outDef._period                        = params._period;
        outDef._bExecutePeriodicOnApplication = params._bExecuteOnApplication ? SW_TRUE : SW_FALSE;
        outDef._bRefreshDurationOnStack       = params._bRefreshOnStack ? SW_TRUE : SW_FALSE;
        outDef._stackLimit                    = params._stackLimit;
        outDef._stackingPolicy                = params._stacking;
        outDef._stackExpirationPolicy         = params._stackExpiration;

        const bool bDurationWithoutLength = outDef._durationPolicy == EffectDurationPolicy::HasDuration && outDef._duration._baseValue <= 0.0f;
        if ( bDurationWithoutLength )
            SW_LOG_WARNING_ID( "AbilityCatalog.EffectHasDurationButSeconds", "%#: effect '%#' is HasDuration but 'seconds' is not positive - it expires on the next update", sourceName, pID );

        for ( XMLNode modifierNode = node.findChild( "Modifier" ); modifierNode; modifierNode = modifierNode.findNextSibling( "Modifier" ) )
        {
            AbilityEffectModifier modifier;
            if ( AbilityCatalogInternal::readModifier( modifierNode, pID, sourceName, modifier ) )
                outDef._listModifier.push_back( modifier );
        }

        for ( XMLNode executionNode = node.findChild( "Execution" ); executionNode; executionNode = executionNode.findNextSibling( "Execution" ) )
        {
            const hashed_string className( executionNode.getAttributeText( "class" ) ); // 인자는 실행 클래스가 읽는다(클래스 이름 필수)
            if ( className.empty() )
            {
                SW_LOG_ERROR_ID( "AbilityCatalog.EffectExecutionWithoutClass", "%#: effect '%#' has an <Execution> without a class - skipped", sourceName, pID );
                return false;
            }
            const auto factoryIter = _mapExecutionFactory.find( className );
            if ( factoryIter == _mapExecutionFactory.end() )
            {
                SW_LOG_WARNING_ID( "AbilityCatalog.EffectUsesExecutionClass", "%#: effect '%#' uses execution class '%#' which is not registered - skipped", sourceName, pID, className.c_str() );
                continue;
            }
            shared_ptr<const IGameplayEffectExecution> pExecution = factoryIter->second( executionNode );
            if ( pExecution == nullptr )
            {
                SW_LOG_ERROR_ID( "AbilityCatalog.EffectExecutionClassInvalid", "%#: effect '%#' has an <Execution class=\"%#\"> with invalid parameters - skipped", sourceName, pID, className.c_str() );
                return false;
            }
            outDef._listExecution.push_back( std::move( pExecution ) );
        }

        return AbilityCatalogInternal::readEffectTagsAndGrants( node, pID, sourceName, outDef );
    }

    bool AbilityCatalog::readCue( const XMLNode& node, string_view sourceName, AbilityPresentationDef& outDef )
    {
        AbilityPresentationParams params;
        if ( XMLFields::readProperties( node, params, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "AbilityCatalog.GameplayCueInvalidParams", "%#: <AbilityPresentation tag=\"%#\"> has invalid fields - skipped", sourceName, params._tag );
            return false;
        }
        const utf8* const pTag = params._tag.c_str();
        outDef._cueTag         = TagID::request( string_view( params._tag ) );
        outDef._prefab         = params._prefab; // 둘 중 하나는 아래에서 필수로 확인한다
        outDef._soundEvent     = params._sound;
        outDef._offset         = params._offset;
        outDef._bOnRemoved     = params._bOnRemoved ? SW_TRUE : SW_FALSE;
        outDef._bPositional    = params._bPositional ? SW_TRUE : SW_FALSE;
        // 보여 줄 것이 없는 정의는 적은 사람의 실수다 — 조용히 아무것도 하지 않는 큐로 두지 않는다.
        if ( outDef._prefab.empty() && outDef._soundEvent.empty() )
        {
            SW_LOG_ERROR_ID( "AbilityCatalog.GameplayCueNeitherPrefab", "%#: gameplay cue '%#' has neither prefab nor sound - skipped", sourceName, pTag );
            return false;
        }
        return true;
    }

    bool AbilityCatalog::readAbility( const XMLNode& node, string_view sourceName, AbilityDef& outDef ) const
    {
        AbilityParams params; // id · 클래스 필수
        if ( XMLFields::readProperties( node, params, sourceName ) == false )
        {
            SW_LOG_WARNING_ID( "AbilityCatalog.AbilityInvalidHeadSkipped", "%#: <Ability id=\"%#\"> has an invalid head (id, class) - skipped", sourceName, params._id.c_str() );
            return false;
        }
        const utf8* const pID = params._id.c_str();
        outDef._id            = params._id;
        outDef._className     = params._class;
        // 클래스 이름은 여기서 찾지 않는다. 클래스는 게임 모듈이 등록하므로 데이터를 읽는 순서에 영향받으면 안 된다. 없는 클래스는 만들 때(`createAbility`) 경고한다.

        AbilityConfig& config              = outDef._config;
        config._id                         = outDef._id;
        config._pCostEffect                = resolveEffectReference( params._cost.empty() ? nullptr : params._cost.c_str(), pID, sourceName );
        config._pCooldownEffect            = resolveEffectReference( params._cooldown.empty() ? nullptr : params._cooldown.c_str(), pID, sourceName );
        config._bRetriggerInstancedAbility = params._bRetrigger ? SW_TRUE : SW_FALSE;
        config._bActivateOnGranted         = params._bActivateOnGranted ? SW_TRUE : SW_FALSE;
        bool bValid                        = true;

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
            AbilityParamEntryParams param;
            if ( XMLFields::readProperties( paramNode, param, sourceName ) == false )
            {
                bValid = false; // 이름 없는 · 깨진 매개변수를 조용히 건너뛰지 않는다
                continue;
            }
            if ( paramNode.findAttribute( "text" ) != nullptr )
            {
                config._mapNameParameter.insert_or_assign( param._name, param._text );
                continue;
            }
            // 글이 아닌 매개변수는 값을 반드시 적는다 — 빠진 값을 0 으로 두지 않는다.
            if ( paramNode.findAttribute( "value" ) != nullptr )
            {
                config._mapParameter.insert_or_assign( param._name, param._value );
                continue;
            }
            SW_LOG_ERROR_ID( "AbilityCatalog.AbilityParamNeedsValue", "%#: ability '%#' <Param name=\"%#\"> needs text or value", sourceName, pID, param._name.c_str() );
            bValid = false;
        }
        if ( bValid == false )
            SW_LOG_ERROR_ID( "AbilityCatalog.AbilityInvalidFieldsSkipped", "%#: ability '%#' has invalid fields - skipped", sourceName, pID );
        return bValid;
    }

    bool AbilityCatalog::readAbilitySet( const XMLNode& node, string_view sourceName, AbilitySetDef& outDef ) const
    {
        AbilityIDParams head;
        if ( XMLFields::readProperties( node, head, sourceName ) == false )
        {
            SW_LOG_WARNING_ID( "AbilityCatalog.AbilitySetWithoutIdSkipped", "%#: <AbilitySet> without an id - skipped", sourceName );
            return false;
        }
        const utf8* const pID = head._id.c_str();
        outDef._id            = head._id;

        for ( XMLNode setNode = node.findChild( "AttributeSet" ); setNode; setNode = setNode.findNextSibling( "AttributeSet" ) )
        {
            AbilitySetAttributeSetEntry setEntry;
            AbilityClassParams          setProperties;
            const bool                  bSetRead = XMLFields::readProperties( setNode, setProperties, sourceName );
            setEntry._className                  = setProperties._class;
            if ( bSetRead == false )
            {
                SW_LOG_ERROR_ID( "AbilityCatalog.AbilitySetAttributeSetWithout", "%#: ability set '%#' has an <AttributeSet> without a class - skipped", sourceName, pID );
                return false;
            }

            for ( XMLNode attributeNode = setNode.findChild( "Attribute" ); attributeNode; attributeNode = attributeNode.findNextSibling( "Attribute" ) )
            {
                // 범위는 적은 쪽만 막는다 — 적지 않은 쪽은 구조체 초기값(실수의 끝)이라 "제한 없음" 이다.
                AbilitySetAttributeEntry attributeEntry;
                if ( XMLFields::readProperties( attributeNode, attributeEntry, sourceName ) == false )
                {
                    SW_LOG_ERROR_ID( "AbilityCatalog.AbilitySetAttributeInvalid", "%#: ability set '%#' has an <Attribute> with invalid fields - skipped", sourceName, pID );
                    return false;
                }
                const bool bHasRange      = attributeNode.findAttribute( "min" ) != nullptr || attributeNode.findAttribute( "max" ) != nullptr;
                attributeEntry._bHasRange = bHasRange ? SW_TRUE : SW_FALSE;
                setEntry._listAttribute.push_back( attributeEntry );
            }
            outDef._listAttributeSet.push_back( std::move( setEntry ) );
        }

        for ( XMLNode abilityNode = node.findChild( "Ability" ); abilityNode; abilityNode = abilityNode.findNextSibling( "Ability" ) )
        {
            AbilitySetAbilityEntry abilityEntry; // id 필수 · 레벨 >= 1 · 입력 >= -1
            if ( XMLFields::readProperties( abilityNode, abilityEntry, sourceName ) == false )
            {
                SW_LOG_ERROR_ID( "AbilityCatalog.AbilitySetGrantsAbility", "%#: ability set '%#' has an <Ability> with invalid fields - skipped", sourceName, pID );
                return false;
            }
            outDef._listAbility.push_back( abilityEntry );
        }

        for ( XMLNode effectNode = node.findChild( "Effect" ); effectNode; effectNode = effectNode.findNextSibling( "Effect" ) )
        {
            AbilitySetEffectEntry effectEntry; // id 필수 · 레벨 >= 1
            if ( XMLFields::readProperties( effectNode, effectEntry, sourceName ) == false )
            {
                SW_LOG_ERROR_ID( "AbilityCatalog.AbilitySetAppliesEffect", "%#: ability set '%#' has an <Effect> with invalid fields - skipped", sourceName, pID );
                return false;
            }
            outDef._listEffect.push_back( effectEntry );
        }

        AbilityCatalogInternal::readTagChildren( node, "Tag", outDef._looseTags );
        return true;
    }

    shared_ptr<const AbilityEffectDef> AbilityCatalog::resolveEffectReference( const utf8* pEffectID, const utf8* pOwnerID, string_view sourceName ) const
    {
        if ( StringUtil::isNullOrEmpty( pEffectID ) )
            return nullptr;
        // 없는 이펙트는 참조 표(`engine/settings/datacatalogreferences.xml` 의 AbilityCatalog 줄)가 오류로 알리고 로드를 멈춘다 — 여기서 따로 알리지 않는다.
        (void)pOwnerID;
        (void)sourceName;
        return findEffect( hashed_string( pEffectID ) );
    }
} // namespace sw

namespace sw
{
    /** @brief `AbilityCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_MODULE_SHARED_ASSET_CACHE( Abilities, AbilityCatalog, SW_GF_API, AssetTypeDef::makeDataFile( AbilityCatalog::kDataKind, AssetPathRule{ "game/", {}, "/data/abilities.xml" }, AssetTypeLoader::of<AbilityCatalog>( MemoryTag::Game ) ) );
} // namespace sw
