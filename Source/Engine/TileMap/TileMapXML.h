/**
 * @file TileMapXML.h
 * @brief 타일맵 XML 문서 스키마 (Engine 소유, Editor/GameFramework 공용)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Common/AssetTypeRegistry.h"

namespace sw
{
    class TileSetAsset;

    /**
     * @brief 셀마다 켜고 끄는 타일 플래그 레이어입니다.
     * @details 레이어를 추가하면 값 하나와 `kArrTileFlagLayerInfo` 의 항목 하나를 추가한다. XML 읽기와 쓰기, 에디터의 레이어 목록과 색,
     *          페인트가 그 테이블을 순회하고, 런타임 `TileMap` 은 레이어 값으로 셀을 조회한다(`isFlagSet`).
     */
    enum class TileFlagLayer : uint8
    {
        Walkable = 0, /**< 걸을 수 있는 셀입니다. 기본값은 켜짐입니다. */
        Encounter,    /**< 조우가 일어나는 셀입니다. */
        PassThrough,  /**< 낭떠러지와 일방 통행 힌트입니다(보행은 가능합니다). */
        Count         /**< 개수를 세는 용도입니다. 레이어가 아닙니다. */
    };

    /** @brief 플래그 레이어 수입니다. */
    inline constexpr size_t kTileFlagLayerCount = static_cast<size_t>( TileFlagLayer::Count );

    /** @brief 플래그 레이어 하나의 이름, 저장 위치, 기본값, 에디터 색입니다. */
    struct TileFlagLayerInfo
    {
        const utf8*   _pName;         /**< 에디터에 보이는 이름입니다. */
        const utf8*   _pXMLAttribute; /**< `<t>` 의 속성 이름입니다(값이 기본값과 다를 때만 씁니다). nullptr 이면 `<t>` 본문 "1"/"0" 입니다. */
        uint32        _onColorRgb;    /**< 에디터에서 켜진 셀의 색(0xRRGGBB)입니다. */
        uint32        _offColorRgb;   /**< 에디터에서 꺼진 셀의 색(0xRRGGBB)입니다. */
        TileFlagLayer _layer;         /**< 레이어입니다. 테이블의 순번과 같아야 합니다. */
        uint8         _defaultValue;  /**< 파일에 없을 때와 새 셀의 값(0 또는 1)입니다. */
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 플래그 레이어 테이블입니다. **레이어마다 항목이 하나이고 순서는 `TileFlagLayer` 값 순서입니다.**
     * @details XML 속성 이름은 파일 형식이다. 바꾸면 기존 맵의 그 레이어가 기본값으로 읽힌다. 쓰는 순서도 이 테이블 순서라
     *          항목 순서를 바꾸면 저장 바이트가 바뀐다. 본문(nullptr)으로 저장하는 레이어는 하나뿐이다.
     */
    inline constexpr TileFlagLayerInfo kArrTileFlagLayerInfo[] = {
        {   "Walkable", nullptr, 0x50A05Au, 0x464650u,    TileFlagLayer::Walkable, 1},
        {  "Encounter",   "enc", 0x78BE5Au, 0x323237u,   TileFlagLayer::Encounter, 0},
        {"PassThrough",    "pt", 0x5078C8u, 0x464650u, TileFlagLayer::PassThrough, 0},
    };

    static_assert( SW_COUNT_OF( kArrTileFlagLayerInfo ) == kTileFlagLayerCount, "TileFlagLayer 를 늘렸으면 kArrTileFlagLayerInfo 에도 줄을 더할 것" );

    /** @brief TileMap XML 전체 문서 */
    struct TileMapXMLData
    {
        /** @brief 타일맵 파일의 접미사입니다. 에디터 종류 표(`EditorAssetType.cpp`)가 이 값을 씁니다. */
        static constexpr string_view kExtension = ".tilemap.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `TileMapXML.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "TileMapXMLData", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0 };

        /**
         * @brief 타일맵 하나가 가질 수 있는 최대 셀 수입니다 (2048 x 2048).
         * @details 셀 수는 `_width x _height` 이고, 셀마다 배열(플래그 레이어와 비주얼)에 8 바이트가 든다. 이 상한이
         *          곧 한 맵이 잡을 수 있는 메모리의 상한(약 32 MiB)이다. 크기는 **파일이나 사용자
         *          입력에서** 오는데(XML 의 `<width>`, 에디터의 Width/Height 필드) 둘 다 int32 를
         *          그대로 받으므로, 상한이 없으면 `100000 x 100000` 한 번에 10^10 셀을 잡으려다
         *          죽는다. 실제 맵은 한 변이 수십 셀이라 실제 사용을 제한하지 않는다.
         */
        static constexpr int32 kMaxTileCount = 1 << 22;

        /** @brief 이 크기가 다룰 수 있는 범위인지 여부입니다. 곱은 int64 로 계산합니다(int32 로는 오버플로합니다). */
        static bool isSizeSupported( int32 width, int32 height )
        {
            if ( width <= 0 || height <= 0 )
                return false;
            return ( static_cast<int64>( width ) * static_cast<int64>( height ) ) <= static_cast<int64>( kMaxTileCount );
        }

        /** @brief 타일 한 셀의 가짜 높이, 틴트, 아틀라스 값입니다. */
        struct Visual
        {
            uint8 _height{ 0 };
            uint8 _tintR{ 180 };
            uint8 _tintG{ 200 };
            uint8 _tintB{ 160 };
            uint8 _atlasID{ 0 };
        };

        /** @brief 타일 좌표에서 다른 맵으로 보내는 워프 */
        struct Warp
        {
            int32  _tileX{ 0 };
            int32  _tileY{ 0 };
            string _targetMap{};
            int32  _targetTileX{ 1 };
            int32  _targetTileY{ 1 };
            string _pairID{};
        };

        string         _name{};
        string         _sourcePath{};
        string         _scenePath{};
        string         _role{};
        int32          _width{ 0 };
        int32          _height{ 0 };
        int32          _spawnX{ 1 };
        int32          _spawnY{ 1 };
        vector<uint8>  _arrFlagLayer[kTileFlagLayerCount]{}; /**< 레이어마다 셀 수만큼의 0 또는 1 입니다(`TileFlagLayer` 순서). */
        vector<Visual> _listVisual{};
        vector<Warp>   _listWarp{};
        /**
         * @brief 타일 레이어의 타일셋(`.tileset.xml`) 경로입니다. 비어 있으면 타일 레이어가 없고 `<tileLayer>` 를 쓰지 않습니다(기존 맵과 바이트까지 같습니다).
         * @details 타일 레이어는 셀마다 **브러시**(그냥 타일, 규칙 타일)를 칠한다. 보일 모습(아틀라스 셀)은 저장하지 않고 읽는 쪽이 규칙으로 고른다
         *          (`TileSetAsset::resolveVisual`). 이웃을 칠하면 모습이 따라 바뀐다(유니티의 Rule Tile, Godot 의 terrain).
         */
        string _tileSetPath{};
        /** @brief 이 맵이 칠한 브러시 이름들(팔레트)입니다. 셀 값 n 은 n - 1 번째 이름입니다. 이름으로 적으므로 타일셋의 순서가 바뀌어도 맵이 그대로입니다. */
        vector<string> _listPaletteName{};
        /** @brief 셀마다 팔레트 번호 + 1 (0 = 빈 셀)이며, 행 우선(y 는 아래로)입니다. 타일 레이어가 없으면 비어 있습니다. */
        vector<uint16> _listTileCell{};

        /** @brief 레이어 하나의 셀 배열입니다. */
        vector<uint8>& getFlagLayer( TileFlagLayer layer ) { return _arrFlagLayer[static_cast<size_t>( layer )]; }
        /** @brief 레이어 하나의 셀 배열입니다. */
        const vector<uint8>& getFlagLayer( TileFlagLayer layer ) const { return _arrFlagLayer[static_cast<size_t>( layer )]; }
        /**
         * @brief 크기를 바꾸고 모든 셀을 기본값으로 채웁니다(플래그 레이어는 테이블의 기본값입니다). 워프를 비웁니다.
         * @return 크기가 `isSizeSupported` 밖이면 아무것도 바꾸지 않고 false 를 반환합니다.
         */
        [[nodiscard]] SW_API bool resetTiles( int32 width, int32 height );

        /** @brief 셀에 칠한 브러시 이름을 반환합니다. 빈 셀이거나 맵 밖이면 빈 문자열입니다. */
        SW_API string_view getTileBrushName( int32 x, int32 y ) const;
        /**
         * @brief 셀에 브러시를 칠합니다(빈 이름은 지우기입니다). 처음 쓰는 이름은 팔레트에 추가합니다. 맵 밖이면 false 를 반환합니다.
         * @details 타일 레이어 배열이 없으면 현재 크기로 만든다.
         */
        SW_API bool setTileBrush( int32 x, int32 y, string_view brushName );
        /**
         * @brief 셀마다 타일셋 브러시 번호 + 1 (0 = 빈 셀)로 옮깁니다. `TileSetAsset::resolveVisual` 이 받는 형태입니다.
         * @return 팔레트의 이름 하나라도 타일셋에 없으면 오류를 남기고 false 를 반환합니다(그 셀은 비웁니다).
         */
        SW_API bool mapTileCells( const TileSetAsset& tileSet, vector<uint16>& outListBrushIndex ) const;

        /** @brief Resource 상대 또는 절대 경로에서 타일맵 XML을 읽습니다. */
        [[nodiscard]] SW_API bool load( string_view path );
        /** @brief XML 본문에서 타일맵을 읽습니다. */
        [[nodiscard]] SW_API bool loadFromXML( string_view xml );
        /** @brief Resource 상대 또는 절대 경로로 타일맵 XML을 씁니다. */
        [[nodiscard]] SW_API bool save( string_view path ) const;
        /**
         * @brief 타일맵 XML 본문을 만듭니다.
         * @details `<t>` 는 항상 `_width × _height` 개를 적는다. 타일 배열이 그보다 짧으면
         *          (크기만 바꾸고 셀을 늘리지 않은 상태) 모자란 셀은 **읽기 쪽 기본값**으로 적고
         *          경고를 남긴다. 배열 밖을 읽지 않으면서 왕복도 어긋나지 않는 방법이다.
         */
        SW_API string toXML() const;
    };

} // namespace sw
