#include "pch.h"

#include "Engine/Character/BodyShape.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Character/CharacterDataReader.h"
#include "Engine/Character/CharacterGeometry.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        struct BodyShapeInternal
        {
            static constexpr const utf8* kArrAxisAttribute[] = { "name", "min", "max", "default" };
            static constexpr const utf8* kArrSideAttribute[] = { "morph" };
            static constexpr const utf8* kArrBoneAttribute[] = { "name", "scale", "offset" };

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
                bool bValid = true;
                if ( pBody != nullptr && side._morph.empty() == false && pBody->findMorph( side._morph ) == nullptr )
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
    BoneProportionEntry& BoneProportion::findOrAddEntry( const hashed_string& bone )
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
        BoneProportionEntry& entry = findOrAddEntry( bone );
        entry._scale               = scale;
        entry._offset              = offset;
    }

    void BoneProportion::accumulate( const BoneProportionEntry& entry, float32 weight )
    {
        BoneProportionEntry& target = findOrAddEntry( entry._bone );
        const float3         scale  = float3::lerp( float3( 1.0f ), entry._scale, weight );
        target._scale               = float3( target._scale._x * scale._x, target._scale._y * scale._y, target._scale._z * scale._z );
        target._offset += entry._offset * weight;
    }

    const BoneProportionEntry* BoneProportion::findBone( const hashed_string& bone ) const
    {
        for ( const BoneProportionEntry& entry : _listEntry )
        {
            if ( entry._bone == bone )
                return &entry;
        }
        return nullptr;
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
            scale = float3( scale._x * entry._scale._x, scale._y * entry._scale._y, scale._z * entry._scale._z );
            local = CharacterGeometryUtil::makeTransform( translation + entry._offset, rotation, scale );
        }
        inoutBones.computeModelTransforms();
    }
} // namespace sw

