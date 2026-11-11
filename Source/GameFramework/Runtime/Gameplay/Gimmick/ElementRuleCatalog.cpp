#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/ElementRuleCatalog.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct ElementRuleCatalogInternal
        {
            static constexpr const utf8* kRootName = "ElementRules";

            static bool isNamed( const XMLNode& node, const utf8* pName ) { return StringUtil::equals( node.getName(), pName ); }
        };

    } // namespace

    /** 격자가 옛 테이블을 가리키므로 제자리에서 바꾸지 않고 떼어 낸다. 스키마 테스트와 카탈로그 편집기도 이 정의로 파일을 읽는다. */
    SW_MODULE_SHARED_ASSET_CACHE( ElementRules, ElementRuleCatalog, SW_GF_API,
                                  ElementRuleCatalog::kAssetType.withLoader( AssetTypeLoader::of<ElementRuleCatalog>( MemoryTag::Game, SharedAssetReloadMode::Detach ) ) );
} // namespace sw

namespace sw
{
    optional<int32> ElementMaterialDef::getParam( const hashed_string& name ) const
    {
        for ( size_t index = 0; index < _listParamName.size(); ++index )
        {
            if ( _listParamName[index] == name )
                return _listParamValue[index];
        }
        return nullopt;
    }

    void ElementRuleCatalog::clear()
    {
        _listFlag.clear();
        _listMaterial.clear();
        _listStatus.clear();
        _listStimulus.clear();
        _listStepRule.clear();
        _stepTime = kDefaultStepTime;
    }

    optional<int32> ElementRuleCatalog::addFlag( const hashed_string& id )
    {
        const optional<int32> existing = findFlag( id );
        if ( existing.has_value() )
            return existing;
        if ( static_cast<int32>( _listFlag.size() ) >= kMaxFlagCount )
            return nullopt;
        _listFlag.push_back( id );
        return static_cast<int32>( _listFlag.size() ) - 1;
    }

    optional<int32> ElementRuleCatalog::addMaterial( const hashed_string& id, const vector<hashed_string>& listFlag )
    {
        if ( findMaterial( id ).has_value() || static_cast<int32>( _listMaterial.size() ) >= kMaxMaterialCount )
            return nullopt;
        ElementMaterialDef material;
        material._id = id;
        for ( const hashed_string& flagName : listFlag )
        {
            const optional<int32> flag = findFlag( flagName );
            if ( flag.has_value() == false )
                return nullopt;
            material._flags |= 1u << static_cast<uint32>( *flag );
        }
        _listMaterial.push_back( material );
        return static_cast<int32>( _listMaterial.size() ) - 1;
    }

    void ElementRuleCatalog::setMaterialParam( int32 material, const hashed_string& name, int32 value )
    {
        if ( material < 0 || material >= static_cast<int32>( _listMaterial.size() ) )
            return;
        ElementMaterialDef& def = _listMaterial[static_cast<size_t>( material )];
        for ( size_t index = 0; index < def._listParamName.size(); ++index )
        {
            if ( def._listParamName[index] == name )
            {
                def._listParamValue[index] = value;
                return;
            }
        }
        def._listParamName.push_back( name );
        def._listParamValue.push_back( value );
    }

    optional<int32> ElementRuleCatalog::addStatus( const hashed_string& id, ElementStatusKind kind, int32 steps )
    {
        if ( findStatus( id ).has_value() || static_cast<int32>( _listStatus.size() ) >= kMaxStatusCount )
            return nullopt;
        ElementStatusDef status;
        status._id    = id;
        status._kind  = kind;
        status._steps = steps;
        _listStatus.push_back( status );
        return static_cast<int32>( _listStatus.size() ) - 1;
    }

    int32 ElementRuleCatalog::addStimulus( const hashed_string& id )
    {
        const optional<int32> existing = findStimulus( id );
        if ( existing.has_value() )
            return *existing;
        ElementStimulusDef stimulus;
        stimulus._id = id;
        _listStimulus.push_back( stimulus );
        return static_cast<int32>( _listStimulus.size() ) - 1;
    }

