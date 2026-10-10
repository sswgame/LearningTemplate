#include "pch.h"

#include "Engine/TileMap/TileSetAsset.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

namespace sw
{
    SW_LOG_CALLER( "TileSet" );

    namespace
    {
        struct TileSetAssetInternal
        {
            /** @brief 공백으로 나뉜 정수 목록을 읽습니다. 숫자가 아닌 것이 있으면 false 입니다. */
            [[nodiscard]] static bool parseIntList( string_view text, vector<int32>& outListValue )
            {
                outListValue.clear();
                size_t cursor = 0;
                while ( cursor < text.size() )
                {
                    while ( cursor < text.size() && ( text[cursor] == ' ' || text[cursor] == ',' || text[cursor] == '\t' ) )
                    {
                        ++cursor;
                    }
                    const size_t start = cursor;
                    while ( cursor < text.size() && text[cursor] != ' ' && text[cursor] != ',' && text[cursor] != '\t' )
                    {
                        ++cursor;
                    }
                    if ( cursor == start )
                        break;
                    int32 value = 0;
                    if ( StringUtil::parseInt( text.substr( start, cursor - start ), value ) == false )
                        return false;
                    outListValue.push_back( value );
                }
                return true;
            }

            /** @brief 아홉 글자 패턴(공백은 건너뜀)을 이웃 여덟 칸의 조건으로 읽습니다. 가운데(다섯째) 글자는 보지 않습니다. */
            [[nodiscard]] static bool parsePattern( string_view text, TileNeighborRule ( &outArrNeighbor )[8] )
            {
                TileNeighborRule arrCell[9]{};
                uint32           count = 0;
                for ( const utf8 character : text )
                {
                    if ( character == ' ' || character == '\t' || character == '\n' || character == '\r' )
                        continue;
                    if ( count >= 9 )
                        return false;
                    switch ( character )
                    {
                        case '.':
                        {
                            arrCell[count] = TileNeighborRule::Any;
                            break;
                        }
                        case 'o':
                        {
                            arrCell[count] = TileNeighborRule::Same;
                            break;
                        }
                        case 'x':
                        {
                            arrCell[count] = TileNeighborRule::Other;
                            break;
                        }
                        default:
                        {
                            return false;
                        }
                    }
                    ++count;
                }
                if ( count != 9 )
                    return false;
                constexpr uint32 kArrCellOfNeighbor[8] = { 0, 1, 2, 3, 5, 6, 7, 8 };
                for ( uint32 neighbor = 0; neighbor < 8; ++neighbor )
                {
                    outArrNeighbor[neighbor] = arrCell[kArrCellOfNeighbor[neighbor]];
                }
                return true;
            }
        };
    } // namespace

    int32 TileVisual::computeCellAt( float32 seconds ) const
    {
        if ( _listFrameCell.empty() )
            return -1;
        if ( isAnimated() == false )
            return _listFrameCell[0];
        const float32 frame = MathUtil::floor( MathUtil::max( seconds, 0.0f ) * _framesPerSecond );
        const uint64  index = static_cast<uint64>( frame ) % static_cast<uint64>( _listFrameCell.size() );
        return _listFrameCell[static_cast<size_t>( index )];
    }

    bool TileSetAsset::loadFromResource( string_view path )
    {
        string text;
        if ( ResourceUtil::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Tile set '%#' not found", path );
            return false;
        }
        return loadFromXMLText( text, path );
    }

    bool TileSetAsset::parseVisual( const XMLNode& node, string_view sourceName, bool bRequired, TileVisual& outVisual ) const
    {
        outVisual = TileVisual{};
        if ( node.findAttribute( "frames" ) != nullptr )
        {
            if ( TileSetAssetInternal::parseIntList( node.getAttributeText( "frames" ), outVisual._listFrameCell ) == false || outVisual._listFrameCell.empty() )
            {
                SW_LOG_ERROR( "%#: <%#> frames must be cell numbers", sourceName, node.getName() );
                return false;
            }
            outVisual._framesPerSecond = node.getAttributeFloat( "fps", 0.0f );
            if ( outVisual._listFrameCell.size() > 1 && outVisual._framesPerSecond <= 0.0f )
            {
                SW_LOG_ERROR( "%#: <%#> has frames but no positive fps", sourceName, node.getName() );
                return false;
            }
        }
        else if ( node.findAttribute( "cell" ) != nullptr )
        {
            outVisual._listFrameCell.push_back( node.getAttributeInt( "cell", -1 ) );
        }
        else if ( bRequired )
        {
            SW_LOG_ERROR( "%#: <%#> needs cell=\"n\" or frames=\"a b c\"", sourceName, node.getName() );
            return false;
        }
        const int32 cellCount = _columnCount * _rowCount;
        for ( const int32 cell : outVisual._listFrameCell )
        {
            if ( cell < 0 || cell >= cellCount )
            {
                SW_LOG_ERROR( "%#: <%#> cell %# is outside the %#x%# atlas", sourceName, node.getName(), cell, _columnCount, _rowCount );
                return false;
            }
        }
        return true;
    }

