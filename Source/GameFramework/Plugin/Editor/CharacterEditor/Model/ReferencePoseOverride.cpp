#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/ReferencePoseOverride.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Asset/AssetDataKind.h"
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
        CharacterIssueList boneIssues;
        if ( pBones != nullptr && validateBones( *pBones, &boneIssues ) == false )
            reader.addIssues( boneIssues );
        return reader.finish();
    }

    bool ReferencePoseOverride::loadFromAsset( const AssetID& assetID, const CharacterBoneArray* pBones )
    {
        const string_view path = assetID.view();
        _listOverride.clear();
        _listMirrorPair.clear();
        CharacterDataReader reader( path );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.loadRoot( document, assetID, "ReferencePose", root ) )
            readRoot( root, reader );
        CharacterIssueList boneIssues;
        if ( pBones != nullptr && validateBones( *pBones, &boneIssues ) == false )
            reader.addIssues( boneIssues );
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
                if ( StringUtil::equals( axisText, ReferencePoseOverrideInternal::kArrAxisName[axis] ) )
                {
                    _mirrorAxis = axis;
                    bKnownAxis  = true;
                }
            }
            if ( bKnownAxis == false )
                reader.addError( "Character.MirrorAxisUnknown", root, string( "has unknown mirrorAxis '" ) + string( axisText ) + "' (X, Y, Z)" );
        }
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Bone" ) )
            {
                reader.reportUnknownAttributes( child, ReferencePoseOverrideInternal::kArrBoneAttribute );
                BoneOverride boneOverride;
                boneOverride._bone = reader.readName( child, "name", true );
                if ( child.hasAttribute( "translation" ) )
                {
                    boneOverride._translation = reader.readFloat3( child, "translation" );
                    boneOverride._fieldMask |= BoneOverride::kTranslationBit;
                }
                if ( child.hasAttribute( "rotation" ) )
                {
                    boneOverride._rotation = reader.readRotation( child, "rotation" );
                    boneOverride._fieldMask |= BoneOverride::kRotationBit;
                }
                if ( child.hasAttribute( "scale" ) )
                {
                    boneOverride._scale = reader.readFloat3( child, "scale" );
                    boneOverride._fieldMask |= BoneOverride::kScaleBit;
                }
                if ( boneOverride._bone.empty() )
                    continue;
                if ( findOverride( boneOverride._bone ) != nullptr )
                {
                    reader.addError( "Character.BoneRepeated", child, string( "repeats bone '" ) + boneOverride._bone.c_str() + "'" );
                    continue;
                }
                _listOverride.push_back( boneOverride );
            }
            else if ( StringUtil::equals( child.getName(), "Mirror" ) )
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

    bool ReferencePoseOverride::removeOverride( const hashed_string& bone )
    {
        for ( size_t index = 0; index < _listOverride.size(); ++index )
        {
            if ( _listOverride[index]._bone == bone )
            {
                _listOverride.erase( _listOverride.begin() + static_cast<ptrdiff_t>( index ) );
                return true;
            }
        }
        return false;
    }

    const BoneOverride* ReferencePoseOverride::findOverride( const hashed_string& bone ) const
    {
        return VectorUtil::findBy<&BoneOverride::_bone>( _listOverride, bone );
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
        // 축에 수직인 면으로 반사한다. 위치는 그 축 성분의 부호를, 회전은 나머지 두 허수 성분의 부호를 뒤집는다.
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

    bool ReferencePoseOverride::validateBones( const CharacterBoneArray& bones, CharacterIssueList* pOutIssues ) const
    {
        bool bValid = true;
        for ( const BoneOverride& boneOverride : _listOverride )
        {
            if ( bones.findBone( boneOverride._bone ) >= 0 )
                continue;
            bValid = false;
            if ( pOutIssues != nullptr )
                pOutIssues->add( "Character.OverrideBoneUnknown", string( "reference pose overrides unknown bone '" ) + boneOverride._bone.c_str() + "'" );
        }
        for ( const BoneMirrorPair& pair : _listMirrorPair )
        {
            const bool bLeftKnown  = bones.findBone( pair._left ) >= 0;
            const bool bRightKnown = bones.findBone( pair._right ) >= 0;
            if ( bLeftKnown && bRightKnown )
                continue;
            bValid = false;
            if ( pOutIssues != nullptr )
                pOutIssues->add( "Character.MirrorPairBoneUnknown", string( "mirror pair names unknown bone '" ) + ( bLeftKnown ? pair._right.c_str() : pair._left.c_str() ) + "'" );
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

    /** @brief 기준 포즈 덮어쓰기 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_RESOURCE_DATA_KIND( ReferencePose, "referencepose", nullptr, nullptr, ".refpose.xml", &AssetDataKindUtil::loadCatalog<ReferencePoseOverride> );
} // namespace sw
