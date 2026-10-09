#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceDatabase.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Serialization/Xml/XmlDocument.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceXmlUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"

namespace sw
{
    namespace
    {
        struct AppearanceDatabaseInternal
        {
            struct DataFile
            {
                const utf8* _pFileName;
                uint8       _bRequired;
            };

            static constexpr DataFile kArrDataFile[] = {
                {        "slots.xml",  SW_TRUE},
                {         "sets.xml", SW_FALSE},
                {  "itemvisuals.xml", SW_FALSE},
                {"customization.xml", SW_FALSE},
                {        "rules.xml", SW_FALSE},
                {      "presets.xml", SW_FALSE},
            };

            static bool isNotFound( const string& lastError ) { return string_view( lastError.c_str(), lastError.size() ).find( "not found" ) != string_view::npos; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AppearanceDatabase::AppearanceDatabase()
        : _slotTable{}
        , _sets{}
        , _visuals{}
        , _schemas{}
        , _rules{}
        , _presets{}
        , _report{}
        , _pItemCatalog{ nullptr }
        , _revision{ 0 }
    {
    }

    void AppearanceDatabase::clear()
    {
        _slotTable.clear();
        _sets.clear();
        _visuals.clear();
        _schemas.clear();
        _rules.clear();
        _presets.clear();
        _report.clear();
        _pItemCatalog = nullptr;
        ++_revision;
    }

    bool AppearanceDatabase::loadFromFolder( string_view folder, const ItemCatalog* pItemCatalog )
    {
        clear();
        for ( const AppearanceDatabaseInternal::DataFile& file : AppearanceDatabaseInternal::kArrDataFile )
        {
            const string path = FileUtil::joinPath( folder, file._pFileName );
            XmlDocument  doc;
            string       absolutePath;
            if ( doc.loadPath( path, &absolutePath ) == false )
            {
                if ( file._bRequired == SW_TRUE || AppearanceDatabaseInternal::isNotFound( doc.getLastError() ) == false )
                    _report.addError( "%#: cannot read appearance data (%#)", path, doc.getLastError() );
                continue;
            }
            // 실패는 각 섹션이 _report 에 오류로 남긴다
            (void)loadSectionFromNode( doc.getRoot(), absolutePath.empty() ? string_view( path ) : string_view( absolutePath ) );
        }
        return finishLoad( pItemCatalog );
    }

    bool AppearanceDatabase::loadSectionFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        if ( doc.parse( xmlText, sourceName ) == false )
        {
            _report.addError( "%#: cannot parse appearance data (%#)", sourceName, doc.getLastError() );
            return false;
        }
        return loadSectionFromNode( doc.getRoot(), sourceName );
    }

    bool AppearanceDatabase::loadSectionFromNode( const XmlNode& root, string_view sourceName )
    {
        const utf8* pName = root.getName();
        ++_revision;
        if ( StringUtil::equals( pName, "SlotTable", true ) )
            return _slotTable.loadFromNode( root, _report, sourceName );
        if ( StringUtil::equals( pName, "EquipSetCatalog", true ) )
            return _sets.loadFromNode( root, _report, sourceName );
        if ( StringUtil::equals( pName, "ItemVisualCatalog", true ) )
            return _visuals.loadFromNode( root, _report, sourceName );
        if ( StringUtil::equals( pName, "CustomizationSchemaCatalog", true ) )
            return _schemas.loadFromNode( root, _report, sourceName );
        if ( StringUtil::equals( pName, "AppearanceRuleTable", true ) )
            return _rules.loadFromNode( root, _report, sourceName );
        if ( StringUtil::equals( pName, "CharacterAppearanceCatalog", true ) )
            return _presets.loadFromNode( root, _report, sourceName );
        _report.addError( "%#: unknown appearance data root <%#>", sourceName, pName );
        return false;
    }

    bool AppearanceDatabase::finishLoad( const ItemCatalog* pItemCatalog, const vector<hashed_string>* pListKnownBodyRegion )
    {
        _pItemCatalog = pItemCatalog;
        if ( _slotTable.getSlots().empty() )
            _report.addError( "appearance data has no slot table" );
        validateSchemas();
        validateSets();
        validateVisuals( pListKnownBodyRegion );
        validateItems();
        validateRules( pListKnownBodyRegion );
        validatePresets();
        ++_revision;
        return _report.hasErrors() == false;
    }

    const ItemVisualDef* AppearanceDatabase::findItemVisual( const hashed_string& itemId ) const
    {
        const ItemDef* pItem = _pItemCatalog != nullptr ? _pItemCatalog->findItem( itemId ) : nullptr;
        return pItem != nullptr ? _visuals.findVisual( pItem->_visualId ) : nullptr;
    }

