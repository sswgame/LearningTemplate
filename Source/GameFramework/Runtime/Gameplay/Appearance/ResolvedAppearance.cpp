#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/ResolvedAppearance.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_set.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceXMLUtil.h"
#include "GameFramework/Runtime/Gameplay/Condition/GameConditions.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Equipment.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

namespace sw
{
    namespace
    {
        struct AppearanceResolverInternal
        {
            /** @brief 규칙 동작 하나의 판정입니다. 같은 대상을 두고 이긴 쪽을 담습니다. */
            struct Decision
            {
                const AppearanceRuleAction* _pAction{ nullptr };
                const AppearanceRuleDef*    _pRule{ nullptr };
            };

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
                vector<hashed_string>        _listHiddenPart{};        ///< 규칙이 숨긴 부품(`<Hide target part>`) — 나머지 부품은 그린다
                uint8                        _bHidden{ SW_FALSE };

                /** @brief 고른 변형 @p variant 의 머티리얼(부품 전체), 구간 머티리얼, 머티리얼 값을 겁니다. 구간 이름은 로드가 부품의 구간인지 봤다(`Appearance.SectionUnknown`). */
                void applyVariantMaterials( const AppearancePartDef& part, const AppearancePartVariantDef& variant, ResolvedPart& inoutResolved, ResolvedAppearance& out ) const
                {
                    if ( variant._material.empty() == false )
                        setPartMaterial( inoutResolved, variant._material );
                    for ( const AppearanceSectionDef& section : variant._listSection )
                    {
                        (void)setSectionMaterial( inoutResolved, section._name, section._material );
                    }
                    for ( const AppearanceMaterialValueDef& value : variant._listMaterialValue )
                    {
                        addMaterialValue( out, _owner, part._name, value._section, value._name, value._value, ResolvedMaterialTarget::Parameter, 0 );
                    }
                }

                /**
                 * @brief 규칙의 머티리얼 교체(`<SwapMaterial>`)를 넓은 대상부터 좁은 대상 순으로 겁니다 — 주인 전체, 부품, 구간, 부품의 구간. 좁은 것이 넓은 것 위에 덮는다.
                 * @details 언리얼 Material Slot 덮어쓰기(슬롯 이름) · 유니티 `Renderer.materials`(서브메시 번호)와 같은 자리를 이름으로 짚는다. 대상이 다르면 충돌이 아니므로(`hasSameTarget`)
                 *          한 부품에 여러 교체가 함께 걸린다. 그 구간이 없는 부품은 건너뛰고 설명에 남긴다.
                 */
                void applyMaterialSwaps( const AppearancePartDef& part, const vector<Decision>& listDecision, ResolvedPart& inoutResolved, ResolvedAppearance& out ) const
                {
                    for ( int32 specificity = 0; specificity < 4; ++specificity )
                    {
                        for ( const Decision& decision : listDecision )
                        {
                            const AppearanceRuleAction& action            = *decision._pAction;
                            const bool                  bTargets          = action._kind == AppearanceRuleActionKind::SwapMaterial && action._target == _owner && ( action._part.empty() || action._part == part._name );
                            const int32                 actionSpecificity = ( action._section.empty() ? 0 : 2 ) + ( action._part.empty() ? 0 : 1 );
                            if ( bTargets == false || actionSpecificity != specificity )
                                continue;
                            if ( action._section.empty() )
                            {
                                setPartMaterial( inoutResolved, action._value );
                                addTrace( out, AppearanceTraceStep::Rule, decision._pRule->_id, "material of '%#.%#' swapped to '%#'", _owner.c_str(), part._name.c_str(), action._value.c_str() );
                            }
                            else if ( setSectionMaterial( inoutResolved, action._section, action._value ) )
                            {
                                addTrace( out, AppearanceTraceStep::Rule, decision._pRule->_id, "material of '%#.%#' section '%#' swapped to '%#'", _owner.c_str(), part._name.c_str(), action._section.c_str(),
                                          action._value.c_str() );
                            }
                            else if ( action._part.empty() == false )
                            {
                                addTrace( out, AppearanceTraceStep::Rule, decision._pRule->_id, "part '%#.%#' has no section '%#' - material swap ignored", _owner.c_str(), part._name.c_str(), action._section.c_str() );
                            }
                        }
                    }
                }
            };

