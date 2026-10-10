#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceResolver.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/formatString.h"
#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceXMLUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/Equipment.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"

namespace sw
{
    namespace
    {
        struct AppearanceResolverInternal
        {
            /** @brief 해석 중의 외형 하나(몸 · 꾸미기 외형 · 칸의 장비 · 세트 완성 표현)입니다. */
            struct Entry
            {
                const ItemVisualDef*         _pVisual{ nullptr };
                const AppearanceSlotRequest* _pRequest{ nullptr }; ///< 칸의 장비일 때
                hashed_string                _owner{};
                hashed_string                _itemID{};
                hashed_string                _state{};
                hashed_string                _customVariant{};         ///< 꾸미기 고르기가 정한 메시 변형
                hashed_string                _customMaterialVariant{}; ///< 꾸미기 고르기가 정한 머티리얼 변형
                uint8                        _bHidden{ SW_FALSE };
            };

            /** @brief 규칙 동작 하나의 판정 — 같은 대상을 두고 이긴 쪽입니다. */
            struct Decision
            {
                const AppearanceRuleAction* _pAction{ nullptr };
                const AppearanceRuleDef*    _pRule{ nullptr };
            };

            /** @brief FNV-1a 64 로 값을 쌓습니다. 이름은 내용 해시(대소문자 무시)라 프로세스 · 기계가 달라도 같습니다. */
            struct HashBuilder
            {

                uint64 _value{ HashUtil::kFnvOffset64 };

                void addUint64( uint64 value )
                {
                    for ( uint32 byteIndex = 0; byteIndex < 8; ++byteIndex )
                    {
                        _value ^= ( value >> ( byteIndex * 8 ) ) & 0xffu;
                        _value *= HashUtil::kFnvPrime64;
                    }
                }
                void addName( const hashed_string& name ) { addUint64( name.empty() ? 0ull : static_cast<uint64>( name.getHash() ) ); }
                void addInt( int32 value ) { addUint64( static_cast<uint64>( static_cast<uint32>( value ) ) ); }
                void addFloat( float32 value )
                {
                    const float32 canonical = value == 0.0f ? 0.0f : value; // -0 과 +0 을 하나로
                    uint32        bits      = 0;
                    Memory::copy( &bits, &canonical, sizeof( bits ) );
                    addUint64( bits );
                }
                void addFloat3( const float3& value )
                {
                    addFloat( value._x );
                    addFloat( value._y );
                    addFloat( value._z );
                }
                void addFloat4( const float4& value )
                {
                    addFloat( value._x );
                    addFloat( value._y );
                    addFloat( value._z );
                    addFloat( value._w );
                }
                void addPlacement( const AppearancePlacement& placement )
                {
                    addInt( static_cast<int32>( placement._listSocket.size() ) );
                    for ( const hashed_string& socket : placement._listSocket )
                    {
                        addName( socket );
                    }
                    addFloat3( placement._offset );
                    addFloat3( placement._rotation );
                }
            };

            template <typename... Args>
            static void addTrace( ResolvedAppearance& out, AppearanceTraceStep step, const hashed_string& source, string_view format, Args&&... args )
            {
                utf8 arrBuffer[constant::kMaxBuffer512];
                FormatString::formatstring( arrBuffer, constant::kMaxBuffer512, format, std::forward<Args>( args )... );
                AppearanceTraceEntry entry;
                entry._step    = step;
                entry._source  = source;
                entry._message = arrBuffer;
                out._listTrace.push_back( entry );
            }

            static hashed_string prefixSocket( const hashed_string& owner, const hashed_string& socket )
            {
                if ( owner.empty() )
                    return socket;
                string name = owner.c_str();
                name += ".";
                name += socket.c_str();
                return hashed_string( string_view( name.c_str(), name.size() ) );
            }

            static void setMorph( ResolvedAppearance& out, const hashed_string& owner, const hashed_string& name, float32 weight )
            {
                for ( ResolvedMorph& morph : out._listMorph )
                {
                    if ( morph._owner == owner && morph._name == name )
                    {
                        morph._weight = weight;
                        return;
                    }
                }
                out._listMorph.push_back( ResolvedMorph{ owner, name, weight } );
            }

            static void addMaterialValue( ResolvedAppearance& out, const hashed_string& owner, const hashed_string& part, const hashed_string& name, const float4& value,
                                          ResolvedMaterialTarget target, int32 channel )
            {
                ResolvedMaterialValue entry;
                entry._owner   = owner;
                entry._part    = part;
                entry._name    = name;
                entry._value   = value;
                entry._target  = target;
                entry._channel = channel;
                out._listMaterialValue.push_back( entry );
            }

            static void addHiddenRegion( ResolvedAppearance& out, const hashed_string& region )
            {
                if ( AppearanceXMLUtil::containsName( out._listHiddenRegion, region ) == false )
                    out._listHiddenRegion.push_back( region );
            }

