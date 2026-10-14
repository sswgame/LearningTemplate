/**
 * @file MeshModifierStack.cpp
 * @brief 모디파이어 스택의 순서 편집 · 적용 · 저장 문서 읽기와 쓰기를 구현합니다.
 */
#include "pch.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierStack.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct MeshModifierStackInternal
        {
            static constexpr string_view kMeshExtension = ".mesh";

            static void writeVector3( const JSONValue& parent, string_view key, const float3& value )
            {
                const JSONValue array = parent.set( key );
                array.setArray();
                array.pushBack().setFloat( static_cast<float64>( value._x ) );
                array.pushBack().setFloat( static_cast<float64>( value._y ) );
                array.pushBack().setFloat( static_cast<float64>( value._z ) );
            }

            static float3 readVector3( const JSONValue& parent, string_view key, const float3& fallback )
            {
                const JSONValue array = parent.get( key );
                if ( array.isArray() == false || array.size() != 3 )
                    return fallback;
                const float64 x = array.at( 0 ).asFloat( static_cast<float64>( fallback._x ) );
                const float64 y = array.at( 1 ).asFloat( static_cast<float64>( fallback._y ) );
                const float64 z = array.at( 2 ).asFloat( static_cast<float64>( fallback._z ) );
                return float3{ static_cast<float32>( x ), static_cast<float32>( y ), static_cast<float32>( z ) };
            }

            static void writeParam( const MeshModifier& modifier, uint32 paramIndex, const JSONValue& params )
            {
                const MeshModifierParamDef&   def   = modifier.getParamDefs()[paramIndex];
                const MeshModifierParamValue& value = modifier.getParamValue( paramIndex );
                switch ( def._kind )
                {
                    case MeshModifierParamKind::Float:
                    case MeshModifierParamKind::Int:
                    case MeshModifierParamKind::Bool:
                    case MeshModifierParamKind::Axis:
                    {
                        params.set( def._pName ).setFloat( static_cast<float64>( value._value._x ) );
                        break;
                    }
                    case MeshModifierParamKind::Vector3:
                    {
                        writeVector3( params, def._pName, value._value );
                        break;
                    }
                    case MeshModifierParamKind::Curve:
                    {
                        const JSONValue array = params.set( def._pName );
                        array.setArray();
                        for ( const float2& key : value._listCurveKey )
                        {
                            const JSONValue pair = array.pushBack();
                            pair.setArray();
                            pair.pushBack().setFloat( static_cast<float64>( key._x ) );
                            pair.pushBack().setFloat( static_cast<float64>( key._y ) );
                        }
                        break;
                    }
                    case MeshModifierParamKind::NumberList:
                    {
                        const JSONValue array = params.set( def._pName );
                        array.setArray();
                        for ( const float32 number : value._listNumber )
                        {
                            array.pushBack().setFloat( static_cast<float64>( number ) );
                        }
                        break;
                    }
                    case MeshModifierParamKind::Reference:
                    case MeshModifierParamKind::AssetPath:
                    {
                        params.set( def._pName ).setString( value._text );
                        break;
                    }
                }
            }

            static void readParam( MeshModifier& modifier, uint32 paramIndex, const JSONValue& source )
            {
                const MeshModifierParamDef& def = modifier.getParamDefs()[paramIndex];
                switch ( def._kind )
                {
                    case MeshModifierParamKind::Float:
                    case MeshModifierParamKind::Int:
                    case MeshModifierParamKind::Bool:
                    case MeshModifierParamKind::Axis:
                    {
                        if ( source.isNumber() || source.isBool() )
                            modifier.setScalar( paramIndex, source.isBool() ? ( source.asBool() ? 1.0f : 0.0f ) : static_cast<float32>( source.asFloat() ) );
                        break;
                    }
                    case MeshModifierParamKind::Vector3:
                    {
                        if ( source.isArray() && source.size() == 3 )
                            modifier.setVector3( paramIndex, float3{ static_cast<float32>( source.at( 0 ).asFloat() ), static_cast<float32>( source.at( 1 ).asFloat() ),
                                                                     static_cast<float32>( source.at( 2 ).asFloat() ) } );
                        break;
                    }
                    case MeshModifierParamKind::Curve:
                    {
                        if ( source.isArray() == false )
                            break;
                        vector<float2> listKey;
                        for ( size_t index = 0; index < source.size(); ++index )
                        {
                            const JSONValue pair = source.at( index );
                            if ( pair.isArray() && pair.size() == 2 )
                                listKey.push_back( float2{ static_cast<float32>( pair.at( 0 ).asFloat() ), static_cast<float32>( pair.at( 1 ).asFloat() ) } );
                        }
                        modifier.setCurveKeys( paramIndex, listKey );
                        break;
                    }
                    case MeshModifierParamKind::NumberList:
                    {
                        if ( source.isArray() == false )
                            break;
                        vector<float32> listNumber;
                        listNumber.reserve( source.size() );
                        for ( size_t index = 0; index < source.size(); ++index )
                        {
                            listNumber.push_back( static_cast<float32>( source.at( index ).asFloat() ) );
                        }
                        modifier.setNumberList( paramIndex, listNumber );
                        break;
                    }
                    case MeshModifierParamKind::Reference:
                    case MeshModifierParamKind::AssetPath:
                    {
                        if ( source.isString() )
                            modifier.setText( paramIndex, source.asString() );
                        break;
                    }
                }
            }

            static void writeModifier( const MeshModifier& modifier, const JSONValue& object )
            {
                object.setObject();
                object.set( "type" ).setString( modifier.getTypeID() );
                object.set( "name" ).setString( modifier.getName() );
                object.set( "enabled" ).setBool( modifier.isEnabled() );
                object.set( "worldSpace" ).setBool( modifier.isWorldSpace() );
                object.set( "spaceReference" ).setString( modifier.getSpaceReference() );
                const MeshModifierGizmo& gizmo = modifier.getGizmo();
                writeVector3( object, "center", gizmo._center );
                writeVector3( object, "rotation", gizmo._rotationDegree );
                object.set( "axis" ).setUint( gizmo._axisIndex );
                const JSONValue params = object.set( "params" );
                params.setObject();
                for ( uint32 paramIndex = 0; paramIndex < modifier.getParamCount(); ++paramIndex )
                {
                    writeParam( modifier, paramIndex, params );
                }
                const JSONValue extra = object.set( "extra" );
                extra.setObject();
                modifier.writeExtraJSON( extra );
            }

            static unique_ptr<MeshModifier> readModifier( const JSONValue& object )
            {
                const string             typeID   = object.get( "type" ).asString();
                unique_ptr<MeshModifier> modifier = MeshModifierRegistry::create( typeID );
                if ( modifier == nullptr )
                {
                    SW_LOG_WARNING( "Unknown mesh modifier type '%#' is skipped", typeID.c_str() );
                    return modifier;
                }
                if ( object.has( "name" ) )
                    modifier->setName( object.get( "name" ).asString() );
                modifier->setEnabled( object.get( "enabled" ).asBool( true ) );
                modifier->setWorldSpace( object.get( "worldSpace" ).asBool( false ) );
                modifier->setSpaceReference( object.get( "spaceReference" ).asString() );
                MeshModifierGizmo& gizmo = modifier->getGizmo();
                gizmo._center            = readVector3( object, "center", gizmo._center );
                gizmo._rotationDegree    = readVector3( object, "rotation", gizmo._rotationDegree );
                gizmo._axisIndex         = static_cast<uint32>( object.get( "axis" ).asUint( gizmo._axisIndex ) );
                if ( gizmo._axisIndex > 2 )
                    gizmo._axisIndex = 1;

                const JSONValue params = object.get( "params" );
                if ( params.isObject() )
                {
                    for ( uint32 paramIndex = 0; paramIndex < modifier->getParamCount(); ++paramIndex )
                    {
                        const utf8* pName = modifier->getParamDefs()[paramIndex]._pName;
                        if ( params.has( pName, false ) )
                            readParam( *modifier, paramIndex, params.get( pName, false ) );
                    }
                }
                modifier->onParamsChanged();
                const JSONValue extra = object.get( "extra" );
                if ( extra.isObject() && modifier->readExtraJSON( extra ) == false )
                    SW_LOG_WARNING( "Mesh modifier '%#' has broken extra state - defaults are used", typeID.c_str() );
                return modifier;
            }

            static string_view stripMeshExtension( string_view meshPath )
            {
                const bool bHasExtension = meshPath.size() >= kMeshExtension.size() &&
                                           meshPath.substr( meshPath.size() - kMeshExtension.size() ) == kMeshExtension;
                return bHasExtension ? meshPath.substr( 0, meshPath.size() - kMeshExtension.size() ) : meshPath;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    MeshModifierStack::MeshModifierStack()
        : _listModifier{}
        , _bRecomputeNormals{ false }
    {
    }

    int32 MeshModifierStack::addModifier( string_view typeID )
    {
        unique_ptr<MeshModifier> modifier = MeshModifierRegistry::create( typeID );
        if ( modifier == nullptr )
            return -1;
        return addModifier( std::move( modifier ) );
    }

    int32 MeshModifierStack::addModifier( unique_ptr<MeshModifier> modifier )
    {
        if ( modifier == nullptr )
            return -1;
        _listModifier.push_back( std::move( modifier ) );
        return static_cast<int32>( _listModifier.size() - 1 );
    }

    bool MeshModifierStack::removeModifier( int32 index )
    {
        if ( index < 0 || getModifierCount() <= index )
            return false;
        _listModifier.erase( _listModifier.begin() + index );
        return true;
    }

    bool MeshModifierStack::moveUp( int32 index )
    {
        if ( index <= 0 || getModifierCount() <= index )
            return false;
        std::swap( _listModifier[static_cast<size_t>( index )], _listModifier[static_cast<size_t>( index - 1 )] );
        return true;
    }

    bool MeshModifierStack::moveDown( int32 index )
    {
        if ( index < 0 || getModifierCount() - 1 <= index )
            return false;
        std::swap( _listModifier[static_cast<size_t>( index )], _listModifier[static_cast<size_t>( index + 1 )] );
        return true;
    }

    void MeshModifierStack::clear()
    {
        _listModifier.clear();
    }

    void MeshModifierStack::apply( MeshModifierMesh& inoutMesh, const MeshModifierEvaluation& evaluation ) const
    {
        for ( const unique_ptr<MeshModifier>& modifier : _listModifier )
        {
            modifier->apply( inoutMesh, evaluation );
        }
        if ( _bRecomputeNormals )
            MeshModifierUtil::recomputeFaceNormals( inoutMesh._listPosition, inoutMesh._listNormal );
    }

    string MeshModifierStack::makeJSONText( int32 indent ) const
    {
        JSONDocument    document;
        const JSONValue root = document.makeObject();
        root.set( "version" ).setUint( kVersion );
        root.set( "recomputeNormals" ).setBool( _bRecomputeNormals );
        const JSONValue modifiers = root.set( "modifiers" );
        modifiers.setArray();
        for ( const unique_ptr<MeshModifier>& modifier : _listModifier )
        {
            MeshModifierStackInternal::writeModifier( *modifier, modifiers.pushBack() );
        }
        return document.dump( indent );
    }

    bool MeshModifierStack::readJSONText( string_view jsonText )
    {
        clear();
        JSONDocument document;
        if ( document.parse( jsonText, "modifiers.json" ) == false )
            return false;
        const JSONValue root = document.getRoot();
        if ( root.isObject() == false || root.get( "version" ).asUint( 0 ) != kVersion )
            return false;
        _bRecomputeNormals        = root.get( "recomputeNormals" ).asBool( false );
        const JSONValue modifiers = root.get( "modifiers" );
        if ( modifiers.isArray() == false )
            return false;
        for ( size_t index = 0; index < modifiers.size(); ++index )
        {
            const JSONValue object = modifiers.at( index );
            if ( object.isObject() == false )
                continue;
            (void)addModifier( MeshModifierStackInternal::readModifier( object ) ); // 모르는 종류는 readModifier 가 경고하고 nullptr 이다
        }
        return true;
    }

    bool MeshModifierStack::saveFile( string_view absolutePath ) const
    {
        const string text = makeJSONText( 2 );
        return FileUtil::writeFile( absolutePath, reinterpret_cast<const uint8*>( text.data() ), text.size() );
    }

    bool MeshModifierStack::loadFile( string_view absolutePath )
    {
        string text;
        if ( FileUtil::readTextFile( absolutePath, text ) == false )
            return false;
        return readJSONText( text );
    }

    string MeshModifierStack::makeSidecarPath( string_view meshPath )
    {
        string path( MeshModifierStackInternal::stripMeshExtension( meshPath ) );
        path.append( kSidecarExtension );
        return path;
    }

    string MeshModifierStack::makeBakedMeshPath( string_view meshPath )
    {
        string path( MeshModifierStackInternal::stripMeshExtension( meshPath ) );
        path.append( kBakedSuffix );
        path.append( MeshModifierStackInternal::kMeshExtension );
        return path;
    }

    MeshModifier* MeshModifierStack::findModifier( int32 index )
    {
        if ( index < 0 || getModifierCount() <= index )
            return nullptr;
        return _listModifier[static_cast<size_t>( index )].get();
    }

    const MeshModifier* MeshModifierStack::findModifier( int32 index ) const
    {
        if ( index < 0 || getModifierCount() <= index )
            return nullptr;
        return _listModifier[static_cast<size_t>( index )].get();
    }
} // namespace sw::editor