namespace sw
{
    bool BodyShapeSet::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        _listAxis.clear();
        CharacterDataReader reader( sourceName );
        XmlDocument         document;
        XmlNode             root;
        if ( reader.parseRoot( document, xmlText, "BodyShapeSet", root ) )
            readRoot( root, reader );
        return reader.finish();
    }

    bool BodyShapeSet::loadFromResource( string_view path )
    {
        _listAxis.clear();
        CharacterDataReader reader( path );
        XmlDocument         document;
        XmlNode             root;
        if ( reader.loadRoot( document, path, "BodyShapeSet", root ) )
            readRoot( root, reader );
        return reader.finish();
    }

    void BodyShapeSet::readRoot( const XmlNode& root, CharacterDataReader& reader )
    {
        reader.reportUnexpectedAttributes( root );
        for ( XmlNode axisNode = root.findChild(); axisNode; axisNode = axisNode.findNextSibling() )
        {
            if ( StringUtil::equals( axisNode.getName(), "Axis", true ) == false )
            {
                reader.reportUnknownElement( axisNode );
                continue;
            }
            reader.reportUnknownAttributes( axisNode, BodyShapeInternal::kArrAxisAttribute );
            BodyShapeAxis axis;
            axis._name    = reader.readName( axisNode, "name", true );
            axis._min     = reader.readFloat( axisNode, "min", -1.0f );
            axis._max     = reader.readFloat( axisNode, "max", 1.0f );
            axis._default = reader.readFloat( axisNode, "default", 0.0f );
            if ( axis._min > 0.0f || axis._max < 0.0f || axis._min >= axis._max )
                reader.addError( axisNode, "needs min <= 0 <= max with min < max" );
            for ( XmlNode sideNode = axisNode.findChild(); sideNode; sideNode = sideNode.findNextSibling() )
            {
                if ( StringUtil::equals( sideNode.getName(), "Positive", true ) )
                    readSide( sideNode, axis._positive, reader );
                else if ( StringUtil::equals( sideNode.getName(), "Negative", true ) )
                    readSide( sideNode, axis._negative, reader );
                else
                    reader.reportUnknownElement( sideNode );
            }
            if ( axis._name.empty() )
                continue;
            if ( findAxis( axis._name ) != nullptr )
            {
                reader.addError( axisNode, string( "repeats axis '" ) + axis._name.c_str() + "'" );
                continue;
            }
            _listAxis.push_back( std::move( axis ) );
        }
    }

    void BodyShapeSet::readSide( const XmlNode& node, BodyShapeAxisSide& outSide, CharacterDataReader& reader )
    {
        reader.reportUnknownAttributes( node, BodyShapeInternal::kArrSideAttribute );
        outSide._morph = reader.readName( node, "morph", false );
        for ( XmlNode boneNode = node.findChild(); boneNode; boneNode = boneNode.findNextSibling() )
        {
            if ( StringUtil::equals( boneNode.getName(), "Bone", true ) == false )
            {
                reader.reportUnknownElement( boneNode );
                continue;
            }
            reader.reportUnknownAttributes( boneNode, BodyShapeInternal::kArrBoneAttribute );
            BoneProportionEntry entry;
            entry._bone   = reader.readName( boneNode, "name", true );
            entry._scale  = reader.readFloat3( boneNode, "scale", float3( 1.0f ) );
            entry._offset = reader.readFloat3( boneNode, "offset", float3::Zero );
            if ( entry._bone.empty() == false )
                outSide._listBone.push_back( entry );
        }
    }

    void BodyShapeSet::addAxis( const BodyShapeAxis& axis )
    {
        for ( BodyShapeAxis& existing : _listAxis )
        {
            if ( existing._name == axis._name )
            {
                existing = axis;
                return;
            }
        }
        _listAxis.push_back( axis );
    }

    const BodyShapeAxis* BodyShapeSet::findAxis( const hashed_string& name ) const
    {
        for ( const BodyShapeAxis& axis : _listAxis )
        {
            if ( axis._name == name )
                return &axis;
        }
        return nullptr;
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
            // 0 이 기본 체형 — 양쪽은 max 에서 1, 음쪽은 min 에서 1.
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

namespace sw
{
    void BodyShapeUtil::applyMorphs( AppearanceGeometry& inoutGeometry, vector_reference<const BodyMorphWeight> listMorphWeight )
    {
        const uint32 vertexCount     = inoutGeometry.getVertexCount();
        bool         bNormalsChanged = false;
        for ( const BodyMorphWeight& morphWeight : listMorphWeight )
        {
            const GeometryMorphTarget* pMorph = inoutGeometry.findMorph( morphWeight._morph );
            if ( pMorph == nullptr || pMorph->_listPositionDelta.size() != vertexCount || morphWeight._weight == 0.0f )
                continue;
            for ( uint32 vertex = 0; vertex < vertexCount; ++vertex )
            {
                inoutGeometry._listPosition[vertex] += pMorph->_listPositionDelta[vertex] * morphWeight._weight;
            }
            if ( pMorph->_listNormalDelta.size() == vertexCount && inoutGeometry._listNormal.size() == vertexCount )
            {
                for ( uint32 vertex = 0; vertex < vertexCount; ++vertex )
                {
                    inoutGeometry._listNormal[vertex] += pMorph->_listNormalDelta[vertex] * morphWeight._weight;
                }
                bNormalsChanged = true;
            }
        }
        if ( bNormalsChanged == false )
            return;
        for ( float3& normal : inoutGeometry._listNormal )
        {
            normal = CharacterGeometryUtil::makeUnitOr( normal, float3::UnitY );
        }
    }

    void BodyShapeUtil::skinToPose( const AppearanceGeometry& bindGeometry, const CharacterBoneArray& bindBones, const CharacterBoneArray& poseBones,
                                    AppearanceGeometry& outGeometry )
    {
        outGeometry = bindGeometry;
        if ( bindGeometry.isSkinned() == false || bindBones.getBoneCount() != poseBones.getBoneCount() )
            return;
        vector<float4x4> listSkinMatrix( bindBones.getBoneCount() );
        for ( uint32 boneIndex = 0; boneIndex < bindBones.getBoneCount(); ++boneIndex )
        {
            listSkinMatrix[boneIndex] = bindBones._listModel[boneIndex].invert() * poseBones._listModel[boneIndex];
        }
        const bool bHasNormal = bindGeometry._listNormal.size() == bindGeometry._listPosition.size();
        for ( uint32 vertex = 0; vertex < bindGeometry.getVertexCount(); ++vertex )
        {
            const SkinInfluence& influence = bindGeometry._listSkin[vertex];
            float3               position  = float3::Zero;
            float3               normal    = float3::Zero;
            float32              total     = 0.0f;
            for ( uint32 slot = 0; slot < CharacterGeometryConstant::kMaxSkinInfluence; ++slot )
            {
                const float32 weight = influence._arrWeight[slot];
                const uint16  joint  = influence._arrJoint[slot];
                if ( weight <= 0.0f || joint >= listSkinMatrix.size() )
                    continue;
                position += float3::transform( bindGeometry._listPosition[vertex], listSkinMatrix[joint] ) * weight;
                if ( bHasNormal )
                    normal += float3::transformVector( bindGeometry._listNormal[vertex], listSkinMatrix[joint] ) * weight;
                total += weight;
            }
            if ( total <= 0.0f )
                continue;
            outGeometry._listPosition[vertex] = position * ( 1.0f / total );
            if ( bHasNormal )
                outGeometry._listNormal[vertex] = CharacterGeometryUtil::makeUnitOr( normal, bindGeometry._listNormal[vertex] );
        }
    }
} // namespace sw