            static int32 findEntry( const vector<Entry>& listEntry, const hashed_string& owner, bool bVisibleOnly )
            {
                for ( size_t index = 0; index < listEntry.size(); ++index )
                {
                    const Entry& entry   = listEntry[index];
                    const bool   bUsable = bVisibleOnly == false || entry._bHidden == SW_FALSE;
                    if ( entry._owner == owner && bUsable )
                        return static_cast<int32>( index );
                }
                return -1;
            }

            static hashed_string getDefaultState( const ItemVisualDef& visual ) { return visual._defaultState; }

            /**
             * @brief 꾸미기 값을 겁니다 — 슬라이더 · 색은 모프 · 본 비율 · 머티리얼, 고르기는 변형 · 외형, 부착은 소켓 부착물. @p ownerIndex 는 그 주인의 항목입니다.
             * @details 몸(주인이 빔)의 고르기 외형은 매개변수 이름을 주인으로 새 항목이 되고, 아이템의 고르기 외형은 @p outListExtraVisual 로 나옵니다(같은 주인).
             */
            static void applyCustomization( const CustomizationSchemaDef& schema, const CustomizationValueSet& normalized, const hashed_string& owner, int32 ownerIndex,
                                            const AppearanceDatabase& database, vector<Entry>& inoutListEntry, vector<const ItemVisualDef*>& outListExtraVisual,
                                            ResolvedAppearance& out )
            {
                for ( const CustomizationParamDef& param : schema._listParameter )
                {
                    const CustomizationValue* pValue = normalized.findValue( param._name );
                    if ( pValue == nullptr )
                        continue;
                    if ( CustomizationUtil::isActive( param, schema, normalized ) == false )
                    {
                        addTrace( out, AppearanceTraceStep::Customization, param._name, "'%#' is inactive (condition not met)", param._name.c_str() );
                        continue;
                    }
                    switch ( param._kind )
                    {
                        case CustomizationKind::Slider:
                        case CustomizationKind::Color:
                        {
                            for ( const CustomizationDriveDef& drive : param._listDrive )
                            {
                                const float32 driven = CustomizationUtil::computeDriveValue( param, drive, pValue->_number._x );
                                switch ( drive._kind )
                                {
                                    case CustomizationDriveKind::Morph:
                                    {
                                        setMorph( out, owner, drive._target, driven );
                                        break;
                                    }
                                    case CustomizationDriveKind::BoneProportion:
                                    {
                                        out._listBoneProportion.push_back( ResolvedBoneProportion{ owner, drive._target, driven } );
                                        break;
                                    }
                                    case CustomizationDriveKind::MaterialScalar:
                                    {
                                        addMaterialValue( out, owner, hashed_string{}, drive._target, float4( driven, 0.0f, 0.0f, 0.0f ), ResolvedMaterialTarget::Parameter, 0 );
                                        break;
                                    }
                                    case CustomizationDriveKind::MaterialColor:
                                    {
                                        addMaterialValue( out, owner, hashed_string{}, drive._target, pValue->_number, ResolvedMaterialTarget::Parameter, 0 );
                                        break;
                                    }
                                    case CustomizationDriveKind::DyeChannel:
                                    {
                                        addMaterialValue( out, owner, hashed_string{}, drive._target, pValue->_number, ResolvedMaterialTarget::DyeChannel, drive._channel );
                                        break;
                                    }
                                    case CustomizationDriveKind::PaletteSwap:
                                    {
                                        addMaterialValue( out, owner, hashed_string{}, drive._target, pValue->_number, ResolvedMaterialTarget::PaletteSwap, 0 );
                                        break;
                                    }
                                }
                            }
                            break;
                        }
                        case CustomizationKind::Choice:
                        {
                            const CustomizationOptionDef* pOption = param.findOption( pValue->_option );
                            if ( pOption == nullptr )
                                break;
                            if ( ownerIndex >= 0 && pOption->_variant.empty() == false )
                                inoutListEntry[static_cast<size_t>( ownerIndex )]._customVariant = pOption->_variant;
                            if ( ownerIndex >= 0 && pOption->_materialVariant.empty() == false )
                                inoutListEntry[static_cast<size_t>( ownerIndex )]._customMaterialVariant = pOption->_materialVariant;
                            const ItemVisualDef* pOptionVisual = pOption->_visual.empty() ? nullptr : database.getVisuals().findVisual( pOption->_visual );
                            if ( pOptionVisual == nullptr )
                                break;
                            if ( owner.empty() )
                            {
                                Entry entry;
                                entry._pVisual = pOptionVisual;
                                entry._owner   = param._name;
                                entry._state   = getDefaultState( *pOptionVisual );
                                inoutListEntry.push_back( entry );
                            }
                            else
                            {
                                outListExtraVisual.push_back( pOptionVisual );
                            }
                            break;
                        }
                        case CustomizationKind::Attachment:
                        {
                            const CustomizationOptionDef* pOption = param.findOption( pValue->_option );
                            if ( pOption == nullptr || pOption->_asset.empty() )
                                break;
                            ResolvedAttachment attachment;
                            attachment._owner     = owner;
                            attachment._parameter = param._name;
                            attachment._option    = pOption->_name;
                            attachment._asset     = pOption->_asset;
                            attachment._placement._listSocket.push_back( prefixSocket( owner, param._socket ) );
                            attachment._placement._offset   = pOption->_offset;
                            attachment._placement._rotation = pOption->_rotation;
                            out._listAttachment.push_back( attachment );
                            break;
                        }
                    }
                }
            }

