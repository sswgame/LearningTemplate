/**
 * @file TileSetAsset.h
 * @brief 타일셋(`.tileset.xml`) — 아틀라스 칸 · 애니메이션 타일 · 규칙 타일(이웃 규칙 자동 타일링) · 충돌 · 이동 비용입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class XmlNode;

    /**
     * @struct TileVisual
     * @brief 칸 하나가 보일 모습 — 아틀라스 칸 하나, 또는 칸 여럿을 일정 속도로 넘기는 애니메이션입니다(유니티 Animated Tile · Godot 타일 애니메이션).
     */
    struct TileVisual
    {
        vector<int32> _listFrameCell; ///< 아틀라스 칸 번호(행 우선, 0 = 왼쪽 위). 하나면 정지 타일입니다
        float32       _framesPerSecond{ 0.0f };

        /** @brief 애니메이션이면 true 입니다(칸이 둘 이상이고 속도가 0 보다 큼). */
        bool isAnimated() const { return _listFrameCell.size() > 1 && _framesPerSecond > 0.0f; }
        /** @brief 시각 @p seconds 에 보일 아틀라스 칸입니다. 칸이 없으면 -1 입니다. */
        SW_API int32 computeCellAt( float32 seconds ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @enum TileNeighborRule
     * @brief 규칙 타일의 이웃 한 칸에 거는 조건입니다(유니티 RuleTile 의 This · NotThis · 상관없음). 패턴 글자는 'o' · 'x' · '.' 입니다.
     */
    enum class TileNeighborRule : uint8
    {
        Any = 0, ///< '.' — 보지 않습니다
        Same,    ///< 'o' — 같은 규칙 타일이어야 합니다
        Other,   ///< 'x' — 같은 규칙 타일이 아니어야 합니다(빈 칸 · 다른 브러시)
    };

    /**
     * @struct TileRule
     * @brief 규칙 하나 — 이웃 여덟 칸의 조건과 맞으면 보일 모습입니다.
     * @details 이웃 순서는 왼위 · 위 · 오위 · 왼 · 오 · 왼아래 · 아래 · 오아래입니다(`kNeighborOffset`). XML 의 `pattern` 은 세 줄을 공백으로 나눈 아홉 글자이고
     *          가운데 글자는 보지 않습니다: `".x. xoo .o."`.
     */
    struct TileRule
    {
        TileNeighborRule _arrNeighbor[8]{};
        TileVisual       _visual;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct TileBrush
     * @brief 칠할 수 있는 것 하나 — 그냥 타일(모습 하나) 또는 규칙 타일(이웃에 따라 모습이 바뀜)입니다. 맵은 칸마다 브러시를 칠합니다.
     */
    struct TileBrush
    {
        hashed_string    _name;
        TileVisual       _defaultVisual; ///< 그냥 타일의 모습, 규칙 타일은 어느 규칙도 안 맞을 때의 모습
        vector<TileRule> _listRule;      ///< 비어 있으면 그냥 타일입니다. 앞 규칙이 먼저 맞습니다
        uint8            _navCost{ 10 }; ///< 이동 비용(1..254, 255 막힘 — `NavGrid` 와 같은 값). 단단한 타일은 막힘입니다
        uint8            _bSolid{ SW_FALSE };
        uint8            _bOutsideIsSame{ SW_FALSE }; ///< 맵 밖 이웃을 같은 타일로 볼지(가장자리에 테두리를 그리지 않으려면 켭니다)

        /** @brief 규칙 타일이면 true 입니다. */
        bool isRuleTile() const { return _listRule.empty() == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class TileSetAsset
     * @brief 아틀라스 하나와 그 칸으로 만든 브러시 목록입니다. 유니티 Tile Palette + Rule Tile · Animated Tile, Godot TileSet(terrain · animation · physics ·
     *        navigation layer)의 자리입니다.
     * @details 형식:
     *          `<TileSet atlas="..." columns="8" rows="8" tileSize="1">`
     *          `  <Tile name="dirt" cell="5" solid="true"/>`
     *          `  <Tile name="water" frames="12 13 14 15" fps="4" navCost="40"/>`
     *          `  <RuleTile name="ground" cell="5" solid="true" outside="same"> <Rule pattern=".x. xoo .o." cell="0"/> ... </RuleTile>`
     *          `</TileSet>`
     *          모르는 요소 · 속성, 같은 이름의 브러시 둘, 아틀라스 밖 칸, 아홉 글자가 아닌 패턴은 읽기 오류입니다.
     */
    class SW_API TileSetAsset
    {
    public:
        /** @brief 이웃 여덟 칸의 (dx, dy) — y 는 아래로 자랍니다(맵의 행 순서). 규칙의 `_arrNeighbor` 순서입니다. */
        static constexpr int32 kNeighborOffset[8][2] = {
            {-1, -1},
            { 0, -1},
            { 1, -1},
            {-1,  0},
            { 1,  0},
            {-1,  1},
            { 0,  1},
            { 1,  1},
        };

        /** @brief 리소스 경로의 타일셋을 읽습니다. 실패하면 오류를 남기고 false 이며 내용은 바뀌지 않습니다. */
        [[nodiscard]] bool loadFromResource( string_view path );
        /** @brief XML 본문을 읽습니다. */
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName );

        /** @brief 아틀라스 텍스처 경로입니다. */
        const string& getAtlasPath() const { return _atlasPath; }
        int32         getColumnCount() const { return _columnCount; }
        int32         getRowCount() const { return _rowCount; }
        /** @brief 타일 한 칸의 월드 크기입니다. */
        float32 getTileSize() const { return _tileSize; }
        /** @brief 아틀라스 칸의 UV 사각형 (u, v, 폭, 높이) 입니다. */
        float4 computeCellUvRect( int32 cell ) const;

        /** @brief 브러시 목록입니다. */
        const vector<TileBrush>& getBrushes() const { return _listBrush; }
        /** @brief 이름의 브러시 순번입니다. 없으면 -1 입니다. */
        int32 findBrush( const hashed_string& name ) const;

        /**
         * @brief 칸 하나의 모습을 고릅니다. 그냥 타일은 그 모습, 규칙 타일은 이웃 조건이 맞는 첫 규칙(없으면 기본 모습)입니다.
         * @param listBrushIndex 칸마다 브러시 순번 + 1 (0 = 빈 칸), 행 우선, y 는 아래로
         * @return 모습입니다. 빈 칸 · 모르는 브러시면 nullptr 입니다.
         */
        const TileVisual* resolveVisual( const vector<uint16>& listBrushIndex, int32 width, int32 height, int32 x, int32 y ) const;

    private:
        /** @brief 노드의 `cell` 또는 `frames` · `fps` 를 읽습니다. 둘 다 없으면 @p bRequired 일 때 오류입니다. */
        [[nodiscard]] bool parseVisual( const XmlNode& node, string_view sourceName, bool bRequired, TileVisual& outVisual ) const;

        string            _atlasPath;
        vector<TileBrush> _listBrush;
        float32           _tileSize{ 1.0f };
        int32             _columnCount{ 1 };
        int32             _rowCount{ 1 };
    };
} // namespace sw
