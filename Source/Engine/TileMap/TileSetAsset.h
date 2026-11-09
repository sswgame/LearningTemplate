/**
 * @file TileSetAsset.h
 * @brief 타일셋(`.tileset.xml`)입니다. 아틀라스 셀, 애니메이션 타일, 규칙 타일(이웃 규칙 자동 타일링), 충돌, 이동 비용을 담습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class XMLDocument;
    class XMLNode;

    /**
     * @struct TileVisual
     * @brief 셀 하나가 보일 모습입니다. 아틀라스 셀 하나이거나, 셀 여럿을 일정 속도로 넘기는 애니메이션입니다(유니티의 Animated Tile, Godot 의 타일 애니메이션).
     */
    struct TileVisual
    {
        vector<int32> _listFrameCell; ///< 아틀라스 셀 번호입니다(행 우선, 0 = 왼쪽 위). 하나면 정지 타일입니다.
        float32       _framesPerSecond{ 0.0f };

        /** @brief 애니메이션이면 true 를 반환합니다(셀이 둘 이상이고 속도가 0 보다 큽니다). */
        bool isAnimated() const { return _listFrameCell.size() > 1 && _framesPerSecond > 0.0f; }
        /** @brief 시각 @p seconds 에 보일 아틀라스 셀을 반환합니다. 셀이 없으면 -1 입니다. */
        SW_API int32 computeCellAt( float32 seconds ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @enum TileNeighborRule
     * @brief 규칙 타일의 이웃 한 셀에 거는 조건입니다(유니티 RuleTile 의 This, NotThis, 상관없음). 패턴 글자는 'o', 'x', '.' 입니다.
     */
    enum class TileNeighborRule : uint8
    {
        Any = 0, ///< '.' 이며 보지 않습니다.
        Same,    ///< 'o' 이며 같은 규칙 타일이어야 합니다.
        Other,   ///< 'x' 이며 같은 규칙 타일이 아니어야 합니다(빈 셀이거나 다른 브러시).
    };

    /**
     * @struct TileRule
     * @brief 규칙 하나입니다. 이웃 여덟 셀의 조건과, 조건이 맞으면 보일 모습을 담습니다.
     * @details 이웃 순서는 왼위, 위, 오위, 왼, 오, 왼아래, 아래, 오아래다(`kNeighborOffset`). XML 의 `pattern` 은 세 줄을 공백으로 나눈 아홉 글자이고
     *          가운데 글자는 보지 않는다. 예를 들면 `".x. xoo .o."` 이다.
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
     * @brief 칠할 수 있는 것 하나입니다. 그냥 타일(모습 하나)이거나 규칙 타일(이웃에 따라 모습이 바뀜)입니다. 맵은 셀마다 브러시를 칠합니다.
     */
    struct TileBrush
    {
        hashed_string    _name;
        TileVisual       _defaultVisual; ///< 그냥 타일의 모습이다. 규칙 타일은 어느 규칙도 맞지 않을 때의 모습이다.
        vector<TileRule> _listRule;      ///< 비어 있으면 그냥 타일이다. 앞 규칙이 먼저 맞는다.
        uint8            _navCost{ 10 }; ///< 이동 비용이다(1..254, 255 는 막힘이며 `NavGrid` 와 같은 값이다). 단단한 타일은 막힘이다.
        uint8            _bSolid{ SW_FALSE };
        uint8            _bOutsideIsSame{ SW_FALSE }; ///< 맵 밖 이웃을 같은 타일로 볼지 정한다(가장자리에 테두리를 그리지 않으려면 켠다).

        /** @brief 규칙 타일이면 true 입니다. */
        bool isRuleTile() const { return _listRule.empty() == false; }
    };
} // namespace sw

namespace sw
{
    /** @brief `<TileSet>` 루트의 칸입니다. 그림 셀 수(`columns` × `rows`)와 타일 한 칸의 월드 크기(`tileSize`)는 모두 양수다(적지 않으면 1). */
    REFLECT()
    struct SW_API TileSetRootParams
    {
        REFLECT_BODY();

        PROPERTY()
        string _name{};
        PROPERTY( AssetPath )
        string _atlas{};
        PROPERTY( AssetPath )
        string _normalAtlas{};
        PROPERTY( Min = 1 )
        int32 _columns{ 1 };
        PROPERTY( Min = 1 )
        int32 _rows{ 1 };
        PROPERTY( Min = 0.0001 )
        float32 _tileSize{ 1.0f };
    };

    /** @brief `<Tile>` · `<RuleTile>` 의 칸입니다. 그림(`cell` · `frames` · `fps`)은 서로 배타라 손으로 읽는다(`TileSetAsset::parseVisual`). */
    REFLECT()
    struct SW_API TileBrushParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _name{};
        PROPERTY()
        bool _bSolid{ false };
        /** @brief 길 찾기 이동 비용입니다(255 는 막힘 — `NavGrid`). */
        PROPERTY( Min = 1, Max = 255 )
        int32 _navCost{ 1 };
        /** @brief 이웃 규칙에서 지도 밖을 무엇으로 볼지입니다(`same` · `empty`, 비우면 empty). */
        PROPERTY()
        string _outside{};
    };

    /** @brief `<RuleTile><Rule pattern>` 의 칸입니다. 그림은 브러시와 같이 손으로 읽는다. */
    REFLECT()
    struct SW_API TileRuleParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _pattern{};
    };

    /** @brief 타일셋 내용의 규칙 위반 하나입니다(`TileSetContent::collectIssues`). 읽기는 이것을 오류로 남기고, 에디터는 그 브러시 · 규칙 옆에 보인다. */
    struct TileSetIssue
    {
        string _message{};                                     ///< 영어 한 문장
        int32  _brushIndex{ constant::invalid_index::kInt32 }; ///< 문제가 난 브러시 번호(타일셋 전체의 문제면 -1)
        int32  _ruleIndex{ constant::invalid_index::kInt32 };  ///< 문제가 난 규칙 번호(브러시 자체 · 기본 모습이면 -1)
    };

    /**
     * @struct TileSetContent
     * @brief 타일셋 파일 한 벌의 내용입니다. 읽기(`TileSetAsset::loadFromXMLText`)가 채우고 쓰기(`saveToXMLText`)가 같은 형식으로 씁니다 — 에디터는 이 사본을 고쳐 쓰고, 런타임은 쓴 글을 다시 읽는다.
     * @details 규칙(아틀라스 밖 셀 · 같은 이름 · 규칙 없는 규칙 타일 · fps 없는 애니메이션 · 칸 수)은 `collectIssues` 한 곳이다. 읽기도 이것으로 판정하므로 에디터가 보이는 문제와 로드 오류가 갈리지 않는다.
     */
    struct SW_API TileSetContent
    {
        string            _name{}; ///< 루트의 `name`(표시용, 비어도 된다)
        string            _atlasPath{};
        string            _normalAtlasPath{};
        vector<TileBrush> _listBrush{};
        float32           _tileSize{ 1.0f };
        int32             _columnCount{ 1 };
        int32             _rowCount{ 1 };

        /** @brief 이름의 브러시 순번입니다. 없으면 비어 있습니다. */
        optional<uint32> findBrush( const hashed_string& name ) const;
        /** @brief 아틀라스 셀 수(`columns` × `rows`)입니다. */
        int32 getCellCount() const { return _columnCount * _rowCount; }
        /**
         * @brief 규칙 위반을 모두 @p outListIssue 에 덧붙입니다. 하나도 없으면 읽을 수 있는 내용입니다.
         */
        void collectIssues( vector<TileSetIssue>& outListIssue ) const;
        /**
         * @brief `.tileset.xml` 글로 씁니다. 읽기의 형식 그대로이고 기본값 칸(`solid` 거짓 · `navCost` 1 · `outside` empty · 빈 노멀 아틀라스)은 적지 않습니다.
         * @details 모습은 셀이 하나면 `cell`, 둘 이상이면 `frames` + `fps` 다. 규칙의 `pattern` 은 세 줄 아홉 글자이고 가운데는 '.' 다.
         */
        string saveToXMLText() const;
    };

    /**
     * @class TileSetAsset
     * @brief 아틀라스 하나와 그 셀로 만든 브러시 목록입니다. 유니티의 Tile Palette, Rule Tile, Animated Tile, Godot 의 TileSet(terrain, animation, physics,
     *        navigation layer)에 해당합니다.
     * @details 형식은 다음과 같다.
     *          `<TileSet atlas="..." normalAtlas="..."? columns="8" rows="8" tileSize="1">`
     *          `  <Tile name="dirt" cell="5" solid="true"/>`
     *          `  <Tile name="water" frames="12 13 14 15" fps="4" navCost="40"/>`
     *          `  <RuleTile name="ground" cell="5" solid="true" outside="same"> <Rule pattern=".x. xoo .o." cell="0"/> ... </RuleTile>`
     *          `</TileSet>`
     *          모르는 요소나 속성, 같은 이름의 브러시 둘, 아틀라스 밖 셀, 아홉 글자가 아닌 패턴은 읽기 오류다.
     */
    class SW_API TileSetAsset
    {
        friend struct AssetTypeLoader;

    public:
        /** @brief 이웃 여덟 셀의 (dx, dy) 입니다. y 는 아래로 자랍니다(맵의 행 순서). 규칙의 `_arrNeighbor` 순서입니다. */
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

        /** @brief 타일셋 파일의 접미사입니다. */
        static constexpr string_view kExtension = ".tileset.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `TileSetAsset.cpp` 의 공유 캐시 줄). 접미사가 있어 에디터 종류 · 콘텐츠 브라우저가 타일셋 편집 창으로 연다. */
        static constexpr AssetTypeDef kAssetType{ "TileSetAsset", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        /** @brief XML 본문을 읽습니다. 형식이 틀리거나 규칙 위반(`TileSetContent::collectIssues`)이 있으면 오류를 남기고 false 이며 내용은 바뀌지 않습니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );
        /**
         * @brief 형식만 읽어 @p outContent 를 채웁니다(모르는 요소 · 속성 · 패턴 글자 · 빠진 모습은 오류). 규칙 위반(`collectIssues`)은 보지 않습니다.
         * @details `loadFromXMLText` 의 앞 절반이다. 에디터의 되돌리기 글은 편집 중의 상태라 위반(밖으로 나간 셀 등)이 든 채로 돌아와야 해 이것으로 읽는다.
         */
        [[nodiscard]] static bool parseContent( string_view xmlText, string_view sourceName, TileSetContent& outContent );
        /** @brief 읽은 문서(조건을 푼 글)를 내용으로 옮깁니다(형식만). 글 입구와 리소스 입구가 함께 쓴다. */
        [[nodiscard]] static bool parseDocument( const XMLDocument& doc, string_view sourceName, TileSetContent& outContent );
        /** @brief 읽은 내용입니다. 에디터는 이것을 복사해 고친다. */
        const TileSetContent& getContent() const { return _content; }

        /** @brief 아틀라스 텍스처 경로입니다. */
        const string& getAtlasPath() const { return _content._atlasPath; }
        /** @brief 아틀라스와 같은 셀 배치의 노멀 맵 경로입니다(빛을 받는 타일). 비어 있으면 없습니다. */
        const string& getNormalAtlasPath() const { return _content._normalAtlasPath; }
        int32         getColumnCount() const { return _content._columnCount; }
        int32         getRowCount() const { return _content._rowCount; }
        /** @brief 타일 한 셀의 월드 크기입니다. */
        float32 getTileSize() const { return _content._tileSize; }
        /** @brief 아틀라스 셀의 UV 사각형 (u, v, 폭, 높이) 입니다. */
        float4 computeCellUVRect( int32 cell ) const { return computeCellUVRect( _content, cell ); }
        /** @brief @p content 의 아틀라스 셀 UV 사각형입니다(에디터가 저장 전 사본으로 그린다 — 런타임과 같은 셀 해석). */
        static float4 computeCellUVRect( const TileSetContent& content, int32 cell );

        /** @brief 브러시 목록입니다. */
        const vector<TileBrush>& getBrushes() const { return _content._listBrush; }
        /** @brief 이름의 브러시 순번입니다. 없으면 비어 있습니다. */
        optional<uint32> findBrush( const hashed_string& name ) const { return _content.findBrush( name ); }

        /**
         * @brief 셀 하나의 모습을 고릅니다. 그냥 타일은 그 모습이고, 규칙 타일은 이웃 조건이 맞는 첫 규칙입니다(없으면 기본 모습).
         * @param listBrushIndex 셀마다 브러시 순번 + 1 (0 = 빈 셀)입니다. 행 우선이고 y 는 아래로 갑니다.
         * @return 모습을 반환합니다. 빈 셀이거나 모르는 브러시면 nullptr 입니다.
         */
        const TileVisual* resolveVisual( const vector<uint16>& listBrushIndex, int32 width, int32 height, int32 x, int32 y ) const;

    private:
        /** @brief 리소스 경로의 타일셋을 읽습니다. 실패하면 오류를 남기고 false 이며 내용은 바뀌지 않습니다. 종류의 로더(공유 캐시)만 부른다 — 쓰는 쪽은 `SharedAssetCache<TileSetAsset>::acquire` 로 한 벌을 쥔다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 형식 뒤의 규칙(`collectIssues`)을 보고 통과하면 내용을 들인다. 실패하면 오류를 남기고 내용은 바뀌지 않는다. */
        [[nodiscard]] bool applyContent( TileSetContent&& loaded, string_view sourceName );
        /** @brief 노드의 `cell` 또는 `frames` 와 `fps` 를 읽습니다. 둘 다 없으면 오류입니다. 값의 규칙(셀 범위 · fps)은 `collectIssues` 가 본다. */
        [[nodiscard]] static bool parseVisual( const XMLNode& node, string_view sourceName, TileVisual& outVisual );
        /** @brief `<Tile>` 또는 `<RuleTile>` 하나를 읽습니다(형식만 — 이름 겹침 · 셀 범위는 `collectIssues`). 잘못되었으면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool parseBrush( const XMLNode& node, string_view sourceName, TileBrush& outBrush );

    private:
        TileSetContent _content;
    };

    /**
     * @brief 타일셋을 경로마다 한 벌 나눠 줍니다("TileSetAsset", 인스턴스화는 `TileSetAsset.cpp`).
     * @details 다시 읽기는 떼어 낸다(`Detach`) — 쥔 쪽은 칸마다 브러시 번호를 지어 두므로 브러시 목록이 제자리에서 바뀌면 번호가 어긋난다.
     *          쥔 쪽(`TileMapRendererComponent`)은 다시 읽은 횟수가 바뀌면 새 객체를 받아 번호 · 배치를 다시 짓는다(유니티 Tile Palette · Godot TileSet 리소스 한 벌, 기억에 의존).
     */
    extern template class SW_API_EXTERN_TEMPLATE SharedAssetCache<TileSetAsset>;
} // namespace sw