            static bool isConditionMet( const AppearanceRuleCondition& condition, const vector<Entry>& listEntry, const CharacterAppearanceSpec& spec )
            {
                bool bMet = false;
                switch ( condition._kind )
                {
                    case AppearanceRuleConditionKind::TargetTag:
                    case AppearanceRuleConditionKind::AnyTag:
                    {
                        for ( const Entry& entry : listEntry )
                        {
                            const bool bOwnerMatches = condition._kind == AppearanceRuleConditionKind::AnyTag || entry._owner == condition._target;
                            if ( entry._bHidden == SW_FALSE && bOwnerMatches && entry._pVisual->_tags.hasTag( condition._tag ) )
                                bMet = true;
                        }
                        break;
                    }
                    case AppearanceRuleConditionKind::CharacterTag:
                    {
                        bMet = spec._tags.hasTag( condition._tag );
                        break;
                    }
                    case AppearanceRuleConditionKind::Occupied:
                    {
                        bMet = findEntry( listEntry, condition._target, true ) >= 0;
                        break;
                    }
                    case AppearanceRuleConditionKind::BodyShape:
                    {
                        bMet = AppearanceXMLUtil::containsName( condition._listName, spec._bodyShape );
                        break;
                    }
                    case AppearanceRuleConditionKind::BodyType:
                    {
                        bMet = AppearanceXMLUtil::containsName( condition._listName, spec._bodyType );
                        break;
                    }
                }
                return bMet != ( condition._bNegate == SW_TRUE );
            }

            /** @brief 규칙의 비숨김 동작을 대상별 판정에 넣습니다 — 높은 우선순위가 이기고, 같으면 먼저 온 규칙(로드가 그런 짝을 막는다). */
            static void decide( const AppearanceRuleDef& rule, const AppearanceRuleAction& action, vector<Decision>& inoutListDecision, ResolvedAppearance& out )
            {
                for ( Decision& decision : inoutListDecision )
                {
                    if ( decision._pAction->hasSameTarget( action ) == false )
                        continue;
                    if ( rule._priority > decision._pRule->_priority )
                    {
                        addTrace( out, AppearanceTraceStep::Rule, decision._pRule->_id, "%# overridden by rule '%#' (priority %# > %#)", toString( action._kind ), rule._id.c_str(),
                                  rule._priority, decision._pRule->_priority );
                        decision._pAction = &action;
                        decision._pRule   = &rule;
                    }
                    else
                    {
                        addTrace( out, AppearanceTraceStep::Rule, rule._id, "%# overridden by rule '%#'", toString( action._kind ), decision._pRule->_id.c_str() );
                    }
                    return;
                }
                inoutListDecision.push_back( Decision{ &action, &rule } );
            }

            static const Decision* findDecision( const vector<Decision>& listDecision, AppearanceRuleActionKind kind, const hashed_string& owner, const hashed_string& part )
            {
                const Decision* pAllParts = nullptr;
                for ( const Decision& decision : listDecision )
                {
                    const AppearanceRuleAction& action = *decision._pAction;
                    if ( action._kind != kind || action._target != owner )
                        continue;
                    if ( action._part == part && part.empty() == false )
                        return &decision;
                    if ( action._part.empty() )
                        pAllParts = &decision;
                }
                return pAllParts;
            }

