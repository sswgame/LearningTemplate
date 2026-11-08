#include "pch.h"

#include "Engine/Character/Fit/FitTables.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Character/CharacterDataReader.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/FitOperator.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct FitTablesInternal
        {

            /** @brief `args="a=1 b=2"` 를 읽는다. 형식이 틀리면 오류. */
            static void readArguments( const XMLNode& node, CharacterDataReader& reader, vector<FitArgument>& outListArgument )
            {
                outListArgument.clear();
                for ( const string_view token : StringUtil::split( node.getAttributeText( "args" ), CharacterDataReader::kTokenSeparators, { ._bDropEmpty = true } ) )
                {
                    string_view name;
                    string_view valueText;
                    FitArgument argument;
                    if ( StringUtil::trySplitKeyValue( token, name, valueText ) == false || StringUtil::parseFloat( valueText, argument._value ) == false )
                    {
                        reader.addError( "Character.FitArgumentMalformed", node, string( "has malformed argument '" ) + string( token ) + "' (name=number)" );
                        continue;
                    }
                    argument._name = hashed_string( name );
                    outListArgument.push_back( argument );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    optional<float32> FitInteractionDef::findArgument( const hashed_string& name ) const
    {
        for ( const FitArgument& argument : _listArgument )
        {
            if ( argument._name == name )
                return argument._value;
        }
        return nullopt;
    }
} // namespace sw

namespace sw
{
    bool FitTables::loadFromXMLText( string_view xmlText, string_view sourceName, const FitOperatorRegistry& operators )
    {
        _listLayer.clear();
        _listRegion.clear();
        _listProfile.clear();
        _listInteraction.clear();
        CharacterDataReader reader( sourceName );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.parseRoot( document, xmlText, "FitTables", root ) )
            readRoot( root, operators, reader );
        return reader.finish();
    }

    bool FitTables::loadFromAsset( const AssetID& assetID, const FitOperatorRegistry& operators )
    {
        const string_view path = assetID.view();
        _listLayer.clear();
        _listRegion.clear();
        _listProfile.clear();
        _listInteraction.clear();
        CharacterDataReader reader( path );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.loadRoot( document, assetID, "FitTables", root ) )
            readRoot( root, operators, reader );
        return reader.finish();
    }

    void FitTables::readRoot( const XMLNode& root, const FitOperatorRegistry& operators, CharacterDataReader& reader )
    {
        reader.reportUnexpectedAttributes( root );
        vector<XMLNode> listInteractionNode;
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            const utf8* pName = child.getName();
            if ( StringUtil::equals( pName, "Layer" ) )
            {
                FitLayerDef layer;
                (void)reader.readFields( child, layer ); // 칸은 FitLayerDef 의 PROPERTY — 오류는 읽개의 목록에 쌓인다
                if ( layer._name.empty() )
                    continue;
                if ( findLayer( layer._name ) != nullptr )
                    reader.addError( "Character.FitLayerRepeated", child, string( "repeats layer '" ) + layer._name.c_str() + "'" );
                else
                    _listLayer.push_back( layer );
            }
            else if ( StringUtil::equals( pName, "Region" ) )
            {
                BodyRegionDef region;
                (void)reader.readFields( child, region, { "bones" } );
                reader.readNameList( child, "bones", region._listBone ); // 이름 목록 글이라 손으로 푼다
                const bool bHasBones = region._listBone.empty() == false;
                const bool bHasGroup = region._group.empty() == false;
                if ( bHasBones == bHasGroup )
                    reader.addError( "Character.FitRegionSourceInvalid", child, "needs exactly one of 'bones' (bone weights) or 'group' (painted vertex group)" );
                if ( region._name.empty() )
                    continue;
                if ( findRegionIndex( region._name ).has_value() )
                    reader.addError( "Character.FitRegionRepeated", child, string( "repeats region '" ) + region._name.c_str() + "'" );
                else
                    _listRegion.push_back( std::move( region ) );
            }
            else if ( StringUtil::equals( pName, "Profile" ) )
            {
                FitProfileDef profile;
                (void)reader.readFields( child, profile ); // 단단함 0..1 등 범위는 PROPERTY 가 본다
                if ( profile._name.empty() )
                    continue;
                if ( findProfile( profile._name ) != nullptr )
                    reader.addError( "Character.FitProfileRepeated", child, string( "repeats profile '" ) + profile._name.c_str() + "'" );
                else
                    _listProfile.push_back( profile );
            }
            else if ( StringUtil::equals( pName, "Interaction" ) )
            {
                // 프로필이 뒤에 적혀도 되게, 줄은 다 모은 뒤에 대조한다.
                listInteractionNode.push_back( child );
            }
            else
            {
                reader.reportUnknownElement( child );
            }
        }
        for ( const XMLNode& node : listInteractionNode )
        {
            FitInteractionDef interaction;
            (void)reader.readFields( node, interaction, { "args" } ); // 인자(`args`)는 `이름=수` 목록 글이라 손으로 푼다
            FitTablesInternal::readArguments( node, reader, interaction._listArgument );
            if ( interaction._inner.empty() == false && findProfile( interaction._inner ) == nullptr )
                reader.addError( "Character.FitProfileUnknown", node, string( "names unknown inner profile '" ) + interaction._inner.c_str() + "'" );
            if ( interaction._outer.empty() == false && findProfile( interaction._outer ) == nullptr )
                reader.addError( "Character.FitProfileUnknown", node, string( "names unknown outer profile '" ) + interaction._outer.c_str() + "'" );
            const IFitOperator* pOperator = interaction._operator.empty() ? nullptr : operators.findOperator( interaction._operator );
            if ( interaction._operator.empty() == false && pOperator == nullptr )
                reader.addError( "Character.FitOperatorUnknown", node, string( "names unknown operator '" ) + interaction._operator.c_str() + "'" );
            if ( pOperator != nullptr )
            {
                for ( const FitArgument& argument : interaction._listArgument )
                {
                    if ( pOperator->acceptsArgument( argument._name ) == false )
                        reader.addError( "Character.FitArgumentUnknown", node, string( "operator '" ) + interaction._operator.c_str() + "' has no argument '" + argument._name.c_str() + "'" );
                }
            }
            _listInteraction.push_back( std::move( interaction ) );
        }
    }

    void FitTables::addLayer( const FitLayerDef& layer )
    {
        for ( FitLayerDef& existing : _listLayer )
        {
            if ( existing._name == layer._name )
            {
                existing = layer;
                return;
            }
        }
        _listLayer.push_back( layer );
    }

    void FitTables::addRegion( const BodyRegionDef& region )
    {
        for ( BodyRegionDef& existing : _listRegion )
        {
            if ( existing._name == region._name )
            {
                existing = region;
                return;
            }
        }
        _listRegion.push_back( region );
    }

    void FitTables::addInteraction( const FitInteractionDef& interaction )
    {
        _listInteraction.push_back( interaction );
    }

    const FitLayerDef* FitTables::findLayer( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&FitLayerDef::_name>( _listLayer, name );
    }

    optional<uint32> FitTables::findRegionIndex( const hashed_string& name ) const
    {
        for ( size_t regionIndex = 0; regionIndex < _listRegion.size(); ++regionIndex )
        {
            if ( _listRegion[regionIndex]._name == name )
                return static_cast<uint32>( regionIndex );
        }
        return nullopt;
    }

    const FitProfileDef* FitTables::findProfile( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&FitProfileDef::_name>( _listProfile, name );
    }

    void FitTables::collectInteractions( const hashed_string& innerProfile, const hashed_string& outerProfile, vector<const FitInteractionDef*>& outListInteraction ) const
    {
        outListInteraction.clear();
        for ( const FitInteractionDef& interaction : _listInteraction )
        {
            if ( interaction._inner == innerProfile && interaction._outer == outerProfile )
                outListInteraction.push_back( &interaction );
        }
    }

    void FitTables::assignRegions( const AppearanceGeometry& geometry, const CharacterBoneArray& bones, vector<uint16>& outListVertexRegion ) const
    {
        const uint32 vertexCount = geometry.getVertexCount();
        outListVertexRegion.assign( vertexCount, CharacterGeometryConstant::kNoGroup );
        vector<int32> listBoneIndex;
        for ( size_t regionIndex = 0; regionIndex < _listRegion.size(); ++regionIndex )
        {
            const BodyRegionDef& region = _listRegion[regionIndex];
            if ( region._group.empty() == false )
            {
                const uint16 group = geometry.findGroup( region._group );
                if ( group == CharacterGeometryConstant::kNoGroup || geometry._listVertexGroup.size() != vertexCount )
                    continue;
                for ( uint32 vertex = 0; vertex < vertexCount; ++vertex )
                {
                    if ( outListVertexRegion[vertex] == CharacterGeometryConstant::kNoGroup && geometry._listVertexGroup[vertex] == group )
                        outListVertexRegion[vertex] = static_cast<uint16>( regionIndex );
                }
                continue;
            }
            if ( geometry.isSkinned() == false )
                continue;
            listBoneIndex.clear();
            for ( const hashed_string& boneName : region._listBone )
            {
                const int32 boneIndex = bones.findBone( boneName );
                if ( boneIndex >= 0 )
                    listBoneIndex.push_back( boneIndex );
            }
            for ( uint32 vertex = 0; vertex < vertexCount; ++vertex )
            {
                if ( outListVertexRegion[vertex] != CharacterGeometryConstant::kNoGroup )
                    continue;
                const SkinInfluence& influence = geometry._listSkin[vertex];
                float32              weight    = 0.0f;
                for ( uint32 slot = 0; slot < CharacterGeometryConstant::kMaxSkinInfluence; ++slot )
                {
                    for ( const int32 boneIndex : listBoneIndex )
                    {
                        if ( static_cast<int32>( influence._arrJoint[slot] ) == boneIndex )
                            weight += influence._arrWeight[slot];
                    }
                }
                if ( weight >= region._minWeight && weight > 0.0f )
                    outListVertexRegion[vertex] = static_cast<uint16>( regionIndex );
            }
        }
    }
} // namespace sw
