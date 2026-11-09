#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceSelection.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_set.h"
#include "Core/Encoding/Base64Util.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceXMLUtil.h"
#include "GameFramework/Runtime/Gameplay/Appearance/ResolvedAppearance.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

namespace sw
{
    namespace
    {
        struct AppearanceSelectionInternal
        {
            static constexpr uint32 kMaxCount         = 1024; ///< 읽을 때의 개수 상한. 망가진 입력이나 악의적인 입력이 큰 할당을 일으키지 않게 한다
            static constexpr int32  kKindBits         = 2;
            static constexpr int32  kLegacySliderBits = 16; ///< 판 1 의 슬라이더 — 그때 스키마 범위 안의 단계. 범위가 바뀐 뒤에는 뜻이 달라져 판 2 는 실제 값을 쓴다
            static constexpr int32  kColorBits        = 8;
            static constexpr int32  kDamageBits       = 8;
            static constexpr int32  kFineBits         = 16; ///< 피부 층 이동 · 크기 · 회전(`SkinLayerValue::kFineSteps`)
            static constexpr int32  kVersionBits      = 8;
            static constexpr uint32 kChecksumBytes    = 4;

            static uint32 toHash32( const hashed_string& name ) { return name.empty() ? 0u : static_cast<uint32>( name.getHash() ); }

            /** @brief 받는 쪽이 알지 못하는 해시의 임시 이름(`#1a2b3c4d`)입니다. 펼칠 때 "지워진 콘텐츠" 로 보고됩니다. */
            static hashed_string makePlaceholder( uint32 hash )
            {
                if ( hash == 0 )
                    return hashed_string{};
                utf8 arrBuffer[constant::buffer::kMax16];
                FormatString::formatstring( arrBuffer, constant::buffer::kMax16, "#%#", Fmt( hash, Format().hex().width( 8 ).zeroPad() ) );
                return hashed_string( string_view( arrBuffer ) );
            }

            static hashed_string findByHash( uint32 hash, const vector<hashed_string>& listCandidate )
            {
                for ( const hashed_string& candidate : listCandidate )
                {
                    if ( toHash32( candidate ) == hash )
                        return candidate;
                }
                return makePlaceholder( hash );
            }

            /** @brief 빈 이름이 아니면 더합니다. 중복은 다 모은 뒤 `removeRepeatedNames` 가 한 번 거른다. */
            static void addName( vector<hashed_string>& inoutList, const hashed_string& name )
            {
                if ( name.empty() == false )
                    inoutList.push_back( name );
            }

            /** @brief 이름의 중복을 처음 나온 자리를 지켜 지웁니다. */
            static void removeRepeatedNames( vector<hashed_string>& inoutList )
            {
                unordered_set<hashed_string> uniqueSetSeen;
                VectorUtil::removeIf( inoutList, [&uniqueSetSeen]( const hashed_string& name )
                { return uniqueSetSeen.insert( name ).second == false; } );
            }

            /** @brief 몸 종류, 체형, 얼굴은 열린 이름이므로 데이터에 나오는 모든 이름을 후보로 모은다. */
            static void collectBodyNames( const AppearanceDatabase& database, vector<hashed_string>& outListName )
            {
                for ( const CharacterAppearanceDef& preset : database.getPresets().getPresets() )
                {
                    for ( const hashed_string& name : preset._listBodyType )
                    {
                        addName( outListName, name );
                    }
                    for ( const hashed_string& name : preset._listBodyShape )
                    {
                        addName( outListName, name );
                    }
                    for ( const hashed_string& name : preset._listFace )
                    {
                        addName( outListName, name );
                    }
                }
                for ( const EquipmentSetDef& set : database.getSets().getSets() )
                {
                    for ( const EquipmentSetVariantDef& variant : set._listVariant )
                    {
                        addName( outListName, variant._bodyType );
                    }
                }
                removeRepeatedNames( outListName );
            }

            static const ItemVisualDef* findShownVisual( const AppearanceDatabase& database, const hashed_string& itemID, const hashed_string& visibleVisual )
            {
                if ( visibleVisual.empty() == false )
                    return database.getVisuals().findVisual( visibleVisual );
                return database.findItemVisual( itemID );
            }

            static const CustomizationSchemaDef* findVisualSchema( const AppearanceDatabase& database, const ItemVisualDef* pVisual )
            {
                return pVisual != nullptr ? database.getSchemas().findSchema( pVisual->_customization ) : nullptr;
            }

            static void writeVarCount( BitWriter& writer, size_t count ) { writer.writeVarUint( static_cast<uint64>( count ) ); }

            [[nodiscard]] static bool readVarCount( BitReader& reader, uint32& outCount )
            {
                const uint64 count = reader.readVarUint();
                if ( reader.hasOverflowed() || count > kMaxCount )
                    return false;
                outCount = static_cast<uint32>( count );
                return true;
            }

