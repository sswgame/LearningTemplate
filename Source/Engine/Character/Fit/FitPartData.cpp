#include "pch.h"

#include "Engine/Character/Fit/FitPartData.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Character/CharacterDataReader.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/FitTables.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct FitPartDataInternal
        {
            static constexpr const utf8* kArrRootAttribute[]       = { "layer", "profile" };
            static constexpr const utf8* kArrHideAttribute[]       = { "region" };
            static constexpr const utf8* kArrRingAttribute[]       = { "bone", "offset", "axis", "radius", "width" };
            static constexpr const utf8* kArrCorrectiveAttribute[] = { "morph" };
            static constexpr const utf8* kArrDeltaAttribute[]      = { "vertex", "offset" };

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
    bool FitPartData::loadFromXMLText( string_view xmlText, string_view sourceName, const FitTables& tables )
    {
        CharacterDataReader reader( sourceName );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.parseRoot( document, xmlText, "PartFit", root ) )
            readRoot( root, tables, reader );
        return reader.finish();
    }

    bool FitPartData::loadFromResource( string_view path, const FitTables& tables )
    {
        CharacterDataReader reader( path );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.loadRoot( document, path, "PartFit", root ) )
            readRoot( root, tables, reader );
        return reader.finish();
    }

    void FitPartData::readRoot( const XMLNode& root, const FitTables& tables, CharacterDataReader& reader )
    {
        _listHiddenRegion.clear();
        _listRing.clear();
        _listCorrective.clear();
        reader.reportUnknownAttributes( root, FitPartDataInternal::kArrRootAttribute );
        _layer   = reader.readName( root, "layer", true );
        _profile = reader.readName( root, "profile", true );
        if ( _layer.empty() == false && tables.findLayer( _layer ) == nullptr )
            reader.addError( root, string( "names unknown layer '" ) + _layer.c_str() + "'" );
        if ( _profile.empty() == false && tables.findProfile( _profile ) == nullptr )
            reader.addError( root, string( "names unknown profile '" ) + _profile.c_str() + "'" );
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            const utf8* pName = child.getName();
            if ( StringUtil::equals( pName, "Hide", true ) )
            {
                reader.reportUnknownAttributes( child, FitPartDataInternal::kArrHideAttribute );
                const hashed_string region = reader.readName( child, "region", true );
                if ( region.empty() )
                    continue;
                if ( tables.findRegionIndex( region ) < 0 )
                    reader.addError( child, string( "names unknown region '" ) + region.c_str() + "'" );
                else
                    _listHiddenRegion.push_back( region );
            }
            else if ( StringUtil::equals( pName, "Ring", true ) )
            {
                reader.reportUnknownAttributes( child, FitPartDataInternal::kArrRingAttribute );
                FitRingDef ring;
                ring._bone   = reader.readName( child, "bone", false );
                ring._offset = reader.readFloat3( child, "offset", float3::Zero );
                ring._axis   = reader.readFloat3( child, "axis", ring._axis );
                ring._radius = reader.readFloat( child, "radius", ring._radius );
                ring._width  = reader.readFloat( child, "width", ring._width );
                if ( ring._radius <= 0.0f || ring._width < 0.0f || ring._axis.getLengthSquared() <= 0.0f )
                    reader.addError( child, "needs radius > 0, width >= 0 and a non-zero axis" );
                else
                    _listRing.push_back( ring );
            }
            else if ( StringUtil::equals( pName, "Corrective", true ) )
            {
                reader.reportUnknownAttributes( child, FitPartDataInternal::kArrCorrectiveAttribute );
                FitCorrectiveDef corrective;
                corrective._morph = reader.readName( child, "morph", false );
                for ( XMLNode deltaNode = child.findChild(); deltaNode; deltaNode = deltaNode.findNextSibling() )
                {
                    if ( StringUtil::equals( deltaNode.getName(), "Delta", true ) == false )
                    {
                        reader.reportUnknownElement( deltaNode );
                        continue;
                    }
                    reader.reportUnknownAttributes( deltaNode, FitPartDataInternal::kArrDeltaAttribute );
                    const int32 vertex = reader.readInt( deltaNode, "vertex", -1 );
                    if ( vertex < 0 )
                    {
                        reader.addError( deltaNode, "needs vertex >= 0" );
                        continue;
                    }
                    FitCorrectiveDelta delta;
                    delta._vertex = static_cast<uint32>( vertex );
                    delta._offset = reader.readFloat3( deltaNode, "offset", float3::Zero );
                    corrective._listDelta.push_back( delta );
                }
                _listCorrective.push_back( std::move( corrective ) );
            }
            else
            {
                reader.reportUnknownElement( child );
            }
        }
    }

    bool FitPartData::validate( const CharacterBoneArray* pBones, const AppearanceGeometry* pGeometry, string* pOutError ) const
    {
        bool bValid = true;
        if ( pBones != nullptr )
        {
            for ( const FitRingDef& ring : _listRing )
            {
                if ( ring._bone.empty() || pBones->findBone( ring._bone ) >= 0 )
                    continue;
                bValid = false;
                FitPartDataInternal::appendError( pOutError, string( "ring names unknown bone '" ) + ring._bone.c_str() + "'" );
            }
        }
        if ( pGeometry != nullptr )
        {
            for ( const FitCorrectiveDef& corrective : _listCorrective )
            {
                for ( const FitCorrectiveDelta& delta : corrective._listDelta )
                {
                    if ( delta._vertex < pGeometry->getVertexCount() )
                        continue;
                    bValid = false;
                    utf8         arrNumber[constant::kMaxBuffer16]{};
                    const uint32 numberLength = StringUtil::formatNumber( arrNumber, constant::kMaxBuffer16, delta._vertex );
                    string       message( "corrective delta names vertex " );
                    message += string_view( arrNumber, numberLength );
                    message += " beyond the mesh";
                    FitPartDataInternal::appendError( pOutError, message );
                }
            }
        }
        return bValid;
    }

    void FitPartData::setLayerAndProfile( const hashed_string& layer, const hashed_string& profile )
    {
        _layer   = layer;
        _profile = profile;
    }
} // namespace sw
