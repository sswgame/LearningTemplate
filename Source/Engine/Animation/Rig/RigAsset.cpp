#include "pch.h"

#include "Engine/Animation/Rig/RigAsset.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Rig/RigConstraintNodes.h"
#include "Engine/Animation/Rig/RigIKNodes.h"
#include "Engine/Animation/Rig/RigSecondaryNodes.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

// RigNodeLibrary — 이 파일만 쓴다(한 파일 전용 도우미는 따로 헤더를 두지 않는다)
namespace sw
{
    class RigNodeRegistry;

    /** @brief 엔진 노드 등록 함수 모음입니다(`RigNodeRegistry::getInstance` 가 처음 한 번 호출합니다). */
    struct RigNodeLibrary
    {
        /** @brief 엔진 노드 종류를 모두 등록합니다. 늘 true 입니다(정적 초기화에 쓰려고 값을 돌려줍니다). */
        [[nodiscard]] static bool registerEngineNodes( RigNodeRegistry& registry );
    };
} // namespace sw

namespace sw
{
    bool RigNodeLibrary::registerEngineNodes( RigNodeRegistry& registry )
    {
        RigIKNodes::registerNodes( registry );
        RigConstraintNodes::registerNodes( registry );
        RigSecondaryNodes::registerNodes( registry );
        return true;
    }
} // namespace sw

namespace sw
{
    namespace
    {
        /** @brief 다음 내용 번호입니다(0 은 "읽지 않음"). */
        uint64 allocateContentID()
        {
            static atomic<uint64> s_nextContentID{ 1 };
            return s_nextContentID.fetch_add( 1, std::memory_order_relaxed );
        }
    } // namespace

    RigNodeRegistry& RigNodeRegistry::getInstance()
    {
        // 엔진 노드는 레지스트리가 처음 만들어질 때 한 번 등록한다(함수 안 정적 초기화라 스레드에 안전하다).
        static RigNodeRegistry s_registry;
        static const bool      s_bEngineNodes = RigNodeLibrary::registerEngineNodes( s_registry );
        (void)s_bEngineNodes;
        return s_registry;
    }

    void RigNodeRegistry::registerNode( const hashed_string& typeName, RigNodeFactory factory )
    {
        _registry.addOrReplace( typeName, factory, "Rig node type" );
    }

    unique_ptr<RigNode> RigNodeRegistry::createNode( const hashed_string& typeName ) const
    {
        const RigNodeFactory* pFactory = _registry.find( typeName );
        if ( pFactory == nullptr || *pFactory == nullptr )
            return nullptr;
        return ( *pFactory )();
    }

    vector<hashed_string> RigNodeRegistry::getTypeNames() const
    {
        vector<hashed_string> listName;
        listName.reserve( _registry.getCount() );
        for ( uint32 index = 0; index < _registry.getCount(); ++index )
        {
            listName.push_back( _registry.getNameAt( index ) );
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

    bool RigAsset::parseXML( string_view xml, string_view sourceLabel )
    {
        SW_MEMORY_SCOPE( Animation );
        clear();
        XMLDocument document;
        XMLNode     root;
        if ( XMLReadUtil::parseRoot( document, xml, sourceLabel, "RigAsset", root ) != XMLRootStatus::Found )
        {
            SW_LOG_ERROR_ID( "RigAsset.RigMalformedXMLExpected", "Rig '%#': malformed XML (expected a <RigAsset> root)", sourceLabel );
            return false;
        }
        if ( parseRoot( root, sourceLabel ) )
        {
            _contentID = allocateContentID();
            return true;
        }
        clear();
        return false;
    }

    bool RigAsset::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "RigAsset.RigCouldNotRead", "Rig '%#' could not be read", path );
            clear();
            return false;
        }
        return parseXML( text, path );
    }

    bool RigAsset::parseRoot( const XMLNode& root, string_view sourceLabel )
    {
        RigXMLReader       reader( root, sourceLabel );
        RigAssetRootFields rootFields;
        if ( reader.readFields( rootFields ) == false )
            return false;
        _bPlanar                 = rootFields._bPlanar ? SW_TRUE : SW_FALSE;
        const RigXMLList targets = reader.readArray( "targets", false );
        const RigXMLList nodes   = reader.readArray( "nodes", true );
        if ( reader.finish() == false )
            return false;
        if ( nodes.size() == 0 )
        {
            SW_LOG_ERROR_ID( "RigAsset.RigNodesMustNot", "Rig '%#': 'nodes' must not be empty", sourceLabel );
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

    bool RigAsset::parseTarget( const XMLNode& value, string_view sourceLabel )
    {
        RigXMLReader    reader( value, sourceLabel );
        RigTargetFields fields;
        bool            bOk = reader.readFields( fields );
        bOk                 = reader.finish() && bOk;
        if ( bOk == false )
            return false;
        RigTargetDef target{};
        target._name                = fields._name;
        target._bone                = fields._bone;
        target._socket              = fields._socket;
        target._object              = fields._object;
        target._unit                = fields._unit;
        target._space               = fields._space;
        target._offset._translation = fields._translation;
        target._offset._rotation    = quaternion::makeFromYawPitchRoll( fields._rotation * MathUtil::kDegreeToRadian );

        const uint32 sourceCount = ( target._bone.empty() ? 0u : 1u ) + ( target._socket.empty() ? 0u : 1u ) + ( target._object.empty() ? 0u : 1u );
        if ( sourceCount != 1 )
        {
            SW_LOG_ERROR_ID( "RigAsset.RigTargetMustName", "Rig '%#': target '%#' must name exactly one of 'bone', 'socket', 'object'", sourceLabel, target._name.c_str() );
            return false;
        }
        if ( target._object.empty() == false && target._unit.empty() == false )
        {
            SW_LOG_ERROR_ID( "RigAsset.RigTargetObjectCannot", "Rig '%#': target '%#' is an object and cannot name a 'unit'", sourceLabel, target._name.c_str() );
            return false;
        }
        target._kind = target._bone.empty() == false ? RigTargetKind::Bone : ( target._socket.empty() == false ? RigTargetKind::Socket : RigTargetKind::Object );
        if ( findTargetIndex( target._name ) >= 0 )
        {
            SW_LOG_ERROR_ID( "RigAsset.RigDuplicateTargetName", "Rig '%#': duplicate target name '%#'", sourceLabel, target._name.c_str() );
            return false;
        }
        _listTarget.push_back( target );
        return true;
    }

    bool RigAsset::parseNode( const XMLNode& value, string_view sourceLabel )
    {
        RigXMLReader  reader( value, sourceLabel );
        hashed_string typeName{};
        if ( reader.readName( "type", typeName, true ) == false )
            return false;
        unique_ptr<RigNode> node = RigNodeRegistry::getInstance().createNode( typeName );
        if ( node == nullptr )
        {
            SW_LOG_ERROR_ID( "RigAsset.RigUnknownNodeType", "Rig '%#': unknown node type '%#'", sourceLabel, typeName.c_str() );
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
                SW_LOG_ERROR_ID( "RigAsset.RigDuplicateNodeName", "Rig '%#': duplicate node name '%#'", sourceLabel, node->getName().c_str() );
                return false;
            }
        }
        _listNode.push_back( std::move( node ) );
        return true;
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( RigAsset, RigAsset::kAssetType );
} // namespace sw