            /** @brief 값 하나를 씁니다(이름 해시 · 종류 · 값). 슬라이더는 실제 값이라 스키마 범위가 바뀌어도 뜻이 그대로다. */
            static void writeValue( BitWriter& writer, const CustomizationParamDef& param, const CustomizationValue& value )
            {
                writer.writeUint32( toHash32( param._name ) );
                writer.writeBits( static_cast<uint32>( param._kind ), kKindBits );
                switch ( param._kind )
                {
                    case CustomizationKind::Slider:
                    {
                        writer.writeFloat( value._number._x );
                        break;
                    }
                    case CustomizationKind::Color:
                    {
                        writer.writeBits( CustomizationParamDef::quantizeColorChannel( value._number._x ), kColorBits );
                        writer.writeBits( CustomizationParamDef::quantizeColorChannel( value._number._y ), kColorBits );
                        writer.writeBits( CustomizationParamDef::quantizeColorChannel( value._number._z ), kColorBits );
                        writer.writeBits( CustomizationParamDef::quantizeColorChannel( value._number._w ), kColorBits );
                        break;
                    }
                    case CustomizationKind::Choice:
                    case CustomizationKind::Attachment:
                    {
                        writer.writeUint32( toHash32( value._option ) );
                        break;
                    }
                }
            }

            /** @brief 스키마가 아는 값만 씁니다(아이템 인스턴스 값 — 정한 값만 싣는다). */
            static void writeValues( BitWriter& writer, const CustomizationValueSet& values, const CustomizationSchemaDef* pSchema )
            {
                vector<const CustomizationValue*> listKnown;
                for ( const CustomizationValue& value : values.getValues() )
                {
                    if ( pSchema != nullptr && pSchema->findParameter( value._parameter ) != nullptr )
                        listKnown.push_back( &value );
                }
                writeVarCount( writer, listKnown.size() );
                for ( const CustomizationValue* pValue : listKnown )
                {
                    writeValue( writer, *pSchema->findParameter( pValue->_parameter ), *pValue );
                }
            }

            /**
             * @brief 선택이 담은 카테고리의 **모든** 매개변수를 씁니다. 정하지 않은 값은 지금 기본값으로 적는다 — 쓴 모습이 그대로 남고, 뒤 판이 기본값을 바꿔도 저장된 캐릭터는
             *        바뀌지 않는다(뒤 판에 새로 생긴 매개변수만 그 판의 기본값을 따른다). 스키마가 모르는 값은 쓰지 않는다.
             */
            static void writeAllValues( BitWriter& writer, const AppearanceSelection& selection, const CustomizationSchemaDef* pSchema )
            {
                vector<const CustomizationParamDef*> listParam;
                if ( pSchema != nullptr )
                {
                    for ( const CustomizationParamDef& param : pSchema->_listParameter )
                    {
                        if ( selection.includesCategory( param._category ) )
                            listParam.push_back( &param );
                    }
                }
                writeVarCount( writer, listParam.size() );
                for ( const CustomizationParamDef* pParam : listParam )
                {
                    const CustomizationValue* pGiven = selection._customization.findValue( pParam->_name );
                    writeValue( writer, *pParam, pGiven != nullptr ? *pGiven : pParam->makeDefaultValue() );
                }
            }

            [[nodiscard]] static bool readValues( BitReader& reader, const CustomizationSchemaDef* pSchema, uint32 formatVersion, CustomizationValueSet& outValues )
            {
                uint32 count = 0;
                if ( readVarCount( reader, count ) == false )
                    return false;
                for ( uint32 index = 0; index < count; ++index )
                {
                    const uint32                 nameHash = reader.readUint32();
                    const CustomizationKind      kind     = static_cast<CustomizationKind>( reader.readBits( kKindBits ) );
                    const CustomizationParamDef* pParam   = nullptr;
                    if ( pSchema != nullptr )
                    {
                        for ( const CustomizationParamDef& param : pSchema->_listParameter )
                        {
                            if ( toHash32( param._name ) == nameHash && param._kind == kind )
                                pParam = &param;
                        }
                    }
                    CustomizationValue value;
                    value._parameter = pParam != nullptr ? pParam->_name : makePlaceholder( nameHash );
                    switch ( kind )
                    {
                        case CustomizationKind::Slider:
                        {
                            if ( formatVersion == 1 )
                            {
                                // 판 1 은 그때 범위 안의 단계다. 그때 범위는 남지 않아 지금 범위로 푼다 — 범위를 바꾼 매개변수는 정확하지 않다(판 2 가 고친 까닭)
                                const uint32 step = reader.readBits( kLegacySliderBits );
                                value._number._x  = pParam != nullptr ? pParam->dequantizeSlider( step ) : 0.0f;
                            }
                            else if ( reader.readFiniteFloat( value._number._x ) == false )
                                return false;
                            break;
                        }
                        case CustomizationKind::Color:
                        {
                            value._number._x = CustomizationParamDef::dequantizeColorChannel( reader.readBits( kColorBits ) );
                            value._number._y = CustomizationParamDef::dequantizeColorChannel( reader.readBits( kColorBits ) );
                            value._number._z = CustomizationParamDef::dequantizeColorChannel( reader.readBits( kColorBits ) );
                            value._number._w = CustomizationParamDef::dequantizeColorChannel( reader.readBits( kColorBits ) );
                            break;
                        }
                        case CustomizationKind::Choice:
                        case CustomizationKind::Attachment:
                        {
                            const uint32 optionHash = reader.readUint32();
                            value._option           = makePlaceholder( optionHash );
                            if ( pParam != nullptr )
                            {
                                for ( const CustomizationOptionDef& option : pParam->_listOption )
                                {
                                    if ( toHash32( option._name ) == optionHash )
                                        value._option = option._name;
                                }
                            }
                            break;
                        }
                    }
                    outValues.setValue( value );
                }
                return reader.hasOverflowed() == false;
            }

