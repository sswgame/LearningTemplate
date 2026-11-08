#include "pch.h"

#include "Engine/Character/Fit/FitPartDef.h"

#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Character/CharacterDataReader.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/FitTables.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct FitPartDefInternal
        {

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
                utf8 arrMessage[constant::buffer::kMax256]{};
                formatstring( arrMessage, constant::buffer::kMax256, "names unknown %# '%#'", pKind, name.c_str() );
                appendError( pOutError, string( arrMessage ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool FitPartDef::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        CharacterDataReader reader( sourceName );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.parseRoot( document, xmlText, "PartFit", root ) )
            readRoot( root, reader );
        return reader.finish();
    }

    bool FitPartDef::loadFromAsset( const AssetID& assetID )
    {
        const string_view   path = assetID.view();
        CharacterDataReader reader( path );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.loadRoot( document, AssetID{ path }, "PartFit", root ) )
            readRoot( root, reader );
        return reader.finish();
    }

    void FitPartDef::readRoot( const XMLNode& root, CharacterDataReader& reader )
    {
        _listHiddenRegion.clear();
        _listRing.clear();
        _listCorrective.clear();
        FitPartRootParams rootParams;
        (void)reader.readFields( root, rootParams ); // 오류는 읽개의 목록에 쌓인다
        _layer   = rootParams._layer;
        _profile = rootParams._profile;
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            const utf8* pName = child.getName();
            if ( StringUtil::equals( pName, "Hide" ) )
            {
                FitHideParams hide;
                (void)reader.readFields( child, hide );
                const hashed_string& region = hide._region;
                if ( region.empty() == false )
                    _listHiddenRegion.push_back( region );
            }
            else if ( StringUtil::equals( pName, "Ring" ) )
            {
                FitRingDef ring;
                const bool bFieldsRead = reader.readFields( child, ring ); // 반지름 > 0 · 폭 >= 0 은 PROPERTY 가 본다
                if ( bFieldsRead && ring._axis.getLengthSquared() <= 0.0f )
                    reader.addError( "Character.FitRingInvalid", child, "needs a non-zero axis" );
                else
                    _listRing.push_back( ring );
            }
            else if ( StringUtil::equals( pName, "Corrective" ) )
            {
                FitCorrectiveDef corrective;
                (void)reader.readFields( child, corrective );
                for ( XMLNode deltaNode = child.findChild(); deltaNode; deltaNode = deltaNode.findNextSibling() )
                {
                    if ( StringUtil::equals( deltaNode.getName(), "Delta" ) == false )
                    {
                        reader.reportUnknownElement( deltaNode );
                        continue;
                    }
                    FitCorrectiveDelta delta;
                    if ( reader.readFields( deltaNode, delta ) ) // 정점 번호는 부호 없는 수라 음수는 읽기 오류다
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

    bool FitPartDef::validateNames( const FitTables& tables, string* pOutError ) const
    {
        bool bValid = true;
        if ( _layer.empty() == false && tables.findLayer( _layer ) == nullptr )
        {
            bValid = false;
            FitPartDefInternal::appendNameError( pOutError, "layer", _layer );
        }
        if ( _profile.empty() == false && tables.findProfile( _profile ) == nullptr )
        {
            bValid = false;
            FitPartDefInternal::appendNameError( pOutError, "profile", _profile );
        }
        for ( const hashed_string& region : _listHiddenRegion )
        {
            if ( tables.findRegionIndex( region ).has_value() )
                continue;
            bValid = false;
            FitPartDefInternal::appendNameError( pOutError, "region", region );
        }
        return bValid;
    }

    bool FitPartDef::validate( const CharacterBoneArray* pBones, const AppearanceGeometry* pGeometry, string* pOutError ) const
    {
        bool bValid = true;
        if ( pBones != nullptr )
        {
            for ( const FitRingDef& ring : _listRing )
            {
                if ( ring._bone.empty() || pBones->findBone( ring._bone ) >= 0 )
                    continue;
                bValid = false;
                FitPartDefInternal::appendError( pOutError, string( "ring names unknown bone '" ) + ring._bone.c_str() + "'" );
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
                    utf8         arrNumber[constant::buffer::kMax16]{};
                    const uint32 numberLength = StringUtil::formatNumber( arrNumber, constant::buffer::kMax16, delta._vertex );
                    string       message( "corrective delta names vertex " );
                    message += string_view( arrNumber, numberLength );
                    message += " beyond the mesh";
                    FitPartDefInternal::appendError( pOutError, message );
                }
            }
        }
        return bValid;
    }

    void FitPartDef::setLayerAndProfile( const hashed_string& layer, const hashed_string& profile )
    {
        _layer   = layer;
        _profile = profile;
    }
} // namespace sw