            /** @brief 외형 규칙의 어휘(`targetTag` · `anyTag` · `occupied`)가 읽는 칸입니다. 규칙 판정에 든 외형 목록입니다. */
            struct AppearanceEntriesSlot
            {
                using Type                      = const vector<Entry>;
                static constexpr uint32 kSlotID = FourCCUtil::make( "APRE" );
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
                utf8 arrBuffer[constant::buffer::kMax512];
                FormatString::formatstring( arrBuffer, constant::buffer::kMax512, format, std::forward<Args>( args )... );
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

            static void addMaterialValue( ResolvedAppearance& out, const hashed_string& owner, const hashed_string& part, const hashed_string& section, const hashed_string& name, const float4& value,
                                          ResolvedMaterialTarget target, int32 channel, ResolvedMaterialShape shape = ResolvedMaterialShape::Vector )
            {
                ResolvedMaterialValue entry;
                entry._shape   = shape;
                entry._owner   = owner;
                entry._part    = part;
                entry._section = section;
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
                return constant::invalid_index::kInt32;
            }

            static hashed_string getDefaultState( const ItemVisualDef& visual ) { return visual._defaultState; }

            /** @brief 주인 항목 @p ownerIndex 의 외형입니다. 항목이 없으면 nullptr 입니다. */
            static const ItemVisualDef* findOwnerVisual( const vector<Entry>& listEntry, int32 ownerIndex )
            {
                return ownerIndex >= 0 && static_cast<size_t>( ownerIndex ) < listEntry.size() ? listEntry[static_cast<size_t>( ownerIndex )]._pVisual : nullptr;
            }

            /** @brief 외형에 염색하지 않는 구간이 있으면 true 입니다 — 그런 외형의 염색은 구간마다 나눠 건다. */
            static bool hasUndyeableSection( const ItemVisualDef& visual )
            {
                for ( const AppearancePartDef& part : visual._listPart )
                {
                    for ( const AppearanceSectionDef& section : part._listSection )
                    {
                        if ( section._bDyeable == false )
                            return true;
                    }
                }
                return false;
            }

            /**
             * @brief 머티리얼 드라이브의 값을 겁니다. 구간을 고르지 않는 드라이브는 주인 전체(부품 · 구간 빈 값)에 하나, 고르는 드라이브(`include` · `exclude` · 염색 제외 구간)는 맞는 구간마다 하나다.
             * @details 구간 별칭은 로드가 외형에 있는지 봤다(`Appearance.SectionTagUnknown`). 별칭이 하나도 맞지 않으면 아무 값도 걸지 않는다(그게 데이터의 뜻이다).
             */
            static void addDriveMaterialValue( ResolvedAppearance& out, const hashed_string& schemaOwner, const ItemVisualDef* pSchemaVisual, const vector<Entry>& listEntry,
                                               const CustomizationDriveDef& drive, const float4& value, ResolvedMaterialTarget target, ResolvedMaterialShape shape )
            {
                // 드라이브가 주인을 적으면(`owner="Hair"`) 몸 스키마의 고르기가 낸 항목이 받는다. 구간 고르기는 그 항목의 외형으로 한다 — 아직 없으면(뒤에 선언한 고르기) 주인 전체다.
                const bool           bDriveOwner     = drive._owner.empty() == false;
                const hashed_string& owner           = bDriveOwner ? drive._owner : schemaOwner;
                const int32          ownerEntry      = bDriveOwner ? findEntry( listEntry, drive._owner, false ) : constant::invalid_index::kInt32;
                const ItemVisualDef* pVisual         = bDriveOwner ? findOwnerVisual( listEntry, ownerEntry ) : pSchemaVisual;
                const int32          channel         = target == ResolvedMaterialTarget::DyeChannel ? drive._channel : 0;
                const bool           bSplitBySection = pVisual != nullptr && ( drive.isSectionTargeted() || ( drive._kind == CustomizationDriveKind::DyeChannel && hasUndyeableSection( *pVisual ) ) );
                if ( bSplitBySection == false )
                {
                    addMaterialValue( out, owner, hashed_string{}, hashed_string{}, drive._target, value, target, channel, shape );
                    return;
                }
                for ( const AppearancePartDef& part : pVisual->_listPart )
                {
                    for ( const AppearanceSectionDef& section : part._listSection )
                    {
                        if ( drive.isDrivenSection( section ) )
                            addMaterialValue( out, owner, part._name, section._name, drive._target, value, target, channel, shape );
                    }
                }
            }

