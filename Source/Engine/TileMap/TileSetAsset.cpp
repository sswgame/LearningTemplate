#include "pch.h"

#include "Engine/TileMap/TileSetAsset.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Asset/Cache/SharedAssetCacheDefinition.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

namespace sw
{
    namespace
    {
        struct TileSetAssetInternal
        {
            static constexpr string_view kFrameListSeparators = " ,\t"; ///< `frames` 정수 목록의 구분 글자
            /** @brief 패턴 글자 아홉 칸 중 이웃 여덟의 자리입니다(가운데 다섯째는 보지 않는다). */
            static constexpr uint32 kArrCellOfNeighbor[8] = { 0, 1, 2, 3, 5, 6, 7, 8 };

            /** @brief 아홉 글자 패턴(공백은 건너뜀)을 이웃 여덟 셀의 조건으로 읽습니다. 가운데(다섯째) 글자는 보지 않습니다. */
            [[nodiscard]] static bool parsePattern( string_view text, TileNeighborRule ( &outArrNeighbor )[8] )
            {
                TileNeighborRule arrCell[9]{};
                uint32           count = 0;
                for ( const utf8 character : text )
                {
                    const bool bWhitespaceCharacter = character == ' ' || character == '\t' || character == '\n' || character == '\r';
                    if ( bWhitespaceCharacter )
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
                for ( uint32 neighbor = 0; neighbor < 8; ++neighbor )
                {
                    outArrNeighbor[neighbor] = arrCell[kArrCellOfNeighbor[neighbor]];
                }
                return true;
            }

            /** @brief 이웃 조건 하나의 패턴 글자입니다(`parsePattern` 의 짝). */
            static utf8 makePatternCharacter( TileNeighborRule rule )
            {
                switch ( rule )
                {
                    case TileNeighborRule::Any:
                        return '.';
                    case TileNeighborRule::Same:
                        return 'o';
                    case TileNeighborRule::Other:
                        return 'x';
                }
                return '.';
            }

            /** @brief 이웃 여덟 조건을 세 줄 아홉 글자 패턴(`".x. xoo .o."`)으로 씁니다. 가운데는 '.' 입니다. */
            static string makePattern( const TileNeighborRule ( &arrNeighbor )[8] )
            {
                utf8 arrCell[9];
                arrCell[4] = '.';
                for ( uint32 neighbor = 0; neighbor < 8; ++neighbor )
                {
                    arrCell[kArrCellOfNeighbor[neighbor]] = makePatternCharacter( arrNeighbor[neighbor] );
                }
                const utf8 arrText[11] = { arrCell[0], arrCell[1], arrCell[2], ' ', arrCell[3], arrCell[4], arrCell[5], ' ', arrCell[6], arrCell[7], arrCell[8] };
                return string( arrText, 11 );
            }

            /** @brief 모습 하나를 노드 속성으로 씁니다 — 셀 하나는 `cell`, 둘 이상은 `frames` + `fps` 입니다(`parseVisual` 의 짝). */
            static void writeVisual( const XMLNode& node, const TileVisual& visual )
            {
                if ( visual._listFrameCell.size() == 1 )
                {
                    node.appendAttribute( "cell", visual._listFrameCell[0] );
                    return;
                }
                string frames;
                for ( const int32 cell : visual._listFrameCell )
                {
                    if ( frames.empty() == false )
                        frames += " ";
                    frames += to_string( cell );
                }
                node.appendAttribute( "frames", string_view( frames ) );
                node.appendAttribute( "fps", visual._framesPerSecond );
            }

            /** @brief 모습 하나의 규칙 위반을 덧붙입니다(셀 없음 · 아틀라스 밖 셀 · fps 없는 애니메이션). */
            static void collectVisualIssues( const TileSetContent& content, const TileVisual& visual, int32 brushIndex, optional<int32> ruleIndex, vector<TileSetIssue>& outListIssue )
            {
                const string_view                        brushName = content._listBrush[static_cast<size_t>( brushIndex )]._name.view();
                StringBuilder<constant::buffer::kMax256> ownerBuilder;
                if ( ruleIndex < 0 )
                    ownerBuilder.appendFormat( "brush '%#'", brushName );
                else
                    ownerBuilder.appendFormat( "rule %# of '%#'", ruleIndex, brushName );
                const string_view owner = ownerBuilder.view();
                if ( visual._listFrameCell.empty() )
                {
                    StringBuilder<constant::buffer::kMax256> textBuilder;
                    textBuilder.appendFormat( "%# has no cell", owner );
                    outListIssue.push_back( TileSetIssue{ string( textBuilder.view() ), brushIndex, ruleIndex } );
                }
                const int32 cellCount = content.getCellCount();
                for ( const int32 cell : visual._listFrameCell )
                {
                    if ( cell < 0 || cell >= cellCount )
                    {
                        StringBuilder<constant::buffer::kMax256> textBuilder;
                        textBuilder.appendFormat( "%# cell %# is outside the %#x%# atlas", owner, cell, content._columnCount, content._rowCount );
                        outListIssue.push_back( TileSetIssue{ string( textBuilder.view() ), brushIndex, ruleIndex } );
                    }
                }
                if ( visual._listFrameCell.size() > 1 && visual._framesPerSecond <= 0.0f )
                {
                    StringBuilder<constant::buffer::kMax256> textBuilder;
                    textBuilder.appendFormat( "%# has frames but no positive fps", owner );
                    outListIssue.push_back( TileSetIssue{ string( textBuilder.view() ), brushIndex, ruleIndex } );
                }
            }
        };
    } // namespace

