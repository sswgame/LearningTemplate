#include "pch.h"

#include "Engine/Character/Fit/FitTables.h"

#include "Core/Container/StringUtil.h"

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
            static constexpr const utf8* kArrLayerAttribute[]       = { "name", "order" };
            static constexpr const utf8* kArrRegionAttribute[]      = { "name", "bones", "group", "minWeight" };
            static constexpr const utf8* kArrProfileAttribute[]     = { "name", "rigidity", "shrinkStrength", "pushDistance", "falloffWidth", "coverageDistance" };
            static constexpr const utf8* kArrInteractionAttribute[] = { "inner", "outer", "operator", "args" };

            /** @brief `args="a=1 b=2"` 를 읽는다. 형식이 틀리면 오류. */
            static void readArguments( const XMLNode& node, CharacterDataReader& reader, vector<FitArgument>& outListArgument )
            {
                outListArgument.clear();
                vector<string_view> listToken;
                CharacterDataReader::splitTokens( node.getAttributeText( "args" ), listToken );
                for ( const string_view token : listToken )
                {
                    const size_t equalPosition = token.find( '=' );
                    FitArgument  argument;
                    const bool   bHasEqual = equalPosition != string_view::npos && equalPosition > 0;
                    if ( bHasEqual == false || StringUtil::parseFloat( token.substr( equalPosition + 1 ), argument._value ) == false )
                    {
                        reader.addError( node, string( "has malformed argument '" ) + string( token ) + "' (name=number)" );
                        continue;
                    }
                    argument._name = hashed_string( token.substr( 0, equalPosition ) );
                    outListArgument.push_back( argument );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float32 FitInteractionDef::findArgument( const hashed_string& name, float32 fallback ) const
    {
        for ( const FitArgument& argument : _listArgument )
        {
            if ( argument._name == name )
                return argument._value;
        }
        return fallback;
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

    bool FitTables::loadFromResource( string_view path, const FitOperatorRegistry& operators )
    {
        _listLayer.clear();
        _listRegion.clear();
        _listProfile.clear();
        _listInteraction.clear();
        CharacterDataReader reader( path );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.loadRoot( document, path, "FitTables", root ) )
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
            if ( StringUtil::equals( pName, "Layer", true ) )
            {
                reader.reportUnknownAttributes( child, FitTablesInternal::kArrLayerAttribute );
                FitLayerDef layer;
                layer._name  = reader.readName( child, "name", true );
                layer._order = reader.readInt( child, "order", 0 );
                if ( layer._name.empty() )
                    continue;
                if ( findLayer( layer._name ) != nullptr )
                    reader.addError( child, string( "repeats layer '" ) + layer._name.c_str() + "'" );
                else
                    _listLayer.push_back( layer );
            }
            else if ( StringUtil::equals( pName, "Region", true ) )
            {
                reader.reportUnknownAttributes( child, FitTablesInternal::kArrRegionAttribute );
                BodyRegionDef region;
                region._name      = reader.readName( child, "name", true );
                region._group     = reader.readName( child, "group", false );
                region._minWeight = reader.readFloat( child, "minWeight", 0.5f );
                reader.readNameList( child, "bones", region._listBone );
                const bool bHasBones = region._listBone.empty() == false;
                const bool bHasGroup = region._group.empty() == false;
                if ( bHasBones == bHasGroup )
                    reader.addError( child, "needs exactly one of 'bones' (bone weights) or 'group' (painted vertex group)" );
                if ( region._name.empty() )
                    continue;
                if ( findRegionIndex( region._name ) >= 0 )
                    reader.addError( child, string( "repeats region '" ) + region._name.c_str() + "'" );
                else
                    _listRegion.push_back( std::move( region ) );
            }
            else if ( StringUtil::equals( pName, "Profile", true ) )
            {
                reader.reportUnknownAttributes( child, FitTablesInternal::kArrProfileAttribute );
                FitProfileDef profile;
                profile._name             = reader.readName( child, "name", true );
                profile._rigidity         = reader.readFloat( child, "rigidity", profile._rigidity );
                profile._shrinkStrength   = reader.readFloat( child, "shrinkStrength", profile._shrinkStrength );
                profile._pushDistance     = reader.readFloat( child, "pushDistance", profile._pushDistance );
                profile._falloffWidth     = reader.readFloat( child, "falloffWidth", profile._falloffWidth );
                profile._coverageDistance = reader.readFloat( child, "coverageDistance", profile._coverageDistance );
                if ( profile._rigidity < 0.0f || profile._rigidity > 1.0f )
                    reader.addError( child, "needs rigidity in [0, 1]" );
                if ( profile._name.empty() )
                    continue;
                if ( findProfile( profile._name ) != nullptr )
                    reader.addError( child, string( "repeats profile '" ) + profile._name.c_str() + "'" );
                else
                    _listProfile.push_back( profile );
            }
            else if ( StringUtil::equals( pName, "Interaction", true ) )
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
            reader.reportUnknownAttributes( node, FitTablesInternal::kArrInteractionAttribute );
            FitInteractionDef interaction;
            interaction._inner    = reader.readName( node, "inner", true );
            interaction._outer    = reader.readName( node, "outer", true );
            interaction._operator = reader.readName( node, "operator", true );
            FitTablesInternal::readArguments( node, reader, interaction._listArgument );
            if ( interaction._inner.empty() == false && findProfile( interaction._inner ) == nullptr )
                reader.addError( node, string( "names unknown inner profile '" ) + interaction._inner.c_str() + "'" );
            if ( interaction._outer.empty() == false && findProfile( interaction._outer ) == nullptr )
                reader.addError( node, string( "names unknown outer profile '" ) + interaction._outer.c_str() + "'" );
            const IFitOperator* pOperator = interaction._operator.empty() ? nullptr : operators.findOperator( interaction._operator );
            if ( interaction._operator.empty() == false && pOperator == nullptr )
                reader.addError( node, string( "names unknown operator '" ) + interaction._operator.c_str() + "'" );
            if ( pOperator != nullptr )
            {
                for ( const FitArgument& argument : interaction._listArgument )
                {
                    if ( pOperator->acceptsArgument( argument._name ) == false )
                        reader.addError( node, string( "operator '" ) + interaction._operator.c_str() + "' has no argument '" + argument._name.c_str() + "'" );
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

    void FitTables::addProfile( const FitProfileDef& profile )
    {
        for ( FitProfileDef& existing : _listProfile )
        {
            if ( existing._name == profile._name )
            {
                existing = profile;
                return;
            }
        }
        _listProfile.push_back( profile );
    }

    void FitTables::addInteraction( const FitInteractionDef& interaction )
    {
        _listInteraction.push_back( interaction );
    }

    const FitLayerDef* FitTables::findLayer( const hashed_string& name ) const
    {
        for ( const FitLayerDef& layer : _listLayer )
        {
            if ( layer._name == name )
                return &layer;
        }
        return nullptr;
    }

    int32 FitTables::findRegionIndex( const hashed_string& name ) const
    {
        for ( size_t regionIndex = 0; regionIndex < _listRegion.size(); ++regionIndex )
        {
            if ( _listRegion[regionIndex]._name == name )
                return static_cast<int32>( regionIndex );
        }
        return -1;
    }

    const FitProfileDef* FitTables::findProfile( const hashed_string& name ) const
    {
        for ( const FitProfileDef& profile : _listProfile )
        {
            if ( profile._name == name )
                return &profile;
        }
        return nullptr;
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