            /**
             * @brief 피부 층을 굽기 순서대로 냅니다. 모르는 층은 버리고 설명에 남기며(지워진 콘텐츠), 숨긴 층은 그리지 않는다. 값은 전송 정밀도로 맞추고 색은 종류의 견본 묶음으로 맞춘다.
             */
            static void emitSkinLayers( const AppearanceDatabase& database, const CharacterAppearanceSpec& spec, ResolvedAppearance& out )
            {
                for ( const SkinLayerValue& given : spec._listSkinLayer )
                {
                    const SkinLayerDef* pDef = database.getSkinLayers().findLayer( given._layer );
                    if ( pDef == nullptr )
                    {
                        addTrace( out, AppearanceTraceStep::Customization, given._layer, "skin layer '%#' is unknown - dropped", given._layer.c_str() );
                        continue;
                    }
                    if ( given._bHidden == SW_TRUE )
                        continue;
                    ResolvedSkinLayer layer;
                    layer._value = given;
                    layer._value.quantize();
                    const CustomizationSchemaCatalog::Palette* pPalette = pDef->_palette.empty() ? nullptr : database.getSchemas().findPalette( pDef->_palette );
                    if ( pPalette != nullptr )
                    {
                        CustomizationParamDef swatches; // 견본 맞추기는 색 매개변수와 같은 식이다
                        swatches._listSwatch = pPalette->_listSwatch;
                        layer._value._color  = swatches.snapToSwatch( layer._value._color );
                    }
                    if ( pDef->_bPlaceable == false )
                    {
                        layer._value._offset          = float2{ 0.5f, 0.5f }; // 배치 없는 층은 피부 UV 그대로다
                        layer._value._scale           = float2{ 1.0f, 1.0f };
                        layer._value._rotationDegrees = 0.0f;
                        layer._value._bMirror         = SW_FALSE;
                    }
                    layer._regionMin       = pDef->_regionMin;
                    layer._regionMax       = pDef->_regionMax;
                    layer._mask            = pDef->_mask;
                    layer._roughness       = pDef->_roughness;
                    layer._roughnessWeight = pDef->_roughnessWeight;
                    layer._metallic        = pDef->_metallic;
                    layer._metallicWeight  = pDef->_metallicWeight;
                    layer._smearResistance = pDef->_smearResistance;
                    layer._blend           = pDef->_blend;
                    layer._bPlaceable      = pDef->_bPlaceable ? SW_TRUE : SW_FALSE;
                    out._listSkinLayer.push_back( layer );
                }
            }

