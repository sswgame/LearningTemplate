#include "pch.h"

#include "Engine/Character/Socket/SocketSet.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Asset/AssetDataKind.h"
#include "Engine/Character/CharacterDataReader.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct SocketSetInternal
        {

            /** @brief 위층 항목에서 적은 필드만 아래층 항목에 옮깁니다. */
            static void mergeSocket( SocketDef& inoutLower, const SocketDef& upper )
            {
                const uint16 mask = upper._fieldMask;
                if ( ( mask & SocketFieldBit::kParent ) != 0 )
                    inoutLower._parent = upper._parent;
                if ( ( mask & SocketFieldBit::kKind ) != 0 )
                    inoutLower._kind = upper._kind;
                if ( ( mask & SocketFieldBit::kTranslation ) != 0 )
                    inoutLower._translation = upper._translation;
                if ( ( mask & SocketFieldBit::kRotation ) != 0 )
                    inoutLower._rotation = upper._rotation;
                if ( ( mask & SocketFieldBit::kScale ) != 0 )
                    inoutLower._scale = upper._scale;
                if ( ( mask & SocketFieldBit::kPreview ) != 0 )
                    inoutLower._previewMesh = upper._previewMesh;
                if ( ( mask & SocketFieldBit::kFallback ) != 0 )
                    inoutLower._listFallback = upper._listFallback;
                if ( ( mask & SocketFieldBit::kAnchor ) != 0 )
                    inoutLower._anchor = upper._anchor;
                inoutLower._fieldMask = static_cast<uint16>( inoutLower._fieldMask | mask );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool SocketKindTable::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        CharacterDataReader reader( sourceName );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.parseRoot( document, xmlText, "SocketKinds", root ) )
            readRoot( root, reader );
        return reader.finish();
    }

    bool SocketKindTable::loadFromAsset( const AssetID& assetID )
    {
        const string_view   path = assetID.view();
        CharacterDataReader reader( path );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.loadRoot( document, assetID, "SocketKinds", root ) )
            readRoot( root, reader );
        return reader.finish();
    }

    bool SocketKindTable::loadDefault()
    {
        const string& path = engine::getSubsystem<EngineDefaultAssets>()._defaultSocketKinds.get();
        if ( loadFromAsset( AssetID{ path } ) )
            return true;
        SW_LOG_ERROR_ID( "SocketKindTable.DefaultSocketKindsCould", "Default socket kinds '%#' could not be loaded", path.c_str() );
        return false;
    }

    void SocketKindTable::readRoot( const XMLNode& root, CharacterDataReader& reader )
    {
        reader.reportUnexpectedAttributes( root );
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Kind" ) == false )
            {
                reader.reportUnknownElement( child );
                continue;
            }
            SocketKindFields kindFields;
            (void)reader.readFields( child, kindFields ); // 오류는 읽개의 목록에 쌓인다
            const hashed_string name = kindFields._name;
            if ( name.empty() )
                continue;
            if ( hasKind( name ) )
            {
                reader.addError( "Character.SocketKindRepeated", child, string( "repeats kind '" ) + name.c_str() + "'" );
                continue;
            }
            _listKind.push_back( name );
        }
    }

    void SocketKindTable::addKind( const hashed_string& name )
    {
        if ( name.empty() || hasKind( name ) )
            return;
        _listKind.push_back( name );
    }

    bool SocketKindTable::hasKind( const hashed_string& name ) const
    {
        return VectorUtil::contains( _listKind, name );
    }
} // namespace sw

namespace sw
{
    float4x4 SocketDef::makeLocalTransform() const
    {
        return CharacterGeometryUtil::makeTransform( _translation, _rotation, _scale );
    }
} // namespace sw