    void ElementRuleCatalog::addStimulusRule( int32 stimulus, const ElementStimulusRule& rule )
    {
        if ( 0 <= stimulus && stimulus < static_cast<int32>( _listStimulus.size() ) )
            _listStimulus[static_cast<size_t>( stimulus )]._listRule.push_back( rule );
    }

    void ElementRuleCatalog::addStepRule( const ElementStepRule& rule )
    {
        _listStepRule.push_back( rule );
    }

    optional<int32> ElementRuleCatalog::findFlag( const hashed_string& id ) const
    {
        for ( size_t index = 0; index < _listFlag.size(); ++index )
        {
            if ( _listFlag[index] == id )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    optional<int32> ElementRuleCatalog::findMaterial( const hashed_string& id ) const
    {
        for ( size_t index = 0; index < _listMaterial.size(); ++index )
        {
            if ( _listMaterial[index]._id == id )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    optional<int32> ElementRuleCatalog::findStatus( const hashed_string& id ) const
    {
        for ( size_t index = 0; index < _listStatus.size(); ++index )
        {
            if ( _listStatus[index]._id == id )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    optional<int32> ElementRuleCatalog::findStimulus( const hashed_string& id ) const
    {
        for ( size_t index = 0; index < _listStimulus.size(); ++index )
        {
            if ( _listStimulus[index]._id == id )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    bool ElementRuleCatalog::hasFlag( int32 material, int32 flag ) const
    {
        const bool bInvalidFlagRequest = material < 0 || material >= static_cast<int32>( _listMaterial.size() ) || flag < 0;
        if ( bInvalidFlagRequest )
            return false;
        return ( _listMaterial[static_cast<size_t>( material )]._flags & ( 1u << static_cast<uint32>( flag ) ) ) != 0;
    }

    bool ElementRuleCatalog::loadFromAsset( const AssetID& assetID )
    {
        XMLDocument doc;
        XMLNode     root;
        string      sourceName;
        if ( XMLReadUtil::loadRootOrWarn( doc, assetID, ElementRuleCatalogInternal::kRootName, root, sourceName ) == false )
            return false;
        return loadRoot( root, sourceName );
    }

    bool ElementRuleCatalog::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        XMLDocument doc;
        XMLNode     root;
        if ( XMLReadUtil::parseRootOrWarn( doc, xmlText, sourceName, ElementRuleCatalogInternal::kRootName, root ) == false )
            return false;
        return loadRoot( root, sourceName );
    }

    bool ElementRuleCatalog::readIndex( const hashed_string& name, const XMLNode& node, const utf8* pAttribute, optional<int32> ( ElementRuleCatalog::*pFind )( const hashed_string& ) const,
                                        optional<int32>& outIndex, string_view sourceName ) const
    {
        outIndex.reset();
        if ( name.empty() ) // 적지 않으면 "없음"(빈 값)
            return true;
        outIndex = ( this->*pFind )( name );
        if ( outIndex.has_value() )
            return true;
        SW_LOG_WARNING_ID( "ElementRuleCatalog.NamesNothingDeclaredTable", "%#: <%#> %#=\"%#\" names nothing declared in this table", sourceName, node.getName(), pAttribute, name.c_str() );
        return false;
    }

    bool ElementRuleCatalog::readStimulusRule( const XMLNode& node, ElementStimulusRule& outRule, string_view sourceName ) const
    {
        ElementStimulusRuleParams params;
        bool                      bValid = XMLFields::readProperties( node, params, sourceName );
        bValid                           = readIndex( params._material, node, "material", &ElementRuleCatalog::findMaterial, outRule._material, sourceName ) && bValid;
        bValid                           = readIndex( params._flag, node, "flag", &ElementRuleCatalog::findFlag, outRule._flag, sourceName ) && bValid;
        bValid                           = readIndex( params._status, node, "status", &ElementRuleCatalog::findStatus, outRule._status, sourceName ) && bValid;
        bValid                           = readIndex( params._without, node, "without", &ElementRuleCatalog::findStatus, outRule._without, sourceName ) && bValid;
        bValid                           = readIndex( params._setMaterial, node, "setMaterial", &ElementRuleCatalog::findMaterial, outRule._setMaterial, sourceName ) && bValid;
        bValid                           = readIndex( params._addStatus, node, "addStatus", &ElementRuleCatalog::findStatus, outRule._addStatus, sourceName ) && bValid;
        bValid                           = readIndex( params._removeStatus, node, "removeStatus", &ElementRuleCatalog::findStatus, outRule._removeStatus, sourceName ) && bValid;
        bValid                           = readIndex( params._floodStatus, node, "floodStatus", &ElementRuleCatalog::findStatus, outRule._floodStatus, sourceName ) && bValid;
        bValid                           = readIndex( params._through, node, "through", &ElementRuleCatalog::findFlag, outRule._floodThrough, sourceName ) && bValid;
        outRule._event                   = params._event;
        if ( outRule._floodStatus.has_value() && outRule._floodThrough.has_value() == false )
        {
            SW_LOG_WARNING_ID( "ElementRuleCatalog.FloodStatusNeedsThroughFlag", "%#: <On floodStatus> needs through=\"<flag>\"", sourceName );
            bValid = false;
        }
        return bValid;
    }

    bool ElementRuleCatalog::readStepRule( const XMLNode& node, ElementStepRule& outRule, string_view sourceName ) const
    {
        if ( StringUtil::equals( node.getName(), "Expire" ) )
            outRule._kind = ElementStepRuleKind::Expire;
        else if ( StringUtil::equals( node.getName(), "Convert" ) )
            outRule._kind = ElementStepRuleKind::Convert;
        else if ( StringUtil::equals( node.getName(), "Spread" ) )
            outRule._kind = ElementStepRuleKind::Spread;
        else
        {
            SW_LOG_WARNING_ID( "ElementRuleCatalog.UnknownStepRuleExpire", "%#: unknown step rule <%#> (Expire, Convert, Spread)", sourceName, node.getName() );
            return false;
        }
        ElementStepRuleParams params; // 상태 필수 · 형태는 ENUM() 이름 · after >= 0
        bool                  bValid = XMLFields::readProperties( node, params, sourceName );
        bValid                       = readIndex( params._status, node, "status", &ElementRuleCatalog::findStatus, outRule._status, sourceName ) && bValid;
        bValid                       = readIndex( params._neighbor, node, "neighbor", &ElementRuleCatalog::findMaterial, outRule._material, sourceName ) && bValid;
        bValid                       = readIndex( params._setMaterial, node, "setMaterial", &ElementRuleCatalog::findMaterial, outRule._setMaterial, sourceName ) && bValid;
        bValid                       = readIndex( params._to, node, "to", &ElementRuleCatalog::findFlag, outRule._toFlag, sourceName ) && bValid;
        outRule._after               = params._after;
        outRule._bCrosswind          = params._bCrosswind;
        outRule._stepsParam          = params._stepsParam;
        outRule._event               = params._event;
        outRule._pattern             = params._pattern;
        if ( bValid && outRule._status.has_value() == false )
        {
            SW_LOG_WARNING_ID( "ElementRuleCatalog.NeedsStatus", "%#: <%#> needs a status", sourceName, node.getName() );
            bValid = false;
        }
        return bValid;
    }

    bool ElementRuleCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        clear();
        // 적지 않으면 clear() 가 둔 기본 스텝 길이다. 0 이하 · 깨진 값은 오류다.
        ElementRuleRootParams rootParams{ _stepTime };
        const bool            bRoot = XMLFields::readProperties( root, rootParams, sourceName ); // stepTime > 0 은 PROPERTY 가 본다
        _stepTime                   = bRoot ? rootParams._stepTime : _stepTime;
        // 이름은 앞에서 선언한 것만 가리킬 수 있다. 그래서 플래그, 상태, 재질을 먼저 모두 읽고 규칙을 읽는다.
        using Element                          = XMLCatalogElement<ElementRuleCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&ElementRuleCatalog::loadFlag>( "Flag" ).uncounted(),
            Element::of<&ElementRuleCatalog::loadStatus>( "Status" ).uncounted(),
            Element::of<&ElementRuleCatalog::loadMaterial>( "Material" ).uncounted(),
            Element::of<&ElementRuleCatalog::loadStimulus>( "Stimulus" ).uncounted(),
            Element::of<&ElementRuleCatalog::loadStep>( "Step" ).uncounted(),
        };
        return XMLCatalogReadResult::readInTableOrder( *this, root, sourceName, kArrElement ).isClean() && bRoot;
    }

    bool ElementRuleCatalog::loadFlag( const XMLNode& node, string_view sourceName )
    {
        ElementNameParams flag;
        const bool        bRead = XMLFields::readProperties( node, flag, sourceName );
        if ( addFlag( flag._id ).has_value() == false )
        {
            SW_LOG_WARNING_ID( "ElementRuleCatalog.MoreFlags", "%#: more than %# flags", sourceName, kMaxFlagCount );
            return false;
        }
        return bRead;
    }

    bool ElementRuleCatalog::loadStatus( const XMLNode& node, string_view sourceName )
    {
        ElementStatusDef status; // 종류는 ENUM() 이름 · steps >= 1
        const bool       bRead = XMLFields::readProperties( node, status, sourceName );
        if ( addStatus( status._id, status._kind, status._steps ).has_value() == false )
        {
            SW_LOG_WARNING_ID( "ElementRuleCatalog.DuplicateStatusMoreStatuses", "%#: duplicate status or more than %# statuses", sourceName, kMaxStatusCount );
            return false;
        }
        return bRead;
    }

    bool ElementRuleCatalog::loadMaterial( const XMLNode& child, string_view sourceName )
    {
        bool bValid = true;

        vector<hashed_string> listFlag;
        StringUtil::forEachToken( child.getAttributeText( "flags" ), ", ;", [&listFlag]( string_view token )
        { listFlag.push_back( hashed_string( string( token ).c_str() ) ); } );
        const optional<int32> material = addMaterial( hashed_string( string( child.getAttributeText( "id" ) ).c_str() ), listFlag );
        if ( material.has_value() == false )
        {
            SW_LOG_WARNING_ID( "ElementRuleCatalog.MaterialIdDuplicateNames", "%#: <Material id=\"%#\"> is a duplicate or names an undeclared flag", sourceName, child.getAttributeText( "id" ) );
            bValid = false;
            return false;
        }
        for ( XMLAttribute attribute = child.getFirstAttribute(); attribute; attribute = attribute.getNext() )
        {
            const bool bReserved = StringUtil::equals( attribute.getName(), "id" ) || StringUtil::equals( attribute.getName(), "flags" );
            if ( bReserved )
                continue;
            int32 value{ 0 };
            if ( StringUtil::parseInt( attribute.getValue(), value ) == false )
            {
                SW_LOG_WARNING_ID( "ElementRuleCatalog.MaterialIdNotInteger", "%#: <Material id=\"%#\"> %#=\"%#\" is not an integer", sourceName, child.getAttributeText( "id" ), attribute.getName(), attribute.getValue() );
                bValid = false;
                continue;
            }
            setMaterialParam( *material, hashed_string( attribute.getName() ), value );
        }
        return bValid;
    }

    bool ElementRuleCatalog::loadStimulus( const XMLNode& child, string_view sourceName )
    {
        bool bValid = true;

        ElementNameParams stimulusProperties;
        bValid               = XMLFields::readProperties( child, stimulusProperties, sourceName ) && bValid;
        const int32 stimulus = addStimulus( stimulusProperties._id );
        for ( XMLNode ruleNode = child.findChild( "On" ); ruleNode; ruleNode = ruleNode.findNextSibling( "On" ) )
        {
            ElementStimulusRule rule;
            bValid = readStimulusRule( ruleNode, rule, sourceName ) && bValid;
            addStimulusRule( stimulus, rule );
        }
        return bValid;
    }

    bool ElementRuleCatalog::loadStep( const XMLNode& stepNode, string_view sourceName )
    {
        bool bValid = true;

        for ( XMLNode ruleNode = stepNode.findChild(); ruleNode; ruleNode = ruleNode.findNextSibling() )
        {
            ElementStepRule rule;
            if ( readStepRule( ruleNode, rule, sourceName ) )
                addStepRule( rule );
            else
                bValid = false;
        }
        return bValid;
    }
} // namespace sw