            static void addFallback( AppearanceSelectionReport& outReport, const hashed_string& slot, const hashed_string& requested, const hashed_string& fallback,
                                     AppearanceFallbackReason reason )
            {
                AppearanceSelectionReport::SlotFallback entry;
                entry._slot          = slot;
                entry._requestedItem = requested;
                entry._fallbackItem  = fallback;
                entry._reason        = reason;
                outReport._listSlotFallback.push_back( entry );
                SW_LOG_WARNING_ID( "AppearanceSelection.AppearanceSlotUsing", "Appearance slot '%#': '%#' %# - using '%#'", slot.c_str(), requested.c_str(), getEnumName( reason ), fallback.c_str() );
            }

            static void addDropped( AppearanceSelectionReport& outReport, const hashed_string& name )
            {
                outReport._listDroppedParameter.push_back( name );
                SW_LOG_WARNING_ID( "AppearanceSelection.AppearanceValueNoLonger", "Appearance value '%#' no longer exists - dropped", name.c_str() );
            }

            static void addClamped( AppearanceSelectionReport& outReport, const hashed_string& name )
            {
                outReport._listClampedParameter.push_back( name );
                SW_LOG_WARNING_ID( "AppearanceSelection.AppearanceValueOutOfRange", "Appearance value '%#' is outside the current range - clamped", name.c_str() );
            }