    optional<int32> TileVisual::computeCellAt( float32 seconds ) const
    {
        if ( _listFrameCell.empty() )
            return nullopt;
        if ( isAnimated() == false )
            return _listFrameCell[0];
        const float32 frame = MathUtil::floor( MathUtil::max( seconds, 0.0f ) * _framesPerSecond );
        const uint64  index = static_cast<uint64>( frame ) % static_cast<uint64>( _listFrameCell.size() );
        return _listFrameCell[static_cast<size_t>( index )];
    }
} // namespace sw

namespace sw
{
    optional<uint32> TileSetContent::findBrush( const hashed_string& name ) const
    {
        for ( size_t brushIndex = 0; brushIndex < _listBrush.size(); ++brushIndex )
        {
            if ( _listBrush[brushIndex]._name == name )
                return static_cast<uint32>( brushIndex );
        }
        return nullopt;
    }

    void TileSetContent::collectIssues( vector<TileSetIssue>& outListIssue ) const
    {
        using Internal = TileSetAssetInternal;
        if ( _columnCount < 1 || _rowCount < 1 )
        {
            StringBuilder<constant::buffer::kMax256> textBuilder;
            textBuilder.appendFormat( "the atlas grid %#x%# needs at least one column and one row", _columnCount, _rowCount );
            outListIssue.push_back( TileSetIssue{ string( textBuilder.view() ), nullopt, nullopt } );
        }
        if ( _tileSize <= 0.0f )
        {
            StringBuilder<constant::buffer::kMax256> textBuilder;
            textBuilder.appendFormat( "tileSize %# must be positive", _tileSize );
            outListIssue.push_back( TileSetIssue{ string( textBuilder.view() ), nullopt, nullopt } );
        }
        for ( size_t brushIndex = 0; brushIndex < _listBrush.size(); ++brushIndex )
        {
            const TileBrush& brush = _listBrush[brushIndex];
            const int32      index = static_cast<int32>( brushIndex );
            if ( brush._name.empty() )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "brush %# has no name", index );
                outListIssue.push_back( TileSetIssue{ string( textBuilder.view() ), index, nullopt } );
            }
            else if ( findBrush( brush._name ) != static_cast<uint32>( brushIndex ) )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "brush '%#' is listed twice", brush._name.view() );
                outListIssue.push_back( TileSetIssue{ string( textBuilder.view() ), index, nullopt } );
            }
            if ( brush._navCost < 1 )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "brush '%#' navCost must be 1 to 255", brush._name.view() );
                outListIssue.push_back( TileSetIssue{ string( textBuilder.view() ), index, nullopt } );
            }
            Internal::collectVisualIssues( *this, brush._defaultVisual, index, nullopt, outListIssue );
            for ( size_t ruleIndex = 0; ruleIndex < brush._listRule.size(); ++ruleIndex )
            {
                Internal::collectVisualIssues( *this, brush._listRule[ruleIndex]._visual, index, static_cast<int32>( ruleIndex ), outListIssue );
            }
        }
    }

    string TileSetContent::saveToXMLText() const
    {
        using Internal = TileSetAssetInternal;
        XMLDocument   doc;
        const XMLNode root = doc.appendRoot( "TileSet" );
        if ( _name.empty() == false )
            root.appendAttribute( "name", string_view( _name ) );
        root.appendAttribute( "atlas", string_view( _atlasPath ) );
        if ( _normalAtlasPath.empty() == false )
            root.appendAttribute( "normalAtlas", string_view( _normalAtlasPath ) );
        root.appendAttribute( "columns", _columnCount );
        root.appendAttribute( "rows", _rowCount );
        root.appendAttribute( "tileSize", _tileSize );
        for ( const TileBrush& brush : _listBrush )
        {
            // 규칙 타일인지는 규칙이 있는지다(`TileBrush::isRuleTile`) — 규칙이 하나도 없는 규칙 타일은 `collectIssues` 가 내지 못하므로 그냥 타일로 쓴다.
            const XMLNode node = root.appendChild( brush.isRuleTile() ? "RuleTile" : "Tile" );
            node.appendAttribute( "name", brush._name.view() );
            Internal::writeVisual( node, brush._defaultVisual );
            if ( brush._bSolid == SW_TRUE )
                node.appendAttribute( "solid", "true" );
            if ( brush._navCost != 1 )
                node.appendAttribute( "navCost", static_cast<int32>( brush._navCost ) );
            if ( brush._bOutsideIsSame == SW_TRUE )
                node.appendAttribute( "outside", "same" );
            for ( const TileRule& rule : brush._listRule )
            {
                const XMLNode ruleNode = node.appendChild( "Rule" );
                ruleNode.appendAttribute( "pattern", string_view( Internal::makePattern( rule._arrNeighbor ) ) );
                Internal::writeVisual( ruleNode, rule._visual );
            }
        }
        return doc.saveToString();
    }
} // namespace sw

