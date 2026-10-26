#include "pch.h"

#include "Engine/Character/Fit/FitPartData.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"

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

            /** @brief "names unknown <종류> '<이름>'" 오류를 덧붙입니다(피팅 표에 없는 이름). */
            static void appendNameError( string* pOutError, const utf8* pKind, const hashed_string& name )
            {
                utf8 arrMessage[constant::kMaxBuffer256]{};
                formatstring( arrMessage, constant::kMaxBuffer256, "names unknown %# '%#'", pKind, name.c_str() );
                appendError( pOutError, string( arrMessage ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool FitPartData::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        CharacterDataReader reader( sourceName );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.parseRoot( document, xmlText, "PartFit", root ) )
            readRoot( root, reader );
        return reader.finish();
    }

    bool FitPartData::loadFromResource( string_view path )
    {
        CharacterDataReader reader( path );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.loadRoot( document, path, "PartFit", root ) )
            readRoot( root, reader );
        return reader.finish();
    }

    void FitPartData::readRoot( const XMLNode& root, CharacterDataReader& reader )
    {
        _listHiddenRegion.clear();
        _listRing.clear();
        _listCorrective.clear();
        reader.reportUnknownAttributes( root, FitPartDataInternal::kArrRootAttribute );
        _layer   = reader.readName( root, "layer", true );
        _profile = reader.readName( root, "profile", true );
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            const utf8* pName = child.getName();
            if ( StringUtil::equals( pName, "Hide" ) )
            {
                reader.reportUnknownAttributes( child, FitPartDataInternal::kArrHideAttribute );
                const hashed_string region = reader.readName( child, "region", true );
                if ( region.empty() == false )
                    _listHiddenRegion.push_back( region );
            }
            else if ( StringUtil::equals( pName, "Ring" ) )
            {
                reader.reportUnknownAttributes( child, FitPartDataInternal::kArrRingAttribute );
                FitRingDef ring;
                ring._bone = reader.readName( child, "bone", false );
                reader.readOptionalFloat3( child, "offset", ring._offset );
                reader.readOptionalFloat3( child, "axis", ring._axis );
                reader.readOptionalFloat( child, "radius", ring._radius );
                reader.readOptionalFloat( child, "width", ring._width );
                const bool bInvalidRing = ring._radius <= 0.0f || ring._width < 0.0f || ring._axis.getLengthSquared() <= 0.0f;
                if ( bInvalidRing )
                    reader.addError( "Character.FitRingInvalid", child, "needs radius > 0, width >= 0 and a non-zero axis" );
                else
                    _listRing.push_back( ring );
            }
            else if ( StringUtil::equals( pName, "Corrective" ) )
            {
                reader.reportUnknownAttributes( child, FitPartDataInternal::kArrCorrectiveAttribute );
                FitCorrectiveDef corrective;
                corrective._morph = reader.readName( child, "morph", false );
                for ( XMLNode deltaNode = child.findChild(); deltaNode; deltaNode = deltaNode.findNextSibling() )
                {
                    if ( StringUtil::equals( deltaNode.getName(), "Delta" ) == false )
                    {
                        reader.reportUnknownElement( deltaNode );
                        continue;
                    }
                    reader.reportUnknownAttributes( deltaNode, FitPartDataInternal::kArrDeltaAttribute );
                    const int32 vertex = reader.readInt( deltaNode, "vertex" );
                    if ( vertex < 0 )
                    {
                        reader.addError( "Character.FitDeltaVertexInvalid", deltaNode, "needs vertex >= 0" );
                        continue;
                    }
                    FitCorrectiveDelta delta;
                    delta._vertex = static_cast<uint32>( vertex );
                    reader.readOptionalFloat3( deltaNode, "offset", delta._offset );
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

    bool FitPartData::validateNames( const FitTables& tables, string* pOutError ) const
    {
        bool bValid = true;
        if ( _layer.empty() == false && tables.findLayer( _layer ) == nullptr )
        {
            bValid = false;
            FitPartDataInternal::appendNameError( pOutError, "layer", _layer );
        }
        if ( _profile.empty() == false && tables.findProfile( _profile ) == nullptr )
        {
            bValid = false;
            FitPartDataInternal::appendNameError( pOutError, "profile", _profile );
        }
        for ( const hashed_string& region : _listHiddenRegion )
        {
            if ( tables.findRegionIndex( region ) >= 0 )
                continue;
            bValid = false;
            FitPartDataInternal::appendNameError( pOutError, "region", region );
        }
        return bValid;
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
