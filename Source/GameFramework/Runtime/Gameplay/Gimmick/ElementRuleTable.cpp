#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/ElementRuleTable.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Foundation/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/GameData/GameDataCache.h"

namespace sw
{
    namespace
    {
        struct ElementRuleTableInternal
        {
            static constexpr const utf8* kRootName = "ElementRules";

            static bool isNamed( const XMLNode& node, const utf8* pName ) { return StringUtil::equals( node.getName(), pName ); }
        };

        /**
         * @class ElementRuleTableCache
         * @brief 경로마다 한 번 읽어서 공유하는 원소 규칙 테이블 캐시입니다 — GameFramework 의 모듈 서브시스템(엔진 범위)이 소유하고 에셋 캐시 레지스트리에 올린다.
         */
        class ElementRuleTableCache final : public GameDataCacheSubsystem<ElementRuleTable>
        {
        public:
            static constexpr const utf8* kSubsystemName = "ElementRuleTableCache";

            ElementRuleTableCache()
                : GameDataCacheSubsystem<ElementRuleTable>{ "ElementRuleTable" }
            {
            }

            const utf8* getSubsystemName() const override { return kSubsystemName; }
        };

        /** @brief 공유 테이블 캐시입니다. 모듈 서브시스템이 서기 전 · 내려간 뒤의 접근은 결함이라 멈춘다 — 테이블을 읽을 엔진이 없다. */
        GameDataCache<ElementRuleTable>& getSharedTableCache()
        {
            return game::getSubsystem<ElementRuleTableCache>().getCache();
        }
    } // namespace

    SW_MODULE_SUBSYSTEM( Engine, ElementRuleTableCache, ElementRuleTableCache );
} // namespace sw

namespace sw
{
    std::optional<int32> ElementMaterialDef::getParam( const hashed_string& name ) const
    {
        for ( size_t index = 0; index < _listParamName.size(); ++index )
        {
            if ( _listParamName[index] == name )
                return _listParamValue[index];
        }
        return std::nullopt;
    }

    void ElementRuleTable::clear()
    {
        _listFlag.clear();
        _listMaterial.clear();
        _listStatus.clear();
        _listStimulus.clear();
        _listStepRule.clear();
        _stepTime = kDefaultStepTime;
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

    void ElementRuleTable::addStepRule( const ElementStepRule& rule )
    {
        _listStepRule.push_back( rule );
    }

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
        const bool bInvalidFlagRequest = material < 0 || material >= static_cast<int32>( _listMaterial.size() ) || flag < 0;
        if ( bInvalidFlagRequest )
            return false;
        return ( _listMaterial[static_cast<size_t>( material )]._flags & ( 1u << static_cast<uint32>( flag ) ) ) != 0;
    }

    bool ElementRuleTable::loadFromAsset( const AssetID& assetID )
    {
        XMLDocument doc;
        XMLNode     root;
        string      sourceName;
        if ( XMLReadUtil::loadRootOrWarn( doc, assetID, ElementRuleTableInternal::kRootName, root, sourceName ) == false )
            return false;
        return loadRoot( root, sourceName );
    }