namespace sw
{
    bool TileSetAsset::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "TileSet.TileSetNotFound", "Tile set '%#' not found", path );
            return false;
        }
        // 리소스 id 로 합친다 — 조건 · 덮어쓰기 층 · 다시 읽기 기록 · 접속 대조를 받는다(글 입구 `loadFromXMLText` 는 id 가 없다).
        XMLDocument doc;
        if ( doc.parseResourceText( text, assetID ) == false )
        {
            SW_LOG_ERROR_ID( "TileSet.ParseFailed", "%#", doc.getLastError() );
            return false;
        }
        TileSetContent loaded;
        if ( parseDocument( doc, path, loaded ) == false )
            return false;
        return applyContent( std::move( loaded ), path );
    }

    bool TileSetAsset::parseVisual( const XMLNode& node, string_view sourceName, TileVisual& outVisual )
    {
        outVisual = TileVisual{};
        if ( node.findAttribute( "frames" ) != nullptr )
        {
            if ( StringUtil::parseIntList( node.getAttributeText( "frames" ), TileSetAssetInternal::kFrameListSeparators, outVisual._listFrameCell ) == false || outVisual._listFrameCell.empty() )
            {
                SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: <%#> frames must be cell numbers", sourceName, node.getName() );
                return false;
            }
            return node.readOptionalAttributeInRange( "fps", 0.0f, MathUtil::kMaxFloat, outVisual._framesPerSecond );
        }
        if ( node.findAttribute( "cell" ) != nullptr )
        {
            const optional<int32> cell = node.readRequiredAttribute<int32>( "cell" );
            if ( cell.has_value() == false )
                return false;
            outVisual._listFrameCell.push_back( *cell );
            return true;
        }
        SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: <%#> needs cell=\"n\" or frames=\"a b c\"", sourceName, node.getName() );
        return false;
    }

    bool TileSetAsset::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        TileSetContent loaded;
        if ( parseContent( xmlText, sourceName, loaded ) == false )
            return false;
        return applyContent( std::move( loaded ), sourceName );
    }

    bool TileSetAsset::applyContent( TileSetContent&& loaded, string_view sourceName )
    {
        // 형식 뒤의 규칙은 한 곳(`collectIssues`)이 판정한다 — 에디터가 브러시 옆에 보이는 문제와 같은 목록이다.
        vector<TileSetIssue> listIssue;
        loaded.collectIssues( listIssue );
        for ( const TileSetIssue& issue : listIssue )
        {
            SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: %#", sourceName, issue._message.c_str() );
        }
        if ( listIssue.empty() == false )
            return false;
        _content = std::move( loaded );
        return true;
    }

    bool TileSetAsset::parseContent( string_view xmlText, string_view sourceName, TileSetContent& outContent )
    {
        // 데이터 문서 길로 읽는다 — 조건(`_when`)을 푼다. 예전의 `parse` 는 조건을 풀지 않았다.
        XMLDocument doc;
        if ( doc.parseData( xmlText, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "TileSet.ParseFailed", "%#", doc.getLastError() );
            return false;
        }
        return parseDocument( doc, sourceName, outContent );
    }

    bool TileSetAsset::parseDocument( const XMLDocument& doc, string_view sourceName, TileSetContent& outContent )
    {
        const XMLNode root = doc.getRoot( "TileSet" );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: the root element must be <TileSet>", sourceName );
            return false;
        }
        // 칸 · 범위는 스키마(`TileSetRootParams`)가 정본이다. 깨진 값 · 범위 밖 값은 읽기 실패다.
        TileSetRootParams rootParams;
        if ( XMLFields::readFields( root, rootParams, sourceName ) == false )
            return false;
        TileSetContent loaded;
        loaded._name            = rootParams._name;
        loaded._atlasPath       = rootParams._atlas;
        loaded._normalAtlasPath = rootParams._normalAtlas;
        loaded._columnCount     = rootParams._columns;
        loaded._rowCount        = rootParams._rows;
        loaded._tileSize        = rootParams._tileSize;

        for ( XMLNode node = root.findChild(); node.isValid(); node = node.findNextSibling() )
        {
            TileBrush brush{};
            if ( parseBrush( node, sourceName, brush ) == false )
                return false;
            loaded._listBrush.push_back( brush );
        }
        outContent = std::move( loaded );
        return true;
    }

    bool TileSetAsset::parseBrush( const XMLNode& node, string_view sourceName, TileBrush& outBrush )
    {
        using Internal       = TileSetAssetInternal;
        const bool bRuleTile = StringUtil::equals( node.getName(), "RuleTile" );
        if ( bRuleTile == false && StringUtil::equals( node.getName(), "Tile" ) == false )
        {
            SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: unknown element <%#> (Tile, RuleTile)", sourceName, node.getName() );
            return false;
        }
        TileBrushParams params;
        if ( XMLFields::readFields( node, params, sourceName, { "cell", "frames", "fps" } ) == false || params._name.empty() )
            return false;
        TileBrush&        brush = outBrush;
        const string_view name  = params._name.view();
        brush._name             = params._name;
        if ( parseVisual( node, sourceName, brush._defaultVisual ) == false )
            return false;
        brush._bSolid                         = params._bSolid ? SW_TRUE : SW_FALSE;
        brush._navCost                        = static_cast<uint8>( params._navCost );
        const string_view outside             = params._outside;
        const bool        bInvalidOutsideMode = outside.empty() == false && StringUtil::equals( outside, "same" ) == false && StringUtil::equals( outside, "empty" ) == false;
        if ( bInvalidOutsideMode )
        {
            SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: outside=\"%#\" must be 'same' or 'empty'", sourceName, outside );
            return false;
        }
        brush._bOutsideIsSame = StringUtil::equals( outside, "same" ) ? SW_TRUE : SW_FALSE;

        for ( XMLNode ruleNode = node.findChild(); ruleNode.isValid(); ruleNode = ruleNode.findNextSibling() )
        {
            if ( bRuleTile == false || StringUtil::equals( ruleNode.getName(), "Rule" ) == false )
            {
                SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: unknown element <%#> in <%#> (only <Rule> inside <RuleTile>)", sourceName, ruleNode.getName(), node.getName() );
                return false;
            }
            TileRuleParams ruleFields;
            if ( XMLFields::readFields( ruleNode, ruleFields, sourceName, { "cell", "frames", "fps" } ) == false )
                return false;
            TileRule rule{};
            if ( Internal::parsePattern( ruleFields._pattern, rule._arrNeighbor ) == false )
            {
                SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: rule pattern '%#' of '%#' must be nine of . o x", sourceName, ruleFields._pattern.c_str(), name );
                return false;
            }
            if ( parseVisual( ruleNode, sourceName, rule._visual ) == false )
                return false;
            brush._listRule.push_back( rule );
        }
        // 규칙 없는 규칙 타일은 형식 오류다 — 내용(`TileSetContent`)에는 "규칙 타일" 표시가 따로 없어 규칙이 비면 그냥 타일이 된다.
        if ( bRuleTile && brush._listRule.empty() )
        {
            SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: rule tile '%#' has no <Rule>", sourceName, name );
            return false;
        }
        return true;
    }

    float4 TileSetAsset::computeCellUVRect( const TileSetContent& content, int32 cell )
    {
        const int32   column = ( content._columnCount > 0 ) ? cell % content._columnCount : 0;
        const int32   row    = ( content._columnCount > 0 ) ? cell / content._columnCount : 0;
        const float32 width  = 1.0f / static_cast<float32>( MathUtil::max( content._columnCount, 1 ) );
        const float32 height = 1.0f / static_cast<float32>( MathUtil::max( content._rowCount, 1 ) );
        return float4{ static_cast<float32>( column ) * width, static_cast<float32>( row ) * height, width, height };
    }

    const TileVisual* TileSetAsset::resolveVisual( const vector<uint16>& listBrushIndex, int32 width, int32 height, int32 x, int32 y ) const
    {
        const vector<TileBrush>& listBrush          = _content._listBrush;
        const bool               bOutsideVisualGrid = x < 0 || y < 0 || x >= width || y >= height;
        if ( bOutsideVisualGrid )
            return nullptr;
        const size_t cellCount = static_cast<size_t>( width ) * static_cast<size_t>( height );
        if ( listBrushIndex.size() < cellCount )
            return nullptr;
        const uint16 brushValue = listBrushIndex[static_cast<size_t>( y ) * static_cast<size_t>( width ) + static_cast<size_t>( x )];
        if ( brushValue == 0 || brushValue > listBrush.size() )
            return nullptr;
        const TileBrush& brush = listBrush[brushValue - 1u];
        if ( brush.isRuleTile() == false )
            return &brush._defaultVisual;

        // 이웃마다 "같은 브러시인가"를 한 번 측정하고, 규칙을 앞에서부터 맞춘다(첫 규칙이 이긴다. 유니티 RuleTile 과 같다).
        bool arrSame[8]{};
        for ( uint32 neighbor = 0; neighbor < 8; ++neighbor )
        {
            const int32 neighborX        = x + kNeighborOffset[neighbor][0];
            const int32 neighborY        = y + kNeighborOffset[neighbor][1];
            const bool  bNeighborOutside = neighborX < 0 || neighborY < 0 || neighborX >= width || neighborY >= height;
            if ( bNeighborOutside )
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

namespace sw
{
    /** 공유 캐시로 찾는 종류다(접미사 `.tileset.xml` — 에디터의 타일셋 편집 창이 이 종류로 문서를 받는다). 칸마다 지은 브러시 번호를 지키려 다시 읽기는 떼어 낸다(`Detach`). */
    SW_SHARED_ASSET_CACHE( TileSetAsset, TileSetAsset, TileSetAsset::kAssetType.withLoader( AssetTypeLoader::of<TileSetAsset>( MemoryTag::Asset, SharedAssetReloadMode::Detach ) ) );
} // namespace sw
