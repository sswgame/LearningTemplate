#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceSelection.h"

#include "Core/Container/StringUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/String/Base64Util.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceResolver.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceXmlUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"

namespace sw
{
    SW_LOG_CALLER( "AppearanceSelection" );

    namespace
    {
        struct AppearanceSelectionInternal
        {
            static constexpr uint32 kMaxCount      = 1024; ///< 읽을 때 개수 상한(망가진 · 악의적인 입력이 큰 할당을 만들지 않게)
            static constexpr int32  kKindBits      = 2;
            static constexpr int32  kSliderBits    = 16;
            static constexpr int32  kColorBits     = 8;
            static constexpr int32  kDamageBits    = 8;
            static constexpr int32  kVersionBits   = 8;
            static constexpr uint32 kChecksumBytes = 4;

            static uint32 toHash32( const hashed_string& name ) { return name.empty() ? 0u : static_cast<uint32>( name.getHash() ); }

            /** @brief 받는 쪽이 모르는 해시의 자리 이름(`#1a2b3c4d`)입니다 — 펼칠 때 "지워진 콘텐츠" 로 보고된다. */
            static hashed_string makePlaceholder( uint32 hash )
            {
                if ( hash == 0 )
                    return hashed_string{};
                utf8 arrBuffer[constant::kMaxBuffer16];
                FormatString::formatstring( arrBuffer, constant::kMaxBuffer16, "#%#", Fmt( hash, Format().hex().width( 8 ).zeroPad() ) );
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

            static void addUnique( vector<hashed_string>& inoutList, const hashed_string& name )
            {
                if ( name.empty() == false && AppearanceXmlUtil::containsName( inoutList, name ) == false )
                    inoutList.push_back( name );
            }

            /** @brief 몸 종류 · 체형 · 얼굴은 열린 이름이라 데이터에 나오는 모든 이름을 후보로 모은다. */
            static void collectBodyNames( const AppearanceDatabase& database, vector<hashed_string>& outListName )
            {
                for ( const CharacterAppearanceDef& preset : database.getPresets().getPresets() )
                {
                    for ( const hashed_string& name : preset._listBodyType )
                    {
                        addUnique( outListName, name );
                    }
                    for ( const hashed_string& name : preset._listBodyShape )
                    {
                        addUnique( outListName, name );
                    }
                    for ( const hashed_string& name : preset._listFace )
                    {
                        addUnique( outListName, name );
                    }
                }
                for ( const EquipSetDef& set : database.getSets().getSets() )
                {
                    for ( const EquipSetVariantDef& variant : set._listVariant )
                    {
                        addUnique( outListName, variant._bodyType );
                    }
                }
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
                    const CustomizationParamDef& param = *pSchema->findParameter( pValue->_parameter );
                    writer.writeUint32( toHash32( param._name ) );
                    writer.writeBits( static_cast<uint32>( param._kind ), kKindBits );
                    switch ( param._kind )
                    {
                        case CustomizationKind::Slider:
                        {
                            writer.writeBits( CustomizationUtil::quantizeSlider( param, pValue->_number._x ), kSliderBits );
                            break;
                        }
                        case CustomizationKind::Color:
                        {
                            writer.writeBits( CustomizationUtil::quantizeColorChannel( pValue->_number._x ), kColorBits );
                            writer.writeBits( CustomizationUtil::quantizeColorChannel( pValue->_number._y ), kColorBits );
                            writer.writeBits( CustomizationUtil::quantizeColorChannel( pValue->_number._z ), kColorBits );
                            writer.writeBits( CustomizationUtil::quantizeColorChannel( pValue->_number._w ), kColorBits );
                            break;
                        }
                        case CustomizationKind::Choice:
                        case CustomizationKind::Attachment:
                        {
                            writer.writeUint32( toHash32( pValue->_option ) );
                            break;
                        }
                    }
                }
            }

            [[nodiscard]] static bool readValues( BitReader& reader, const CustomizationSchemaDef* pSchema, CustomizationValueSet& outValues )
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
                            const uint32 step = reader.readBits( kSliderBits );
                            value._number._x  = pParam != nullptr ? CustomizationUtil::dequantizeSlider( *pParam, step ) : 0.0f;
                            break;
                        }
                        case CustomizationKind::Color:
                        {
                            value._number._x = CustomizationUtil::dequantizeColorChannel( reader.readBits( kColorBits ) );
                            value._number._y = CustomizationUtil::dequantizeColorChannel( reader.readBits( kColorBits ) );
                            value._number._z = CustomizationUtil::dequantizeColorChannel( reader.readBits( kColorBits ) );
                            value._number._w = CustomizationUtil::dequantizeColorChannel( reader.readBits( kColorBits ) );
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
                SW_LOG_WARNING( "Appearance slot '%#': '%#' %# - using '%#'", slot.c_str(), requested.c_str(), toString( reason ), fallback.c_str() );
            }