            static void emitParts( const ItemVisualDef& visual, const Entry& entry, int32 stageIndex, const vector<Decision>& listDecision, ResolvedAppearance& out )
            {
                const AppearanceStateDef*       pState       = visual.findState( entry._state );
                const AppearanceDamageStageDef* pStage       = stageIndex >= 0 ? &visual._listDamageStage[static_cast<size_t>( stageIndex )] : nullptr;
                const Decision*                 pRuleVariant = findDecision( listDecision, AppearanceRuleActionKind::ChooseVariant, entry._owner, hashed_string{} );
                for ( const AppearancePartDef& part : visual._listPart )
                {
                    if ( pState != nullptr && AppearanceXMLUtil::containsName( pState->_listHiddenPart, part._name ) )
                        continue;
                    AppearancePlacement placement = part._placement;
                    if ( pState != nullptr )
                    {
                        for ( const AppearanceStateDef::Placement& statePlacement : pState->_listPlacement )
                        {
                            if ( statePlacement._part == part._name )
                                placement = statePlacement._placement;
                        }
                    }
                    const bool  bHitOff    = entry._pRequest != nullptr && AppearanceXMLUtil::containsName( entry._pRequest->_listDetachedPart, part._name );
                    const int32 breakIndex = part._breakStage.empty() ? -1 : visual.findDamageStageIndex( part._breakStage );
                    const bool  bStageOff  = breakIndex >= 0 && stageIndex >= breakIndex;
                    if ( bHitOff && part._bBreakable == SW_FALSE )
                        addTrace( out, AppearanceTraceStep::Damage, entry._owner, "part '%#' is not breakable - detach request ignored", part._name.c_str() );
                    if ( part._bBreakable == SW_TRUE && ( bHitOff || bStageOff ) )
                    {
                        ResolvedDetachedPart detached;
                        detached._owner            = entry._owner;
                        detached._itemID           = entry._itemID;
                        detached._partName         = part._name;
                        detached._asset            = part._asset;
                        detached._impulse          = part._breakImpulse;
                        detached._placement        = placement;
                        detached._bFromDamageStage = bHitOff ? SW_FALSE : SW_TRUE;
                        out._listDetachedPart.push_back( detached );
                        addTrace( out, AppearanceTraceStep::Damage, entry._owner, "part '%#' broke off (%#)", part._name.c_str(), bHitOff ? "hit" : "damage stage" );
                        continue;
                    }
                    if ( part._kind == AppearancePartKind::BodyModification )
                    {
                        for ( const AppearanceMorphDef& morph : part._listMorph )
                        {
                            setMorph( out, hashed_string{}, morph._name, morph._weight );
                        }
                        for ( const AppearanceMaterialValueDef& value : part._listMaterialValue )
                        {
                            addMaterialValue( out, hashed_string{}, hashed_string{}, value._name, value._value, ResolvedMaterialTarget::Parameter, 0 );
                        }
                        for ( const hashed_string& region : part._listHiddenRegion )
                        {
                            addHiddenRegion( out, region );
                            addTrace( out, AppearanceTraceStep::Output, entry._owner, "body region '%#' hidden by '%#'", region.c_str(), visual._id.c_str() );
                        }
                        continue;
                    }
                    ResolvedPart resolved;
                    resolved._owner       = entry._owner;
                    resolved._itemID      = entry._itemID;
                    resolved._visualID    = visual._id;
                    resolved._partName    = part._name;
                    resolved._kind        = part._kind;
                    resolved._asset       = part._asset;
                    resolved._material    = part._material;
                    resolved._skeleton    = part._skeleton;
                    resolved._socketSet   = part._socketSet;
                    resolved._layer       = part._layer;
                    resolved._bDeforms    = part._bDeforms;
                    resolved._state       = entry._state;
                    resolved._placement   = placement;
                    resolved._damageStage = pStage != nullptr ? pStage->_name : hashed_string{};

                    // 메시 변형: 규칙 > 피해 단계 > 꾸미기. 부품에 없는 변형 이름은 그 부품에는 아무 일도 하지 않는다.
                    hashed_string variant = entry._customVariant;
                    if ( pStage != nullptr )
                    {
                        for ( const AppearanceDamageStageDef::PartVariant& stageVariant : pStage->_listVariant )
                        {
                            if ( stageVariant._part == part._name )
                                variant = stageVariant._variant;
                        }
                    }
                    if ( pRuleVariant != nullptr )
                        variant = pRuleVariant->_pAction->_name;
                    const AppearancePartVariantDef* pVariant = variant.empty() ? nullptr : part.findVariant( variant );
                    if ( pVariant != nullptr )
                    {
                        resolved._variant = pVariant->_name;
                        if ( pVariant->_asset.empty() == false )
                            resolved._asset = pVariant->_asset;
                        if ( pVariant->_material.empty() == false )
                            resolved._material = pVariant->_material;
                    }
                    const AppearancePartVariantDef* pMaterialVariant = entry._customMaterialVariant.empty() ? nullptr : part.findMaterialVariant( entry._customMaterialVariant );
                    if ( pMaterialVariant != nullptr )
                    {
                        resolved._materialVariant = pMaterialVariant->_name;
                        resolved._material        = pMaterialVariant->_material;
                    }
                    const Decision* pSwapMesh = findDecision( listDecision, AppearanceRuleActionKind::SwapMesh, entry._owner, part._name );
                    if ( pSwapMesh != nullptr )
                    {
                        resolved._asset = pSwapMesh->_pAction->_value;
                        addTrace( out, AppearanceTraceStep::Rule, pSwapMesh->_pRule->_id, "mesh of '%#.%#' swapped to '%#'", entry._owner.c_str(), part._name.c_str(), resolved._asset.c_str() );
                    }
                    const Decision* pSwapMaterial = findDecision( listDecision, AppearanceRuleActionKind::SwapMaterial, entry._owner, part._name );
                    if ( pSwapMaterial != nullptr )
                    {
                        resolved._material = pSwapMaterial->_pAction->_value;
                        addTrace( out, AppearanceTraceStep::Rule, pSwapMaterial->_pRule->_id, "material of '%#.%#' swapped to '%#'", entry._owner.c_str(), part._name.c_str(),
                                  resolved._material.c_str() );
                    }
                    if ( resolved._socketSet.empty() == false )
                        out._listSocketSource.push_back( ResolvedSocketSource{ entry._owner, part._name, part._socketSet } );
                    out._listPart.push_back( resolved );
                }
                if ( pStage != nullptr )
                {
                    for ( const AppearanceMaterialValueDef& value : pStage->_listMaterialValue )
                    {
                        addMaterialValue( out, entry._owner, value._part, value._name, value._value, ResolvedMaterialTarget::Parameter, 0 );
                    }
                }
            }