namespace sw
{
    bool SocketSet::loadFromXMLText( string_view xmlText, string_view sourceName, const SocketKindTable& kinds, const CharacterBoneArray* pBones )
    {
        clear();
        CharacterDataReader reader( sourceName );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.parseRoot( document, xmlText, "SocketSet", root ) )
            readRoot( root, kinds, reader );
        if ( pBones != nullptr )
        {
            CharacterIssueList boneIssues;
            if ( validateBones( *pBones, &boneIssues ) == false )
                reader.addIssues( boneIssues );
        }
        return reader.finish();
    }

    bool SocketSet::loadFromAsset( const AssetID& assetID, const SocketKindTable& kinds, const CharacterBoneArray* pBones )
    {
        const string_view path = assetID.view();
        clear();
        CharacterDataReader reader( path );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.loadRoot( document, assetID, "SocketSet", root ) )
            readRoot( root, kinds, reader );
        if ( pBones != nullptr )
        {
            CharacterIssueList boneIssues;
            if ( validateBones( *pBones, &boneIssues ) == false )
                reader.addIssues( boneIssues );
        }
        return reader.finish();
    }

    void SocketSet::readRoot( const XMLNode& root, const SocketKindTable& kinds, CharacterDataReader& reader )
    {
        reader.reportUnexpectedAttributes( root );
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Socket" ) )
                readSocket( child, kinds, reader );
            else if ( StringUtil::equals( child.getName(), "VirtualBone" ) )
                readVirtualBone( child, reader );
            else
                reader.reportUnknownElement( child );
        }
    }

    void SocketSet::readSocket( const XMLNode& node, const SocketKindTable& kinds, CharacterDataReader& reader )
    {
        // 칸은 스키마로 읽고, 적은 칸만 덮어쓰기 표시를 단다(층이 쌓일 때 적지 않은 칸은 아래 층의 값을 둔다).
        SocketFields fields;
        (void)reader.readFields( node, fields, { "fallback" } );
        SocketDef socket;
        socket._fieldMask = 0;
        socket._name      = fields._name;
        if ( socket._name.empty() )
            return;
        if ( findSocket( socket._name ) != nullptr )
        {
            reader.addError( "Character.SocketRepeated", node, string( "repeats socket '" ) + socket._name.c_str() + "'" );
            return;
        }
        const auto markWritten = [&node, &socket]( const utf8* pName, uint16 bit )
        {
            const bool bWritten = node.hasAttribute( pName );
            if ( bWritten )
                socket._fieldMask |= bit;
            return bWritten;
        };
        if ( markWritten( "parent", SocketFieldBit::kParent ) )
            socket._parent = fields._parent;
        if ( markWritten( "kind", SocketFieldBit::kKind ) )
        {
            socket._kind = fields._kind;
            if ( socket._kind.empty() == false && kinds.hasKind( socket._kind ) == false )
                reader.addError( "Character.SocketKindUnknown", node, string( "has unknown kind '" ) + socket._kind.c_str() + "'" );
        }
        if ( markWritten( "translation", SocketFieldBit::kTranslation ) )
            socket._translation = fields._translation;
        if ( markWritten( "rotation", SocketFieldBit::kRotation ) )
            socket._rotation = quaternion::makeFromYawPitchRoll( fields._rotation * MathUtil::kDegreeToRadian );
        if ( markWritten( "scale", SocketFieldBit::kScale ) )
            socket._scale = fields._scale;
        if ( markWritten( "preview", SocketFieldBit::kPreview ) )
            socket._previewMesh = string( StringUtil::trim( fields._preview ) );
        if ( markWritten( "fallback", SocketFieldBit::kFallback ) )
            reader.readNameList( node, "fallback", socket._listFallback );
        if ( markWritten( "anchor", SocketFieldBit::kAnchor ) )
            socket._anchor = fields._anchor;
        _listSocket.push_back( std::move( socket ) );
    }

    void SocketSet::readVirtualBone( const XMLNode& node, CharacterDataReader& reader )
    {
        VirtualBoneFields fields;
        (void)reader.readFields( node, fields );
        VirtualBoneDef virtualBone;
        virtualBone._name                 = fields._name;
        virtualBone._from                 = fields._from;
        virtualBone._to                   = fields._to;
        virtualBone._weight               = fields._weight;
        const bool bIncompleteVirtualBone = virtualBone._name.empty() || virtualBone._from.empty() || virtualBone._to.empty();
        if ( bIncompleteVirtualBone )
            return;
        if ( findVirtualBone( virtualBone._name ) != nullptr )
        {
            reader.addError( "Character.VirtualBoneRepeated", node, string( "repeats virtual bone '" ) + virtualBone._name.c_str() + "'" );
            return;
        }
        _listVirtualBone.push_back( virtualBone );
    }

    string SocketSet::saveToXMLText() const
    {
        XMLDocument document;
        XMLNode     root = document.appendRoot( "SocketSet" );
        for ( const VirtualBoneDef& virtualBone : _listVirtualBone )
        {
            XMLNode node = root.appendChild( "VirtualBone" );
            node.appendAttribute( "name", virtualBone._name.view() );
            node.appendAttribute( "from", virtualBone._from.view() );
            node.appendAttribute( "to", virtualBone._to.view() );
            node.appendAttribute( "weight", CharacterDataReader::formatFloat( virtualBone._weight ).c_str() );
        }
        for ( const SocketDef& socket : _listSocket )
        {
            XMLNode node = root.appendChild( "Socket" );
            node.appendAttribute( "name", socket._name.view() );
            const uint16 mask = socket._fieldMask;
            if ( ( mask & SocketFieldBit::kParent ) != 0 && socket._parent.empty() == false )
                node.appendAttribute( "parent", socket._parent.view() );
            if ( ( mask & SocketFieldBit::kKind ) != 0 && socket._kind.empty() == false )
                node.appendAttribute( "kind", socket._kind.view() );
            if ( ( mask & SocketFieldBit::kTranslation ) != 0 )
                node.appendAttribute( "translation", CharacterDataReader::formatFloat3( socket._translation ).c_str() );
            if ( ( mask & SocketFieldBit::kRotation ) != 0 )
                node.appendAttribute( "rotation", CharacterDataReader::formatRotation( socket._rotation ).c_str() );
            if ( ( mask & SocketFieldBit::kScale ) != 0 )
                node.appendAttribute( "scale", CharacterDataReader::formatFloat3( socket._scale ).c_str() );
            if ( ( mask & SocketFieldBit::kPreview ) != 0 && socket._previewMesh.empty() == false )
                node.appendAttribute( "preview", socket._previewMesh.c_str() );
            if ( ( mask & SocketFieldBit::kFallback ) != 0 && socket._listFallback.empty() == false )
                node.appendAttribute( "fallback", CharacterDataReader::formatNameList( socket._listFallback ).c_str() );
            if ( ( mask & SocketFieldBit::kAnchor ) != 0 )
                node.appendAttribute( "anchor", engine::getSubsystem<TypeRegistry>().enumToString( socket._anchor ) );
        }
        return document.saveToString();
    }

    void SocketSet::addSocket( const SocketDef& socket )
    {
        for ( SocketDef& existing : _listSocket )
        {
            if ( existing._name == socket._name )
            {
                existing = socket;
                return;
            }
        }
        _listSocket.push_back( socket );
    }

    void SocketSet::addVirtualBone( const VirtualBoneDef& virtualBone )
    {
        for ( VirtualBoneDef& existing : _listVirtualBone )
        {
            if ( existing._name == virtualBone._name )
            {
                existing = virtualBone;
                return;
            }
        }
        _listVirtualBone.push_back( virtualBone );
    }

    const SocketDef* SocketSet::findSocket( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&SocketDef::_name>( _listSocket, name );
    }

    const VirtualBoneDef* SocketSet::findVirtualBone( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&VirtualBoneDef::_name>( _listVirtualBone, name );
    }

    void SocketSet::clear()
    {
        _listSocket.clear();
        _listVirtualBone.clear();
    }

    void SocketSet::applyOverride( const SocketSet& upper )
    {
        for ( const VirtualBoneDef& virtualBone : upper._listVirtualBone )
        {
            addVirtualBone( virtualBone );
        }
        for ( const SocketDef& upperSocket : upper._listSocket )
        {
            bool bMerged = false;
            for ( SocketDef& lowerSocket : _listSocket )
            {
                if ( lowerSocket._name == upperSocket._name )
                {
                    SocketSetInternal::mergeSocket( lowerSocket, upperSocket );
                    bMerged = true;
                    break;
                }
            }
            if ( bMerged == false )
                _listSocket.push_back( upperSocket );
        }
    }

    bool SocketSet::validateReferences( const SocketKindTable& kinds, CharacterIssueList* pOutIssues ) const
    {
        bool bValid = true;
        for ( const SocketDef& socket : _listSocket )
        {
            if ( socket._kind.empty() == false && kinds.hasKind( socket._kind ) == false )
            {
                bValid = false;
                if ( pOutIssues != nullptr )
                    pOutIssues->add( "Character.SocketKindUnknown", string( "socket '" ) + socket._name.c_str() + "' has unknown kind '" + socket._kind.c_str() + "'" );
            }
            for ( const hashed_string& fallback : socket._listFallback )
            {
                if ( findSocket( fallback ) != nullptr )
                    continue;
                bValid = false;
                if ( pOutIssues != nullptr )
                    pOutIssues->add( "Character.SocketFallbackUnknown", string( "socket '" ) + socket._name.c_str() + "' has unknown fallback '" + fallback.c_str() + "'" );
            }
        }
        return bValid;
    }

    bool SocketSet::validateBones( const CharacterBoneArray& bones, CharacterIssueList* pOutIssues ) const
    {
        bool bValid = true;
        for ( const VirtualBoneDef& virtualBone : _listVirtualBone )
        {
            const bool bFromKnown = bones.findBone( virtualBone._from ) >= 0;
            const bool bToKnown   = bones.findBone( virtualBone._to ) >= 0;
            if ( bFromKnown && bToKnown )
                continue;
            bValid = false;
            if ( pOutIssues != nullptr )
            {
                pOutIssues->add( "Character.VirtualBoneBoneUnknown", string( "virtual bone '" ) + virtualBone._name.c_str() + "' names a bone the skeleton does not have ('" +
                                                                         ( bFromKnown ? virtualBone._to.c_str() : virtualBone._from.c_str() ) + "')" );
            }
        }
        for ( const SocketDef& socket : _listSocket )
        {
            const bool bParentEmptyOrResolved = socket._parent.empty() || bones.findBone( socket._parent ) >= 0 || findVirtualBone( socket._parent ) != nullptr;
            if ( bParentEmptyOrResolved )
                continue;
            bValid = false;
            if ( pOutIssues != nullptr )
                pOutIssues->add( "Character.SocketParentBoneUnknown", string( "socket '" ) + socket._name.c_str() + "' has unknown parent bone '" + socket._parent.c_str() + "'" );
        }
        return bValid;
    }

    bool SocketSet::computeSocketTransform( const SocketDef& socket, const CharacterBoneArray& bones, float4x4& outUnitTransform ) const
    {
        const float4x4 local = socket.makeLocalTransform();
        if ( socket._parent.empty() )
        {
            outUnitTransform = local;
            return true;
        }
        const int32 boneIndex = bones.findBone( socket._parent );
        if ( boneIndex >= 0 )
        {
            outUnitTransform = local * bones._listModel[static_cast<size_t>( boneIndex )];
            return true;
        }
        const VirtualBoneDef* pVirtualBone = findVirtualBone( socket._parent );
        float4x4              parentTransform;
        if ( pVirtualBone == nullptr || computeVirtualBoneTransform( *pVirtualBone, bones, parentTransform ) == false )
            return false;
        outUnitTransform = local * parentTransform;
        return true;
    }

    bool SocketSet::computeVirtualBoneTransform( const VirtualBoneDef& virtualBone, const CharacterBoneArray& bones, float4x4& outUnitTransform )
    {
        const int32 fromIndex = bones.findBone( virtualBone._from );
        const int32 toIndex   = bones.findBone( virtualBone._to );
        if ( fromIndex < 0 || toIndex < 0 )
            return false;
        outUnitTransform = CharacterGeometryUtil::blendTransforms( bones._listModel[static_cast<size_t>( fromIndex )],
                                                                   bones._listModel[static_cast<size_t>( toIndex )], virtualBone._weight );
        return true;
    }

    /** @brief 소켓 종류 표 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_RESOURCE_DATA_KIND( SocketKinds, "socketkinds", nullptr, nullptr, ".socketkinds.xml", &AssetDataKind::loadCatalog<SocketKindTable> );
} // namespace sw
