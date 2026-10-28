#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceDatabase.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"

#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/Fit/FitTables.h"
#include "Engine/FileSystem/AssetIO.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceXMLUtil.h"
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

            /** @brief 데이터베이스 객체마다 새 번호를 줍니다(0 은 쓰지 않는다 — 외형 상태의 "아직 받지 않음"). */
            static uint64 takeNextInstanceID() { return s_nextInstanceID.fetch_add( 1 ); }

            inline static atomic<uint64> s_nextInstanceID{ 1 };
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
        , _reloadedMulticast{}
        , _revision{ 0 }
        , _instanceID{ AppearanceDatabaseInternal::takeNextInstanceID() }
    {
    }

    bool AppearanceDatabase::reloadFromFolder( string_view folder, const ItemCatalog* pItemCatalog, string& outReport )
    {
        // 깨진 파일이 돌고 있는 외형을 지우지 않도록 임시로 먼저 읽어 검사한다(작은 파일이라 두 번 읽는다).
        AppearanceDatabase probe;
        if ( probe.loadFromFolder( folder, pItemCatalog ) == false )
        {
            outReport = probe.getReport().joined();
            return false;
        }
        if ( loadFromFolder( folder, pItemCatalog ) == false )
        {
            outReport = _report.joined(); // 두 읽기 사이에 파일이 또 바뀌었다 — 다음 변경 알림이 다시 읽는다
            return false;
        }
        _reloadedMulticast.broadcast();
        return true;
    }

    Subscription AppearanceDatabase::registerReloaded( const Delegate<void()>& delegate )
    {
        return _reloadedMulticast.add( delegate );
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
            XMLDocument  doc;
            string       absolutePath;
            if ( doc.loadResource( AssetID{ path }, &absolutePath ) == false )
            {
                if ( file._bRequired == SW_TRUE || AppearanceDatabaseInternal::isNotFound( doc.getLastError() ) == false )
                    _report.addErrorWithID( "Appearance.DataUnreadable", "%#: cannot read appearance data (%#)", path, doc.getLastError() );
                continue;
            }
            // 실패는 각 섹션이 _report 에 오류로 남긴다
            (void)loadSectionFromNode( doc.getRoot(), absolutePath.empty() ? string_view( path ) : string_view( absolutePath ) );
        }
        // 프리셋 에셋 — 폴더가 없으면 없는 것이다(선택). 나열은 팩을 아는 `AssetIO` 로 한다(디스크만 훑으면 팩 빌드에서 0개로 읽힌다).
        // 나열이 이름순이라 같은 id 는 뒤 파일이 이긴다(실행마다 같다). 폴더가 있는데 나열에 실패하면 오류다.
        const string             presetFolder = FileUtil::joinPath( folder, kPresetFolder );
        vector<AssetID>          listPresetFile;
        bool                     bPresetsRead = true;
        const AssetFolderListing listing      = AssetIO::collectAssets( AssetID{ presetFolder }, kPresetExtension, listPresetFile );
        if ( listing == AssetFolderListing::Failed )
        {
            _report.addErrorWithID( "Appearance.PresetFolderUnreadable", "%#: cannot list the appearance preset folder", presetFolder );
            bPresetsRead = false;
        }
        for ( const AssetID& presetFile : listPresetFile )
        {
            bPresetsRead = readPresetFile( presetFile ) && bPresetsRead; // 검사는 아래 finishLoad 가 한 번 한다
        }
        // 몸 부품이 피팅 표를 가리키면 그 표들의 영역이 숨김 영역 이름의 기준이다 — 표가 하나도 없으면 영역을 대조할 근거가 없다(숨김 영역은 피팅 표 없이는 쓰이지 않는다).
        vector<hashed_string> listBodyRegion;
        const bool            bHasFitTables = collectBodyRegions( listBodyRegion );
        return finishLoad( pItemCatalog, bHasFitTables ? &listBodyRegion : nullptr ) && bPresetsRead;
    }

    bool AppearanceDatabase::collectBodyRegions( vector<hashed_string>& outListRegion )
    {
        outListRegion.clear();
        // 처음 나온 순서를 지키며 중복을 거른다 — 집합이 거르고 목록이 순서를 든다.
        vector<hashed_string>        listTablesPath;
        unordered_set<hashed_string> uniqueSetTablesPath;
        for ( const ItemVisualDef& visual : _visuals.getVisuals() )
        {
            for ( const AppearancePartDef& part : visual._listPart )
            {
                if ( part._fitTables.empty() == false && uniqueSetTablesPath.insert( part._fitTables ).second )
                    listTablesPath.push_back( part._fitTables );
            }
        }
        unordered_set<hashed_string> uniqueSetRegion;
        for ( const hashed_string& path : listTablesPath )
        {
            const shared_ptr<const FitTables> tables = SharedAssetCache<FitTables>::acquire( path.c_str() );
            if ( tables == nullptr )
            {
                _report.addErrorWithID( "Appearance.FitTablesUnreadable", "fit tables '%#' cannot be read (the loader logged why)", path.c_str() );
                continue;
            }
            for ( const BodyRegionDef& region : tables->getRegions() )
            {
                if ( uniqueSetRegion.insert( region._name ).second )
                    outListRegion.push_back( region._name );
            }
        }
        return listTablesPath.empty() == false;
    }

    bool AppearanceDatabase::loadPresetFile( const AssetID& presetID )
    {
        const size_t errorCountBefore = _report.getErrors().size();
        if ( readPresetFile( presetID ) == false )
            return false;
        validatePresets();
        return _report.getErrors().size() == errorCountBefore;
    }

    bool AppearanceDatabase::readPresetFile( const AssetID& presetID )
    {
        const string_view path = presetID.view();
        XMLDocument       doc;
        if ( doc.loadResource( presetID ) == false )
        {
            _report.addErrorWithID( "Appearance.DataUnreadable", "%#: cannot read the appearance preset (%#)", path, doc.getLastError() );
            return false;
        }
        ++_revision;
        return _presets.loadPresetFromNode( doc.getRoot(), _report, path );
    }

    bool AppearanceDatabase::loadSectionFromXMLText( string_view xmlText, string_view sourceName )
    {
        XMLDocument doc;
        if ( doc.parse( xmlText, sourceName ) == false )
        {
            _report.addErrorWithID( "Appearance.DataUnparsable", "%#: cannot parse appearance data (%#)", sourceName, doc.getLastError() );
            return false;
        }
        return loadSectionFromNode( doc.getRoot(), sourceName );
    }

    bool AppearanceDatabase::loadSectionFromNode( const XMLNode& root, string_view sourceName )
    {
        const utf8* pName = root.getName();
        ++_revision;
        if ( StringUtil::equals( pName, "SlotTable" ) )
            return _slotTable.loadFromNode( root, _report, sourceName );
        if ( StringUtil::equals( pName, "EquipSetCatalog" ) )
            return _sets.loadFromNode( root, _report, sourceName );
        if ( StringUtil::equals( pName, "ItemVisualCatalog" ) )
            return _visuals.loadFromNode( root, _report, sourceName );
        if ( StringUtil::equals( pName, "CustomizationSchemaCatalog" ) )
            return _schemas.loadFromNode( root, _report, sourceName );
        if ( StringUtil::equals( pName, "AppearanceRuleTable" ) )
            return _rules.loadFromNode( root, _report, sourceName );
        if ( StringUtil::equals( pName, "CharacterAppearanceCatalog" ) )
            return _presets.loadFromNode( root, _report, sourceName );
        _report.addErrorWithID( "Appearance.RootUnknown", "%#: unknown appearance data root <%#>", sourceName, pName );
        return false;
    }

    bool AppearanceDatabase::finishLoad( const ItemCatalog* pItemCatalog, const vector<hashed_string>* pListKnownBodyRegion )
    {
        _pItemCatalog = pItemCatalog;
        if ( _slotTable.getSlots().empty() )
            _report.addErrorWithID( "Appearance.SlotTableMissing", "appearance data has no slot table" );
        validateSchemas();
        validateSets();
        validateVisuals( pListKnownBodyRegion );
        validateItems();
        validateRules( pListKnownBodyRegion );
        validatePresets();
        ++_revision;
        return _report.hasErrors() == false;
    }

    const ItemVisualDef* AppearanceDatabase::findItemVisual( const hashed_string& itemID ) const
    {
        const ItemDef* pItem = _pItemCatalog != nullptr ? _pItemCatalog->find( itemID ) : nullptr;
        return pItem != nullptr ? _visuals.findVisual( pItem->_visualID ) : nullptr;
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
        for ( const ItemDef& item : _pItemCatalog->getAll() )
        {
            if ( item._visualID.empty() == false && _visuals.findVisual( item._visualID ) == nullptr )
                _report.addErrorWithID( "Appearance.VisualUnknown", "item '%#' names unknown visual '%#'", item._id.c_str(), item._visualID.c_str() );
            for ( const EquipCondition& condition : item._listEquipCondition )
            {
                const bool bSetCondition = condition._kind == EquipConditionKind::SetComplete || condition._kind == EquipConditionKind::SetPieces;
                if ( bSetCondition && _sets.hasSet( condition._setID ) == false )
                    _report.addErrorWithID( "Appearance.SetUnknown", "item '%#' requires unknown set '%#'", item._id.c_str(), condition._setID.c_str() );
            }
        }
        vector<hashed_string> listCycle;
        if ( EquipConditionUtil::findConditionCycle( *_pItemCatalog, &_sets, listCycle ) )
        {
            string chain;
            for ( const hashed_string& itemID : listCycle )
            {
                if ( chain.empty() == false )
                    chain += " -> ";
                chain += itemID.c_str();
            }
            _report.addErrorWithID( "Appearance.EquipConditionCycle", "equip conditions form a cycle: %#", chain );
        }
        // 공유 코드와 네트워크는 아이템, 외형, 프리셋 id 를 32 비트 해시로 싣는다.
        const vector<ItemDef>& listItem = _pItemCatalog->getAll();
        for ( size_t lhs = 0; lhs < listItem.size(); ++lhs )
        {
            for ( size_t rhs = lhs + 1; rhs < listItem.size(); ++rhs )
            {
                if ( static_cast<uint32>( listItem[lhs]._id.getHash() ) == static_cast<uint32>( listItem[rhs]._id.getHash() ) )
                    _report.addErrorWithID( "Appearance.IDHashClash", "items '%#' and '%#' share a 32-bit id hash - rename one", listItem[lhs]._id.c_str(), listItem[rhs]._id.c_str() );
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
                    _report.addErrorWithID( "Appearance.SlotUnknown", "set '%#' names unknown slot '%#'", set._id.c_str(), piece._slot.c_str() );
            }
            vector<hashed_string> listItem;
            _sets.collectSetItems( set._id, listItem );
            for ( const hashed_string& itemID : listItem )
            {
                if ( _pItemCatalog != nullptr && _pItemCatalog->find( itemID ) == nullptr )
                    _report.addErrorWithID( "Appearance.ItemUnknown", "set '%#' names unknown item '%#'", set._id.c_str(), itemID.c_str() );
            }
            if ( set._completeVisual.empty() == false && _visuals.findVisual( set._completeVisual ) == nullptr )
                _report.addErrorWithID( "Appearance.VisualUnknown", "set '%#' complete visual '%#' is unknown", set._id.c_str(), set._completeVisual.c_str() );
        }
    }

    void AppearanceDatabase::validateVisuals( const vector<hashed_string>* pListKnownBodyRegion )
    {
        for ( const ItemVisualDef& visual : _visuals.getVisuals() )
        {
            if ( visual._occupancy.empty() == false && _slotTable.findOccupancy( visual._occupancy ) == nullptr )
                _report.addErrorWithID( "Appearance.OccupancyUnknown", "visual '%#' names unknown occupancy '%#'", visual._id.c_str(), visual._occupancy.c_str() );
            if ( visual._customization.empty() == false && _schemas.findSchema( visual._customization ) == nullptr )
                _report.addErrorWithID( "Appearance.SchemaUnknown", "visual '%#' names unknown customization schema '%#'", visual._id.c_str(), visual._customization.c_str() );
            if ( pListKnownBodyRegion == nullptr )
                continue;
            for ( const AppearancePartDef& part : visual._listPart )
            {
                for ( const hashed_string& region : part._listHiddenRegion )
                {
                    if ( VectorUtil::contains( *pListKnownBodyRegion, region ) == false )
                        _report.addErrorWithID( "Appearance.BodyRegionUnknown", "visual '%#' part '%#' hides unknown body region '%#'", visual._id.c_str(), part._name.c_str(), region.c_str() );
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
                // 고르기가 추가하는 외형의 주인은 매개변수 이름이다. 슬롯 이름과 겹치면 규칙 대상과 소켓 접두어가 둘 다 가리키게 되어 모호해진다.
                if ( _slotTable.hasSlot( param._name ) )
                    _report.addErrorWithID( "Appearance.ParameterNameIsSlot", "schema '%#' parameter '%#' has the name of a slot - owner names must be unique", schema._id.c_str(), param._name.c_str() );
                for ( const CustomizationOptionDef& option : param._listOption )
                {
                    if ( option._visual.empty() == false && _visuals.findVisual( option._visual ) == nullptr )
                        _report.addErrorWithID( "Appearance.VisualUnknown", "schema '%#' option '%#.%#' names unknown visual '%#'", schema._id.c_str(), param._name.c_str(), option._name.c_str(), option._visual.c_str() );
                    if ( option._variant.empty() == false && isVariantDeclared( option._variant ) == false )
                        _report.addErrorWithID( "Appearance.VisualUndeclared", "schema '%#' option '%#.%#' names variant '%#' no visual declares", schema._id.c_str(), param._name.c_str(), option._name.c_str(), option._variant.c_str() );
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
                    _report.addErrorWithID( "Appearance.SlotUnknown", "rule '%#' condition names unknown slot '%#'", rule._id.c_str(), condition._target.c_str() );
            }
            for ( const AppearanceRuleAction& action : rule._listAction )
            {
                if ( action._target.empty() == false && isOwnerName( action._target ) == false )
                    _report.addErrorWithID( "Appearance.RuleTargetUnknown", "rule '%#' %# names unknown target '%#'", rule._id.c_str(), getEnumName( action._kind ), action._target.c_str() );
                if ( action._kind == AppearanceRuleActionKind::ChooseVariant && isVariantDeclared( action._name ) == false )
                    _report.addErrorWithID( "Appearance.VisualUndeclared", "rule '%#' chooses variant '%#' no visual declares", rule._id.c_str(), action._name.c_str() );
                if ( action._part.empty() == false && isPartDeclared( action._part ) == false )
                    _report.addErrorWithID( "Appearance.VisualUndeclared", "rule '%#' names part '%#' no visual declares", rule._id.c_str(), action._part.c_str() );
                const bool bUnknownBodyRegion = action._kind == AppearanceRuleActionKind::HideRegion && pListKnownBodyRegion != nullptr && VectorUtil::contains( *pListKnownBodyRegion, action._name ) == false;
                if ( bUnknownBodyRegion )
                    _report.addErrorWithID( "Appearance.BodyRegionUnknown", "rule '%#' hides unknown body region '%#'", rule._id.c_str(), action._name.c_str() );
            }
        }
    }

    void AppearanceDatabase::validateItemValues( const CustomizationValueSet& values, const hashed_string& schemaID, const utf8* pWhere )
    {
        if ( values.isEmpty() )
            return;
        const CustomizationSchemaDef* pSchema = _schemas.findSchema( schemaID );
        if ( pSchema == nullptr )
        {
            _report.addErrorWithID( "Appearance.ValuesWithoutSchema", "%# sets customization values but has no customization schema", pWhere );
            return;
        }
        for ( const CustomizationValue& value : values.getValues() )
        {
            const CustomizationParamDef* pParam = pSchema->findParameter( value._parameter );
            if ( pParam == nullptr )
                _report.addErrorWithID( "Appearance.ParameterUnknown", "%# sets unknown parameter '%#' of schema '%#'", pWhere, value._parameter.c_str(), schemaID.c_str() );
            else if ( value._option.empty() == false && pParam->findOption( value._option ) == nullptr )
                _report.addErrorWithID( "Appearance.OptionUnknown", "%# sets unknown option '%#' of '%#'", pWhere, value._option.c_str(), value._parameter.c_str() );
        }
    }

    void AppearanceDatabase::validatePresets()
    {
        for ( const CharacterAppearanceDef& preset : _presets.getPresets() )
        {
            CharacterAppearanceDef merged;
            if ( preset._parent.empty() == false && _presets.findPreset( preset._parent ) == nullptr )
            {
                _report.addErrorWithID( "Appearance.PresetParentUnknown", "preset '%#' names unknown parent '%#'", preset._id.c_str(), preset._parent.c_str() );
                continue;
            }
            if ( _presets.mergeChain( preset._id, merged ) == false )
            {
                _report.addErrorWithID( "Appearance.PresetParentCycle", "preset '%#' parent chain forms a cycle", preset._id.c_str() );
                continue;
            }
            const string where = string( "preset '" ) + preset._id.c_str() + "'";
            if ( preset._schema.empty() == false && _schemas.findSchema( preset._schema ) == nullptr )
                _report.addErrorWithID( "Appearance.SchemaUnknown", "%# names unknown schema '%#'", where, preset._schema.c_str() );
            const CustomizationSchemaDef* pSchema = _schemas.findSchema( merged._schema );
            for ( const CharacterAppearanceValueDef& value : preset._listValue )
            {
                const CustomizationParamDef* pParam = pSchema != nullptr ? pSchema->findParameter( value._parameter ) : nullptr;
                if ( pParam == nullptr )
                {
                    _report.addErrorWithID( "Appearance.ParameterUnknown", "%# sets unknown parameter '%#'", where, value._parameter.c_str() );
                    continue;
                }
                const bool bOptionKind = pParam->_kind == CustomizationKind::Choice || pParam->_kind == CustomizationKind::Attachment;
                const bool bFits       = ( pParam->_kind == CustomizationKind::Slider && value._bHasNumber == SW_TRUE ) || ( pParam->_kind == CustomizationKind::Color && value._listColor.empty() == false ) || ( bOptionKind && value._listOption.empty() == false );
                if ( bFits == false )
                    _report.addErrorWithID( "Appearance.ValueDoesNotFit", "%# value '%#' does not fit its %# parameter", where, value._parameter.c_str(), getEnumName( pParam->_kind ) );
                for ( const hashed_string& option : value._listOption )
                {
                    if ( bOptionKind && pParam->findOption( option ) == nullptr )
                        _report.addErrorWithID( "Appearance.OptionUnknown", "%# value '%#' names unknown option '%#'", where, value._parameter.c_str(), option.c_str() );
                }
            }
            for ( const hashed_string& setID : preset._listSet )
            {
                if ( _sets.hasSet( setID ) == false )
                    _report.addErrorWithID( "Appearance.SetUnknown", "%# equips unknown set '%#'", where, setID.c_str() );
            }
            for ( const hashed_string& body : preset._listBody )
            {
                if ( _visuals.findVisual( body ) == nullptr )
                    _report.addErrorWithID( "Appearance.BodyVisualUnknown", "%# names unknown body visual '%#'", where, body.c_str() );
            }
            for ( const CharacterAppearanceSlotDef& slot : preset._listSlot )
            {
                if ( _slotTable.hasSlot( slot._slot ) == false )
                    _report.addErrorWithID( "Appearance.SlotUnknown", "%# equips unknown slot '%#'", where, slot._slot.c_str() );
                if ( slot._visibleVisual.empty() == false && _visuals.findVisual( slot._visibleVisual ) == nullptr )
                    _report.addErrorWithID( "Appearance.VisualUnknown", "%# slot '%#' shows unknown visual '%#'", where, slot._slot.c_str(), slot._visibleVisual.c_str() );
                for ( const hashed_string& itemID : slot._listItem )
                {
                    const ItemDef*       pItem   = _pItemCatalog != nullptr ? _pItemCatalog->find( itemID ) : nullptr;
                    const ItemVisualDef* pVisual = pItem != nullptr ? _visuals.findVisual( pItem->_visualID ) : nullptr;
                    if ( _pItemCatalog != nullptr && pItem == nullptr )
                        _report.addErrorWithID( "Appearance.ItemUnknown", "%# slot '%#' names unknown item '%#'", where, slot._slot.c_str(), itemID.c_str() );
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
                    _report.addErrorWithID( "Appearance.IDHashClash", "presets '%#' and '%#' share a 32-bit id hash - rename one", listPreset[lhs]._id.c_str(), listPreset[rhs]._id.c_str() );
            }
        }
    }
} // namespace sw
