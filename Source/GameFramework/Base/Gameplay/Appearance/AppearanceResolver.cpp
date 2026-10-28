#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceResolver.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Container/formatString.h"
#include "Core/Container/unordered_set.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/EnumName.h"

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
            /** @brief 해석 중인 외형 하나입니다. 몸, 꾸미기 외형, 슬롯의 장비, 세트 완성 표현이 있습니다. */
            struct Entry
            {
                const ItemVisualDef*         _pVisual{ nullptr };
                const AppearanceSlotRequest* _pRequest{ nullptr }; ///< 슬롯의 장비일 때
                hashed_string                _owner{};
                hashed_string                _itemID{};
                hashed_string                _state{};
                hashed_string                _customVariant{};         ///< 꾸미기 고르기가 정한 메시 변형
                hashed_string                _customMaterialVariant{}; ///< 꾸미기 고르기가 정한 머티리얼 변형
                uint8                        _bHidden{ SW_FALSE };
            };

            /** @brief 규칙 동작 하나의 판정입니다. 같은 대상을 두고 이긴 쪽을 담습니다. */
            struct Decision
            {
                const AppearanceRuleAction* _pAction{ nullptr };
                const AppearanceRuleDef*    _pRule{ nullptr };
            };

            /** @brief FNV-1a 64 로 값을 누적합니다. 이름은 내용 해시(대소문자 무시)이므로 프로세스나 기계가 달라도 같은 값이 나옵니다. */
            struct HashBuilder
            {
                uint64 _value{ HashUtil::kFNVOffset64 };

                void addUint64( uint64 value )
                {
                    for ( uint32 byteIndex = 0; byteIndex < 8; ++byteIndex )
                    {
                        _value = HashUtil::fnvStep64( _value, ( value >> ( byteIndex * 8 ) ) & 0xffu );
                    }
                }
                void addName( const hashed_string& name ) { addUint64( name.empty() ? 0ull : static_cast<uint64>( name.getHash() ) ); }
                void addInt( int32 value ) { addUint64( static_cast<uint64>( static_cast<uint32>( value ) ) ); }
                void addFloat( float32 value )
                {
                    const float32 canonical = value == 0.0f ? 0.0f : value; // -0 과 +0 을 같은 값으로 취급한다
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
                                          ResolvedMaterialTarget target, int32 channel, ResolvedMaterialShape shape = ResolvedMaterialShape::Vector )
            {
                ResolvedMaterialValue entry;
                entry._shape   = shape;
                entry._owner   = owner;
                entry._part    = part;
                entry._name    = name;
                entry._value   = value;
                entry._target  = target;
                entry._channel = channel;
                out._listMaterialValue.push_back( entry );
            }

            /** @brief 숨길 영역을 더합니다. 중복은 해석 끝에서 한 번 거른다(`removeRepeatedHiddenRegions`). */
            static void addHiddenRegion( ResolvedAppearance& out, const hashed_string& region ) { out._listHiddenRegion.push_back( region ); }

            /** @brief 숨길 영역의 중복을 처음 나온 자리를 지켜 지웁니다 — 순서가 해시에 들어간다. */
            static void removeRepeatedHiddenRegions( ResolvedAppearance& out )
            {
                unordered_set<hashed_string> uniqueSetSeen;
                VectorUtil::removeIf( out._listHiddenRegion, [&uniqueSetSeen]( const hashed_string& region )
                { return uniqueSetSeen.insert( region ).second == false; } );
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
             * @brief 꾸미기 값을 적용합니다. 슬라이더와 색은 모프, 본 비율, 머티리얼에 적용하고, 고르기는 변형과 외형에, 부착은 소켓 부착물에 적용합니다. @p ownerIndex 는 그 주인의 항목입니다.
             * @details 몸(주인이 빔)의 고르기 외형은 매개변수 이름을 주인으로 하는 새 항목이 된다. 아이템의 고르기 외형은 같은 주인으로 @p outListExtraVisual 에 담겨 나온다.
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
                                        addMaterialValue( out, owner, hashed_string{}, drive._target, float4( driven, 0.0f, 0.0f, 0.0f ), ResolvedMaterialTarget::Parameter, 0,
                                                          ResolvedMaterialShape::Scalar );
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
                            const bool bTagVisible   = entry._bHidden == SW_FALSE && bOwnerMatches && entry._pVisual->_tags.hasTag( condition._tag );
                            if ( bTagVisible )
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
                        bMet = VectorUtil::contains( condition._listName, spec._bodyShape );
                        break;
                    }
                    case AppearanceRuleConditionKind::BodyType:
                    {
                        bMet = VectorUtil::contains( condition._listName, spec._bodyType );
                        break;
                    }
                }
                return bMet != ( condition._bNegate == SW_TRUE );
            }

            /** @brief 규칙의 숨김이 아닌 동작을 대상별 판정에 넣습니다. 우선순위가 높은 쪽이 이기고, 같으면 먼저 온 규칙이 이깁니다. 로드 단계에서 우선순위가 같은 충돌 짝을 막습니다. */
            static void decide( const AppearanceRuleDef& rule, const AppearanceRuleAction& action, vector<Decision>& inoutListDecision, ResolvedAppearance& out )
            {
                for ( Decision& decision : inoutListDecision )
                {
                    if ( decision._pAction->hasSameTarget( action ) == false )
                        continue;
                    if ( rule._priority > decision._pRule->_priority )
                    {
                        addTrace( out, AppearanceTraceStep::Rule, decision._pRule->_id, "%# overridden by rule '%#' (priority %# > %#)", getEnumName( action._kind ), rule._id.c_str(),
                                  rule._priority, decision._pRule->_priority );
                        decision._pAction = &action;
                        decision._pRule   = &rule;
                    }
                    else
                    {
                        addTrace( out, AppearanceTraceStep::Rule, rule._id, "%# overridden by rule '%#'", getEnumName( action._kind ), decision._pRule->_id.c_str() );
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

            /** @brief 상태가 바꾼 부품 배치입니다. 상태가 없거나 그 부품의 배치가 없으면 부품의 기본 배치입니다. */
            static AppearancePlacement findPartPlacement( const AppearanceStateDef* pState, const AppearancePartDef& part )
            {
                AppearancePlacement placement = part._placement;
                if ( pState == nullptr )
                    return placement;
                for ( const AppearanceStateDef::Placement& statePlacement : pState->_listPlacement )
                {
                    if ( statePlacement._part == part._name )
                        placement = statePlacement._placement;
                }
                return placement;
            }

            /** @brief 맞아서 떨어지거나 피해 단계로 부서진 부품이면 떨어진 부품으로 내보내고 true 입니다. */
            static bool emitDetachedPart( const ItemVisualDef& visual, const Entry& entry, const AppearancePartDef& part, const AppearancePlacement& placement, int32 stageIndex,
                                          ResolvedAppearance& out )
            {
                const bool  bHitOff    = entry._pRequest != nullptr && VectorUtil::contains( entry._pRequest->_listDetachedPart, part._name );
                const int32 breakIndex = part._breakStage.empty() ? -1 : visual.findDamageStageIndex( part._breakStage );
                const bool  bStageOff  = breakIndex >= 0 && stageIndex >= breakIndex;
                if ( bHitOff && part._bBreakable == SW_FALSE )
                    addTrace( out, AppearanceTraceStep::Damage, entry._owner, "part '%#' is not breakable - detach request ignored", part._name.c_str() );
                const bool bDetached = part._bBreakable == SW_TRUE && ( bHitOff || bStageOff );
                if ( bDetached == false )
                    return false;
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
                return true;
            }

            /** @brief 몸 변형 부품의 모프, 머티리얼 값, 숨김 영역을 몸(주인이 빔)에 적용합니다. */
            static void emitBodyModification( const ItemVisualDef& visual, const Entry& entry, const AppearancePartDef& part, ResolvedAppearance& out )
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
            }

            /**
             * @brief 보이는 부품 하나를 해석합니다. 메시 변형, 머티리얼 변형, 규칙의 메시와 머티리얼 교체를 적용합니다.
             * @details 메시 변형의 우선순위는 규칙, 피해 단계, 꾸미기 순이다. 부품에 없는 변형 이름은 그 부품에 아무 영향을 주지 않는다.
             */
            static ResolvedPart resolvePart( const ItemVisualDef& visual, const Entry& entry, const AppearancePartDef& part, const AppearancePlacement& placement,
                                             const AppearanceDamageStageDef* pStage, const Decision* pRuleVariant, const vector<Decision>& listDecision,
                                             ResolvedAppearance& out )
            {
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
                resolved._rig         = part._rig;
                resolved._fit         = part._fit;
                resolved._fitTables   = part._fitTables;
                resolved._bodyShapes  = part._bodyShapes;
                resolved._layer       = part._layer;
                resolved._bDeforms    = part._bDeforms;
                resolved._state       = entry._state;
                resolved._placement   = placement;
                resolved._damageStage = pStage != nullptr ? pStage->_name : hashed_string{};

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
                return resolved;
            }

            static void emitParts( const ItemVisualDef& visual, const Entry& entry, int32 stageIndex, const vector<Decision>& listDecision, ResolvedAppearance& out )
            {
                const AppearanceStateDef*       pState       = visual.findState( entry._state );
                const AppearanceDamageStageDef* pStage       = stageIndex >= 0 ? &visual._listDamageStage[static_cast<size_t>( stageIndex )] : nullptr;
                const Decision*                 pRuleVariant = findDecision( listDecision, AppearanceRuleActionKind::ChooseVariant, entry._owner, hashed_string{} );
                for ( const AppearancePartDef& part : visual._listPart )
                {
                    if ( pState != nullptr && VectorUtil::contains( pState->_listHiddenPart, part._name ) )
                        continue;
                    const AppearancePlacement placement = findPartPlacement( pState, part );
                    if ( emitDetachedPart( visual, entry, part, placement, stageIndex, out ) )
                        continue;
                    if ( part._kind == AppearancePartKind::BodyModification )
                    {
                        emitBodyModification( visual, entry, part, out );
                        continue;
                    }
                    const ResolvedPart resolved = resolvePart( visual, entry, part, placement, pStage, pRuleVariant, listDecision, out );
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

            /** @brief 1 단계입니다. 몸 항목을 넣고 캐릭터 꾸미기를 적용합니다. */
            static void addBodyEntries( const AppearanceDatabase& database, const CharacterAppearanceSpec& spec, vector<Entry>& inoutListEntry, ResolvedAppearance& out )
            {
                int32                bodyIndex = -1;
                const ItemVisualDef* pBody     = database.getVisuals().findVisual( spec._bodyVisual );
                if ( pBody != nullptr )
                {
                    Entry body;
                    body._pVisual = pBody;
                    body._state   = getDefaultState( *pBody );
                    inoutListEntry.push_back( body );
                    bodyIndex = static_cast<int32>( inoutListEntry.size() ) - 1;
                }
                const CustomizationSchemaDef* pSchema = database.getSchemas().findSchema( spec._schema );
                if ( pSchema == nullptr )
                    return;
                CustomizationValueSet normalized;
                vector<hashed_string> listDropped;
                CustomizationUtil::normalize( *pSchema, spec._customization, normalized, &listDropped );
                for ( const hashed_string& dropped : listDropped )
                {
                    addTrace( out, AppearanceTraceStep::Customization, dropped, "'%#' is not in schema '%#' - dropped", dropped.c_str(), pSchema->_id.c_str() );
                }
                vector<const ItemVisualDef*> listUnused;
                applyCustomization( *pSchema, normalized, hashed_string{}, bodyIndex, database, inoutListEntry, listUnused, out );
            }

            /** @brief 2 단계입니다. 슬롯마다 장비 항목을 넣습니다. 형상 변경(transmog)과 조건이 깨진 장비의 숨김을 여기서 정합니다. */
            static void addSlotEntries( const AppearanceDatabase& database, const CharacterAppearanceSpec& spec, vector<Entry>& inoutListEntry, ResolvedAppearance& out )
            {
                const ItemCatalog* pItemCatalog = database.getItemCatalog();
                for ( const AppearanceSlotRequest& request : spec._listSlot )
                {
                    if ( request._itemID.empty() )
                        continue;
                    const ItemDef*       pItem    = pItemCatalog != nullptr ? pItemCatalog->find( request._itemID ) : nullptr;
                    const hashed_string  visualID = request._visibleVisual.empty() == false ? request._visibleVisual : ( pItem != nullptr ? pItem->_visualID : hashed_string{} );
                    const ItemVisualDef* pVisual  = database.getVisuals().findVisual( visualID );
                    if ( pVisual == nullptr )
                    {
                        addTrace( out, AppearanceTraceStep::Output, request._slot, "item '%#' has no visual", request._itemID.c_str() );
                        continue;
                    }
                    Entry entry;
                    entry._pVisual  = pVisual;
                    entry._pRequest = &request;
                    entry._owner    = request._slot;
                    entry._itemID   = request._itemID;
                    entry._state    = request._state.empty() ? getDefaultState( *pVisual ) : request._state;
                    if ( request._visibleVisual.empty() == false )
                        addTrace( out, AppearanceTraceStep::Output, request._slot, "'%#' shows visual '%#' (transmog)", request._itemID.c_str(), visualID.c_str() );
                    if ( request._bSuppressed == SW_TRUE )
                    {
                        entry._bHidden = SW_TRUE;
                        addTrace( out, AppearanceTraceStep::Output, request._slot, "'%#' hidden - its equip condition is broken", request._itemID.c_str() );
                    }
                    inoutListEntry.push_back( entry );
                }
            }

            /** @brief 3 단계입니다. 슬롯 점유를 테이블 순서대로 보며, 먼저 보이는 장비의 점유가 이깁니다. */
            static void applyOccupancy( const AppearanceDatabase& database, vector<Entry>& inoutListEntry, ResolvedAppearance& out )
            {
                for ( size_t index = 0; index < inoutListEntry.size(); ++index )
                {
                    const Entry& occupier      = inoutListEntry[index];
                    const bool   bSkipOccupier = occupier._pRequest == nullptr || occupier._bHidden == SW_TRUE || occupier._pVisual->_occupancy.empty();
                    if ( bSkipOccupier )
                        continue;
                    const SlotOccupancyDef* pOccupancy = database.getSlotTable().findOccupancy( occupier._pVisual->_occupancy );
                    if ( pOccupancy == nullptr )
                        continue;
                    for ( const hashed_string& slot : pOccupancy->_listSlot )
                    {
                        const int32 otherIndex = slot == occupier._owner ? -1 : findEntry( inoutListEntry, slot, true );
                        if ( otherIndex < 0 )
                            continue;
                        inoutListEntry[static_cast<size_t>( otherIndex )]._bHidden = SW_TRUE;
                        addTrace( out, AppearanceTraceStep::Occupancy, occupier._owner, "'%#' occupies slot '%#' - '%#' hidden", occupier._itemID.c_str(), slot.c_str(),
                                  inoutListEntry[static_cast<size_t>( otherIndex )]._itemID.c_str() );
                    }
                }
            }

            /** @brief 4 단계입니다. 세트 완성 표현을 요청된 아이템으로 판정합니다. 숨김 정책으로 꺼진 장비는 세지 않습니다. */
            static void applySetCompletion( const AppearanceDatabase& database, const CharacterAppearanceSpec& spec, vector<Entry>& inoutListEntry, ResolvedAppearance& out )
            {
                for ( const EquipSetDef& set : database.getSets().getSets() )
                {
                    const ItemVisualDef* pCompleteVisual = set._completeVisual.empty() ? nullptr : database.getVisuals().findVisual( set._completeVisual );
                    if ( pCompleteVisual == nullptr )
                        continue;
                    const EquipSetProgress progress = database.getSets().computeProgressWith( set, spec._bodyType, [&spec]( const hashed_string& slot )
                    {
                        const AppearanceSlotRequest* pRequest = spec.findSlot( slot );
                        return pRequest == nullptr || pRequest->_bSuppressed == SW_TRUE ? hashed_string{} : pRequest->_itemID;
                    } );
                    if ( progress.isComplete() == false )
                        continue;
                    for ( const hashed_string& slot : set._listCompleteSlot )
                    {
                        const int32 replacedIndex = findEntry( inoutListEntry, slot, true );
                        if ( replacedIndex >= 0 )
                            inoutListEntry[static_cast<size_t>( replacedIndex )]._bHidden = SW_TRUE;
                    }
                    Entry complete;
                    complete._pVisual = pCompleteVisual;
                    complete._owner   = set._listCompleteSlot.front();
                    complete._state   = getDefaultState( *pCompleteVisual );
                    inoutListEntry.push_back( complete );
                    addTrace( out, AppearanceTraceStep::SetComplete, set._id, "set complete - visual '%#' replaces %# slot(s)", set._completeVisual.c_str(),
                              static_cast<int32>( set._listCompleteSlot.size() ) );
                }
            }

            /**
             * @brief 5 단계입니다. 규칙을 판정하고, 숨김 동작은 바로 적용하며, 나머지 동작은 대상별 판정 @p outListDecision 에 모읍니다.
             * @details 조건은 지금의 목록(점유와 세트 처리 뒤, 규칙 동작 전)으로만 판정한다. 그래서 규칙끼리 서로의 숨김에 영향을 주지 않는다.
             */
            static void applyRules( const AppearanceDatabase& database, const CharacterAppearanceSpec& spec, vector<Entry>& inoutListEntry, vector<Decision>& outListDecision,
                                    ResolvedAppearance& out )
            {
                const vector<Entry>              listSnapshot = inoutListEntry;
                vector<const AppearanceRuleDef*> listFired;
                for ( const AppearanceRuleDef& rule : database.getRules().getRules() )
                {
                    bool bFires = true;
                    for ( const AppearanceRuleCondition& condition : rule._listCondition )
                    {
                        bFires = bFires && isConditionMet( condition, listSnapshot, spec );
                    }
                    if ( bFires == false )
                        continue;
                    listFired.push_back( &rule );
                    addTrace( out, AppearanceTraceStep::Rule, rule._id, "fired (priority %#)", rule._priority );
                    for ( const AppearanceRuleAction& action : rule._listAction )
                    {
                        if ( action.isHide() == false )
                            decide( rule, action, outListDecision, out );
                    }
                }
                for ( const AppearanceRuleDef* pRule : listFired )
                {
                    for ( const AppearanceRuleAction& action : pRule->_listAction )
                    {
                        if ( action._kind == AppearanceRuleActionKind::HideRegion )
                        {
                            addHiddenRegion( out, action._name );
                            addTrace( out, AppearanceTraceStep::Rule, pRule->_id, "body region '%#' hidden", action._name.c_str() );
                            continue;
                        }
                        if ( action.isHide() == false )
                            continue;
                        for ( Entry& entry : inoutListEntry )
                        {
                            const bool bMatches   = action._kind == AppearanceRuleActionKind::HideTarget ? entry._owner == action._target : entry._pVisual->_tags.hasTag( action._tag );
                            const bool bHideEntry = bMatches && entry._bHidden == SW_FALSE && entry._owner.empty() == false;
                            if ( bHideEntry )
                            {
                                entry._bHidden = SW_TRUE;
                                addTrace( out, AppearanceTraceStep::Rule, pRule->_id, "'%#' hidden", entry._owner.c_str() );
                            }
                        }
                    }
                }
            }

            /** @brief 부품 밖으로 나가는 판정(모프, 고른 변형의 기록, 소켓 덮어쓰기)을 출력에 적용합니다. 소켓 덮어쓰기는 요청의 것 위에 같은 이름을 바꿉니다. */
            static void emitDecisions( const CharacterAppearanceSpec& spec, const vector<Decision>& listDecision, ResolvedAppearance& out )
            {
                for ( const Decision& decision : listDecision )
                {
                    const AppearanceRuleAction& action = *decision._pAction;
                    if ( action._kind == AppearanceRuleActionKind::ApplyMorph )
                    {
                        setMorph( out, action._target, action._name, action._weight );
                        addTrace( out, AppearanceTraceStep::Rule, decision._pRule->_id, "morph '%#' = %#", action._name.c_str(), action._weight );
                    }
                    else if ( action._kind == AppearanceRuleActionKind::ChooseVariant )
                    {
                        addTrace( out, AppearanceTraceStep::Rule, decision._pRule->_id, "variant '%#' chosen for '%#'", action._name.c_str(), action._target.c_str() );
                    }
                }
                out._listSocketOverride = spec._listSocketOverride;
                for ( const Decision& decision : listDecision )
                {
                    const AppearanceRuleAction& action = *decision._pAction;
                    if ( action._kind != AppearanceRuleActionKind::OverrideSocket )
                        continue;
                    AppearanceSocketOverride socketOverride;
                    socketOverride._name      = action._name;
                    socketOverride._placement = action._placement;
                    bool bReplaced            = false;
                    for ( AppearanceSocketOverride& existing : out._listSocketOverride )
                    {
                        if ( existing._name == socketOverride._name )
                        {
                            existing  = socketOverride;
                            bReplaced = true;
                        }
                    }
                    if ( bReplaced == false )
                        out._listSocketOverride.push_back( socketOverride );
                    addTrace( out, AppearanceTraceStep::Rule, decision._pRule->_id, "socket '%#' overridden", action._name.c_str() );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const ResolvedPart* ResolvedAppearance::findPart( const hashed_string& owner, const hashed_string& partName ) const
    {
        return VectorUtil::findIf( _listPart, [&]( const ResolvedPart& part )
        { return part._owner == owner && part._partName == partName; } );
    }

    bool ResolvedAppearance::hasOwner( const hashed_string& owner ) const
    {
        return VectorUtil::containsBy<&ResolvedPart::_owner>( _listPart, owner );
    }

    bool ResolvedAppearance::isRegionHidden( const hashed_string& region ) const
    {
        return VectorUtil::contains( _listHiddenRegion, region );
    }

    const ResolvedMorph* ResolvedAppearance::findMorph( const hashed_string& owner, const hashed_string& name ) const
    {
        return VectorUtil::findIf( _listMorph, [&]( const ResolvedMorph& morph )
        { return morph._owner == owner && morph._name == name; } );
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
        outResolved            = ResolvedAppearance{};
        outResolved._presetID  = spec._presetID;
        outResolved._bodyType  = spec._bodyType;
        outResolved._bodyShape = spec._bodyShape;
        outResolved._face      = spec._face;

        // 단계 순서가 결과다. 뒤 단계는 앞 단계가 숨긴 항목을 보지 않는다.
        vector<Internal::Entry> listEntry;
        Internal::addBodyEntries( database, spec, listEntry, outResolved );
        Internal::addSlotEntries( database, spec, listEntry, outResolved );
        Internal::applyOccupancy( database, listEntry, outResolved );
        Internal::applySetCompletion( database, spec, listEntry, outResolved );
        vector<Internal::Decision> listDecision;
        Internal::applyRules( database, spec, listEntry, listDecision, outResolved );

        // 출력은 몸, 꾸미기 외형, 슬롯 순서다.
        for ( size_t index = 0; index < listEntry.size(); ++index )
        {
            if ( listEntry[index]._bHidden == SW_TRUE )
                continue;
            Internal::emitEntry( database, listEntry[index], listEntry, static_cast<int32>( index ), listDecision, outResolved );
        }
        Internal::emitDecisions( spec, listDecision, outResolved );
        Internal::removeRepeatedHiddenRegions( outResolved );
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
            builder.addName( part._fit );
            builder.addName( part._fitTables );
            builder.addName( part._bodyShapes );
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
            builder.addName( part._rig );
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
            const ItemDef*       pDef = pCatalog != nullptr ? pCatalog->find( item._itemID ) : nullptr;
            const float32        worn = pDef != nullptr && pDef->_maxDurability > 0.0f ? 1.0f - MathUtil::saturate( item._durability / pDef->_maxDurability ) : 0.0f;
            request._customization    = item._customization;
            request._listDetachedPart = item._listDetachedPart;
            request._damage           = AppearanceResolver::snapDamage( MathUtil::max( item._damage, worn ) );
            request._bSuppressed      = pEquipSlot->_bSuppressed;
        }
    }
} // namespace sw
