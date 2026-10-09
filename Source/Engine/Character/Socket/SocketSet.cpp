#include "pch.h"

#include "Engine/Character/Socket/SocketSet.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Character/CharacterDataReader.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        struct SocketSetInternal
        {
            static constexpr const utf8* kArrKindAttribute[]        = { "name" };
            static constexpr const utf8* kArrSocketAttribute[]      = { "name", "parent", "kind", "translation", "rotation", "scale", "preview", "fallback", "anchor" };
            static constexpr const utf8* kArrVirtualBoneAttribute[] = { "name", "from", "to", "weight" };

            /** @brief 위층 항목의 적은 칸만 아래층 항목에 옮긴다. */
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
    bool SocketKindTable::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        CharacterDataReader reader( sourceName );
        XmlDocument         document;
        XmlNode             root;
        if ( reader.parseRoot( document, xmlText, "SocketKinds", root ) )
            readRoot( root, reader );
        return reader.finish();
    }

    bool SocketKindTable::loadFromResource( string_view path )
    {
        CharacterDataReader reader( path );
        XmlDocument         document;
        XmlNode             root;
        if ( reader.loadRoot( document, path, "SocketKinds", root ) )
            readRoot( root, reader );
        return reader.finish();
    }

    void SocketKindTable::readRoot( const XmlNode& root, CharacterDataReader& reader )
    {
        reader.reportUnexpectedAttributes( root );
        for ( XmlNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Kind", true ) == false )
            {
                reader.reportUnknownElement( child );
                continue;
            }
            reader.reportUnknownAttributes( child, SocketSetInternal::kArrKindAttribute );
            const hashed_string name = reader.readName( child, "name", true );
            if ( name.empty() )
                continue;
            if ( hasKind( name ) )
            {
                reader.addError( child, string( "repeats kind '" ) + name.c_str() + "'" );
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
        for ( const hashed_string& kind : _listKind )
        {
            if ( kind == name )
                return true;
        }
        return false;
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
    bool SocketSet::loadFromXmlText( string_view xmlText, string_view sourceName, const SocketKindTable& kinds, const CharacterBoneArray* pBones )
    {
        clear();
        CharacterDataReader reader( sourceName );
        XmlDocument         document;
        XmlNode             root;
        if ( reader.parseRoot( document, xmlText, "SocketSet", root ) )
            readRoot( root, kinds, reader );
        if ( pBones != nullptr )
        {
            string boneError;
            if ( validateBones( *pBones, &boneError ) == false )
                reader.addError( boneError );
        }
        return reader.finish();
    }

    bool SocketSet::loadFromResource( string_view path, const SocketKindTable& kinds, const CharacterBoneArray* pBones )
    {
        clear();
        CharacterDataReader reader( path );
        XmlDocument         document;
        XmlNode             root;
        if ( reader.loadRoot( document, path, "SocketSet", root ) )
            readRoot( root, kinds, reader );
        if ( pBones != nullptr )
        {
            string boneError;
            if ( validateBones( *pBones, &boneError ) == false )
                reader.addError( boneError );
        }
        return reader.finish();
    }

    void SocketSet::readRoot( const XmlNode& root, const SocketKindTable& kinds, CharacterDataReader& reader )
    {
        reader.reportUnexpectedAttributes( root );
        for ( XmlNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Socket", true ) )
                readSocket( child, kinds, reader );
            else if ( StringUtil::equals( child.getName(), "VirtualBone", true ) )
                readVirtualBone( child, reader );
            else
                reader.reportUnknownElement( child );
        }
    }

    void SocketSet::readSocket( const XmlNode& node, const SocketKindTable& kinds, CharacterDataReader& reader )
    {
        reader.reportUnknownAttributes( node, SocketSetInternal::kArrSocketAttribute );
        SocketDef socket;
        socket._fieldMask = 0;
        socket._name      = reader.readName( node, "name", true );
        if ( socket._name.empty() )
            return;
        if ( findSocket( socket._name ) != nullptr )
        {
            reader.addError( node, string( "repeats socket '" ) + socket._name.c_str() + "'" );
            return;
        }
        if ( CharacterDataReader::hasAttribute( node, "parent" ) )
        {
            socket._parent = reader.readName( node, "parent", false );
            socket._fieldMask |= SocketFieldBit::kParent;
        }
        if ( CharacterDataReader::hasAttribute( node, "kind" ) )
        {
            socket._kind = reader.readName( node, "kind", true );
            socket._fieldMask |= SocketFieldBit::kKind;
            if ( socket._kind.empty() == false && kinds.hasKind( socket._kind ) == false )
                reader.addError( node, string( "has unknown kind '" ) + socket._kind.c_str() + "'" );
        }
        if ( CharacterDataReader::hasAttribute( node, "translation" ) )
        {
            socket._translation = reader.readFloat3( node, "translation", float3::Zero );
            socket._fieldMask |= SocketFieldBit::kTranslation;
        }
        if ( CharacterDataReader::hasAttribute( node, "rotation" ) )
        {
            socket._rotation = reader.readRotation( node, "rotation", quaternion::Identity );
            socket._fieldMask |= SocketFieldBit::kRotation;
        }
        if ( CharacterDataReader::hasAttribute( node, "scale" ) )
        {
            socket._scale = reader.readFloat3( node, "scale", float3( 1.0f ) );
            socket._fieldMask |= SocketFieldBit::kScale;
        }
        if ( CharacterDataReader::hasAttribute( node, "preview" ) )
        {
            socket._previewMesh = string( StringUtil::trim( node.getAttributeText( "preview" ) ) );
            socket._fieldMask |= SocketFieldBit::kPreview;
        }
        if ( CharacterDataReader::hasAttribute( node, "fallback" ) )
        {
            reader.readNameList( node, "fallback", socket._listFallback );
            socket._fieldMask |= SocketFieldBit::kFallback;
        }
        if ( CharacterDataReader::hasAttribute( node, "anchor" ) )
        {
            const string_view anchorText = StringUtil::trim( node.getAttributeText( "anchor" ) );
            if ( engine::getTypeRegistry().enumFromString( anchorText, socket._anchor ) == false )
                reader.addError( node, string( "has unknown anchor '" ) + string( anchorText ) + "' (Bone, Surface)" );
            socket._fieldMask |= SocketFieldBit::kAnchor;
        }
        _listSocket.push_back( std::move( socket ) );
    }

    void SocketSet::readVirtualBone( const XmlNode& node, CharacterDataReader& reader )
    {
        reader.reportUnknownAttributes( node, SocketSetInternal::kArrVirtualBoneAttribute );
        VirtualBoneDef virtualBone;
        virtualBone._name   = reader.readName( node, "name", true );
        virtualBone._from   = reader.readName( node, "from", true );
        virtualBone._to     = reader.readName( node, "to", true );
        virtualBone._weight = reader.readFloat( node, "weight", 0.5f );
        if ( virtualBone._name.empty() || virtualBone._from.empty() || virtualBone._to.empty() )
            return;
        if ( findVirtualBone( virtualBone._name ) != nullptr )
        {
            reader.addError( node, string( "repeats virtual bone '" ) + virtualBone._name.c_str() + "'" );
            return;
        }
        _listVirtualBone.push_back( virtualBone );
    }

    string SocketSet::saveToXmlText() const
    {
        XmlDocument document;
        XmlNode     root = document.appendRoot( "SocketSet" );
        for ( const VirtualBoneDef& virtualBone : _listVirtualBone )
        {
            XmlNode node = root.appendChild( "VirtualBone" );
            node.appendAttribute( "name", virtualBone._name.view() );
            node.appendAttribute( "from", virtualBone._from.view() );
            node.appendAttribute( "to", virtualBone._to.view() );
            node.appendAttribute( "weight", CharacterDataReader::formatFloat( virtualBone._weight ).c_str() );
        }
        for ( const SocketDef& socket : _listSocket )
        {
            XmlNode node = root.appendChild( "Socket" );
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
                node.appendAttribute( "anchor", engine::getTypeRegistry().enumToString( socket._anchor ) );
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
        for ( const SocketDef& socket : _listSocket )
        {
            if ( socket._name == name )
                return &socket;
        }
        return nullptr;
    }

    const VirtualBoneDef* SocketSet::findVirtualBone( const hashed_string& name ) const
    {
        for ( const VirtualBoneDef& virtualBone : _listVirtualBone )
        {
            if ( virtualBone._name == name )
                return &virtualBone;
        }
        return nullptr;
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

    bool SocketSet::validateBones( const CharacterBoneArray& bones, string* pOutError ) const
    {
        bool bValid = true;
        for ( const VirtualBoneDef& virtualBone : _listVirtualBone )
        {
            const bool bFromKnown = bones.findBone( virtualBone._from ) >= 0;
            const bool bToKnown   = bones.findBone( virtualBone._to ) >= 0;
            if ( bFromKnown && bToKnown )
                continue;
            bValid = false;
            if ( pOutError != nullptr )
            {
                if ( pOutError->empty() == false )
                    *pOutError += "\n";
                *pOutError += string( "virtual bone '" ) + virtualBone._name.c_str() + "' names a bone the skeleton does not have ('" +
                              ( bFromKnown ? virtualBone._to.c_str() : virtualBone._from.c_str() ) + "')";
            }
        }
        for ( const SocketDef& socket : _listSocket )
        {
            if ( socket._parent.empty() || bones.findBone( socket._parent ) >= 0 || findVirtualBone( socket._parent ) != nullptr )
                continue;
            bValid = false;
            if ( pOutError != nullptr )
            {
                if ( pOutError->empty() == false )
                    *pOutError += "\n";
                *pOutError += string( "socket '" ) + socket._name.c_str() + "' has unknown parent bone '" + socket._parent.c_str() + "'";
            }
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
} // namespace sw
