#include "pch.h"

#include "GameFramework/Gimmick/ElementRuleTable.h"

#include "Core/Container/unordered_map.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "ElementRuleTable" );

    namespace
    {
        struct ElementRuleTableInternal
        {
            static constexpr const utf8* kRootName = "ElementRules";

            static bool isNamed( const XmlNode& node, const utf8* pName ) { return StringUtil::equals( node.getName(), pName, true ); }
        };

        std::mutex                                          s_sharedTableMutex{};
        unordered_map<string, unique_ptr<ElementRuleTable>> s_mapSharedTable{};
    } // namespace
} // namespace sw

namespace sw
{
    int32 ElementMaterialDef::getParam( const hashed_string& name, int32 fallback ) const
    {
        for ( size_t index = 0; index < _listParamName.size(); ++index )
        {
            if ( _listParamName[index] == name )
                return _listParamValue[index];
        }
        return fallback;
    }

    void ElementRuleTable::clear()
    {
        _listFlag.clear();
        _listMaterial.clear();
        _listStatus.clear();
        _listStimulus.clear();
        _listStepRule.clear();
        _stepTime = 0.25f;
    }

    int32 ElementRuleTable::addFlag( const hashed_string& id )
    {
        const int32 existing = findFlag( id );
        if ( existing >= 0 )
            return existing;
        if ( static_cast<int32>( _listFlag.size() ) >= kMaxFlagCount )
            return -1;
        _listFlag.push_back( id );
        return static_cast<int32>( _listFlag.size() ) - 1;
    }

    int32 ElementRuleTable::addMaterial( const hashed_string& id, const vector<hashed_string>& listFlag )
    {
        if ( findMaterial( id ) >= 0 || static_cast<int32>( _listMaterial.size() ) >= kMaxMaterialCount )
            return -1;
        ElementMaterialDef material;
        material._id = id;
        for ( const hashed_string& flagName : listFlag )
        {
            const int32 flag = findFlag( flagName );
            if ( flag < 0 )
                return -1;
            material._flags |= 1u << static_cast<uint32>( flag );
        }
        _listMaterial.push_back( material );
        return static_cast<int32>( _listMaterial.size() ) - 1;
    }

    void ElementRuleTable::setMaterialParam( int32 material, const hashed_string& name, int32 value )
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

    int32 ElementRuleTable::addStatus( const hashed_string& id, ElementStatusKind kind, int32 steps )
    {
        if ( findStatus( id ) >= 0 || static_cast<int32>( _listStatus.size() ) >= kMaxStatusCount )
            return -1;
        ElementStatusDef status;
        status._id    = id;
        status._kind  = kind;
        status._steps = steps;
        _listStatus.push_back( status );
        return static_cast<int32>( _listStatus.size() ) - 1;
    }

    int32 ElementRuleTable::addStimulus( const hashed_string& id )
    {
        const int32 existing = findStimulus( id );
        if ( existing >= 0 )
            return existing;
        ElementStimulusDef stimulus;
        stimulus._id = id;
        _listStimulus.push_back( stimulus );
        return static_cast<int32>( _listStimulus.size() ) - 1;
    }

    void ElementRuleTable::addStimulusRule( int32 stimulus, const ElementStimulusRule& rule )
    {
        if ( 0 <= stimulus && stimulus < static_cast<int32>( _listStimulus.size() ) )
            _listStimulus[static_cast<size_t>( stimulus )]._listRule.push_back( rule );
    }

    void ElementRuleTable::addStepRule( const ElementStepRule& rule ) { _listStepRule.push_back( rule ); }

    int32 ElementRuleTable::findFlag( const hashed_string& id ) const
    {
        for ( size_t index = 0; index < _listFlag.size(); ++index )
        {
            if ( _listFlag[index] == id )
                return static_cast<int32>( index );
        }
        return -1;
    }

    int32 ElementRuleTable::findMaterial( const hashed_string& id ) const
    {
        for ( size_t index = 0; index < _listMaterial.size(); ++index )
        {
            if ( _listMaterial[index]._id == id )
                return static_cast<int32>( index );
        }
        return -1;
    }