            /**
             * @brief 몸 채널 층을 모아 몸의 체형 축 값(`_listBoneProportion`, 주인이 빈 것)을 다시 씁니다. 바닥은 꾸미기 슬라이더, 그 위에 몸 프로필 층, 보이는 장비 층(슬롯 표 순서)을 우선순위로 얹는다.
             * @details 숨겨진 장비(점유 · 규칙 · 조건 · 플레이어 숨김)는 몸을 누르지 않는다 — 보이지 않는 갑옷이 가슴을 조이지 않는다.
             */
            static void applyBodyChannels( const AppearanceDatabase& database, const CharacterAppearanceSpec& spec, const vector<Entry>& listEntry, ResolvedAppearance& out )
            {
                vector<BodyChannelLayerDef>    listLayer;
                const CharacterBodyProfileDef* pProfile = spec._bodyProfile.empty() ? nullptr : database.getBodyProfiles().findProfile( spec._bodyProfile );
                if ( pProfile != nullptr )
                    listLayer.insert( listLayer.end(), pProfile->_listChannel.begin(), pProfile->_listChannel.end() );
                for ( const Entry& entry : listEntry )
                {
                    if ( entry._bHidden == SW_TRUE || entry._pVisual == nullptr )
                        continue;
                    for ( const BodyChannelLayerDef& layer : entry._pVisual->_listBodyChannel )
                    {
                        listLayer.push_back( layer );
                        addTrace( out, AppearanceTraceStep::Output, entry._owner, "'%#' sets body channel '%#' (%# %#)", entry._pVisual->_id.c_str(), layer._name.c_str(), getEnumName( layer._blend ), layer._value );
                    }
                }
                if ( listLayer.empty() )
                    return;
                vector<BodyChannelValue> listValue;
                for ( const ResolvedBoneProportion& proportion : out._listBoneProportion )
                {
                    if ( proportion._owner.empty() )
                        listValue.push_back( BodyChannelValue{ proportion._name, proportion._value } );
                }
                BodyChannelLayerDef::blendLayers( span<const BodyChannelLayerDef>( listLayer.data(), listLayer.size() ), listValue );
                VectorUtil::removeIf( out._listBoneProportion, []( const ResolvedBoneProportion& proportion )
                { return proportion._owner.empty(); } );
                for ( const BodyChannelValue& value : listValue )
                    out._listBoneProportion.push_back( ResolvedBoneProportion{ hashed_string{}, value._channel, value._value } );
            }

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
                    if ( param.isActive( schema, normalized ) == false )
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
                                const float32 driven = param.computeDriveValue( drive, pValue->_number._x );
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
                                        addDriveMaterialValue( out, owner, findOwnerVisual( inoutListEntry, ownerIndex ), inoutListEntry, drive, float4( driven, 0.0f, 0.0f, 0.0f ), ResolvedMaterialTarget::Parameter,
                                                               ResolvedMaterialShape::Scalar );
                                        break;
                                    }
                                    case CustomizationDriveKind::MaterialColor:
                                    {
                                        addDriveMaterialValue( out, owner, findOwnerVisual( inoutListEntry, ownerIndex ), inoutListEntry, drive, pValue->_number, ResolvedMaterialTarget::Parameter, ResolvedMaterialShape::Vector );
                                        break;
                                    }
                                    case CustomizationDriveKind::DyeChannel:
                                    {
                                        addDriveMaterialValue( out, owner, findOwnerVisual( inoutListEntry, ownerIndex ), inoutListEntry, drive, pValue->_number, ResolvedMaterialTarget::DyeChannel, ResolvedMaterialShape::Vector );
                                        break;
                                    }
                                    case CustomizationDriveKind::PaletteSwap:
                                    {
                                        addMaterialValue( out, owner, hashed_string{}, hashed_string{}, drive._target, pValue->_number, ResolvedMaterialTarget::PaletteSwap, 0 );
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

            /** @brief 보이는 외형 중 하나가 태그를 가지면(주인을 주면 그 주인만) 참입니다. */
            static bool hasVisibleTag( const vector<Entry>& listEntry, const hashed_string* pOwner, TagID tag )
            {
                for ( const Entry& entry : listEntry )
                {
                    const bool bOwnerMatches = pOwner == nullptr || entry._owner == *pOwner;
                    if ( entry._bHidden == SW_FALSE && bOwnerMatches && entry._pVisual->_tags.hasTag( tag ) )
                        return true;
                }
                return false;
            }

            static bool readTargetTag( const GameplayContext& context, const AppearanceTargetTagArgs& args )
            {
                const vector<Entry>* pListEntry = context.find<AppearanceEntriesSlot>();
                return pListEntry != nullptr && hasVisibleTag( *pListEntry, &args._target, args._tag );
            }

            static bool readAnyTag( const GameplayContext& context, const ConditionTagArg& args )
            {
                const vector<Entry>* pListEntry = context.find<AppearanceEntriesSlot>();
                return pListEntry != nullptr && hasVisibleTag( *pListEntry, nullptr, args._tag );
            }

            static bool readOccupied( const GameplayContext& context, const ConditionNameArg& args )
            {
                const vector<Entry>* pListEntry = context.find<AppearanceEntriesSlot>();
                return pListEntry != nullptr && findEntry( *pListEntry, args._name, true ) >= 0;
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
                const bool            bHitOff    = entry._pRequest != nullptr && VectorUtil::contains( entry._pRequest->_listDetachedPart, part._name );
                const optional<int32> breakIndex = part._breakStage.empty() ? optional<int32>{} : visual.findDamageStageIndex( part._breakStage );
                const bool            bStageOff  = breakIndex.has_value() && stageIndex >= *breakIndex;
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
                    addMaterialValue( out, hashed_string{}, hashed_string{}, value._section, value._name, value._value, ResolvedMaterialTarget::Parameter, 0 );
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
                resolved._owner        = entry._owner;
                resolved._itemID       = entry._itemID;
                resolved._visualID     = visual._id;
                resolved._partName     = part._name;
                resolved._kind         = part._kind;
                resolved._asset        = part._asset;
                resolved._material     = part._material;
                resolved._listSection  = part._listSection;
                resolved._skeleton     = part._skeleton;
                resolved._socketSet    = part._socketSet;
                resolved._rig          = part._rig;
                resolved._fit          = part._fit;
                resolved._fitTables    = part._fitTables;
                resolved._bodyShapes   = part._bodyShapes;
                resolved._layer        = part._layer;
                resolved._lodDetail    = part._lodDetail;
                resolved._lodDropLevel = part._lodDropLevel;
                resolved._bDeforms     = part._bDeforms;
                resolved._state        = entry._state;
                resolved._placement    = placement;
                resolved._damageStage  = pStage != nullptr ? pStage->_name : hashed_string{};

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
                    if ( pVariant->_skeleton.empty() == false )
                        resolved._skeleton = pVariant->_skeleton;
                    entry.applyVariantMaterials( part, *pVariant, resolved, out );
                }
                const AppearancePartVariantDef* pMaterialVariant = entry._customMaterialVariant.empty() ? nullptr : part.findMaterialVariant( entry._customMaterialVariant );
                if ( pMaterialVariant != nullptr )
                {
                    resolved._materialVariant = pMaterialVariant->_name;
                    entry.applyVariantMaterials( part, *pMaterialVariant, resolved, out );
                }
                const Decision* pSwapMesh = findDecision( listDecision, AppearanceRuleActionKind::SwapMesh, entry._owner, part._name );
                if ( pSwapMesh != nullptr )
                {
                    resolved._asset = pSwapMesh->_pAction->_value;
                    addTrace( out, AppearanceTraceStep::Rule, pSwapMesh->_pRule->_id, "mesh of '%#.%#' swapped to '%#'", entry._owner.c_str(), part._name.c_str(), resolved._asset.c_str() );
                }
                entry.applyMaterialSwaps( part, listDecision, resolved, out );
                return resolved;
            }