    bool TileSetAsset::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        using Internal = TileSetAssetInternal;
        XMLDocument doc;
        if ( doc.parse( xmlText, sourceName ) == false )
        {
            SW_LOG_ERROR( "%#", doc.getLastError() );
            return false;
        }
        const XMLNode root = doc.getRoot( "TileSet" );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "%#: the root element must be <TileSet>", sourceName );
            return false;
        }
        static constexpr const utf8* kArrRootAttribute[] = { "name", "atlas", "normalAtlas", "columns", "rows", "tileSize" };
        if ( XMLNameCheck::reportUnknownAttributes( root, kArrRootAttribute, sourceName ) == false )
            return false;

        TileSetAsset loaded;
        loaded._atlasPath       = string( root.getAttributeText( "atlas" ) );
        loaded._normalAtlasPath = string( root.getAttributeText( "normalAtlas" ) );
        loaded._columnCount     = root.getAttributeInt( "columns", 1 );
        loaded._rowCount        = root.getAttributeInt( "rows", 1 );
        loaded._tileSize        = root.getAttributeFloat( "tileSize", 1.0f );
        if ( loaded._columnCount <= 0 || loaded._rowCount <= 0 || loaded._tileSize <= 0.0f )
        {
            SW_LOG_ERROR( "%#: columns, rows and tileSize must be positive", sourceName );
            return false;
        }

        for ( XMLNode node = root.findChild(); node.isValid(); node = node.findNextSibling() )
        {
            const bool bRuleTile = StringUtil::equals( node.getName(), "RuleTile", true );
            if ( bRuleTile == false && StringUtil::equals( node.getName(), "Tile", true ) == false )
            {
                SW_LOG_ERROR( "%#: unknown element <%#> (Tile, RuleTile)", sourceName, node.getName() );
                return false;
            }
            static constexpr const utf8* kArrBrushAttribute[] = { "name", "cell", "frames", "fps", "solid", "navCost", "outside" };
            if ( XMLNameCheck::reportUnknownAttributes( node, kArrBrushAttribute, sourceName ) == false )
                return false;

            TileBrush         brush{};
            const string_view name = node.getAttributeText( "name" );
            if ( name.empty() )
            {
                SW_LOG_ERROR( "%#: a <%#> has no name", sourceName, node.getName() );
                return false;
            }
            brush._name = hashed_string( name );
            if ( loaded.findBrush( brush._name ) >= 0 )
            {
                SW_LOG_ERROR( "%#: brush '%#' is listed twice", sourceName, name );
                return false;
            }
            if ( loaded.parseVisual( node, sourceName, true, brush._defaultVisual ) == false )
                return false;
            brush._bSolid             = node.getAttributeBool( "solid", false ) ? SW_TRUE : SW_FALSE;
            const int32 navCost       = node.getAttributeInt( "navCost", 10 );
            brush._navCost            = static_cast<uint8>( MathUtil::clamp( navCost, 1, 255 ) );
            const string_view outside = node.getAttributeText( "outside" );
            if ( outside.empty() == false && StringUtil::equals( outside, "same", true ) == false && StringUtil::equals( outside, "empty", true ) == false )
            {
                SW_LOG_ERROR( "%#: outside=\"%#\" must be 'same' or 'empty'", sourceName, outside );
                return false;
            }
            brush._bOutsideIsSame = StringUtil::equals( outside, "same", true ) ? SW_TRUE : SW_FALSE;

            for ( XMLNode ruleNode = node.findChild(); ruleNode.isValid(); ruleNode = ruleNode.findNextSibling() )
            {
                if ( bRuleTile == false || StringUtil::equals( ruleNode.getName(), "Rule", true ) == false )
                {
                    SW_LOG_ERROR( "%#: unknown element <%#> in <%#> (only <Rule> inside <RuleTile>)", sourceName, ruleNode.getName(), node.getName() );
                    return false;
                }
                static constexpr const utf8* kArrRuleAttribute[] = { "pattern", "cell", "frames", "fps" };
                if ( XMLNameCheck::reportUnknownAttributes( ruleNode, kArrRuleAttribute, sourceName ) == false )
                    return false;
                TileRule rule{};
                if ( Internal::parsePattern( ruleNode.getAttributeText( "pattern" ), rule._arrNeighbor ) == false )
                {
                    SW_LOG_ERROR( "%#: rule pattern '%#' of '%#' must be nine of . o x", sourceName, ruleNode.getAttributeText( "pattern" ), name );
                    return false;
                }
                if ( loaded.parseVisual( ruleNode, sourceName, true, rule._visual ) == false )
                    return false;
                brush._listRule.push_back( rule );
            }
            if ( bRuleTile && brush._listRule.empty() )
            {
                SW_LOG_ERROR( "%#: rule tile '%#' has no <Rule>", sourceName, name );
                return false;
            }
            loaded._listBrush.push_back( brush );
        }
        *this = loaded;
        return true;
    }

    float4 TileSetAsset::computeCellUvRect( int32 cell ) const
    {
        const int32   column = ( _columnCount > 0 ) ? cell % _columnCount : 0;
        const int32   row    = ( _columnCount > 0 ) ? cell / _columnCount : 0;
        const float32 width  = 1.0f / static_cast<float32>( MathUtil::max( _columnCount, 1 ) );
        const float32 height = 1.0f / static_cast<float32>( MathUtil::max( _rowCount, 1 ) );
        return float4{ static_cast<float32>( column ) * width, static_cast<float32>( row ) * height, width, height };
    }

    int32 TileSetAsset::findBrush( const hashed_string& name ) const
    {
        for ( size_t brushIndex = 0; brushIndex < _listBrush.size(); ++brushIndex )
        {
            if ( _listBrush[brushIndex]._name == name )
                return static_cast<int32>( brushIndex );
        }
        return -1;
    }

    const TileVisual* TileSetAsset::resolveVisual( const vector<uint16>& listBrushIndex, int32 width, int32 height, int32 x, int32 y ) const
    {
        if ( x < 0 || y < 0 || x >= width || y >= height )
            return nullptr;
        const size_t cellCount = static_cast<size_t>( width ) * static_cast<size_t>( height );
        if ( listBrushIndex.size() < cellCount )
            return nullptr;
        const uint16 brushValue = listBrushIndex[static_cast<size_t>( y ) * static_cast<size_t>( width ) + static_cast<size_t>( x )];
        if ( brushValue == 0 || brushValue > _listBrush.size() )
            return nullptr;
        const TileBrush& brush = _listBrush[brushValue - 1u];
        if ( brush.isRuleTile() == false )
            return &brush._defaultVisual;

        // 이웃마다 "같은 브러시인가" 를 한 번 재고, 규칙을 앞에서부터 맞춘다(첫 규칙이 이긴다 — 유니티 RuleTile 과 같다).
        bool arrSame[8]{};
        for ( uint32 neighbor = 0; neighbor < 8; ++neighbor )
        {
            const int32 neighborX = x + kNeighborOffset[neighbor][0];
            const int32 neighborY = y + kNeighborOffset[neighbor][1];
            if ( neighborX < 0 || neighborY < 0 || neighborX >= width || neighborY >= height )
            {
                arrSame[neighbor] = brush._bOutsideIsSame != SW_FALSE;
                continue;
            }
            arrSame[neighbor] = listBrushIndex[static_cast<size_t>( neighborY ) * static_cast<size_t>( width ) + static_cast<size_t>( neighborX )] == brushValue;
        }
        for ( const TileRule& rule : brush._listRule )
        {
            bool bMatch = true;
            for ( uint32 neighbor = 0; neighbor < 8 && bMatch; ++neighbor )
            {
                const TileNeighborRule condition = rule._arrNeighbor[neighbor];
                if ( condition == TileNeighborRule::Same && arrSame[neighbor] == false )
                    bMatch = false;
                else if ( condition == TileNeighborRule::Other && arrSame[neighbor] )
                    bMatch = false;
            }
            if ( bMatch )
                return &rule._visual;
        }
        return &brush._defaultVisual;
    }
} // namespace sw