    int32 ElementRuleTable::findStatus( const hashed_string& id ) const
    {
        for ( size_t index = 0; index < _listStatus.size(); ++index )
        {
            if ( _listStatus[index]._id == id )
                return static_cast<int32>( index );
        }
        return -1;
    }

    int32 ElementRuleTable::findStimulus( const hashed_string& id ) const
    {
        for ( size_t index = 0; index < _listStimulus.size(); ++index )
        {
            if ( _listStimulus[index]._id == id )
                return static_cast<int32>( index );
        }
        return -1;
    }

    bool ElementRuleTable::hasFlag( int32 material, int32 flag ) const
    {
        if ( material < 0 || material >= static_cast<int32>( _listMaterial.size() ) || flag < 0 )
            return false;
        return ( _listMaterial[static_cast<size_t>( material )]._flags & ( 1u << static_cast<uint32>( flag ) ) ) != 0;
    }

    bool ElementRuleTable::loadFromResource( string_view path )
    {
        XmlDocument doc;
        XmlNode     root;
        string      sourceName;
        if ( GameDataXml::loadRoot( doc, path, ElementRuleTableInternal::kRootName, root, sourceName ) == false )
            return false;
        return loadRoot( root, sourceName );
    }

    bool ElementRuleTable::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        XmlNode     root;
        if ( GameDataXml::parseRoot( doc, xmlText, sourceName, ElementRuleTableInternal::kRootName, root ) == false )
            return false;
        return loadRoot( root, sourceName );
    }

    bool ElementRuleTable::readIndex( const XmlNode& node, const utf8* pAttribute, int32 ( ElementRuleTable::*pFind )( const hashed_string& ) const, int32& outIndex,
                                      string_view sourceName ) const
    {
        outIndex          = -1;
        const utf8* pText = node.findAttribute( pAttribute );
        if ( pText == nullptr )
            return true;
        outIndex = ( this->*pFind )( hashed_string( pText ) );
        if ( outIndex >= 0 )
            return true;
        SW_LOG_WARNING( "%#: <%#> %#=\"%#\" names nothing declared in this table", sourceName, node.getName(), pAttribute, pText );
        return false;
    }

    bool ElementRuleTable::readStimulusRule( const XmlNode& node, ElementStimulusRule& outRule, string_view sourceName ) const
    {
        bool bValid    = readIndex( node, "material", &ElementRuleTable::findMaterial, outRule._material, sourceName );
        bValid         = readIndex( node, "flag", &ElementRuleTable::findFlag, outRule._flag, sourceName ) && bValid;
        bValid         = readIndex( node, "status", &ElementRuleTable::findStatus, outRule._status, sourceName ) && bValid;
        bValid         = readIndex( node, "without", &ElementRuleTable::findStatus, outRule._without, sourceName ) && bValid;
        bValid         = readIndex( node, "setMaterial", &ElementRuleTable::findMaterial, outRule._setMaterial, sourceName ) && bValid;
        bValid         = readIndex( node, "addStatus", &ElementRuleTable::findStatus, outRule._addStatus, sourceName ) && bValid;
        bValid         = readIndex( node, "removeStatus", &ElementRuleTable::findStatus, outRule._removeStatus, sourceName ) && bValid;
        bValid         = readIndex( node, "floodStatus", &ElementRuleTable::findStatus, outRule._floodStatus, sourceName ) && bValid;
        bValid         = readIndex( node, "through", &ElementRuleTable::findFlag, outRule._floodThrough, sourceName ) && bValid;
        outRule._event = hashed_string( node.getAttributeText( "event" ).empty() ? "" : string( node.getAttributeText( "event" ) ).c_str() );
        if ( outRule._floodStatus >= 0 && outRule._floodThrough < 0 )
        {
            SW_LOG_WARNING( "%#: <On floodStatus> needs through=\"<flag>\"", sourceName );
            bValid = false;
        }
        return bValid;
    }

    bool ElementRuleTable::readStepRule( const XmlNode& node, ElementStepRule& outRule, string_view sourceName ) const
    {
        if ( ElementRuleTableInternal::isNamed( node, "Expire" ) )
            outRule._kind = ElementStepRuleKind::Expire;
        else if ( ElementRuleTableInternal::isNamed( node, "Convert" ) )
            outRule._kind = ElementStepRuleKind::Convert;
        else if ( ElementRuleTableInternal::isNamed( node, "Spread" ) )
            outRule._kind = ElementStepRuleKind::Spread;
        else
        {
            SW_LOG_WARNING( "%#: unknown step rule <%#> (Expire, Convert, Spread)", sourceName, node.getName() );
            return false;
        }
        bool bValid               = readIndex( node, "status", &ElementRuleTable::findStatus, outRule._status, sourceName );
        bValid                    = readIndex( node, "neighbor", &ElementRuleTable::findMaterial, outRule._material, sourceName ) && bValid;
        bValid                    = readIndex( node, "setMaterial", &ElementRuleTable::findMaterial, outRule._setMaterial, sourceName ) && bValid;
        bValid                    = readIndex( node, "to", &ElementRuleTable::findFlag, outRule._toFlag, sourceName ) && bValid;
        outRule._after            = node.getAttributeInt( "after", 1 );
        outRule._bCrosswind       = node.getAttributeBool( "crosswind", false ) ? SW_TRUE : SW_FALSE;
        outRule._stepsParam       = hashed_string( string( node.getAttributeText( "stepsParam" ) ).c_str() );
        outRule._event            = hashed_string( string( node.getAttributeText( "event" ) ).c_str() );
        const string_view pattern = node.getAttributeText( "pattern" );
        if ( pattern.empty() || StringUtil::equals( pattern, "Neighbors4", true ) )
            outRule._pattern = ElementSpreadPattern::Neighbors4;
        else if ( StringUtil::equals( pattern, "Wind", true ) )
            outRule._pattern = ElementSpreadPattern::Wind;
        else
        {
            SW_LOG_WARNING( "%#: <%#> has unknown pattern '%#' (Neighbors4, Wind)", sourceName, node.getName(), pattern );
            bValid = false;
        }
        if ( outRule._status < 0 )
        {
            SW_LOG_WARNING( "%#: <%#> needs a status", sourceName, node.getName() );
            bValid = false;
        }
        return bValid;
    }

    bool ElementRuleTable::loadRoot( const XmlNode& root, string_view sourceName )
    {
        clear();
        _stepTime   = root.getAttributeFloat( "stepTime", 0.25f );
        bool bValid = true;
        // 이름은 앞에서 선언한 것만 가리킨다 — 깃발 · 재질 · 상태를 먼저 모두 읽고 규칙을 읽는다.
        for ( XmlNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( ElementRuleTableInternal::isNamed( child, "Flag" ) )
            {
                if ( addFlag( hashed_string( string( child.getAttributeText( "id" ) ).c_str() ) ) < 0 )
                {
                    SW_LOG_WARNING( "%#: more than %# flags", sourceName, kMaxFlagCount );
                    bValid = false;
                }
            }
            else if ( ElementRuleTableInternal::isNamed( child, "Status" ) )
            {
                const string_view kindText = child.getAttributeText( "kind" );
                ElementStatusKind kind{ ElementStatusKind::Age };
                if ( StringUtil::equals( kindText, "Countdown", true ) )
                    kind = ElementStatusKind::Countdown;
                else if ( StringUtil::equals( kindText, "Age", true ) == false )
                {
                    SW_LOG_WARNING( "%#: <Status> has unknown kind '%#' (Age, Countdown)", sourceName, kindText );
                    bValid = false;
                }
                if ( addStatus( hashed_string( string( child.getAttributeText( "id" ) ).c_str() ), kind, child.getAttributeInt( "steps", 1 ) ) < 0 )
                {
                    SW_LOG_WARNING( "%#: duplicate status or more than %# statuses", sourceName, kMaxStatusCount );
                    bValid = false;
                }
            }
        }
        for ( XmlNode child = root.findChild( "Material" ); child; child = child.findNextSibling( "Material" ) )
        {
            vector<hashed_string> listFlag;
            GameDataXml::forEachToken( child.getAttributeText( "flags" ), ", ;", [&listFlag]( string_view token )
            { listFlag.push_back( hashed_string( string( token ).c_str() ) ); } );
            const int32 material = addMaterial( hashed_string( string( child.getAttributeText( "id" ) ).c_str() ), listFlag );
            if ( material < 0 )
            {
                SW_LOG_WARNING( "%#: <Material id=\"%#\"> is a duplicate or names an undeclared flag", sourceName, child.getAttributeText( "id" ) );
                bValid = false;
                continue;
            }
            for ( XmlAttribute attribute = child.getFirstAttribute(); attribute; attribute = attribute.getNext() )
            {
                const bool bReserved = StringUtil::equals( attribute.getName(), "id", true ) || StringUtil::equals( attribute.getName(), "flags", true );
                if ( bReserved )
                    continue;
                int32 value{ 0 };
                if ( StringUtil::parseInt( attribute.getValue(), value ) == false )
                {
                    SW_LOG_WARNING( "%#: <Material id=\"%#\"> %#=\"%#\" is not an integer", sourceName, child.getAttributeText( "id" ), attribute.getName(), attribute.getValue() );
                    bValid = false;
                    continue;
                }
                setMaterialParam( material, hashed_string( attribute.getName() ), value );
            }
        }
        for ( XmlNode child = root.findChild( "Stimulus" ); child; child = child.findNextSibling( "Stimulus" ) )
        {
            const int32 stimulus = addStimulus( hashed_string( string( child.getAttributeText( "id" ) ).c_str() ) );
            for ( XmlNode ruleNode = child.findChild( "On" ); ruleNode; ruleNode = ruleNode.findNextSibling( "On" ) )
            {
                ElementStimulusRule rule;
                bValid = readStimulusRule( ruleNode, rule, sourceName ) && bValid;
                addStimulusRule( stimulus, rule );
            }
        }
        for ( XmlNode stepNode = root.findChild( "Step" ); stepNode; stepNode = stepNode.findNextSibling( "Step" ) )
        {
            for ( XmlNode ruleNode = stepNode.findChild(); ruleNode; ruleNode = ruleNode.findNextSibling() )
            {
                ElementStepRule rule;
                if ( readStepRule( ruleNode, rule, sourceName ) )
                    addStepRule( rule );
                else
                    bValid = false;
            }
        }
        for ( XmlNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            const bool bKnown = ElementRuleTableInternal::isNamed( child, "Flag" ) || ElementRuleTableInternal::isNamed( child, "Status" ) ||
                                ElementRuleTableInternal::isNamed( child, "Material" ) || ElementRuleTableInternal::isNamed( child, "Stimulus" ) ||
                                ElementRuleTableInternal::isNamed( child, "Step" );
            if ( bKnown == false )
            {
                SW_LOG_WARNING( "%#: unknown element <%#> in <ElementRules>", sourceName, child.getName() );
                bValid = false;
            }
        }
        return bValid;
    }

    const ElementRuleTable* ElementRuleTable::findShared( string_view path )
    {
        std::lock_guard<std::mutex> lock( s_sharedTableMutex );
        const string                key( path );
        const auto                  mapIter = s_mapSharedTable.find( key );
        if ( mapIter != s_mapSharedTable.end() )
            return mapIter->second.get();
        unique_ptr<ElementRuleTable> table = make_unique<ElementRuleTable>();
        if ( table->loadFromResource( path ) == false )
            table.reset();
        const ElementRuleTable* pTable = table.get();
        s_mapSharedTable[key]          = std::move( table );
        return pTable;
    }
} // namespace sw