            /** @brief 부품 머티리얼을 바꿉니다. 구간이 있는 부품은 모든 구간의 것을 바꾼다(부품 머티리얼과 구간은 함께 쓰지 않는다 — `Appearance.SectionWithPartMaterial`). */
            static void setPartMaterial( ResolvedPart& inoutPart, const hashed_string& material )
            {
                if ( inoutPart._listSection.empty() )
                {
                    inoutPart._material = material;
                    return;
                }
                for ( AppearanceSectionDef& section : inoutPart._listSection )
                {
                    section._material = material;
                }
            }

            /** @brief 이름이 @p section 인 구간의 머티리얼을 바꿉니다. 부품에 그 구간이 없으면 false 입니다. */
            static bool setSectionMaterial( ResolvedPart& inoutPart, const hashed_string& section, const hashed_string& material )
            {
                AppearanceSectionDef* pSection = VectorUtil::findBy<&AppearanceSectionDef::_name>( inoutPart._listSection, section );
                if ( pSection == nullptr )
                    return false;
                pSection->_material = material;
                return true;
            }

            /**
             * @brief 모은 몸 채널 값으로 부품 머티리얼 스칼라(`<ChannelMaterial>`)를 겁니다. 몸 채널을 모은 뒤(`applyBodyChannels`) 부른다 — 장비 층까지 얹은 값이 닿는다.
             * @details 나온 부품(`_listPart`)에만 건다 — 숨긴 부품 · 숨긴 항목은 값을 받지 않는다. 모은 값에 없는 채널은 중립 0 이다.
             */
            static void applyChannelMaterials( const vector<Entry>& listEntry, ResolvedAppearance& out )
            {
                for ( const Entry& entry : listEntry )
                {
                    if ( entry._bHidden == SW_TRUE || entry._pVisual == nullptr )
                        continue;
                    for ( const AppearancePartDef& part : entry._pVisual->_listPart )
                    {
                        if ( part._listChannelMaterial.empty() )
                            continue;
                        const bool bEmitted = std::any_of( out._listPart.begin(), out._listPart.end(), [&]( const ResolvedPart& resolved )
                        { return resolved._owner == entry._owner && resolved._partName == part._name; } );
                        if ( bEmitted == false )
                            continue;
                        for ( const AppearanceChannelMaterialDef& channelMaterial : part._listChannelMaterial )
                        {
                            float32 channelValue = 0.0f;
                            for ( const ResolvedBoneProportion& proportion : out._listBoneProportion )
                            {
                                if ( proportion._owner.empty() && proportion._name == channelMaterial._channel )
                                    channelValue = proportion._value;
                            }
                            if ( VectorUtil::contains( out._listMaterialChannel, channelMaterial._channel ) == false )
                                out._listMaterialChannel.push_back( channelMaterial._channel );
                            const float32 value = channelMaterial.computeValue( channelValue );
                            addMaterialValue( out, entry._owner, part._name, hashed_string{}, channelMaterial._name, float4( value, 0.0f, 0.0f, 0.0f ), ResolvedMaterialTarget::Parameter, 0,
                                              ResolvedMaterialShape::Scalar );
                            addTrace( out, AppearanceTraceStep::Output, entry._owner, "body channel '%#' = %# sets '%#' of part '%#' to %#", channelMaterial._channel.c_str(), channelValue,
                                      channelMaterial._name.c_str(), part._name.c_str(), value );
                        }
                    }
                }
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
                    if ( VectorUtil::contains( entry._listHiddenPart, part._name ) )
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
                        addMaterialValue( out, entry._owner, value._part, value._section, value._name, value._value, ResolvedMaterialTarget::Parameter, 0 );
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
                    pSchema->normalize( pRequest->_customization, normalized, &listDropped );
                    for ( const hashed_string& dropped : listDropped )
                    {
                        addTrace( out, AppearanceTraceStep::Customization, inoutEntry._owner, "'%#' is not in schema '%#' - dropped", dropped.c_str(), pSchema->_id.c_str() );
                    }
                    applyCustomization( *pSchema, normalized, inoutEntry._owner, entryIndex, database, inoutListEntry, listExtraVisual, out );
                }
                const Entry entry      = inoutListEntry[static_cast<size_t>( entryIndex )];
                int32       stageIndex = constant::invalid_index::kInt32;
                if ( pRequest != nullptr )
                {
                    stageIndex = visual.computeDamageStageIndex( ResolvedAppearance::snapDamage( pRequest->_damage ) );
                    if ( stageIndex >= 0 )
                        addTrace( out, AppearanceTraceStep::Damage, entry._owner, "damage stage '%#'", visual._listDamageStage[static_cast<size_t>( stageIndex )]._name.c_str() );
                }
                emitParts( visual, entry, stageIndex, listDecision, out );
                for ( const ItemVisualDef* pExtraVisual : listExtraVisual )
                {
                    Entry extra  = entry;
                    extra._state = getDefaultState( *pExtraVisual );
                    emitParts( *pExtraVisual, extra, constant::invalid_index::kInt32, listDecision, out );
                }
            }