    bool ElementRuleTable::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        XMLDocument doc;
        XMLNode     root;
        if ( XMLReadUtil::parseRootOrWarn( doc, xmlText, sourceName, ElementRuleTableInternal::kRootName, root ) == false )
            return false;
        return loadRoot( root, sourceName );
    }

    bool ElementRuleTable::readIndex( const hashed_string& name, const XMLNode& node, const utf8* pAttribute, int32 ( ElementRuleTable::*pFind )( const hashed_string& ) const,
                                      int32& outIndex, string_view sourceName ) const
    {
        outIndex = -1;
        if ( name.empty() ) // 적지 않으면 "없음"(-1)
            return true;
        outIndex = ( this->*pFind )( name );
        if ( outIndex >= 0 )
            return true;
        SW_LOG_WARNING_ID( "ElementRuleTable.NamesNothingDeclaredTable", "%#: <%#> %#=\"%#\" names nothing declared in this table", sourceName, node.getName(), pAttribute, name.c_str() );
        return false;
    }

    bool ElementRuleTable::readStimulusRule( const XMLNode& node, ElementStimulusRule& outRule, string_view sourceName ) const
    {
        ElementStimulusRuleFields fields;
        bool                      bValid = XMLFields::readFields( node, fields, sourceName );
        bValid                           = readIndex( fields._material, node, "material", &ElementRuleTable::findMaterial, outRule._material, sourceName ) && bValid;
        bValid                           = readIndex( fields._flag, node, "flag", &ElementRuleTable::findFlag, outRule._flag, sourceName ) && bValid;
        bValid                           = readIndex( fields._status, node, "status", &ElementRuleTable::findStatus, outRule._status, sourceName ) && bValid;
        bValid                           = readIndex( fields._without, node, "without", &ElementRuleTable::findStatus, outRule._without, sourceName ) && bValid;
        bValid                           = readIndex( fields._setMaterial, node, "setMaterial", &ElementRuleTable::findMaterial, outRule._setMaterial, sourceName ) && bValid;
        bValid                           = readIndex( fields._addStatus, node, "addStatus", &ElementRuleTable::findStatus, outRule._addStatus, sourceName ) && bValid;
        bValid                           = readIndex( fields._removeStatus, node, "removeStatus", &ElementRuleTable::findStatus, outRule._removeStatus, sourceName ) && bValid;
        bValid                           = readIndex( fields._floodStatus, node, "floodStatus", &ElementRuleTable::findStatus, outRule._floodStatus, sourceName ) && bValid;
        bValid                           = readIndex( fields._through, node, "through", &ElementRuleTable::findFlag, outRule._floodThrough, sourceName ) && bValid;
        outRule._event                   = fields._event;
        if ( outRule._floodStatus >= 0 && outRule._floodThrough < 0 )
        {
            SW_LOG_WARNING_ID( "ElementRuleTable.FloodStatusNeedsThroughFlag", "%#: <On floodStatus> needs through=\"<flag>\"", sourceName );
            bValid = false;
        }
        return bValid;
    }

    bool ElementRuleTable::readStepRule( const XMLNode& node, ElementStepRule& outRule, string_view sourceName ) const
    {
        if ( XMLReadUtil::isElement( node.getName(), "Expire" ) )
            outRule._kind = ElementStepRuleKind::Expire;
        else if ( XMLReadUtil::isElement( node.getName(), "Convert" ) )
            outRule._kind = ElementStepRuleKind::Convert;
        else if ( XMLReadUtil::isElement( node.getName(), "Spread" ) )
            outRule._kind = ElementStepRuleKind::Spread;
        else
        {
            SW_LOG_WARNING_ID( "ElementRuleTable.UnknownStepRuleExpire", "%#: unknown step rule <%#> (Expire, Convert, Spread)", sourceName, node.getName() );
            return false;
        }
        ElementStepRuleFields fields; // 상태 필수 · 형태는 ENUM() 이름 · after >= 0
        bool                  bValid = XMLFields::readFields( node, fields, sourceName );
        bValid                       = readIndex( fields._status, node, "status", &ElementRuleTable::findStatus, outRule._status, sourceName ) && bValid;
        bValid                       = readIndex( fields._neighbor, node, "neighbor", &ElementRuleTable::findMaterial, outRule._material, sourceName ) && bValid;
        bValid                       = readIndex( fields._setMaterial, node, "setMaterial", &ElementRuleTable::findMaterial, outRule._setMaterial, sourceName ) && bValid;
        bValid                       = readIndex( fields._to, node, "to", &ElementRuleTable::findFlag, outRule._toFlag, sourceName ) && bValid;
        outRule._after               = fields._after;
        outRule._bCrosswind          = fields._bCrosswind;
        outRule._stepsParam          = fields._stepsParam;
        outRule._event               = fields._event;
        outRule._pattern             = fields._pattern;
        if ( bValid && outRule._status < 0 )
        {
            SW_LOG_WARNING_ID( "ElementRuleTable.NeedsStatus", "%#: <%#> needs a status", sourceName, node.getName() );
            bValid = false;
        }
        return bValid;
    }

    bool ElementRuleTable::loadRoot( const XMLNode& root, string_view sourceName )
    {
        clear();
        // 적지 않으면 clear() 가 둔 기본 스텝 길이다. 0 이하 · 깨진 값은 오류다.
        ElementRuleRootFields rootFields{ _stepTime };
        bool                  bValid = XMLFields::readFields( root, rootFields, sourceName ); // stepTime > 0 은 PROPERTY 가 본다
        _stepTime                    = bValid ? rootFields._stepTime : _stepTime;
        // 이름은 앞에서 선언한 것만 가리킬 수 있다. 그래서 플래그, 재질, 상태를 먼저 모두 읽고 규칙을 읽는다.
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( XMLReadUtil::isElement( child.getName(), "Flag" ) )
            {
                ElementNameFields flag;
                bValid = XMLFields::readFields( child, flag, sourceName ) && bValid;
                if ( addFlag( flag._id ) < 0 )
                {
                    SW_LOG_WARNING_ID( "ElementRuleTable.MoreFlags", "%#: more than %# flags", sourceName, kMaxFlagCount );
                    bValid = false;
                }
            }
            else if ( XMLReadUtil::isElement( child.getName(), "Status" ) )
            {
                ElementStatusDef status; // 종류는 ENUM() 이름 · steps >= 1
                bValid = XMLFields::readFields( child, status, sourceName ) && bValid;
                if ( addStatus( status._id, status._kind, status._steps ) < 0 )
                {
                    SW_LOG_WARNING_ID( "ElementRuleTable.DuplicateStatusMoreStatuses", "%#: duplicate status or more than %# statuses", sourceName, kMaxStatusCount );
                    bValid = false;
                }
            }
        }
        for ( XMLNode child = root.findChild( "Material" ); child; child = child.findNextSibling( "Material" ) )
        {
            vector<hashed_string> listFlag;
            StringUtil::forEachToken( child.getAttributeText( "flags" ), ", ;", [&listFlag]( string_view token )
            { listFlag.push_back( hashed_string( string( token ).c_str() ) ); } );
            const int32 material = addMaterial( hashed_string( string( child.getAttributeText( "id" ) ).c_str() ), listFlag );
            if ( material < 0 )
            {
                SW_LOG_WARNING_ID( "ElementRuleTable.MaterialIdDuplicateNames", "%#: <Material id=\"%#\"> is a duplicate or names an undeclared flag", sourceName, child.getAttributeText( "id" ) );
                bValid = false;
                continue;
            }
            for ( XMLAttribute attribute = child.getFirstAttribute(); attribute; attribute = attribute.getNext() )
            {
                const bool bReserved = StringUtil::equals( attribute.getName(), "id" ) || StringUtil::equals( attribute.getName(), "flags" );
                if ( bReserved )
                    continue;
                int32 value{ 0 };
                if ( StringUtil::parseInt( attribute.getValue(), value ) == false )
                {
                    SW_LOG_WARNING_ID( "ElementRuleTable.MaterialIdNotInteger", "%#: <Material id=\"%#\"> %#=\"%#\" is not an integer", sourceName, child.getAttributeText( "id" ), attribute.getName(), attribute.getValue() );
                    bValid = false;
                    continue;
                }
                setMaterialParam( material, hashed_string( attribute.getName() ), value );
            }
        }
        for ( XMLNode child = root.findChild( "Stimulus" ); child; child = child.findNextSibling( "Stimulus" ) )
        {
            ElementNameFields stimulusFields;
            bValid               = XMLFields::readFields( child, stimulusFields, sourceName ) && bValid;
            const int32 stimulus = addStimulus( stimulusFields._id );
            for ( XMLNode ruleNode = child.findChild( "On" ); ruleNode; ruleNode = ruleNode.findNextSibling( "On" ) )
            {
                ElementStimulusRule rule;
                bValid = readStimulusRule( ruleNode, rule, sourceName ) && bValid;
                addStimulusRule( stimulus, rule );
            }
        }
        for ( XMLNode stepNode = root.findChild( "Step" ); stepNode; stepNode = stepNode.findNextSibling( "Step" ) )
        {
            for ( XMLNode ruleNode = stepNode.findChild(); ruleNode; ruleNode = ruleNode.findNextSibling() )
            {
                ElementStepRule rule;
                if ( readStepRule( ruleNode, rule, sourceName ) )
                    addStepRule( rule );
                else
                    bValid = false;
            }
        }
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            const bool bKnown = XMLReadUtil::isElement( child.getName(), "Flag" ) || XMLReadUtil::isElement( child.getName(), "Status" ) ||
                                XMLReadUtil::isElement( child.getName(), "Material" ) || XMLReadUtil::isElement( child.getName(), "Stimulus" ) ||
                                XMLReadUtil::isElement( child.getName(), "Step" );
            if ( bKnown == false )
            {
                SW_LOG_WARNING_ID( "ElementRuleTable.UnknownElementElementRules", "%#: unknown element <%#> in <ElementRules>", sourceName, child.getName() );
                bValid = false;
            }
        }
        return bValid;
    }

    const ElementRuleTable* ElementRuleTable::findShared( string_view path )
    {
        return getSharedTableCache().find( path );
    }

    uint32 ElementRuleTable::getSharedReloadCount()
    {
        return getSharedTableCache().getReloadCount();
    }
} // namespace sw

namespace sw
{
    /** @brief `ElementRuleTable` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_ASSET_TYPE( ElementRules, AssetTypeDef::makeDataFile( "elementrules", AssetPathRule{ {}, {}, ".elements.xml" }, AssetTypeLoader::of<ElementRuleTable>() ) );
} // namespace sw