    bool AppearanceDatabase::isOwnerName( const hashed_string& name ) const
    {
        if ( _slotTable.hasSlot( name ) )
            return true;
        for ( const CustomizationSchemaDef& schema : _schemas.getSchemas() )
        {
            if ( schema.findParameter( name ) != nullptr )
                return true;
        }
        return false;
    }

    bool AppearanceDatabase::isVariantDeclared( const hashed_string& variant ) const
    {
        for ( const ItemVisualDef& visual : _visuals.getVisuals() )
        {
            for ( const AppearancePartDef& part : visual._listPart )
            {
                if ( part.findVariant( variant ) != nullptr )
                    return true;
            }
        }
        return false;
    }

    bool AppearanceDatabase::isPartDeclared( const hashed_string& part ) const
    {
        for ( const ItemVisualDef& visual : _visuals.getVisuals() )
        {
            if ( visual.findPart( part ) != nullptr )
                return true;
        }
        return false;
    }

    void AppearanceDatabase::validateItems()
    {
        if ( _pItemCatalog == nullptr )
            return;
        for ( const ItemDef& item : _pItemCatalog->getItems() )
        {
            if ( item._visualId.empty() == false && _visuals.findVisual( item._visualId ) == nullptr )
                _report.addError( "item '%#' names unknown visual '%#'", item._id.c_str(), item._visualId.c_str() );
            for ( const EquipCondition& condition : item._listEquipCondition )
            {
                const bool bSetCondition = condition._kind == EquipConditionKind::SetComplete || condition._kind == EquipConditionKind::SetPieces;
                if ( bSetCondition && _sets.hasSet( condition._setId ) == false )
                    _report.addError( "item '%#' requires unknown set '%#'", item._id.c_str(), condition._setId.c_str() );
            }
        }
        vector<hashed_string> listCycle;
        if ( EquipConditionUtil::findConditionCycle( *_pItemCatalog, &_sets, listCycle ) )
        {
            string chain;
            for ( const hashed_string& itemId : listCycle )
            {
                if ( chain.empty() == false )
                    chain += " -> ";
                chain += itemId.c_str();
            }
            _report.addError( "equip conditions form a cycle: %#", chain );
        }
        // 공유 코드 · 네트워크는 아이템 · 외형 · 프리셋 id 를 32 비트 해시로 싣는다.
        const vector<ItemDef>& listItem = _pItemCatalog->getItems();
        for ( size_t lhs = 0; lhs < listItem.size(); ++lhs )
        {
            for ( size_t rhs = lhs + 1; rhs < listItem.size(); ++rhs )
            {
                if ( static_cast<uint32>( listItem[lhs]._id.getHash() ) == static_cast<uint32>( listItem[rhs]._id.getHash() ) )
                    _report.addError( "items '%#' and '%#' share a 32-bit id hash - rename one", listItem[lhs]._id.c_str(), listItem[rhs]._id.c_str() );
            }
        }
    }

    void AppearanceDatabase::validateSets()
    {
        for ( const EquipSetDef& set : _sets.getSets() )
        {
            for ( const EquipSetPieceDef& piece : set._listPiece )
            {
                if ( _slotTable.hasSlot( piece._slot ) == false )
                    _report.addError( "set '%#' names unknown slot '%#'", set._id.c_str(), piece._slot.c_str() );
            }
            vector<hashed_string> listItem;
            _sets.collectSetItems( set._id, listItem );
            for ( const hashed_string& itemId : listItem )
            {
                if ( _pItemCatalog != nullptr && _pItemCatalog->findItem( itemId ) == nullptr )
                    _report.addError( "set '%#' names unknown item '%#'", set._id.c_str(), itemId.c_str() );
            }
            if ( set._completeVisual.empty() == false && _visuals.findVisual( set._completeVisual ) == nullptr )
                _report.addError( "set '%#' complete visual '%#' is unknown", set._id.c_str(), set._completeVisual.c_str() );
        }
    }

