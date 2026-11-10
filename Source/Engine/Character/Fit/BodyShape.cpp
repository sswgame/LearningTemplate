#include "pch.h"

#include "Engine/Character/Fit/BodyShape.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

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
            static constexpr const utf8* kArrUnsupportedAttribute[] = { "name" };

            /** @brief 축 값 @p axisValue 의 한쪽 끝 가중치(0..1)입니다. 0 이 기본 체형이고 양쪽은 max, 음쪽은 min 에서 1 이며 응답 곡선(`_response` 지수)을 지난다. */
            static float32 computeSideWeight( const BodyShapeAxis& axis, float32 axisValue )
            {
                const bool    bPositive = axisValue > 0.0f;
                const float32 span      = bPositive ? axis._max : -axis._min;
                const float32 linear    = span > 0.0f ? MathUtil::abs( axisValue ) / span : 0.0f;
                return linear > 0.0f ? MathUtil::pow( linear, axis._response ) : 0.0f;
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
                                      CharacterIssueList& outIssues )
            {
                bool       bValid            = true;
                const bool bUnknownMorphName = pBody != nullptr && side._morph.empty() == false && pBody->findMorph( side._morph ) == nullptr;
                if ( bUnknownMorphName )
                {
                    bValid = false;
                    outIssues.add( "BodyShape.AxisMorphUnknown", string( "body shape axis '" ) + axis._name.c_str() + "' names unknown morph '" + side._morph.c_str() + "'" );
                }
                if ( pBones == nullptr )
                    return bValid;
                for ( const BoneProportionEntry& entry : side._listBone )
                {
                    if ( pBones->findBone( entry._bone ).has_value() )
                        continue;
                    bValid = false;
                    outIssues.add( "BodyShape.AxisBoneUnknown", string( "body shape axis '" ) + axis._name.c_str() + "' names unknown bone '" + entry._bone.c_str() + "'" );
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
        _listEntry.push_back( BoneProportionEntry{ bone, float3( 1.0f ), float3::Zero, float3::Zero } );
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
        target._rotation += entry._rotation * weight; // 작은 각(굽 · 조임)을 겹치는 층이라 오일러 각을 더한다 — 큰 회전을 겹칠 일이 생기면 쿼터니언 곱으로 바꾼다
    }

    quaternion BoneProportion::makeRotation( const BoneProportionEntry& entry )
    {
        return quaternion::makeFromYawPitchRoll( entry._rotation );
    }

    const BoneProportionEntry* BoneProportion::findBone( const hashed_string& bone ) const
    {
        return VectorUtil::findBy<&BoneProportionEntry::_bone>( _listEntry, bone );
    }

    void BoneProportion::apply( CharacterBoneArray& inoutBones ) const
    {
        for ( const BoneProportionEntry& entry : _listEntry )
        {
            const optional<uint32> boneIndex = inoutBones.findBone( entry._bone );
            if ( boneIndex.has_value() == false )
                continue;
            float4x4&  local = inoutBones._listLocal[*boneIndex];
            float3     scale;
            quaternion rotation;
            float3     translation;
            (void)local.decompose( scale, rotation, translation );
            scale = float3::multiply( scale, entry._scale );
            local = CharacterGeometryUtil::makeTransform( translation + entry._offset, rotation * makeRotation( entry ), scale );
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
            local._rotation     = local._rotation * makeRotation( entry ); // 본 자기 축에서 돌린다(로컬 뒤에 적용)
            inoutPose.setBoneTransform( *boneIndex, local );
        }
    }
} // namespace sw