            /** @brief 아이템 인스턴스 값을 보이는 외형의 스키마로 걸러 냅니다. */
            static void filterItemValues( const CustomizationSchemaDef* pSchema, const CustomizationValueSet& values, CustomizationValueSet& outValues, AppearanceSelectionReport& outReport )
            {
                outValues.clear();
                for ( const CustomizationValue& value : values.getValues() )
                {
                    const CustomizationParamDef* pParam      = pSchema != nullptr ? pSchema->findParameter( value._parameter ) : nullptr;
                    const bool                   bOptionKind = pParam != nullptr && ( pParam->_kind == CustomizationKind::Choice || pParam->_kind == CustomizationKind::Attachment );
                    if ( pParam == nullptr )
                        addDropped( outReport, value._parameter );
                    else if ( bOptionKind && pParam->findOption( value._option ) == nullptr )
                        addDropped( outReport, value._option );
                    else
                        outValues.setValue( value );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AppearanceSelection::includesCategory( const hashed_string& category ) const
    {
        return _listCategory.empty() || VectorUtil::contains( _listCategory, category );
    }

    void AppearanceSelection::captureSelection( const CharacterAppearanceSpec& spec, AppearanceSelection& outSelection )
    {
        outSelection                = AppearanceSelection{};
        outSelection._basePresetID  = spec._presetID;
        outSelection._seed          = spec._seed;
        outSelection._schema        = spec._schema;
        outSelection._bodyType      = spec._bodyType;
        outSelection._bodyShape     = spec._bodyShape;
        outSelection._face          = spec._face;
        outSelection._customization = spec._customization;
        outSelection._listSlot      = spec._listSlot;
        outSelection._listSkinLayer = spec._listSkinLayer;
    }

    void AppearanceSelection::makePartial( const AppearanceSelection& selection, const AppearanceDatabase& database, const vector<hashed_string>& listCategory,
                                           AppearanceSelection& outPartial )
    {
        outPartial                            = AppearanceSelection{};
        outPartial._basePresetID              = selection._basePresetID;
        outPartial._seed                      = selection._seed;
        outPartial._schema                    = selection._schema;
        outPartial._listCategory              = listCategory;
        const CustomizationSchemaDef* pSchema = database.getSchemas().findSchema( selection._schema );
        for ( const CustomizationValue& value : selection._customization.getValues() )
        {
            const CustomizationParamDef* pParam = pSchema != nullptr ? pSchema->findParameter( value._parameter ) : nullptr;
            if ( pParam != nullptr && outPartial.includesCategory( pParam->_category ) )
                outPartial._customization.setValue( value );
        }
        if ( outPartial.includesCategory( hashed_string( AppearanceSelection::kLoadoutCategory ) ) )
            outPartial._listSlot = selection._listSlot;
        for ( const SkinLayerValue& layer : selection._listSkinLayer )
        {
            const SkinLayerDef* pDef = database.getSkinLayers().findLayer( layer._layer );
            if ( pDef != nullptr && outPartial.includesCategory( pDef->_category ) )
                outPartial._listSkinLayer.push_back( layer );
        }
    }

    void AppearanceSelection::applySelection( const AppearanceDatabase& database, const AppearanceSelection& selection, const CharacterAppearanceSpec& current,
                                              const IAppearanceUnlockQuery* pUnlockQuery, CharacterAppearanceSpec& outSpec, AppearanceSelectionReport& outReport )
    {
        using Internal = AppearanceSelectionInternal;
        outReport      = AppearanceSelectionReport{};

        // 슬롯의 기본값(지워졌거나 잠긴 아이템이 돌아갈 값)은 기준 프리셋의 것이다.
        CharacterAppearanceSpec baseSpec = current;
        const bool              bBaseKnown =
            selection._basePresetID.empty() == false && database.getPresets().expand( selection._basePresetID, selection._seed, database.getSlotCatalog(), database.getSets(), database.getSchemas(), baseSpec );
        if ( selection._basePresetID.empty() == false && bBaseKnown == false )
        {
            outReport._bUnknownBasePreset = SW_TRUE;
            SW_LOG_WARNING_ID( "AppearanceSelection.AppearanceBasePresetNo", "Appearance base preset '%#' no longer exists - applying on top of the current look", selection._basePresetID.c_str() );
            baseSpec = current;
        }
        outSpec = selection.isPartial() ? current : baseSpec;
        if ( selection.isPartial() == false )
        {
            if ( selection._bodyType.empty() == false )
                outSpec._bodyType = selection._bodyType;
            if ( selection._bodyShape.empty() == false )
                outSpec._bodyShape = selection._bodyShape;
            if ( selection._face.empty() == false )
                outSpec._face = selection._face;
        }

        const CustomizationSchemaDef* pSchema = database.getSchemas().findSchema( outSpec._schema );
        for ( const CustomizationValue& value : selection._customization.getValues() )
        {
            const CustomizationParamDef* pParam = pSchema != nullptr ? pSchema->findParameter( value._parameter ) : nullptr;
            if ( pParam == nullptr )
            {
                Internal::addDropped( outReport, value._parameter );
                continue;
            }
            if ( selection.includesCategory( pParam->_category ) == false )
                continue;
            const bool bOptionKind = pParam->_kind == CustomizationKind::Choice || pParam->_kind == CustomizationKind::Attachment;
            if ( bOptionKind && pParam->findOption( value._option ) == nullptr )
            {
                Internal::addDropped( outReport, value._option );
                continue;
            }
            if ( pParam->_kind == CustomizationKind::Slider && ( value._number._x < pParam->_min || value._number._x > pParam->_max ) )
            {
                // 저장한 판보다 범위가 좁아졌다 — 끝에 붙이고 알린다(해석기도 범위로 자르지만 플레이어가 모르게 바뀌지 않게)
                CustomizationValue clamped = value;
                clamped._number._x         = MathUtil::clamp( value._number._x, pParam->_min, pParam->_max );
                Internal::addClamped( outReport, value._parameter );
                outSpec._customization.setValue( clamped );
                continue;
            }
            outSpec._customization.setValue( value );
        }

        // 피부 층: 전체 선택은 목록을 바꾸고, 부분 선택은 그 카테고리의 층만 바꾼다(다른 카테고리의 층은 순서를 지켜 남는다). 모르는 층은 버리고 보고한다.
        if ( selection.isPartial() == false )
            outSpec._listSkinLayer.clear();
        VectorUtil::removeIf( outSpec._listSkinLayer, [&]( const SkinLayerValue& layer )
        {
            const SkinLayerDef* pDef = database.getSkinLayers().findLayer( layer._layer );
            return pDef == nullptr || selection.includesCategory( pDef->_category );
        } );
        for ( const SkinLayerValue& layer : selection._listSkinLayer )
        {
            const SkinLayerDef* pDef = database.getSkinLayers().findLayer( layer._layer );
            if ( pDef == nullptr )
                Internal::addDropped( outReport, layer._layer );
            else if ( selection.includesCategory( pDef->_category ) )
                outSpec._listSkinLayer.push_back( layer );
        }

        if ( selection.includesCategory( hashed_string( AppearanceSelection::kLoadoutCategory ) ) == false )
            return;
        const ItemCatalog* pItemCatalog = database.getItemCatalog();
        for ( const AppearanceSlotRequest& request : selection._listSlot )
        {
            AppearanceSlotRequest* pTarget = outSpec.findSlot( request._slot );
            if ( pTarget == nullptr )
            {
                Internal::addFallback( outReport, request._slot, request._itemID, hashed_string{}, AppearanceFallbackReason::UnknownSlot );
                continue;
            }
            const AppearanceSlotRequest* pBaseSlot   = baseSpec.findSlot( request._slot );
            const hashed_string          defaultItem = pBaseSlot != nullptr ? pBaseSlot->_itemID : hashed_string{};
            AppearanceSlotRequest        accepted    = request;
            if ( request._itemID.empty() == false )
            {
                const bool bMissing = pItemCatalog == nullptr || pItemCatalog->find( request._itemID ) == nullptr;
                const bool bLocked  = bMissing == false && pUnlockQuery != nullptr && pUnlockQuery->isItemUnlocked( request._itemID ) == false;
                if ( bMissing || bLocked )
                {
                    Internal::addFallback( outReport, request._slot, request._itemID, defaultItem, bMissing ? AppearanceFallbackReason::MissingItem : AppearanceFallbackReason::LockedItem );
                    // 슬롯을 통째로 되돌린다 - 형상 변경이 남으면 잠긴 아이템의 외형이 기본 아이템 위에 그대로 보인다
                    accepted       = pBaseSlot != nullptr ? *pBaseSlot : AppearanceSlotRequest{};
                    accepted._slot = request._slot;
                }
            }
            if ( accepted._visibleVisual.empty() == false && database.getVisuals().findVisual( accepted._visibleVisual ) == nullptr )
            {
                Internal::addFallback( outReport, request._slot, accepted._visibleVisual, accepted._itemID, AppearanceFallbackReason::MissingVisual );
                accepted._visibleVisual = hashed_string{};
            }
            const ItemVisualDef*  pShown = Internal::findShownVisual( database, accepted._itemID, accepted._visibleVisual );
            CustomizationValueSet filtered;
            Internal::filterItemValues( Internal::findVisualSchema( database, pShown ), accepted._customization, filtered, outReport );
            accepted._customization = filtered;
            *pTarget                = accepted;
        }
    }

    expected<void> AppearanceSelection::makePresetDef( const AppearanceDatabase& database, const AppearanceSelection& selection, const hashed_string& presetID,
                                                       CharacterAppearanceDef& outDef )
    {
        outDef = CharacterAppearanceDef{};
        if ( presetID.empty() || selection.isPartial() )
            return unexpected( Error{ ErrorKind::InvalidArgument, presetID.empty() ? "a preset needs an id" : "a partial selection (only some categories) cannot become a preset" } );
        outDef._id            = presetID;
        outDef._parent        = selection._basePresetID;
        outDef._schema        = selection._schema;
        outDef._listSkinLayer = selection._listSkinLayer;
        if ( selection._bodyType.empty() == false )
            outDef._listBodyType.push_back( selection._bodyType );
        if ( selection._bodyShape.empty() == false )
            outDef._listBodyShape.push_back( selection._bodyShape );
        if ( selection._face.empty() == false )
            outDef._listFace.push_back( selection._face );

        // 꾸미기 값 — 종류는 스키마가 안다(값만으로는 슬라이더와 색을 가를 수 없다).
        const CustomizationSchemaDef* pSchema = database.getSchemas().findSchema( selection._schema );
        for ( const CustomizationValue& value : selection._customization.getValues() )
        {
            const CustomizationParamDef* pParam = pSchema != nullptr ? pSchema->findParameter( value._parameter ) : nullptr;
            if ( pParam == nullptr )
            {
                Error error{ ErrorKind::NotFound, "the value is not a parameter of the schema (value, schema)", value._parameter.c_str() };
                error.appendDetail( ", " ).appendDetail( selection._schema.c_str() );
                return unexpected( error );
            }
            CharacterAppearanceValueDef valueDef;
            valueDef._parameter = value._parameter;
            switch ( pParam->_kind )
            {
                case CustomizationKind::Slider:
                {
                    valueDef._bHasNumber = SW_TRUE;
                    valueDef._min        = value._number._x;
                    valueDef._max        = value._number._x;
                    break;
                }
                case CustomizationKind::Color:
                {
                    valueDef._listColor.push_back( value._number );
                    break;
                }
                case CustomizationKind::Choice:
                case CustomizationKind::Attachment:
                {
                    valueDef._listOption.push_back( value._option );
                    break;
                }
            }
            outDef._listValue.push_back( valueDef );
        }

        for ( const AppearanceSlotRequest& request : selection._listSlot )
        {
            const bool bUnrepresentable = request._customization.isEmpty() == false || request._listDetachedPart.empty() == false || request._bSuppressed == SW_TRUE;
            if ( bUnrepresentable )
                return unexpected( Error{ ErrorKind::Unsupported, "the slot has item instance values, detached parts or a suppressed state, which a preset cannot carry yet", request._slot.c_str() } );
            CharacterAppearanceSlotDef slot;
            slot._slot = request._slot;
            if ( request._itemID.empty() )
                slot._bClear = SW_TRUE;
            else
                slot._listItem.push_back( request._itemID );
            slot._visibleVisual = request._visibleVisual;
            slot._state         = request._state;
            slot._damage        = request._damage;
            outDef._listSlot.push_back( slot );
        }
        return {};
    }

    void AppearanceSelection::previewSelection( const AppearanceDatabase& database, const AppearanceSelection& selection, const CharacterAppearanceSpec& current,
                                                const IAppearanceUnlockQuery* pUnlockQuery, ResolvedAppearance& outResolved, AppearanceSelectionReport& outReport )
    {
        CharacterAppearanceSpec previewSpec;
        applySelection( database, selection, current, pUnlockQuery, previewSpec, outReport );
        ResolvedAppearance::resolve( database, previewSpec, outResolved );
    }

    void AppearanceSelection::writeSelection( BitWriter& writer, const AppearanceSelection& selection, const AppearanceDatabase& database, uint32 version )
    {
        using Internal = AppearanceSelectionInternal;
        writer.writeUint32( Internal::toHash32( selection._basePresetID ) );
        writer.writeVarUint( selection._seed );
        writer.writeUint32( Internal::toHash32( selection._schema ) );
        writer.writeUint32( Internal::toHash32( selection._bodyType ) );
        writer.writeUint32( Internal::toHash32( selection._bodyShape ) );
        writer.writeUint32( Internal::toHash32( selection._face ) );
        Internal::writeVarCount( writer, selection._listCategory.size() );
        for ( const hashed_string& category : selection._listCategory )
        {
            writer.writeUint32( Internal::toHash32( category ) );
        }
        Internal::writeAllValues( writer, selection, database.getSchemas().findSchema( selection._schema ) );
        Internal::writeVarCount( writer, selection._listSlot.size() );
        for ( const AppearanceSlotRequest& request : selection._listSlot )
        {
            writer.writeUint32( Internal::toHash32( request._slot ) );
            writer.writeUint32( Internal::toHash32( request._itemID ) );
            writer.writeUint32( Internal::toHash32( request._visibleVisual ) );
            writer.writeUint32( Internal::toHash32( request._state ) );
            writer.writeBits( static_cast<uint32>( ResolvedAppearance::snapDamage( request._damage ) * 255.0f + 0.5f ), Internal::kDamageBits );
            writer.writeBool( request._bSuppressed == SW_TRUE );
            if ( version >= 3 )
                writer.writeBool( request._bHiddenByPlayer == SW_TRUE );
            Internal::writeVarCount( writer, request._listDetachedPart.size() );
            for ( const hashed_string& part : request._listDetachedPart )
            {
                writer.writeUint32( Internal::toHash32( part ) );
            }
            const ItemVisualDef* pShown = Internal::findShownVisual( database, request._itemID, request._visibleVisual );
            Internal::writeValues( writer, request._customization, Internal::findVisualSchema( database, pShown ) );
        }
        // 판 3: 피부 층. 격자는 `SkinLayerValue::quantize` 와 같다 — 받는 쪽의 굽기 결과가 보낸 쪽과 같다.
        if ( version < 3 )
            return;
        Internal::writeVarCount( writer, selection._listSkinLayer.size() );
        for ( const SkinLayerValue& layer : selection._listSkinLayer )
        {
            writer.writeUint32( Internal::toHash32( layer._layer ) );
            writer.writeBits( CustomizationParamDef::quantizeColorChannel( layer._color._x ), Internal::kColorBits );
            writer.writeBits( CustomizationParamDef::quantizeColorChannel( layer._color._y ), Internal::kColorBits );
            writer.writeBits( CustomizationParamDef::quantizeColorChannel( layer._color._z ), Internal::kColorBits );
            writer.writeBits( CustomizationParamDef::quantizeColorChannel( layer._color._w ), Internal::kColorBits );
            writer.writeBits( CustomizationParamDef::quantizeColorChannel( layer._opacity ), Internal::kColorBits );
            writer.writeBits( SkinLayerValue::encodeFine( layer._offset._x, 0.0f, 1.0f ), Internal::kFineBits );
            writer.writeBits( SkinLayerValue::encodeFine( layer._offset._y, 0.0f, 1.0f ), Internal::kFineBits );
            writer.writeBits( SkinLayerValue::encodeFine( layer._scale._x, SkinLayerValue::kMinScale, SkinLayerValue::kMaxScale ), Internal::kFineBits );
            writer.writeBits( SkinLayerValue::encodeFine( layer._scale._y, SkinLayerValue::kMinScale, SkinLayerValue::kMaxScale ), Internal::kFineBits );
            writer.writeBits( SkinLayerValue::encodeFine( layer._rotationDegrees, -SkinLayerValue::kMaxRotation, SkinLayerValue::kMaxRotation ), Internal::kFineBits );
            writer.writeBool( layer._bMirror == SW_TRUE );
            writer.writeBool( layer._bHidden == SW_TRUE );
        }
    }

    bool AppearanceSelection::readSelection( BitReader& reader, const AppearanceDatabase& database, AppearanceSelection& outSelection, uint32 formatVersion )
    {
        using Internal = AppearanceSelectionInternal;
        outSelection   = AppearanceSelection{};

        vector<hashed_string> listPresetID;
        for ( const CharacterAppearanceDef& preset : database.getPresets().getPresets() )
        {
            listPresetID.push_back( preset._id );
        }
        vector<hashed_string> listSchemaID;
        vector<hashed_string> listCategory{ hashed_string( AppearanceSelection::kLoadoutCategory ) };
        for ( const CustomizationSchemaDef& schema : database.getSchemas().getSchemas() )
        {
            listSchemaID.push_back( schema._id );
            for ( const CustomizationParamDef& param : schema._listParameter )
            {
                Internal::addName( listCategory, param._category );
            }
        }
        Internal::removeRepeatedNames( listCategory );
        vector<hashed_string> listBodyName;
        Internal::collectBodyNames( database, listBodyName );

        outSelection._basePresetID = Internal::findByHash( reader.readUint32(), listPresetID );
        outSelection._seed         = reader.readVarUint32();
        outSelection._schema       = Internal::findByHash( reader.readUint32(), listSchemaID );
        outSelection._bodyType     = Internal::findByHash( reader.readUint32(), listBodyName );
        outSelection._bodyShape    = Internal::findByHash( reader.readUint32(), listBodyName );
        outSelection._face         = Internal::findByHash( reader.readUint32(), listBodyName );
        uint32 categoryCount       = 0;
        if ( Internal::readVarCount( reader, categoryCount ) == false )
            return false;
        for ( uint32 index = 0; index < categoryCount; ++index )
        {
            outSelection._listCategory.push_back( Internal::findByHash( reader.readUint32(), listCategory ) );
        }
        if ( Internal::readValues( reader, database.getSchemas().findSchema( outSelection._schema ), formatVersion, outSelection._customization ) == false )
            return false;

        vector<hashed_string> listSlotName;
        for ( const AppearanceSlotDef& slot : database.getSlotCatalog().getSlots() )
        {
            listSlotName.push_back( slot._name );
        }
        vector<hashed_string> listItemID;
        if ( database.getItemCatalog() != nullptr )
        {
            for ( const ItemDef& item : database.getItemCatalog()->getAll() )
            {
                listItemID.push_back( item._id );
            }
        }
        vector<hashed_string> listVisualID;
        for ( const ItemVisualDef& visual : database.getVisuals().getVisuals() )
        {
            listVisualID.push_back( visual._id );
        }
        uint32 slotCount = 0;
        if ( Internal::readVarCount( reader, slotCount ) == false )
            return false;
        for ( uint32 index = 0; index < slotCount; ++index )
        {
            AppearanceSlotRequest request;
            request._slot                = Internal::findByHash( reader.readUint32(), listSlotName );
            request._itemID              = Internal::findByHash( reader.readUint32(), listItemID );
            request._visibleVisual       = Internal::findByHash( reader.readUint32(), listVisualID );
            const uint32 stateHash       = reader.readUint32();
            request._damage              = static_cast<float32>( reader.readBits( Internal::kDamageBits ) ) / 255.0f;
            request._bSuppressed         = reader.readBool() ? SW_TRUE : SW_FALSE;
            request._bHiddenByPlayer     = formatVersion >= 3 && reader.readBool() ? SW_TRUE : SW_FALSE; // 판 1 · 2 에는 없다
            const ItemVisualDef*  pShown = Internal::findShownVisual( database, request._itemID, request._visibleVisual );
            vector<hashed_string> listStateName;
            vector<hashed_string> listPartName;
            if ( pShown != nullptr )
            {
                for ( const AppearanceStateDef& state : pShown->_listState )
                {
                    listStateName.push_back( state._name );
                }
                for ( const AppearancePartDef& part : pShown->_listPart )
                {
                    listPartName.push_back( part._name );
                }
            }
            request._state     = Internal::findByHash( stateHash, listStateName );
            uint32 detachCount = 0;
            if ( Internal::readVarCount( reader, detachCount ) == false )
                return false;
            for ( uint32 detachIndex = 0; detachIndex < detachCount; ++detachIndex )
            {
                request._listDetachedPart.push_back( Internal::findByHash( reader.readUint32(), listPartName ) );
            }
            if ( Internal::readValues( reader, Internal::findVisualSchema( database, pShown ), formatVersion, request._customization ) == false )
                return false;
            outSelection._listSlot.push_back( request );
        }
        if ( formatVersion >= 3 )
        {
            vector<hashed_string> listLayerID;
            for ( const SkinLayerDef& layer : database.getSkinLayers().getLayers() )
                listLayerID.push_back( layer._id );
            uint32 layerCount = 0;
            if ( Internal::readVarCount( reader, layerCount ) == false )
                return false;
            for ( uint32 index = 0; index < layerCount; ++index )
            {
                SkinLayerValue layer;
                layer._layer           = Internal::findByHash( reader.readUint32(), listLayerID );
                layer._color._x        = CustomizationParamDef::dequantizeColorChannel( reader.readBits( Internal::kColorBits ) );
                layer._color._y        = CustomizationParamDef::dequantizeColorChannel( reader.readBits( Internal::kColorBits ) );
                layer._color._z        = CustomizationParamDef::dequantizeColorChannel( reader.readBits( Internal::kColorBits ) );
                layer._color._w        = CustomizationParamDef::dequantizeColorChannel( reader.readBits( Internal::kColorBits ) );
                layer._opacity         = CustomizationParamDef::dequantizeColorChannel( reader.readBits( Internal::kColorBits ) );
                layer._offset._x       = SkinLayerValue::decodeFine( reader.readBits( Internal::kFineBits ), 0.0f, 1.0f );
                layer._offset._y       = SkinLayerValue::decodeFine( reader.readBits( Internal::kFineBits ), 0.0f, 1.0f );
                layer._scale._x        = SkinLayerValue::decodeFine( reader.readBits( Internal::kFineBits ), SkinLayerValue::kMinScale, SkinLayerValue::kMaxScale );
                layer._scale._y        = SkinLayerValue::decodeFine( reader.readBits( Internal::kFineBits ), SkinLayerValue::kMinScale, SkinLayerValue::kMaxScale );
                layer._rotationDegrees = SkinLayerValue::decodeFine( reader.readBits( Internal::kFineBits ), -SkinLayerValue::kMaxRotation, SkinLayerValue::kMaxRotation );
                layer._bMirror         = reader.readBool() ? SW_TRUE : SW_FALSE;
                layer._bHidden         = reader.readBool() ? SW_TRUE : SW_FALSE;
                outSelection._listSkinLayer.push_back( layer );
            }
        }
        return reader.hasOverflowed() == false;
    }

    string AppearanceSelection::encode( const AppearanceSelection& selection, const AppearanceDatabase& database )
    {
        BitWriter writer;
        writer.writeBits( kShareCodeVersion, AppearanceSelectionInternal::kVersionBits );
        AppearanceSelection::writeSelection( writer, selection, database );
        vector<uint8> bytes = writer.releaseBytes();
        const uint32  crc   = HashUtil::computeCRC32( bytes.data(), bytes.size() );
        for ( uint32 byteIndex = 0; byteIndex < AppearanceSelectionInternal::kChecksumBytes; ++byteIndex )
        {
            bytes.push_back( static_cast<uint8>( ( crc >> ( byteIndex * 8 ) ) & 0xffu ) );
        }
        return Base64Util::encodeURL( bytes.data(), bytes.size() );
    }

    expected<void> AppearanceSelection::decode( string_view code, const AppearanceDatabase& database, AppearanceSelection& outSelection )
    {
        using Internal = AppearanceSelectionInternal;
        vector<uint8> bytes;
        if ( Base64Util::decodeURL( StringUtil::trim( code ), bytes ) == false || bytes.size() <= Internal::kChecksumBytes )
            return unexpected( Error{ ErrorKind::InvalidArgument, "not a share code" } );
        const size_t payloadSize = bytes.size() - Internal::kChecksumBytes;
        uint32       storedCRC   = 0;
        for ( uint32 byteIndex = 0; byteIndex < Internal::kChecksumBytes; ++byteIndex )
        {
            storedCRC |= static_cast<uint32>( bytes[payloadSize + byteIndex] ) << ( byteIndex * 8 );
        }
        if ( HashUtil::computeCRC32( bytes.data(), payloadSize ) != storedCRC )
            return unexpected( Error{ ErrorKind::InvalidArgument, "checksum mismatch" } );
        BitReader    reader( bytes.data(), static_cast<int32>( payloadSize ) );
        const uint32 version = reader.readBits( Internal::kVersionBits );
        if ( version < kOldestShareCodeVersion || version > kShareCodeVersion ) // 옛 판은 읽는다(저장된 프리셋이 공유 코드다) — 판을 올리는 커밋은 옛 판 읽기를 남긴다
            return unexpected( Error{ ErrorKind::Unsupported, "unsupported share code version" } );
        if ( AppearanceSelection::readSelection( reader, database, outSelection, version ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "truncated share code" } );
        return {};
    }
} // namespace sw