    void AppearanceDatabase::validateVisuals( const vector<hashed_string>* pListKnownBodyRegion )
    {
        for ( const ItemVisualDef& visual : _visuals.getVisuals() )
        {
            if ( visual._occupancy.empty() == false && _slotTable.findOccupancy( visual._occupancy ) == nullptr )
                _report.addError( "visual '%#' names unknown occupancy '%#'", visual._id.c_str(), visual._occupancy.c_str() );
            if ( visual._customization.empty() == false && _schemas.findSchema( visual._customization ) == nullptr )
                _report.addError( "visual '%#' names unknown customization schema '%#'", visual._id.c_str(), visual._customization.c_str() );
            if ( pListKnownBodyRegion == nullptr )
                continue;
            for ( const AppearancePartDef& part : visual._listPart )
            {
                for ( const hashed_string& region : part._listHiddenRegion )
                {
                    if ( AppearanceXmlUtil::containsName( *pListKnownBodyRegion, region ) == false )
                        _report.addError( "visual '%#' part '%#' hides unknown body region '%#'", visual._id.c_str(), part._name.c_str(), region.c_str() );
                }
            }
        }
    }

    void AppearanceDatabase::validateSchemas()
    {
        for ( const CustomizationSchemaDef& schema : _schemas.getSchemas() )
        {
            for ( const CustomizationParamDef& param : schema._listParameter )
            {
                // 고르기가 더하는 외형의 주인은 매개변수 이름이다 — 칸 이름과 겹치면 규칙 대상 · 소켓 앞머리가 둘을 가리킨다.
                if ( _slotTable.hasSlot( param._name ) )
                    _report.addError( "schema '%#' parameter '%#' has the name of a slot - owner names must be unique", schema._id.c_str(), param._name.c_str() );
                for ( const CustomizationOptionDef& option : param._listOption )
                {
                    if ( option._visual.empty() == false && _visuals.findVisual( option._visual ) == nullptr )
                        _report.addError( "schema '%#' option '%#.%#' names unknown visual '%#'", schema._id.c_str(), param._name.c_str(), option._name.c_str(), option._visual.c_str() );
                    if ( option._variant.empty() == false && isVariantDeclared( option._variant ) == false )
                        _report.addError( "schema '%#' option '%#.%#' names variant '%#' no visual declares", schema._id.c_str(), param._name.c_str(), option._name.c_str(), option._variant.c_str() );
                }
            }
        }
    }

    void AppearanceDatabase::validateRules( const vector<hashed_string>* pListKnownBodyRegion )
    {
        for ( const AppearanceRuleDef& rule : _rules.getRules() )
        {
            for ( const AppearanceRuleCondition& condition : rule._listCondition )
            {
                const bool bNamesOwner = condition._kind == AppearanceRuleConditionKind::TargetTag || condition._kind == AppearanceRuleConditionKind::Occupied;
                if ( bNamesOwner && isOwnerName( condition._target ) == false )
                    _report.addError( "rule '%#' condition names unknown slot '%#'", rule._id.c_str(), condition._target.c_str() );
            }
            for ( const AppearanceRuleAction& action : rule._listAction )
            {
                if ( action._target.empty() == false && isOwnerName( action._target ) == false )
                    _report.addError( "rule '%#' %# names unknown target '%#'", rule._id.c_str(), toString( action._kind ), action._target.c_str() );
                if ( action._kind == AppearanceRuleActionKind::ChooseVariant && isVariantDeclared( action._name ) == false )
                    _report.addError( "rule '%#' chooses variant '%#' no visual declares", rule._id.c_str(), action._name.c_str() );
                if ( action._part.empty() == false && isPartDeclared( action._part ) == false )
                    _report.addError( "rule '%#' names part '%#' no visual declares", rule._id.c_str(), action._part.c_str() );
                if ( action._kind == AppearanceRuleActionKind::HideRegion && pListKnownBodyRegion != nullptr && AppearanceXmlUtil::containsName( *pListKnownBodyRegion, action._name ) == false )
                    _report.addError( "rule '%#' hides unknown body region '%#'", rule._id.c_str(), action._name.c_str() );
            }
        }
    }

    void AppearanceDatabase::validateItemValues( const CustomizationValueSet& values, const hashed_string& schemaId, const utf8* pWhere )
    {
        if ( values.isEmpty() )
            return;
        const CustomizationSchemaDef* pSchema = _schemas.findSchema( schemaId );
        if ( pSchema == nullptr )
        {
            _report.addError( "%# sets customization values but has no customization schema", pWhere );
            return;
        }
        for ( const CustomizationValue& value : values.getValues() )
        {
            const CustomizationParamDef* pParam = pSchema->findParameter( value._parameter );
            if ( pParam == nullptr )
                _report.addError( "%# sets unknown parameter '%#' of schema '%#'", pWhere, value._parameter.c_str(), schemaId.c_str() );
            else if ( value._option.empty() == false && pParam->findOption( value._option ) == nullptr )
                _report.addError( "%# sets unknown option '%#' of '%#'", pWhere, value._option.c_str(), value._parameter.c_str() );
        }
    }