namespace sw
{
    bool BodyShapeSet::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        _listAxis.clear();
        _listUnsupported.clear();
        _params = BodyShapeSetParams{};
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
        _listUnsupported.clear();
        _params = BodyShapeSetParams{};
        CharacterDataReader reader( path );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.loadRoot( document, assetID, "BodyShapeSet", root ) )
            readRoot( root, reader );
        return reader.finish();
    }

    void BodyShapeSet::readRoot( const XMLNode& root, CharacterDataReader& reader )
    {
        (void)reader.readFields( root, _params ); // 모르는 속성도 이 읽기가 오류로 남긴다
        for ( XMLNode axisNode = root.findChild(); axisNode; axisNode = axisNode.findNextSibling() )
        {
            if ( StringUtil::equals( axisNode.getName(), "Unsupported" ) )
            {
                hashed_string name;
                reader.reportUnknownAttributes( axisNode, BodyShapeInternal::kArrUnsupportedAttribute );
                if ( const utf8* pName = axisNode.findAttribute( "name" ); pName != nullptr && pName[0] != '\0' )
                    name = hashed_string( pName );
                if ( name.empty() || VectorUtil::contains( _listUnsupported, name ) )
                    reader.addError( "Character.BodyUnsupportedInvalid", axisNode, "needs a unique non-empty 'name'" );
                else
                    _listUnsupported.push_back( name );
                continue;
            }
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
        for ( const hashed_string& unsupported : _listUnsupported )
        {
            if ( findAxis( unsupported ) != nullptr )
                reader.addError( "Character.BodyUnsupportedInvalid", root, string( "declares mapped axis '" ) + unsupported.c_str() + "' as unsupported" );
        }
        if ( _params._footLiftAxis.empty() == false && findAxis( _params._footLiftAxis ) == nullptr )
            reader.addError( "Character.BodyFootLiftAxisUnknown", root, string( "footLiftAxis names unknown axis '" ) + _params._footLiftAxis.c_str() + "'" );
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

    bool BodyShapeSet::validate( const AppearanceGeometry* pBody, const CharacterBoneArray* pBones, CharacterIssueList& outIssues ) const
    {
        bool bValid = true;
        for ( const BodyShapeAxis& axis : _listAxis )
        {
            bValid = BodyShapeInternal::validateSide( axis, axis._positive, pBody, pBones, outIssues ) && bValid;
            bValid = BodyShapeInternal::validateSide( axis, axis._negative, pBody, pBones, outIssues ) && bValid;
        }
        return bValid;
    }

    bool BodyShapeSet::evaluate( vector_reference<const BodyShapeValue> listValue, vector<BodyMorphWeight>& outListMorphWeight, BoneProportion& outProportion,
                                 CharacterIssueList& outIssues ) const
    {
        outListMorphWeight.clear();
        outProportion.clear();
        bool bValid = true;
        for ( const BodyShapeValue& value : listValue )
        {
            if ( findAxis( value._axis ) != nullptr || isUnsupported( value._axis ) )
                continue;
            bValid = false;
            outIssues.add( "BodyShape.AxisUnknown", string( "unknown body shape axis '" ) + value._axis.c_str() + "'" );
        }
        for ( const BodyShapeAxis& axis : _listAxis )
        {
            float32 axisValue = axis._default;
            for ( const BodyShapeValue& value : listValue )
            {
                if ( value._axis == axis._name )
                    axisValue = value._value;
            }
            axisValue                       = MathUtil::clamp( axisValue, axis._min, axis._max );
            const float32            weight = BodyShapeInternal::computeSideWeight( axis, axisValue );
            const BodyShapeAxisSide& side   = axisValue > 0.0f ? axis._positive : axis._negative;
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

    bool BodyShapeSet::isUnsupported( const hashed_string& axis ) const
    {
        return VectorUtil::contains( _listUnsupported, axis );
    }

    float32 BodyShapeSet::computeFootLift( vector_reference<const BodyShapeValue> listValue ) const
    {
        const BodyShapeAxis* pAxis = _params._footLiftAxis.empty() ? nullptr : findAxis( _params._footLiftAxis );
        if ( pAxis == nullptr )
            return 0.0f;
        float32 axisValue = pAxis->_default;
        for ( const BodyShapeValue& value : listValue )
        {
            if ( value._axis == pAxis->_name )
                axisValue = value._value;
        }
        return MathUtil::clamp( axisValue, pAxis->_min, pAxis->_max );
    }
} // namespace sw