            static void addDropped( AppearanceSelectionReport& outReport, const hashed_string& name )
            {
                outReport._listDroppedParameter.push_back( name );
                SW_LOG_WARNING( "Appearance value '%#' no longer exists - dropped", name.c_str() );
            }

            /** @brief 아이템 인스턴스 값을 보이는 외형의 스키마로 거릅니다. */
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
    const utf8* toString( AppearanceFallbackReason reason )
    {
        switch ( reason )
        {
            case AppearanceFallbackReason::MissingItem:
                return "MissingItem";
            case AppearanceFallbackReason::LockedItem:
                return "LockedItem";
            case AppearanceFallbackReason::UnknownSlot:
                return "UnknownSlot";
            case AppearanceFallbackReason::MissingVisual:
                return "MissingVisual";
        }
        return "Unknown";
    }

    bool AppearanceSelection::includesCategory( const hashed_string& category ) const
    {
        return _listCategory.empty() || AppearanceXmlUtil::containsName( _listCategory, category );
    }

    void AppearanceSelectionUtil::captureSelection( const CharacterAppearanceSpec& spec, AppearanceSelection& outSelection )
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
    }

    void AppearanceSelectionUtil::makePartial( const AppearanceSelection& selection, const AppearanceDatabase& database, const vector<hashed_string>& listCategory,
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
    }

    void AppearanceSelectionUtil::applySelection( const AppearanceDatabase& database, const AppearanceSelection& selection, const CharacterAppearanceSpec& current,
                                                  const IAppearanceUnlockQuery* pUnlockQuery, CharacterAppearanceSpec& outSpec, AppearanceSelectionReport& outReport )
    {
        using Internal = AppearanceSelectionInternal;
        outReport      = AppearanceSelectionReport{};

        // 칸의 기본(지워졌거나 잠긴 아이템이 돌아갈 곳)은 기준 프리셋의 것이다.
        CharacterAppearanceSpec baseSpec = current;
        const bool              bBaseKnown =
            selection._basePresetID.empty() == false && database.getPresets().expand( selection._basePresetID, selection._seed, database.getSlotTable(), database.getSets(), database.getSchemas(), baseSpec );
        if ( selection._basePresetID.empty() == false && bBaseKnown == false )
        {
            outReport._bUnknownBasePreset = SW_TRUE;
            SW_LOG_WARNING( "Appearance base preset '%#' no longer exists - applying on top of the current look", selection._basePresetID.c_str() );
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
            outSpec._customization.setValue( value );
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
                const bool bMissing = pItemCatalog == nullptr || pItemCatalog->findItem( request._itemID ) == nullptr;
                const bool bLocked  = bMissing == false && pUnlockQuery != nullptr && pUnlockQuery->isItemUnlocked( request._itemID ) == false;
                if ( bMissing || bLocked )
                {
                    Internal::addFallback( outReport, request._slot, request._itemID, defaultItem, bMissing ? AppearanceFallbackReason::MissingItem : AppearanceFallbackReason::LockedItem );
                    accepted._itemID = defaultItem;
                    accepted._customization.clear();
                    accepted._listDetachedPart.clear();
                    accepted._damage = 0.0f;
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

    void AppearanceSelectionUtil::previewSelection( const AppearanceDatabase& database, const AppearanceSelection& selection, const CharacterAppearanceSpec& current,
                                                    const IAppearanceUnlockQuery* pUnlockQuery, ResolvedAppearance& outResolved, AppearanceSelectionReport& outReport )
    {
        CharacterAppearanceSpec previewSpec;
        applySelection( database, selection, current, pUnlockQuery, previewSpec, outReport );
        AppearanceResolver::resolve( database, previewSpec, outResolved );
    }

    void AppearanceSelectionCodec::writeSelection( BitWriter& writer, const AppearanceSelection& selection, const AppearanceDatabase& database )
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
        Internal::writeValues( writer, selection._customization, database.getSchemas().findSchema( selection._schema ) );
        Internal::writeVarCount( writer, selection._listSlot.size() );
        for ( const AppearanceSlotRequest& request : selection._listSlot )
        {
            writer.writeUint32( Internal::toHash32( request._slot ) );
            writer.writeUint32( Internal::toHash32( request._itemID ) );
            writer.writeUint32( Internal::toHash32( request._visibleVisual ) );
            writer.writeUint32( Internal::toHash32( request._state ) );
            writer.writeBits( static_cast<uint32>( AppearanceResolver::snapDamage( request._damage ) * 255.0f + 0.5f ), Internal::kDamageBits );
            writer.writeBool( request._bSuppressed == SW_TRUE );
            Internal::writeVarCount( writer, request._listDetachedPart.size() );
            for ( const hashed_string& part : request._listDetachedPart )
            {
                writer.writeUint32( Internal::toHash32( part ) );
            }
            const ItemVisualDef* pShown = Internal::findShownVisual( database, request._itemID, request._visibleVisual );
            Internal::writeValues( writer, request._customization, Internal::findVisualSchema( database, pShown ) );
        }
    }

    bool AppearanceSelectionCodec::readSelection( BitReader& reader, const AppearanceDatabase& database, AppearanceSelection& outSelection )
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
                Internal::addUnique( listCategory, param._category );
            }
        }
        vector<hashed_string> listBodyName;
        Internal::collectBodyNames( database, listBodyName );

