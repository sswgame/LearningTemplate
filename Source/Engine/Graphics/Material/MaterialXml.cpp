#include "pch.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialUtil.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        /** @brief 머티리얼 XML 의 반복 구조(enum 항목 · 문자열 목록)를 읽고 쓰는 TU 로컬 헬퍼. */
        struct MaterialXmlInternal
        {
            static string nodeText( XmlNode node )
            {
                if ( node.isValid() == false || node.text() == nullptr )
                    return {};
                return string{ StringUtil::trim( node.text() ) };
            }

            static void parseEnumEntries( XmlNode parent, vector<MaterialEnumEntry>& outListEntry )
            {
                outListEntry.clear();
                if ( parent.isValid() == false )
                    return;
                XmlNode list = parent.child( "_enumEntries" );
                if ( list.isValid() == false )
                    list = parent.child( "enumEntries" );
                if ( list.isValid() == false )
                    return;
                for ( XmlNode item = list.child( "item" ); item; item = item.next( "item" ) )
                {
                    MaterialEnumEntry entry{};
                    entry._name            = MaterialUtil::fieldText( item, "name" );
                    const string valueText = MaterialUtil::fieldText( item, "value" );
                    if ( valueText.empty() == false )
                    {
                        uint64 val{ 0 };
                        StringUtil::parseUint64( valueText, val, 0 );
                        entry._value = static_cast<uint32>( val );
                    }
                    if ( entry._name.empty() == false )
                        outListEntry.push_back( std::move( entry ) );
                }
            }

            static void parseStringListItems( XmlNode list, vector<string>& outListItem )
            {
                outListItem.clear();
                if ( list.isValid() == false )
                    return;
                for ( XmlNode item = list.child( "item" ); item; item = item.next( "item" ) )
                {
                    string itemText = nodeText( item );
                    if ( itemText.empty() )
                        itemText = MaterialUtil::fieldText( item, "value" );
                    if ( itemText.empty() )
                        itemText = MaterialUtil::fieldText( item, "name" );
                    if ( itemText.empty() == false )
                        outListItem.push_back( std::move( itemText ) );
                }
            }

            static void appendMaterialStringList( XmlNode parent, const utf8* pTag, const vector<string>& listValue )
            {
                if ( listValue.empty() )
                    return;
                XmlNode list = parent.appendChild( pTag );
                for ( const string& valueStr : listValue )
                {
                    XmlNode item = list.appendChild( "item" );
                    item.setValue( valueStr );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /** @brief Attribute first, then same-name child element. */
    string MaterialUtil::fieldText( XmlNode node, const utf8* pName )
    {
        if ( node.isValid() == false || pName == nullptr )
            return {};

        const utf8* pAttr = node.attribute( pName, false );
        if ( pAttr != nullptr )
            return string{ StringUtil::trim( pAttr ) };
        const utf8* pText = node.childText( pName, false );
        if ( pText != nullptr )
            return string{ StringUtil::trim( pText ) };
        return {};
    }

    bool MaterialUtil::parseBoolField( XmlNode node, const utf8* pName, bool defaultValue )
    {
        const string text = MaterialUtil::fieldText( node, pName );
        if ( text.empty() )
            return defaultValue;
        return MaterialUtil::parseBoolToken( text );
    }

    MaterialProperty MaterialUtil::parsePropertyNode( XmlNode item )
    {
        MaterialProperty prop{};
        prop._name = MaterialUtil::fieldText( item, "name" );
        uint32 size{ 0 };
        string typeStr = MaterialUtil::fieldText( item, "type" );
        if ( typeStr.empty() )
            typeStr = MaterialUtil::fieldText( item, "cpuType" );
        if ( typeStr.empty() == false )
            prop._type = MaterialUtil::stringToType( typeStr, size );
        const string shaderTypeStr = MaterialUtil::fieldText( item, "shaderType" );
        if ( shaderTypeStr.empty() == false )
            prop._shaderType = MaterialUtil::stringToType( shaderTypeStr, size );
        prop._defaultValue = MaterialUtil::fieldText( item, "defaultValue" );
        prop._value        = MaterialUtil::fieldText( item, "value" );
        if ( prop._defaultValue.empty() && prop._value.empty() == false )
            prop._defaultValue = prop._value;
        if ( prop._value.empty() && prop._defaultValue.empty() == false )
            prop._value = prop._defaultValue;
        prop._assetPath     = MaterialUtil::fieldText( item, "assetPath" );
        prop._enumType      = MaterialUtil::fieldText( item, "enumType" );
        prop._displayName   = MaterialUtil::fieldText( item, "displayName" );
        prop._group         = MaterialUtil::fieldText( item, "group" );
        prop._tooltip       = MaterialUtil::fieldText( item, "tooltip" );
        prop._shaderKeyword = MaterialUtil::fieldText( item, "shaderKeyword" );
        const string minStr = MaterialUtil::fieldText( item, "min" );
        const string maxStr = MaterialUtil::fieldText( item, "max" );
        if ( minStr.empty() == false )
            StringUtil::parseFloat( minStr, prop._min );
        if ( maxStr.empty() == false )
            StringUtil::parseFloat( maxStr, prop._max );
        prop._bHdr      = MaterialUtil::parseBoolField( item, "bHdr", false );
        prop._bSrgb     = MaterialUtil::parseBoolField( item, "bSrgb", true );
        prop._bHidden   = MaterialUtil::parseBoolField( item, "bHidden", false );
        prop._bAdvanced = MaterialUtil::parseBoolField( item, "bAdvanced", false );
        MaterialXmlInternal::parseEnumEntries( item, prop._listEnumEntry );
        return prop;
    }

    void MaterialUtil::appendAttribute( XmlNode parent, const utf8* pName, string_view value )
    {
        if ( parent.isValid() == false || pName == nullptr || value.empty() )
            return;
        parent.appendAttribute( pName, value );
    }

    void MaterialUtil::appendBoolAttr( XmlNode parent, const utf8* pName, bool value )
    {
        parent.appendAttribute( pName, value );
    }

    RHIBlendMode MaterialUtil::parseBlendMode( string_view modeName )
    {
        if ( StringUtil::equals( modeName, "Transparent", true ) || StringUtil::equals( modeName, "AlphaBlend", true ) )
            return RHIBlendMode::Transparent;
        return RHIBlendMode::Opaque;
    }

    const utf8* MaterialUtil::blendModeToString( RHIBlendMode mode )
    {
        return mode == RHIBlendMode::Transparent ? "Transparent" : "Opaque";
    }

    MaterialQualityLevel MaterialUtil::parseQuality( string_view qualityName )
    {
        const EnumInfo* pInfo = engine::getTypeRegistry().findEnum( hashed_string( "sw::MaterialQualityLevel" ) );
        int64           value{ 0 };
        if ( pInfo != nullptr && pInfo->tryParse( qualityName, value ) )
            return static_cast<MaterialQualityLevel>( value );
        return MaterialQualityLevel::High;
    }

    const utf8* MaterialUtil::qualityToString( MaterialQualityLevel quality )
    {
        const EnumInfo* pInfo = engine::getTypeRegistry().findEnum( hashed_string( "sw::MaterialQualityLevel" ) );
        const utf8*     pStr  = pInfo != nullptr ? pInfo->valueToCString( static_cast<int64>( quality ) ) : nullptr;
        if ( pStr != nullptr )
            return pStr;
        return "High";
    }

    void MaterialUtil::parsePermutationNode( XmlNode root, MaterialPermutationDesc& out )
    {
        out = MaterialPermutationDesc{};
        if ( root.isValid() == false )
            return;
        XmlNode perm = root.child( "_permutations" );
        if ( perm.isValid() == false )
            return;

        const string quality = MaterialUtil::fieldText( perm, "quality" );
        if ( quality.empty() == false )
            out._quality = MaterialUtil::parseQuality( quality );
        const string lod = MaterialUtil::fieldText( perm, "shaderLOD" );
        if ( lod.empty() == false )
        {
            uint64 lodVal{ 0 };
            StringUtil::parseUint64( lod, lodVal, 10 );
            out._shaderLOD = static_cast<uint32>( lodVal );
        }
        const string usage = MaterialUtil::fieldText( perm, "usage" );
        if ( usage.empty() == false )
        {
            const EnumInfo* pUsageEnum = engine::getTypeRegistry().findEnum( hashed_string( "sw::MaterialUsageFlags" ) );
            if ( pUsageEnum != nullptr )
                out._usage = static_cast<MaterialUsageFlags>( pUsageEnum->stringFlagsToValue( usage ) );
        }

        XmlNode always = perm.child( "_alwaysDefines" );
        MaterialXmlInternal::parseStringListItems( always, out._listAlwaysDefine );

        XmlNode switches = perm.child( "_staticSwitches" );
        if ( switches.isValid() )
        {
            for ( XmlNode item = switches.child( "item" ); item; item = item.next( "item" ) )
            {
                MaterialStaticSwitch entry{};
                entry._name           = MaterialUtil::fieldText( item, "name" );
                entry._keyword        = MaterialUtil::fieldText( item, "keyword" );
                entry._keywordOff     = MaterialUtil::fieldText( item, "keywordOff" );
                entry._bEnabled       = MaterialUtil::parseBoolField( item, "bEnabled", false );
                entry._bShaderFeature = MaterialUtil::parseBoolField( item, "bShaderFeature", true );
                if ( entry._name.empty() && entry._keyword.empty() == false )
                    entry._name = entry._keyword;
                if ( entry._keyword.empty() == false || entry._name.empty() == false )
                    out._listStaticSwitch.push_back( std::move( entry ) );
            }
        }

        XmlNode multiCompileNode = perm.child( "_multiCompiles" );
        if ( multiCompileNode.isValid() )
        {
            for ( XmlNode item = multiCompileNode.child( "item" ); item; item = item.next( "item" ) )
            {
                MaterialMultiCompile multiCompile{};
                multiCompile._name     = MaterialUtil::fieldText( item, "name" );
                multiCompile._selected = MaterialUtil::fieldText( item, "selected" );
                XmlNode opts           = item.child( "_options" );
                MaterialXmlInternal::parseStringListItems( opts, multiCompile._listOption );
                if ( multiCompile._selected.empty() == false || multiCompile._listOption.empty() == false )
                    out._listMultiCompile.push_back( std::move( multiCompile ) );
            }
        }
    }

    void MaterialUtil::appendPermutationNode( XmlNode root, const MaterialPermutationDesc& perm )
    {
        XmlNode node = root.appendChild( "_permutations" );
        MaterialUtil::appendAttribute( node, "quality", MaterialUtil::qualityToString( perm._quality ) );
        node.appendAttribute( "shaderLOD", perm._shaderLOD );
        {
            const EnumInfo* pUsageEnum = engine::getTypeRegistry().findEnum( hashed_string( "sw::MaterialUsageFlags" ) );
            const utf8*     pUsageStr  = pUsageEnum != nullptr ? pUsageEnum->valueToCString( static_cast<int64>( perm._usage ) ) : nullptr;
            MaterialUtil::appendAttribute( node, "usage", pUsageStr != nullptr ? pUsageStr : "None" );
        }
        MaterialXmlInternal::appendMaterialStringList( node, "_alwaysDefines", perm._listAlwaysDefine );

        if ( perm._listStaticSwitch.empty() == false )
        {
            XmlNode list = node.appendChild( "_staticSwitches" );
            for ( const MaterialStaticSwitch& entry : perm._listStaticSwitch )
            {
                XmlNode item = list.appendChild( "item" );
                MaterialUtil::appendAttribute( item, "name", entry._name );
                MaterialUtil::appendAttribute( item, "keyword", entry._keyword );
                if ( entry._keywordOff.empty() == false )
                    MaterialUtil::appendAttribute( item, "keywordOff", entry._keywordOff );
                MaterialUtil::appendBoolAttr( item, "bEnabled", entry._bEnabled );
                MaterialUtil::appendBoolAttr( item, "bShaderFeature", entry._bShaderFeature );
            }
        }

        if ( perm._listMultiCompile.empty() == false )
        {
            XmlNode list = node.appendChild( "_multiCompiles" );
            for ( const MaterialMultiCompile& mc : perm._listMultiCompile )
            {
                XmlNode item = list.appendChild( "item" );
                MaterialUtil::appendAttribute( item, "name", mc._name );
                MaterialUtil::appendAttribute( item, "selected", mc._selected );
                MaterialXmlInternal::appendMaterialStringList( item, "_options", mc._listOption );
            }
        }
    }

    bool Material::loadFromFile( string_view assetRelativePath )
    {
        XmlDocument doc;
        if ( doc.loadPath( assetRelativePath ) == false )
            return false;
        return loadFromXml( doc.saveToString() );
    }

    TaskHandle Material::loadFromFileAsync( string_view assetRelativePath )
    {
        TaskHandle handle = engine::getTaskManager().emplaceTask(
            "LoadMaterialAsync",
            SW_DELEGATE_FUNCTION( TaskArgsDelegate, Material::loadFromFileAsyncJob ),
            MakeTaskArgs( _asyncLoadState, string( assetRelativePath ) ) );
        handle.submit();
        return handle;
    }

    void Material::loadFromFileAsyncJob( const TaskArgs& args )
    {
        shared_ptr<AsyncLoadState> state = args.get<shared_ptr<AsyncLoadState>>( 0 );
        if ( state == nullptr )
            return;

        std::scoped_lock<mutex> lock{ state->_mutex };
        if ( state->_pMaterial == nullptr )
            return;
        state->_pMaterial->loadFromFile( args.get<string>( 1 ) );
    }

    bool Material::saveToFile( string_view assetRelativePath ) const
    {
        string absPath = ResourceUtil::getResourcePath( assetRelativePath );
        if ( absPath.empty() )
            absPath = assetRelativePath;

        XmlDocument doc;
        if ( doc.parse( saveToString() ) == false )
            return false;
        return doc.saveFile( absPath );
    }

    string Material::saveToString() const
    {
        this->syncDescFromRuntime();

        XmlDocument doc;
        XmlNode     root = doc.appendRoot( "MaterialDesc" );

        engine::getResourceManager().getAssetFormatRegistry().writeXmlVersion( root, AssetFormatVersions::kMaterial );
        MaterialUtil::appendAttribute( root, "name", _desc._name );
        MaterialUtil::appendAttribute( root, "shaderPath", _desc._shaderPath );
        MaterialUtil::appendAttribute( root, "blendMode", MaterialUtil::blendModeToString( _blendMode ) );

        XmlNode props = root.appendChild( "_properties" );

        for ( const MaterialProperty& prop : _data._listProperty )
        {
            XmlNode item = props.appendChild( "item" );

            MaterialUtil::appendAttribute( item, "name", prop._name );
            MaterialUtil::appendAttribute( item, "type", MaterialUtil::typeToString( prop._type ) );
            if ( prop._shaderType != MaterialPropertyType::Unknown )
                MaterialUtil::appendAttribute( item, "shaderType", MaterialUtil::typeToString( prop._shaderType ) );
            if ( prop._defaultValue.empty() == false )
                MaterialUtil::appendAttribute( item, "defaultValue", prop._defaultValue );
            if ( prop._value.empty() == false && prop._value != prop._defaultValue )
                MaterialUtil::appendAttribute( item, "value", prop._value );
            if ( prop._assetPath.empty() == false )
                MaterialUtil::appendAttribute( item, "assetPath", prop._assetPath );
            if ( prop._enumType.empty() == false )
                MaterialUtil::appendAttribute( item, "enumType", prop._enumType );
            if ( prop._displayName.empty() == false )
                MaterialUtil::appendAttribute( item, "displayName", prop._displayName );
            if ( prop._group.empty() == false )
                MaterialUtil::appendAttribute( item, "group", prop._group );
            if ( prop._tooltip.empty() == false )
                MaterialUtil::appendAttribute( item, "tooltip", prop._tooltip );
            if ( prop._shaderKeyword.empty() == false )
                MaterialUtil::appendAttribute( item, "shaderKeyword", prop._shaderKeyword );
            if ( prop._type == MaterialPropertyType::Range )
            {
                item.appendAttribute( "min", prop._min );
                item.appendAttribute( "max", prop._max );
            }
            if ( prop._type == MaterialPropertyType::Color )
            {
                MaterialUtil::appendBoolAttr( item, "bHdr", prop._bHdr );
                MaterialUtil::appendBoolAttr( item, "bSrgb", prop._bSrgb );
            }
            if ( MaterialUtil::isTextureType( prop._type ) )
                MaterialUtil::appendBoolAttr( item, "bSrgb", prop._bSrgb );
            if ( prop._bHidden )
                MaterialUtil::appendBoolAttr( item, "bHidden", true );
            if ( prop._bAdvanced )
                MaterialUtil::appendBoolAttr( item, "bAdvanced", true );

            if ( prop._listEnumEntry.empty() == false )
            {
                XmlNode list = item.appendChild( "_enumEntries" );
                for ( const MaterialEnumEntry& enumEntry : prop._listEnumEntry )
                {
                    XmlNode eItem = list.appendChild( "item" );
                    MaterialUtil::appendAttribute( eItem, "name", enumEntry._name );
                    eItem.appendAttribute( "value", enumEntry._value );
                }
            }
        }

        MaterialUtil::appendPermutationNode( root, _desc._permutations );

        return doc.saveToString();
    }

    bool Material::loadFromXml( string_view xmlText )
    {
        XmlDocument doc;
        if ( doc.parse( xmlText ) == false )
            return false;

        XmlNode root = doc.root( "MaterialDesc" );
        if ( root.isValid() == false )
            return false;

        if ( engine::getResourceManager().getAssetFormatRegistry().upgradeXml( AssetKind::Material, doc, root, AssetFormatVersions::kMaterial ) ==
             false )
            return false;

        _desc       = MaterialDesc{};
        _desc._name = MaterialUtil::fieldText( root, "name" );
        setShaderPath( MaterialUtil::fieldText( root, "shaderPath" ) );
        _desc._blendMode = MaterialUtil::fieldText( root, "blendMode" );

        XmlNode props = root.child( "_properties" );
        if ( props.isValid() )
        {
            for ( XmlNode item = props.child( "item" ); item; item = item.next( "item" ) )
            {
                MaterialProperty prop = MaterialUtil::parsePropertyNode( item );
                if ( prop._name.empty() == false )
                    _desc._listProperty.push_back( std::move( prop ) );
            }
        }

        MaterialUtil::parsePermutationNode( root, _desc._permutations );

        applyDescToRuntime();
        return true;
    }

    void Material::applyDescToRuntime()
    {
        _data._listProperty = _desc._listProperty;
        _blendMode          = MaterialUtil::parseBlendMode( _desc._blendMode );
        rebuildPackedBuffer();
    }

    void Material::syncDescFromRuntime() const
    {
        auto self                 = const_cast<Material*>( this );
        self->_desc._listProperty = _data._listProperty;
        self->_desc._blendMode    = MaterialUtil::blendModeToString( _blendMode );
        self->_desc._name         = _desc._name;
        self->setShaderPath( _desc._shaderPath );
    }
} // namespace sw
