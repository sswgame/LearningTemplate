#include "pch.h"

#include "Engine/Animation/Rig/RigAsset.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Rig/RigNodeLibrary.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    SW_LOG_CALLER( "RigAsset" );

    namespace
    {
        struct RigAssetInternal
        {
            /** @brief 다음 내용 번호입니다(0 은 "읽지 않음"). */
            static uint64 allocateContentID()
            {
                static atomic<uint64> s_nextContentID{ 1 };
                return s_nextContentID.fetch_add( 1, std::memory_order_relaxed );
            }
        };
    } // namespace

    RigNodeRegistry& RigNodeRegistry::getInstance()
    {
        // 엔진 노드는 등록부가 처음 만들어질 때 한 번 든다(함수 안 정적 초기화라 스레드에 안전하다).
        static RigNodeRegistry s_registry;
        static const bool      s_bEngineNodes = RigNodeLibrary::registerEngineNodes( s_registry );
        (void)s_bEngineNodes;
        return s_registry;
    }

    void RigNodeRegistry::registerNode( const hashed_string& typeName, RigNodeFactory factory )
    {
        _mapFactory[typeName] = factory;
    }

    unique_ptr<RigNode> RigNodeRegistry::createNode( const hashed_string& typeName ) const
    {
        const auto it = _mapFactory.find( typeName );
        if ( it == _mapFactory.end() || it->second == nullptr )
            return nullptr;
        return it->second();
    }

    vector<hashed_string> RigNodeRegistry::getTypeNames() const
    {
        vector<hashed_string> listName;
        for ( const auto& [name, factory] : _mapFactory )
        {
            listName.push_back( name );
        }
        return listName;
    }

    RigAsset::RigAsset()
        : _listTarget{}
        , _listNode{}
        , _contentID{ 0 }
        , _bPlanar{ SW_FALSE }
    {
    }

    RigAsset::~RigAsset() = default;

    RigAsset::RigAsset( RigAsset&& other ) noexcept
        : _listTarget{ std::move( other._listTarget ) }
        , _listNode{ std::move( other._listNode ) }
        , _contentID{ other._contentID }
        , _bPlanar{ other._bPlanar }
    {
    }

    RigAsset& RigAsset::operator=( RigAsset&& other ) noexcept
    {
        _listTarget = std::move( other._listTarget );
        _listNode   = std::move( other._listNode );
        _contentID  = other._contentID;
        _bPlanar    = other._bPlanar;
        return *this;
    }

    void RigAsset::clear()
    {
        _listTarget.clear();
        _listNode.clear();
        _contentID = 0;
        _bPlanar   = SW_FALSE;
    }

    int32 RigAsset::findTargetIndex( const hashed_string& name ) const
    {
        for ( size_t index = 0; index < _listTarget.size(); ++index )
        {
            if ( _listTarget[index]._name == name )
                return static_cast<int32>( index );
        }
        return -1;
    }

    bool RigAsset::parseJSON( string_view json, string_view sourceLabel )
    {
        SW_MEMORY_SCOPE( Animation );
        clear();
        JSONDocument document;
        if ( document.parse( json, sourceLabel ) == false )
        {
            SW_LOG_ERROR( "Rig '%#': malformed JSON", sourceLabel );
            return false;
        }
        if ( parseRoot( document.getRoot(), sourceLabel ) )
        {
            _contentID = RigAssetInternal::allocateContentID();
            return true;
        }
        clear();
        return false;
    }

    bool RigAsset::loadFromResource( string_view path )
    {
        string text;
        if ( ResourceUtil::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Rig '%#' could not be read", path );
            clear();
            return false;
        }
        return parseJSON( text, path );
    }

    bool RigAsset::parseRoot( const JSONValue& root, string_view sourceLabel )
    {
        RigJSONReader reader( root, sourceLabel );
        bool          bPlanar = false;
        if ( reader.readBool( "planar", bPlanar, false ) == false )
            return false;
        _bPlanar                = bPlanar ? SW_TRUE : SW_FALSE;
        const JSONValue targets = reader.readArray( "targets", false );
        const JSONValue nodes   = reader.readArray( "nodes", true );
        if ( reader.finish() == false )
            return false;
        if ( nodes.size() == 0 )
        {
            SW_LOG_ERROR( "Rig '%#': 'nodes' must not be empty", sourceLabel );
            return false;
        }
        for ( size_t index = 0; targets.isValid() && index < targets.size(); ++index )
        {
            if ( parseTarget( targets.at( index ), sourceLabel ) == false )
                return false;
        }
        for ( size_t index = 0; index < nodes.size(); ++index )
        {
            if ( parseNode( nodes.at( index ), sourceLabel ) == false )
                return false;
        }
        return true;
    }

    bool RigAsset::parseTarget( const JSONValue& value, string_view sourceLabel )
    {
        RigJSONReader reader( value, sourceLabel );
        RigTargetDef  target{};
        bool          bOk = reader.readName( "name", target._name, true ) && reader.readName( "bone", target._bone, false ) &&
                   reader.readName( "socket", target._socket, false ) && reader.readName( "object", target._object, false ) &&
                   reader.readName( "unit", target._unit, false ) && reader.readName( "space", target._space, false ) &&
                   reader.readFloat3( "translation", target._offset._translation, false ) &&
                   reader.readRotationDegrees( "rotation", target._offset._rotation, false );
        bOk = reader.finish() && bOk;
        if ( bOk == false )
            return false;

        const uint32 sourceCount = ( target._bone.empty() ? 0u : 1u ) + ( target._socket.empty() ? 0u : 1u ) + ( target._object.empty() ? 0u : 1u );
        if ( sourceCount != 1 )
        {
            SW_LOG_ERROR( "Rig '%#': target '%#' must name exactly one of 'bone', 'socket', 'object'", sourceLabel, target._name.c_str() );
            return false;
        }
        if ( target._object.empty() == false && target._unit.empty() == false )
        {
            SW_LOG_ERROR( "Rig '%#': target '%#' is an object and cannot name a 'unit'", sourceLabel, target._name.c_str() );
            return false;
        }
        target._kind = target._bone.empty() == false ? RigTargetKind::Bone : ( target._socket.empty() == false ? RigTargetKind::Socket : RigTargetKind::Object );
        if ( findTargetIndex( target._name ) >= 0 )
        {
            SW_LOG_ERROR( "Rig '%#': duplicate target name '%#'", sourceLabel, target._name.c_str() );
            return false;
        }
        _listTarget.push_back( target );
        return true;
    }

    bool RigAsset::parseNode( const JSONValue& value, string_view sourceLabel )
    {
        RigJSONReader reader( value, sourceLabel );
        hashed_string typeName{};
        if ( reader.readName( "type", typeName, true ) == false )
            return false;
        unique_ptr<RigNode> node = RigNodeRegistry::getInstance().createNode( typeName );
        if ( node == nullptr )
        {
            SW_LOG_ERROR( "Rig '%#': unknown node type '%#'", sourceLabel, typeName.c_str() );
            return false;
        }
        bool bOk = node->parseCommon( reader ) && node->parse( reader );
        bOk      = reader.finish() && bOk;
        if ( bOk == false )
            return false;
        for ( const unique_ptr<RigNode>& existing : _listNode )
        {
            if ( existing->getName() == node->getName() )
            {
                SW_LOG_ERROR( "Rig '%#': duplicate node name '%#'", sourceLabel, node->getName().c_str() );
                return false;
            }
        }
        _listNode.push_back( std::move( node ) );
        return true;
    }
} // namespace sw