    void AppearanceDatabase::validatePresets()
    {
        for ( const CharacterAppearanceDef& preset : _presets.getPresets() )
        {
            CharacterAppearanceDef merged;
            if ( preset._parent.empty() == false && _presets.findPreset( preset._parent ) == nullptr )
            {
                _report.addError( "preset '%#' names unknown parent '%#'", preset._id.c_str(), preset._parent.c_str() );
                continue;
            }
            if ( _presets.mergeChain( preset._id, merged ) == false )
            {
                _report.addError( "preset '%#' parent chain forms a cycle", preset._id.c_str() );
                continue;
            }
            const string where = string( "preset '" ) + preset._id.c_str() + "'";
            if ( preset._schema.empty() == false && _schemas.findSchema( preset._schema ) == nullptr )
                _report.addError( "%# names unknown schema '%#'", where, preset._schema.c_str() );
            const CustomizationSchemaDef* pSchema = _schemas.findSchema( merged._schema );
            for ( const CharacterAppearanceValueDef& value : preset._listValue )
            {
                const CustomizationParamDef* pParam = pSchema != nullptr ? pSchema->findParameter( value._parameter ) : nullptr;
                if ( pParam == nullptr )
                {
                    _report.addError( "%# sets unknown parameter '%#'", where, value._parameter.c_str() );
                    continue;
                }
                const bool bOptionKind = pParam->_kind == CustomizationKind::Choice || pParam->_kind == CustomizationKind::Attachment;
                const bool bFits       = ( pParam->_kind == CustomizationKind::Slider && value._bHasNumber == SW_TRUE ) || ( pParam->_kind == CustomizationKind::Color && value._listColor.empty() == false ) || ( bOptionKind && value._listOption.empty() == false );
                if ( bFits == false )
                    _report.addError( "%# value '%#' does not fit its %# parameter", where, value._parameter.c_str(), toString( pParam->_kind ) );
                for ( const hashed_string& option : value._listOption )
                {
                    if ( bOptionKind && pParam->findOption( option ) == nullptr )
                        _report.addError( "%# value '%#' names unknown option '%#'", where, value._parameter.c_str(), option.c_str() );
                }
            }
            for ( const hashed_string& setId : preset._listSet )
            {
                if ( _sets.hasSet( setId ) == false )
                    _report.addError( "%# equips unknown set '%#'", where, setId.c_str() );
            }
            for ( const hashed_string& body : preset._listBody )
            {
                if ( _visuals.findVisual( body ) == nullptr )
                    _report.addError( "%# names unknown body visual '%#'", where, body.c_str() );
            }
            for ( const CharacterAppearanceSlotDef& slot : preset._listSlot )
            {
                if ( _slotTable.hasSlot( slot._slot ) == false )
                    _report.addError( "%# equips unknown slot '%#'", where, slot._slot.c_str() );
                if ( slot._visibleVisual.empty() == false && _visuals.findVisual( slot._visibleVisual ) == nullptr )
                    _report.addError( "%# slot '%#' shows unknown visual '%#'", where, slot._slot.c_str(), slot._visibleVisual.c_str() );
                for ( const hashed_string& itemId : slot._listItem )
                {
                    const ItemDef*       pItem   = _pItemCatalog != nullptr ? _pItemCatalog->findItem( itemId ) : nullptr;
                    const ItemVisualDef* pVisual = pItem != nullptr ? _visuals.findVisual( pItem->_visualId ) : nullptr;
                    if ( _pItemCatalog != nullptr && pItem == nullptr )
                        _report.addError( "%# slot '%#' names unknown item '%#'", where, slot._slot.c_str(), itemId.c_str() );
                    else if ( pVisual != nullptr )
                        validateItemValues( slot._customization, pVisual->_customization, where.c_str() );
                }
            }
        }
        // 프리셋 id 도 32 비트 해시로 싣는다.
        const vector<CharacterAppearanceDef>& listPreset = _presets.getPresets();
        for ( size_t lhs = 0; lhs < listPreset.size(); ++lhs )
        {
            for ( size_t rhs = lhs + 1; rhs < listPreset.size(); ++rhs )
            {
                if ( static_cast<uint32>( listPreset[lhs]._id.getHash() ) == static_cast<uint32>( listPreset[rhs]._id.getHash() ) )
                    _report.addError( "presets '%#' and '%#' share a 32-bit id hash - rename one", listPreset[lhs]._id.c_str(), listPreset[rhs]._id.c_str() );
            }
        }
    }
} // namespace sw