        outSelection._basePresetID = Internal::findByHash( reader.readUint32(), listPresetID );
        outSelection._seed         = static_cast<uint32>( reader.readVarUint() );
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
        if ( Internal::readValues( reader, database.getSchemas().findSchema( outSelection._schema ), outSelection._customization ) == false )
            return false;

        vector<hashed_string> listSlotName;
        for ( const AppearanceSlotDef& slot : database.getSlotTable().getSlots() )
        {
            listSlotName.push_back( slot._name );
        }
        vector<hashed_string> listItemID;
        if ( database.getItemCatalog() != nullptr )
        {
            for ( const ItemDef& item : database.getItemCatalog()->getItems() )
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
            if ( Internal::readValues( reader, Internal::findVisualSchema( database, pShown ), request._customization ) == false )
                return false;
            outSelection._listSlot.push_back( request );
        }
        return reader.hasOverflowed() == false;
    }

    string AppearanceShareCode::encode( const AppearanceSelection& selection, const AppearanceDatabase& database )
    {
        BitWriter writer;
        writer.writeBits( kVersion, AppearanceSelectionInternal::kVersionBits );
        AppearanceSelectionCodec::writeSelection( writer, selection, database );
        vector<uint8> bytes = writer.releaseBytes();
        const uint32  crc   = StringUtil::computeCrc32( bytes.data(), bytes.size() );
        for ( uint32 byteIndex = 0; byteIndex < AppearanceSelectionInternal::kChecksumBytes; ++byteIndex )
        {
            bytes.push_back( static_cast<uint8>( ( crc >> ( byteIndex * 8 ) ) & 0xffu ) );
        }
        return Base64Util::encodeURL( bytes.data(), bytes.size() );
    }

    bool AppearanceShareCode::decode( string_view code, const AppearanceDatabase& database, AppearanceSelection& outSelection, string* pOutReason )
    {
        using Internal = AppearanceSelectionInternal;
        vector<uint8> bytes;
        if ( Base64Util::decodeURL( StringUtil::trim( code ), bytes ) == false || bytes.size() <= Internal::kChecksumBytes )
        {
            if ( pOutReason != nullptr )
                *pOutReason = "not a share code";
            return false;
        }
        const size_t payloadSize = bytes.size() - Internal::kChecksumBytes;
        uint32       storedCrc   = 0;
        for ( uint32 byteIndex = 0; byteIndex < Internal::kChecksumBytes; ++byteIndex )
        {
            storedCrc |= static_cast<uint32>( bytes[payloadSize + byteIndex] ) << ( byteIndex * 8 );
        }
        if ( StringUtil::computeCrc32( bytes.data(), payloadSize ) != storedCrc )
        {
            if ( pOutReason != nullptr )
                *pOutReason = "checksum mismatch";
            return false;
        }
        BitReader    reader( bytes.data(), static_cast<int32>( payloadSize ) );
        const uint32 version = reader.readBits( Internal::kVersionBits );
        if ( version == 0 || version > kVersion )
        {
            if ( pOutReason != nullptr )
                *pOutReason = "unsupported share code version";
            return false;
        }
        if ( AppearanceSelectionCodec::readSelection( reader, database, outSelection ) == false )
        {
            if ( pOutReason != nullptr )
                *pOutReason = "truncated share code";
            return false;
        }
        return true;
    }
} // namespace sw