            /** @brief 1 단계입니다. 몸 항목을 넣고 캐릭터 꾸미기를 적용합니다. */
            static void addBodyEntries( const AppearanceDatabase& database, const CharacterAppearanceSpec& spec, vector<Entry>& inoutListEntry, ResolvedAppearance& out )
            {
                int32                bodyIndex = constant::invalid_index::kInt32;
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
                pSchema->normalize( spec._customization, normalized, &listDropped );
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
                    else if ( request._bHiddenByPlayer == SW_TRUE )
                    {
                        entry._bHidden = SW_TRUE;
                        addTrace( out, AppearanceTraceStep::Output, request._slot, "'%#' hidden by the player", request._itemID.c_str() );
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
                    const SlotOccupancyDef* pOccupancy = database.getSlotCatalog().findOccupancy( occupier._pVisual->_occupancy );
                    if ( pOccupancy == nullptr )
                        continue;
                    for ( const hashed_string& slot : pOccupancy->_listSlot )
                    {
                        const int32 otherIndex = slot == occupier._owner ? constant::invalid_index::kInt32 : findEntry( inoutListEntry, slot, true );
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
                for ( const EquipmentSetDef& set : database.getSets().getSets() )
                {
                    const ItemVisualDef* pCompleteVisual = set._completeVisual.empty() ? nullptr : database.getVisuals().findVisual( set._completeVisual );
                    if ( pCompleteVisual == nullptr )
                        continue;
                    const EquipmentSetProgress progress = database.getSets().computeProgressWith( set, spec._bodyType, [&spec]( const hashed_string& slot )
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
                    GameplayContext world;
                    world.set<AppearanceEntriesSlot>( &listSnapshot );
                    world.set<SubjectTagsSlot>( &spec._tags );
                    world.set<BodyShapeSlot>( &spec._bodyShape );
                    world.set<BodyTypeSlot>( &spec._bodyType );
                    bool bFires = true;
                    for ( const AppearanceRuleCondition& condition : rule._listCondition )
                    {
                        bFires = bFires && condition._expr.evaluate( world );
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
                            const bool bPartOnly = action._kind == AppearanceRuleActionKind::HideTarget && action._part.empty() == false;
                            if ( bPartOnly )
                            {
                                if ( entry._owner == action._target && VectorUtil::contains( entry._listHiddenPart, action._part ) == false )
                                {
                                    entry._listHiddenPart.push_back( action._part );
                                    addTrace( out, AppearanceTraceStep::Rule, pRule->_id, "part '%#.%#' hidden", entry._owner.c_str(), action._part.c_str() );
                                }
                                continue;
                            }
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
                    else if ( action._kind == AppearanceRuleActionKind::MaterialValue )
                    {
                        // 부품 · 피해 단계 · 꾸미기 값 뒤에 넣는다 — 같은 매개변수면 규칙이 이긴다(적용은 목록 순서다).
                        addMaterialValue( out, action._target, action._part, action._section, action._name, action._materialValue, ResolvedMaterialTarget::Parameter, 0 );
                        addTrace( out, AppearanceTraceStep::Rule, decision._pRule->_id, "material value '%#' set on '%#.%#' section '%#'", action._name.c_str(), action._target.c_str(), action._part.c_str(),
                                  action._section.c_str() );
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

    SW_CONDITION( targetTag, AppearanceTargetTagArgs, &AppearanceResolverInternal::readTargetTag );
    SW_CONDITION( anyTag, ConditionTagArg, &AppearanceResolverInternal::readAnyTag );
    SW_CONDITION( occupied, ConditionNameArg, &AppearanceResolverInternal::readOccupied );
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

    float32 ResolvedAppearance::snapDamage( float32 damage )
    {
        return static_cast<float32>( static_cast<uint32>( MathUtil::saturate( damage ) * 255.0f + 0.5f ) ) / 255.0f;
    }

    void ResolvedAppearance::resolve( const AppearanceDatabase& database, const CharacterAppearanceSpec& spec, ResolvedAppearance& outResolved )
    {
        using Internal           = AppearanceResolverInternal;
        outResolved              = ResolvedAppearance{};
        outResolved._presetID    = spec._presetID;
        outResolved._bodyType    = spec._bodyType;
        outResolved._bodyShape   = spec._bodyShape;
        outResolved._face        = spec._face;
        outResolved._bodyProfile = spec._bodyProfile;

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
        Internal::applyBodyChannels( database, spec, listEntry, outResolved );
        Internal::applyChannelMaterials( listEntry, outResolved );
        Internal::emitSkinLayers( database, spec, outResolved );
        Internal::removeRepeatedHiddenRegions( outResolved );
        outResolved._meshHash = computeMeshHash( outResolved );
        outResolved._hash     = computeHash( outResolved );
    }

    uint64 ResolvedAppearance::computeMeshHash( const ResolvedAppearance& resolved )
    {
        AppearanceResolverInternal::HashBuilder builder;
        builder.addName( resolved._bodyType );
        builder.addName( resolved._bodyShape );
        builder.addName( resolved._face );
        for ( const ResolvedPart& part : resolved._listPart )
        {
            if ( part.isSkinnedFollower() == false )
                continue; // 휘지 않는 조각 · 프리팹 · 스프라이트는 병합 · 베이크 메시에 들지 않는다
            builder.addName( part._owner );
            builder.addName( part._partName );
            builder.addName( part._asset );
            builder.addName( part._material );
            builder.addName( part._skeleton );
            builder.addName( part._fit );
            builder.addName( part._fitTables );
            builder.addName( part._bodyShapes );
            builder.addInt( part._bDeforms );
            // LOD 규칙은 합친 메시의 낮은 단계를 바꾼다 — 같은 차림을 나누는 캐시 키(`AppearanceBakedMeshCache`)에 든다.
            builder.addFloat( part._lodDetail );
            builder.addUint64( part._lodDropLevel );
            for ( const AppearanceSectionDef& section : part._listSection ) // 구간 머티리얼(변형 · 규칙 반영)은 병합 메시의 구간이 된다 — 병합 캐시 키가 이 해시다
            {
                builder.addName( section._name );
                builder.addName( section._material );
                builder.addFloat( section._lodDetail );
                builder.addUint64( section._lodDropLevel );
            }
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
            builder.addName( value._section );
            builder.addName( value._name );
            builder.addFloat4( value._value );
            builder.addInt( value._channel );
            builder.addInt( static_cast<int32>( value._target ) );
            builder.addInt( static_cast<int32>( value._shape ) );
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

    uint64 ResolvedAppearance::computeHash( const ResolvedAppearance& resolved )
    {
        AppearanceResolverInternal::HashBuilder builder;
        builder.addUint64( computeMeshHash( resolved ) );
        builder.addName( resolved._presetID );
        builder.addName( resolved._bodyProfile );
        for ( const ResolvedSkinLayer& layer : resolved._listSkinLayer ) // 굽기 텍스처가 바뀌는 값(메시 해시에는 넣지 않는다 — 병합 메시는 그대로다)
        {
            builder.addName( layer._value._layer );
            builder.addFloat4( layer._value._color );
            builder.addFloat( layer._value._opacity );
            builder.addFloat( layer._value._offset._x );
            builder.addFloat( layer._value._offset._y );
            builder.addFloat( layer._value._scale._x );
            builder.addFloat( layer._value._scale._y );
            builder.addFloat( layer._value._rotationDegrees );
            builder.addInt( layer._value._bMirror );
        }
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
            builder.addName( detached._asset );
            builder.addPlacement( detached._placement );
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
            const EquipmentSlot* pEquipSlot = nullptr;
            for ( const EquipmentSlot& equipSlot : equipment.getSlots() )
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
            request._damage           = ResolvedAppearance::snapDamage( MathUtil::max( item._damage, worn ) );
            request._bSuppressed      = pEquipSlot->_bSuppressed;
        }
    }
} // namespace sw
