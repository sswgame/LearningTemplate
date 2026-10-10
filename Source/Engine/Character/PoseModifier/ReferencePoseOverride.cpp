#include "pch.h"

#include "Engine/Character/PoseModifier/ReferencePoseOverride.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Character/CharacterDataReader.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct ReferencePoseOverrideInternal
        {
            static constexpr const utf8* kArrRootAttribute[]   = { "mirrorAxis" };
            static constexpr const utf8* kArrBoneAttribute[]   = { "name", "translation", "rotation", "scale" };
            static constexpr const utf8* kArrMirrorAttribute[] = { "left", "right" };
            static constexpr const utf8* kArrAxisName[]        = { "X", "Y", "Z" };

            static void appendError( string* pOutError, const string& message )
            {
                if ( pOutError == nullptr )
                    return;
                if ( pOutError->empty() == false )
                    *pOutError += "\n";
                *pOutError += message;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ReferencePoseOverride::loadFromXMLText( string_view xmlText, string_view sourceName, const CharacterBoneArray* pBones )
    {
        _listOverride.clear();
        _listMirrorPair.clear();
        CharacterDataReader reader( sourceName );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.parseRoot( document, xmlText, "ReferencePose", root ) )
            readRoot( root, reader );
        string boneError;
        if ( pBones != nullptr && validateBones( *pBones, &boneError ) == false )
            reader.addError( boneError );
        return reader.finish();
    }

    bool ReferencePoseOverride::loadFromResource( string_view path, const CharacterBoneArray* pBones )
    {
        _listOverride.clear();
        _listMirrorPair.clear();
        CharacterDataReader reader( path );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.loadRoot( document, path, "ReferencePose", root ) )
            readRoot( root, reader );
        string boneError;
        if ( pBones != nullptr && validateBones( *pBones, &boneError ) == false )
            reader.addError( boneError );
        return reader.finish();
    }

    void ReferencePoseOverride::readRoot( const XMLNode& root, CharacterDataReader& reader )
    {
        reader.reportUnknownAttributes( root, ReferencePoseOverrideInternal::kArrRootAttribute );
        _mirrorAxis                = 0;
        const string_view axisText = StringUtil::trim( root.getAttributeText( "mirrorAxis" ) );
        if ( axisText.empty() == false )
        {
            bool bKnownAxis = false;
            for ( uint8 axis = 0; axis < 3; ++axis )
            {
                if ( StringUtil::equals( axisText, ReferencePoseOverrideInternal::kArrAxisName[axis], true ) )
                {
                    _mirrorAxis = axis;
                    bKnownAxis  = true;
                }
            }
            if ( bKnownAxis == false )
                reader.addError( root, string( "has unknown mirrorAxis '" ) + string( axisText ) + "' (X, Y, Z)" );
        }
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Bone", true ) )
            {
                reader.reportUnknownAttributes( child, ReferencePoseOverrideInternal::kArrBoneAttribute );
                BoneOverride boneOverride;
                boneOverride._bone = reader.readName( child, "name", true );
                if ( CharacterDataReader::hasAttribute( child, "translation" ) )
                {
                    boneOverride._translation = reader.readFloat3( child, "translation", float3::Zero );
                    boneOverride._fieldMask |= BoneOverride::kTranslationBit;
                }
                if ( CharacterDataReader::hasAttribute( child, "rotation" ) )
                {
                    boneOverride._rotation = reader.readRotation( child, "rotation", quaternion::Identity );
                    boneOverride._fieldMask |= BoneOverride::kRotationBit;
                }
                if ( CharacterDataReader::hasAttribute( child, "scale" ) )
                {
                    boneOverride._scale = reader.readFloat3( child, "scale", float3( 1.0f ) );
                    boneOverride._fieldMask |= BoneOverride::kScaleBit;
                }
                if ( boneOverride._bone.empty() )
                    continue;
                if ( findOverride( boneOverride._bone ) != nullptr )
                {
                    reader.addError( child, string( "repeats bone '" ) + boneOverride._bone.c_str() + "'" );
                    continue;
                }
                _listOverride.push_back( boneOverride );
            }
            else if ( StringUtil::equals( child.getName(), "Mirror", true ) )
            {
                reader.reportUnknownAttributes( child, ReferencePoseOverrideInternal::kArrMirrorAttribute );
                const hashed_string left  = reader.readName( child, "left", true );
                const hashed_string right = reader.readName( child, "right", true );
                if ( left.empty() == false && right.empty() == false )
                    addMirrorPair( left, right );
            }
            else
            {
                reader.reportUnknownElement( child );
            }
        }
    }

    string ReferencePoseOverride::saveToXMLText() const
    {
        XMLDocument document;
        XMLNode     root = document.appendRoot( "ReferencePose" );
        root.appendAttribute( "mirrorAxis", ReferencePoseOverrideInternal::kArrAxisName[_mirrorAxis] );
        for ( const BoneOverride& boneOverride : _listOverride )
        {
            XMLNode node = root.appendChild( "Bone" );
            node.appendAttribute( "name", boneOverride._bone.view() );
            if ( ( boneOverride._fieldMask & BoneOverride::kTranslationBit ) != 0 )
                node.appendAttribute( "translation", CharacterDataReader::formatFloat3( boneOverride._translation ).c_str() );
            if ( ( boneOverride._fieldMask & BoneOverride::kRotationBit ) != 0 )
                node.appendAttribute( "rotation", CharacterDataReader::formatRotation( boneOverride._rotation ).c_str() );
            if ( ( boneOverride._fieldMask & BoneOverride::kScaleBit ) != 0 )
                node.appendAttribute( "scale", CharacterDataReader::formatFloat3( boneOverride._scale ).c_str() );
        }
        for ( const BoneMirrorPair& pair : _listMirrorPair )
        {
            XMLNode node = root.appendChild( "Mirror" );
            node.appendAttribute( "left", pair._left.view() );
            node.appendAttribute( "right", pair._right.view() );
        }
        return document.saveToString();
    }

    void ReferencePoseOverride::setOverride( const BoneOverride& boneOverride )
    {
        for ( BoneOverride& existing : _listOverride )
        {
            if ( existing._bone != boneOverride._bone )
                continue;
            if ( ( boneOverride._fieldMask & BoneOverride::kTranslationBit ) != 0 )
                existing._translation = boneOverride._translation;
            if ( ( boneOverride._fieldMask & BoneOverride::kRotationBit ) != 0 )
                existing._rotation = boneOverride._rotation;
            if ( ( boneOverride._fieldMask & BoneOverride::kScaleBit ) != 0 )
                existing._scale = boneOverride._scale;
            existing._fieldMask = static_cast<uint8>( existing._fieldMask | boneOverride._fieldMask );
            return;
        }
        _listOverride.push_back( boneOverride );
    }

    const BoneOverride* ReferencePoseOverride::findOverride( const hashed_string& bone ) const
    {
        for ( const BoneOverride& boneOverride : _listOverride )
        {
            if ( boneOverride._bone == bone )
                return &boneOverride;
        }
        return nullptr;
    }

    void ReferencePoseOverride::addMirrorPair( const hashed_string& left, const hashed_string& right )
    {
        if ( findMirrorBone( left ).empty() == false )
            return;
        _listMirrorPair.push_back( BoneMirrorPair{ left, right } );
    }

    hashed_string ReferencePoseOverride::findMirrorBone( const hashed_string& bone ) const
    {
        for ( const BoneMirrorPair& pair : _listMirrorPair )
        {
            if ( pair._left == bone )
                return pair._right;
            if ( pair._right == bone )
                return pair._left;
        }
        return hashed_string{};
    }

    bool ReferencePoseOverride::mirrorOverride( const hashed_string& sourceBone )
    {
        const hashed_string targetBone = findMirrorBone( sourceBone );
        const BoneOverride* pSource    = findOverride( sourceBone );
        if ( targetBone.empty() || pSource == nullptr )
            return false;
        // 축에 수직인 면으로 비춘다 — 위치는 그 축 성분의 부호, 회전은 나머지 두 허수 성분의 부호를 뒤집는다.
        BoneOverride mirrored     = *pSource;
        mirrored._bone            = targetBone;
        float32* pTranslation     = mirrored._translation.data();
        pTranslation[_mirrorAxis] = -pTranslation[_mirrorAxis];
        float32 arrImaginary[3]   = { mirrored._rotation._x, mirrored._rotation._y, mirrored._rotation._z };
        for ( uint8 axis = 0; axis < 3; ++axis )
        {
            if ( axis != _mirrorAxis )
                arrImaginary[axis] = -arrImaginary[axis];
        }
        mirrored._rotation = quaternion( arrImaginary[0], arrImaginary[1], arrImaginary[2], mirrored._rotation._w );
        setOverride( mirrored );
        return true;
    }

    bool ReferencePoseOverride::validateBones( const CharacterBoneArray& bones, string* pOutError ) const
    {
        bool bValid = true;
        for ( const BoneOverride& boneOverride : _listOverride )
        {
            if ( bones.findBone( boneOverride._bone ) >= 0 )
                continue;
            bValid = false;
            ReferencePoseOverrideInternal::appendError( pOutError, string( "reference pose overrides unknown bone '" ) + boneOverride._bone.c_str() + "'" );
        }
        for ( const BoneMirrorPair& pair : _listMirrorPair )
        {
            const bool bLeftKnown  = bones.findBone( pair._left ) >= 0;
            const bool bRightKnown = bones.findBone( pair._right ) >= 0;
            if ( bLeftKnown && bRightKnown )
                continue;
            bValid = false;
            ReferencePoseOverrideInternal::appendError( pOutError, string( "mirror pair names unknown bone '" ) + ( bLeftKnown ? pair._right.c_str() : pair._left.c_str() ) + "'" );
        }
        return bValid;
    }

    void ReferencePoseOverride::apply( CharacterBoneArray& inoutBones ) const
    {
        for ( const BoneOverride& boneOverride : _listOverride )
        {
            const int32 boneIndex = inoutBones.findBone( boneOverride._bone );
            if ( boneIndex < 0 )
                continue;
            float4x4&  local = inoutBones._listLocal[static_cast<size_t>( boneIndex )];
            float3     scale;
            quaternion rotation;
            float3     translation;
            (void)local.decompose( scale, rotation, translation );
            if ( ( boneOverride._fieldMask & BoneOverride::kTranslationBit ) != 0 )
                translation = boneOverride._translation;
            if ( ( boneOverride._fieldMask & BoneOverride::kRotationBit ) != 0 )
                rotation = boneOverride._rotation;
            if ( ( boneOverride._fieldMask & BoneOverride::kScaleBit ) != 0 )
                scale = boneOverride._scale;
            local = CharacterGeometryUtil::makeTransform( translation, rotation, scale );
        }
        inoutBones.computeModelTransforms();
    }
} // namespace sw