            static void emitEntry( const AppearanceDatabase& database, Entry& inoutEntry, vector<Entry>& inoutListEntry, int32 entryIndex, const vector<Decision>& listDecision,
                                   ResolvedAppearance& out )
            {
                const ItemVisualDef& visual = *inoutEntry._pVisual;
                if ( inoutEntry._state.empty() == false && visual.findState( inoutEntry._state ) == nullptr )
                {
                    addTrace( out, AppearanceTraceStep::Output, inoutEntry._owner, "visual '%#' has no state '%#' - default state used", visual._id.c_str(), inoutEntry._state.c_str() );
                    inoutEntry._state = getDefaultState( visual );
                }
                vector<const ItemVisualDef*>  listExtraVisual;
                const AppearanceSlotRequest*  pRequest = inoutEntry._pRequest;
                const CustomizationSchemaDef* pSchema  = visual._customization.empty() ? nullptr : database.getSchemas().findSchema( visual._customization );
                if ( pRequest != nullptr && pSchema != nullptr )
                {
                    CustomizationValueSet normalized;
                    vector<hashed_string> listDropped;
                    CustomizationUtil::normalize( *pSchema, pRequest->_customization, normalized, &listDropped );
                    for ( const hashed_string& dropped : listDropped )
                    {
                        addTrace( out, AppearanceTraceStep::Customization, inoutEntry._owner, "'%#' is not in schema '%#' - dropped", dropped.c_str(), pSchema->_id.c_str() );
                    }
                    applyCustomization( *pSchema, normalized, inoutEntry._owner, entryIndex, database, inoutListEntry, listExtraVisual, out );
                }
                const Entry entry      = inoutListEntry[static_cast<size_t>( entryIndex )];
                int32       stageIndex = -1;
                if ( pRequest != nullptr )
                {
                    stageIndex = visual.computeDamageStageIndex( AppearanceResolver::snapDamage( pRequest->_damage ) );
                    if ( stageIndex >= 0 )
                        addTrace( out, AppearanceTraceStep::Damage, entry._owner, "damage stage '%#'", visual._listDamageStage[static_cast<size_t>( stageIndex )]._name.c_str() );
                }
                emitParts( visual, entry, stageIndex, listDecision, out );
                for ( const ItemVisualDef* pExtraVisual : listExtraVisual )
                {
                    Entry extra  = entry;
                    extra._state = getDefaultState( *pExtraVisual );
                    emitParts( *pExtraVisual, extra, -1, listDecision, out );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( AppearanceTraceStep step )
    {
        switch ( step )
        {
            case AppearanceTraceStep::Customization:
                return "Customization";
            case AppearanceTraceStep::Occupancy:
                return "Occupancy";
            case AppearanceTraceStep::SetComplete:
                return "SetComplete";
            case AppearanceTraceStep::Damage:
                return "Damage";
            case AppearanceTraceStep::Rule:
                return "Rule";
            case AppearanceTraceStep::Output:
                return "Output";
        }
        return "Unknown";
    }

    const ResolvedPart* ResolvedAppearance::findPart( const hashed_string& owner, const hashed_string& partName ) const
    {
        for ( const ResolvedPart& part : _listPart )
        {
            if ( part._owner == owner && part._partName == partName )
                return &part;
        }
        return nullptr;
    }

    bool ResolvedAppearance::hasOwner( const hashed_string& owner ) const
    {
        for ( const ResolvedPart& part : _listPart )
        {
            if ( part._owner == owner )
                return true;
        }
        return false;
    }

    bool ResolvedAppearance::isRegionHidden( const hashed_string& region ) const
    {
        return AppearanceXMLUtil::containsName( _listHiddenRegion, region );
    }

    const ResolvedMorph* ResolvedAppearance::findMorph( const hashed_string& owner, const hashed_string& name ) const
    {
        for ( const ResolvedMorph& morph : _listMorph )
        {
            if ( morph._owner == owner && morph._name == name )
                return &morph;
        }
        return nullptr;
    }

    uint32 ResolvedAppearance::countTrace( const hashed_string& source ) const
    {
        uint32 count = 0;
        for ( const AppearanceTraceEntry& entry : _listTrace )
        {
            if ( entry._source == source )
                ++count;
        }
        return count;
    }

    float32 AppearanceResolver::snapDamage( float32 damage )
    {
        return static_cast<float32>( static_cast<uint32>( MathUtil::saturate( damage ) * 255.0f + 0.5f ) ) / 255.0f;
    }

    void AppearanceResolver::resolve( const AppearanceDatabase& database, const CharacterAppearanceSpec& spec, ResolvedAppearance& outResolved )
    {
        using Internal         = AppearanceResolverInternal;
        using Entry            = AppearanceResolverInternal::Entry;
        outResolved            = ResolvedAppearance{};
        outResolved._presetID  = spec._presetID;
        outResolved._bodyType  = spec._bodyType;
        outResolved._bodyShape = spec._bodyShape;
        outResolved._face      = spec._face;

        // 1) 몸 + 캐릭터 꾸미기.
        vector<Entry>        listEntry;
        int32                bodyIndex = -1;
        const ItemVisualDef* pBody     = database.getVisuals().findVisual( spec._bodyVisual );
        if ( pBody != nullptr )
        {
            Entry body;
            body._pVisual = pBody;
            body._state   = Internal::getDefaultState( *pBody );
            listEntry.push_back( body );
            bodyIndex = 0;
        }
        const CustomizationSchemaDef* pSchema = database.getSchemas().findSchema( spec._schema );
        if ( pSchema != nullptr )
        {
            CustomizationValueSet normalized;
            vector<hashed_string> listDropped;
            CustomizationUtil::normalize( *pSchema, spec._customization, normalized, &listDropped );
            for ( const hashed_string& dropped : listDropped )
            {
                Internal::addTrace( outResolved, AppearanceTraceStep::Customization, dropped, "'%#' is not in schema '%#' - dropped", dropped.c_str(), pSchema->_id.c_str() );
            }
            vector<const ItemVisualDef*> listUnused;
            Internal::applyCustomization( *pSchema, normalized, hashed_string{}, bodyIndex, database, listEntry, listUnused, outResolved );
        }

        // 2) 칸 — 형상 변경 · 숨김.
        const ItemCatalog* pItemCatalog = database.getItemCatalog();
        for ( const AppearanceSlotRequest& request : spec._listSlot )
        {
            if ( request._itemID.empty() )
                continue;
            const ItemDef*       pItem    = pItemCatalog != nullptr ? pItemCatalog->findItem( request._itemID ) : nullptr;
            const hashed_string  visualID = request._visibleVisual.empty() == false ? request._visibleVisual : ( pItem != nullptr ? pItem->_visualID : hashed_string{} );
            const ItemVisualDef* pVisual  = database.getVisuals().findVisual( visualID );
            if ( pVisual == nullptr )
            {
                Internal::addTrace( outResolved, AppearanceTraceStep::Output, request._slot, "item '%#' has no visual", request._itemID.c_str() );
                continue;
            }
            Entry entry;
            entry._pVisual  = pVisual;
            entry._pRequest = &request;
            entry._owner    = request._slot;
            entry._itemID   = request._itemID;
            entry._state    = request._state.empty() ? Internal::getDefaultState( *pVisual ) : request._state;
            if ( request._visibleVisual.empty() == false )
                Internal::addTrace( outResolved, AppearanceTraceStep::Output, request._slot, "'%#' shows visual '%#' (transmog)", request._itemID.c_str(), visualID.c_str() );
            if ( request._bSuppressed == SW_TRUE )
            {
                entry._bHidden = SW_TRUE;
                Internal::addTrace( outResolved, AppearanceTraceStep::Output, request._slot, "'%#' hidden - its equip condition is broken", request._itemID.c_str() );
            }
            listEntry.push_back( entry );
        }

        // 3) 칸 점유 — 표 순서로, 먼저 보이는 장비의 점유가 이긴다.
        for ( size_t index = 0; index < listEntry.size(); ++index )
        {
            const Entry& occupier = listEntry[index];
            if ( occupier._pRequest == nullptr || occupier._bHidden == SW_TRUE || occupier._pVisual->_occupancy.empty() )
                continue;
            const SlotOccupancyDef* pOccupancy = database.getSlotTable().findOccupancy( occupier._pVisual->_occupancy );
            if ( pOccupancy == nullptr )
                continue;
            for ( const hashed_string& slot : pOccupancy->_listSlot )
            {
                const int32 otherIndex = slot == occupier._owner ? -1 : Internal::findEntry( listEntry, slot, true );
                if ( otherIndex < 0 )
                    continue;
                listEntry[static_cast<size_t>( otherIndex )]._bHidden = SW_TRUE;
                Internal::addTrace( outResolved, AppearanceTraceStep::Occupancy, occupier._owner, "'%#' occupies slot '%#' - '%#' hidden", occupier._itemID.c_str(), slot.c_str(),
                                    listEntry[static_cast<size_t>( otherIndex )]._itemID.c_str() );
            }
        }

        // 4) 세트 완성 표현 — 요청된 아이템(숨김 정책으로 꺼진 것은 빼고)으로 판정한다.
        for ( const EquipSetDef& set : database.getSets().getSets() )
        {
            const ItemVisualDef* pCompleteVisual = set._completeVisual.empty() ? nullptr : database.getVisuals().findVisual( set._completeVisual );
            if ( pCompleteVisual == nullptr )
                continue;
            const EquipSetProgress progress = database.getSets().computeProgressWith( set, spec._bodyType, [&]( const hashed_string& slot )
            {
                const AppearanceSlotRequest* pRequest = spec.findSlot( slot );
                return pRequest == nullptr || pRequest->_bSuppressed == SW_TRUE ? hashed_string{} : pRequest->_itemID;
            } );
            if ( progress.isComplete() == false )
                continue;
            for ( const hashed_string& slot : set._listCompleteSlot )
            {
                const int32 replacedIndex = Internal::findEntry( listEntry, slot, true );
                if ( replacedIndex >= 0 )
                    listEntry[static_cast<size_t>( replacedIndex )]._bHidden = SW_TRUE;
            }
            Entry complete;
            complete._pVisual = pCompleteVisual;
            complete._owner   = set._listCompleteSlot.front();
            complete._state   = Internal::getDefaultState( *pCompleteVisual );
            listEntry.push_back( complete );
            Internal::addTrace( outResolved, AppearanceTraceStep::SetComplete, set._id, "set complete - visual '%#' replaces %# slot(s)", set._completeVisual.c_str(),
                                static_cast<int32>( set._listCompleteSlot.size() ) );
        }

        // 5) 규칙 — 지금(점유 · 세트 뒤, 규칙 동작 전)의 목록으로만 판정한다.
        const vector<Entry>              listSnapshot = listEntry;
        vector<Internal::Decision>       listDecision;
        vector<const AppearanceRuleDef*> listFired;
        for ( const AppearanceRuleDef& rule : database.getRules().getRules() )
        {
            bool bFires = true;
            for ( const AppearanceRuleCondition& condition : rule._listCondition )
            {
                bFires = bFires && Internal::isConditionMet( condition, listSnapshot, spec );
            }
            if ( bFires == false )
                continue;
            listFired.push_back( &rule );
            Internal::addTrace( outResolved, AppearanceTraceStep::Rule, rule._id, "fired (priority %#)", rule._priority );
            for ( const AppearanceRuleAction& action : rule._listAction )
            {
                if ( action.isHide() == false )
                    Internal::decide( rule, action, listDecision, outResolved );
            }
        }
        for ( const AppearanceRuleDef* pRule : listFired )
        {
            for ( const AppearanceRuleAction& action : pRule->_listAction )
            {
                if ( action._kind == AppearanceRuleActionKind::HideRegion )
                {
                    Internal::addHiddenRegion( outResolved, action._name );
                    Internal::addTrace( outResolved, AppearanceTraceStep::Rule, pRule->_id, "body region '%#' hidden", action._name.c_str() );
                    continue;
                }
                if ( action.isHide() == false )
                    continue;
                for ( Entry& entry : listEntry )
                {
                    const bool bMatches = action._kind == AppearanceRuleActionKind::HideTarget ? entry._owner == action._target : entry._pVisual->_tags.hasTag( action._tag );
                    if ( bMatches && entry._bHidden == SW_FALSE && entry._owner.empty() == false )
                    {
                        entry._bHidden = SW_TRUE;
                        Internal::addTrace( outResolved, AppearanceTraceStep::Rule, pRule->_id, "'%#' hidden", entry._owner.c_str() );
                    }
                }
            }
        }

        // 6) 출력 — 몸 → 꾸미기 외형 → 칸 순서.
        for ( size_t index = 0; index < listEntry.size(); ++index )
        {
            if ( listEntry[index]._bHidden == SW_TRUE )
                continue;
            Internal::emitEntry( database, listEntry[index], listEntry, static_cast<int32>( index ), listDecision, outResolved );
        }
        for ( const Internal::Decision& decision : listDecision )
        {
            const AppearanceRuleAction& action = *decision._pAction;
            if ( action._kind == AppearanceRuleActionKind::ApplyMorph )
            {
                Internal::setMorph( outResolved, action._target, action._name, action._weight );
                Internal::addTrace( outResolved, AppearanceTraceStep::Rule, decision._pRule->_id, "morph '%#' = %#", action._name.c_str(), action._weight );
            }
            else if ( action._kind == AppearanceRuleActionKind::ChooseVariant )
            {
                Internal::addTrace( outResolved, AppearanceTraceStep::Rule, decision._pRule->_id, "variant '%#' chosen for '%#'", action._name.c_str(), action._target.c_str() );
            }
        }
        outResolved._listSocketOverride = spec._listSocketOverride;
        for ( const Internal::Decision& decision : listDecision )
        {
            const AppearanceRuleAction& action = *decision._pAction;
            if ( action._kind != AppearanceRuleActionKind::OverrideSocket )
                continue;
            AppearanceSocketOverride socketOverride;
            socketOverride._name      = action._name;
            socketOverride._placement = action._placement;
            bool bReplaced            = false;
            for ( AppearanceSocketOverride& existing : outResolved._listSocketOverride )
            {
                if ( existing._name == socketOverride._name )
                {
                    existing  = socketOverride;
                    bReplaced = true;
                }
            }
            if ( bReplaced == false )
                outResolved._listSocketOverride.push_back( socketOverride );
            Internal::addTrace( outResolved, AppearanceTraceStep::Rule, decision._pRule->_id, "socket '%#' overridden", action._name.c_str() );
        }
        outResolved._meshHash = computeMeshHash( outResolved );
        outResolved._hash     = computeHash( outResolved );
    }

    uint64 AppearanceResolver::computeMeshHash( const ResolvedAppearance& resolved )
    {
        AppearanceResolverInternal::HashBuilder builder;
        builder.addName( resolved._bodyType );
        builder.addName( resolved._bodyShape );
        builder.addName( resolved._face );
        for ( const ResolvedPart& part : resolved._listPart )
        {
            if ( part._kind != AppearancePartKind::Skinned )
                continue;
            builder.addName( part._owner );
            builder.addName( part._partName );
            builder.addName( part._asset );
            builder.addName( part._material );
            builder.addName( part._skeleton );
            builder.addInt( part._bDeforms );
        }
        for ( const ResolvedMorph& morph : resolved._listMorph )
        {
            builder.addName( morph._owner );
            builder.addName( morph._name );
            builder.addFloat( morph._weight );
        }
        for ( const ResolvedMaterialValue& value : resolved._listMaterialValue )
        {
            builder.addName( value._owner );
            builder.addName( value._part );
            builder.addName( value._name );
            builder.addFloat4( value._value );
            builder.addInt( value._channel );
            builder.addInt( static_cast<int32>( value._target ) );
        }
        for ( const ResolvedBoneProportion& proportion : resolved._listBoneProportion )
        {
            builder.addName( proportion._owner );
            builder.addName( proportion._name );
            builder.addFloat( proportion._value );
        }
        for ( const hashed_string& region : resolved._listHiddenRegion )
        {
            builder.addName( region );
        }
        return builder._value;
    }

    uint64 AppearanceResolver::computeHash( const ResolvedAppearance& resolved )
    {
        AppearanceResolverInternal::HashBuilder builder;
        builder.addUint64( computeMeshHash( resolved ) );
        builder.addName( resolved._presetID );
        for ( const ResolvedPart& part : resolved._listPart )
        {
            builder.addName( part._owner );
            builder.addName( part._itemID );
            builder.addName( part._visualID );
            builder.addName( part._partName );
            builder.addName( part._variant );
            builder.addName( part._materialVariant );
            builder.addName( part._asset );
            builder.addName( part._material );
            builder.addName( part._skeleton );
            builder.addName( part._socketSet );
            builder.addName( part._state );
            builder.addName( part._damageStage );
            builder.addInt( part._layer );
            builder.addInt( static_cast<int32>( part._kind ) );
            builder.addInt( part._bDeforms );
            builder.addPlacement( part._placement );
        }
        for ( const ResolvedAttachment& attachment : resolved._listAttachment )
        {
            builder.addName( attachment._owner );
            builder.addName( attachment._parameter );
            builder.addName( attachment._option );
            builder.addName( attachment._asset );
            builder.addPlacement( attachment._placement );
        }
        for ( const ResolvedSocketSource& source : resolved._listSocketSource )
        {
            builder.addName( source._owner );
            builder.addName( source._partName );
            builder.addName( source._socketSet );
        }
        for ( const AppearanceSocketOverride& socketOverride : resolved._listSocketOverride )
        {
            builder.addName( socketOverride._name );
            builder.addPlacement( socketOverride._placement );
        }
        for ( const ResolvedDetachedPart& detached : resolved._listDetachedPart )
        {
            builder.addName( detached._owner );
            builder.addName( detached._itemID );
            builder.addName( detached._partName );
            builder.addFloat3( detached._impulse );
            builder.addInt( detached._bFromDamageStage );
        }
        return builder._value;
    }

    void AppearanceInputUtil::applyEquipment( const Equipment& equipment, CharacterAppearanceSpec& inoutSpec )
    {
        const ItemCatalog* pCatalog = equipment.getCatalog();
        for ( AppearanceSlotRequest& request : inoutSpec._listSlot )
        {
            const EquipSlot* pEquipSlot = nullptr;
            for ( const EquipSlot& equipSlot : equipment.getSlots() )
            {
                if ( equipSlot._name == request._slot )
                    pEquipSlot = &equipSlot;
            }
            const bool bEmpty = pEquipSlot == nullptr || pEquipSlot->_item.isEmpty();
            request._itemID   = bEmpty ? hashed_string{} : pEquipSlot->_item._itemID;
            if ( bEmpty )
            {
                request._customization.clear();
                request._listDetachedPart.clear();
                request._damage      = 0.0f;
                request._bSuppressed = SW_FALSE;
                continue;
            }
            const InventorySlot& item = pEquipSlot->_item;
            const ItemDef*       pDef = pCatalog != nullptr ? pCatalog->findItem( item._itemID ) : nullptr;
            const float32        worn = pDef != nullptr && pDef->_maxDurability > 0.0f ? 1.0f - MathUtil::saturate( item._durability / pDef->_maxDurability ) : 0.0f;
            request._customization    = item._customization;
            request._listDetachedPart = item._listDetachedPart;
            request._damage           = AppearanceResolver::snapDamage( MathUtil::max( item._damage, worn ) );
            request._bSuppressed      = pEquipSlot->_bSuppressed;
        }
    }
} // namespace sw
