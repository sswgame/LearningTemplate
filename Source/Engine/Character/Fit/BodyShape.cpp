#include "pch.h"

#include "Engine/Character/Fit/BodyShape.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Character/CharacterDataReader.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct BodyShapeInternal
        {

            static void appendError( string* pOutError, const string& message )
            {
                if ( pOutError == nullptr )
                    return;
                if ( pOutError->empty() == false )
                    *pOutError += "\n";
                *pOutError += message;
            }

            static void addMorphWeight( vector<BodyMorphWeight>& inoutListMorphWeight, const hashed_string& morph, float32 weight )
            {
                if ( morph.empty() || weight <= 0.0f )
                    return;
                for ( BodyMorphWeight& existing : inoutListMorphWeight )
                {
                    if ( existing._morph == morph )
                    {
                        existing._weight += weight;
                        return;
                    }
                }
                inoutListMorphWeight.push_back( BodyMorphWeight{ morph, weight } );
            }

            static bool validateSide( const BodyShapeAxis& axis, const BodyShapeAxisSide& side, const AppearanceGeometry* pBody, const CharacterBoneArray* pBones,
                                      string* pOutError )
            {
                bool       bValid            = true;
                const bool bUnknownMorphName = pBody != nullptr && side._morph.empty() == false && pBody->findMorph( side._morph ) == nullptr;
                if ( bUnknownMorphName )
                {
                    bValid = false;
                    appendError( pOutError, string( "body shape axis '" ) + axis._name.c_str() + "' names unknown morph '" + side._morph.c_str() + "'" );
                }
                if ( pBones == nullptr )
                    return bValid;
                for ( const BoneProportionEntry& entry : side._listBone )
                {
                    if ( pBones->findBone( entry._bone ) >= 0 )
                        continue;
                    bValid = false;
                    appendError( pOutError, string( "body shape axis '" ) + axis._name.c_str() + "' names unknown bone '" + entry._bone.c_str() + "'" );
                }
                return bValid;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    BoneProportionEntry& BoneProportion::getOrCreateEntry( const hashed_string& bone )
    {
        for ( BoneProportionEntry& entry : _listEntry )
        {
            if ( entry._bone == bone )
                return entry;
        }
        _listEntry.push_back( BoneProportionEntry{ bone, float3( 1.0f ), float3::Zero } );
        return _listEntry.back();
    }

    void BoneProportion::setBone( const hashed_string& bone, const float3& scale, const float3& offset )
    {
        BoneProportionEntry& entry = getOrCreateEntry( bone );
        entry._scale               = scale;
        entry._offset              = offset;
    }

    void BoneProportion::accumulate( const BoneProportionEntry& entry, float32 weight )
    {
        BoneProportionEntry& target = getOrCreateEntry( entry._bone );
        const float3         scale  = float3::lerp( float3( 1.0f ), entry._scale, weight );
        target._scale               = float3::multiply( target._scale, scale );
        target._offset += entry._offset * weight;
    }

    const BoneProportionEntry* BoneProportion::findBone( const hashed_string& bone ) const
    {
        return VectorUtil::findBy<&BoneProportionEntry::_bone>( _listEntry, bone );
    }

    void BoneProportion::apply( CharacterBoneArray& inoutBones ) const
    {
        for ( const BoneProportionEntry& entry : _listEntry )
        {
            const int32 boneIndex = inoutBones.findBone( entry._bone );
            if ( boneIndex < 0 )
                continue;
            float4x4&  local = inoutBones._listLocal[static_cast<size_t>( boneIndex )];
            float3     scale;
            quaternion rotation;
            float3     translation;
            (void)local.decompose( scale, rotation, translation );
            scale = float3::multiply( scale, entry._scale );
            local = CharacterGeometryUtil::makeTransform( translation + entry._offset, rotation, scale );
        }
        inoutBones.computeModelTransforms();
    }

    bool BoneProportion::checkBones( const Skeleton& skeleton, string_view sourceLabel ) const
    {
        bool bValid = true;
        for ( const BoneProportionEntry& entry : _listEntry )
        {
            if ( skeleton.findBoneIndex( entry._bone ).has_value() )
                continue;
            SW_LOG_ERROR_ID( "BoneProportion.BoneNotInSkeleton", "Body shape '%#': bone '%#' is not in the skeleton - the proportion does nothing", sourceLabel, entry._bone.c_str() );
            bValid = false;
        }
        return bValid;
    }

    void BoneProportion::applyToPose( const Skeleton& skeleton, Pose& inoutPose ) const
    {
        for ( const BoneProportionEntry& entry : _listEntry )
        {
            // 스켈레톤에 없는 본은 `checkBones` 가 알렸다.
            const optional<uint32> boneIndex = skeleton.findBoneIndex( entry._bone );
            if ( boneIndex.has_value() == false || *boneIndex >= inoutPose.getBoneCount() )
                continue;
            BoneTransform local = inoutPose.getBoneTransform( *boneIndex );
            local._scale        = float3::multiply( local._scale, entry._scale );
            local._translation  = local._translation + entry._offset;
            inoutPose.setBoneTransform( *boneIndex, local );
        }
    }
} // namespace sw

namespace sw
{
    bool BodyShapeSet::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        _listAxis.clear();
        CharacterDataReader reader( sourceName );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.parseRoot( document, xmlText, "BodyShapeSet", root ) )
            readRoot( root, reader );
        return reader.finish();
    }

    bool BodyShapeSet::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        _listAxis.clear();
        CharacterDataReader reader( path );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.loadRoot( document, assetID, "BodyShapeSet", root ) )
            readRoot( root, reader );
        return reader.finish();
    }

    void BodyShapeSet::readRoot( const XMLNode& root, CharacterDataReader& reader )
    {
        reader.reportUnexpectedAttributes( root );
        for ( XMLNode axisNode = root.findChild(); axisNode; axisNode = axisNode.findNextSibling() )
        {
            if ( StringUtil::equals( axisNode.getName(), "Axis" ) == false )
            {
                reader.reportUnknownElement( axisNode );
                continue;
            }
            BodyShapeAxis axis;
            (void)reader.readFields( axisNode, axis ); // 칸은 BodyShapeAxis 의 PROPERTY — 오류는 읽개의 목록에 쌓인다
            const bool bInvalidAxisRange = axis._min > 0.0f || axis._max < 0.0f || axis._min >= axis._max;
            if ( bInvalidAxisRange )
                reader.addError( "Character.BodyAxisRangeInvalid", axisNode, "needs min <= 0 <= max with min < max" );
            for ( XMLNode sideNode = axisNode.findChild(); sideNode; sideNode = sideNode.findNextSibling() )
            {
                if ( StringUtil::equals( sideNode.getName(), "Positive" ) )
                    readSide( sideNode, axis._positive, reader );
                else if ( StringUtil::equals( sideNode.getName(), "Negative" ) )
                    readSide( sideNode, axis._negative, reader );
                else
                    reader.reportUnknownElement( sideNode );
            }
            if ( axis._name.empty() )
                continue;
            if ( findAxis( axis._name ) != nullptr )
            {
                reader.addError( "Character.BodyAxisRepeated", axisNode, string( "repeats axis '" ) + axis._name.c_str() + "'" );
                continue;
            }
            _listAxis.push_back( std::move( axis ) );
        }
    }

    void BodyShapeSet::readSide( const XMLNode& node, BodyShapeAxisSide& outSide, CharacterDataReader& reader )
    {
        (void)reader.readFields( node, outSide );
        for ( XMLNode boneNode = node.findChild(); boneNode; boneNode = boneNode.findNextSibling() )
        {
            if ( StringUtil::equals( boneNode.getName(), "Bone" ) == false )
            {
                reader.reportUnknownElement( boneNode );
                continue;
            }
            BoneProportionEntry entry;
            (void)reader.readFields( boneNode, entry );
            if ( entry._bone.empty() == false )
                outSide._listBone.push_back( entry );
        }
    }

    const BodyShapeAxis* BodyShapeSet::findAxis( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&BodyShapeAxis::_name>( _listAxis, name );
    }

    bool BodyShapeSet::validate( const AppearanceGeometry* pBody, const CharacterBoneArray* pBones, string* pOutError ) const
    {
        bool bValid = true;
        for ( const BodyShapeAxis& axis : _listAxis )
        {
            bValid = BodyShapeInternal::validateSide( axis, axis._positive, pBody, pBones, pOutError ) && bValid;
            bValid = BodyShapeInternal::validateSide( axis, axis._negative, pBody, pBones, pOutError ) && bValid;
        }
        return bValid;
    }

    bool BodyShapeSet::evaluate( vector_reference<const BodyShapeValue> listValue, vector<BodyMorphWeight>& outListMorphWeight, BoneProportion& outProportion,
                                 string* pOutError ) const
    {
        outListMorphWeight.clear();
        outProportion.clear();
        bool bValid = true;
        for ( const BodyShapeValue& value : listValue )
        {
            if ( findAxis( value._axis ) != nullptr )
                continue;
            bValid = false;
            BodyShapeInternal::appendError( pOutError, string( "unknown body shape axis '" ) + value._axis.c_str() + "'" );
        }
        for ( const BodyShapeAxis& axis : _listAxis )
        {
            float32 axisValue = axis._default;
            for ( const BodyShapeValue& value : listValue )
            {
                if ( value._axis == axis._name )
                    axisValue = value._value;
            }
            axisValue = MathUtil::clamp( axisValue, axis._min, axis._max );
            // 0 이 기본 체형이다. 양쪽은 max 에서 1, 음쪽은 min 에서 1 이 된다.
            const bool               bPositive = axisValue > 0.0f;
            const float32            span      = bPositive ? axis._max : -axis._min;
            const float32            weight    = span > 0.0f ? MathUtil::abs( axisValue ) / span : 0.0f;
            const BodyShapeAxisSide& side      = bPositive ? axis._positive : axis._negative;
            if ( weight <= 0.0f )
                continue;
            BodyShapeInternal::addMorphWeight( outListMorphWeight, side._morph, weight );
            for ( const BoneProportionEntry& entry : side._listBone )
            {
                outProportion.accumulate( entry, weight );
            }
        }
        return bValid;
    }

} // namespace sw
