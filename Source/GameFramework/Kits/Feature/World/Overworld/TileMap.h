/**
 * @file TileMap.h
 * @brief 오버월드 타일맵입니다. Engine 의 타일맵 스키마(`TileMapXMLData`) 위에서 걷기, 조우, 통과, 워프 타일을 조회합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/TileMap/TileMapXML.h"

#include "GameFramework/Base/Foundation/Utility/Grid/GridTopology.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 워프와 HD-2D 비주얼. 파일 스키마(`TileMapXMLData`)의 타입을 그대로 쓴다
    // ------------------------------------------------------------------------------
    /** @brief 타일 좌표에서 다른 맵으로 보내는 워프입니다. */
    using TileWarp = TileMapXMLData::Warp;
    /** @brief HD-2D 1차 비주얼입니다. 타일별 가짜 높이와 틴트를 가지며, 메시 패스가 생기기 전까지는 소프트웨어 렌더링과 디버그용으로만 씁니다. */
    using OverworldTileVisual = TileMapXMLData::Visual;

    // ------------------------------------------------------------------------------
    // 2) TileMap. XML 그리드와 워프 목록을 가진다
    // ------------------------------------------------------------------------------
    /**
     * @brief HD-2D 오버월드 타일 그리드입니다.
     * @details 맵 데이터는 파일 스키마 `TileMapXMLData` 를 그대로 가지고 있으므로 플래그 레이어, 비주얼, 워프를 필드마다 복사하지 않습니다.
     *          타일 속성은 Engine 레이어 테이블(`kArrTileFlagLayerInfo`)의 레이어로 조회합니다(`isFlagSet`).
     *          조우가 **일어나는 타일**은 `TileFlagLayer::Encounter` 이고, 무엇을 만나는지는 장르 키트의 지역 테이블이 정합니다.
     *          지역은 존 id 와 태그로 구분합니다(`ZoneTracker`).
     */
    class SW_KIT_API TileMap
    {
    public:
        TileMap();

        /**
         * @brief Resource 상대 XML 에서 타일맵을 불러옵니다. 타일 레이어가 있으면 그 타일셋도 읽어 단단한 브러시 칸을 막습니다(`addSolidLayer`).
         * @return 맵이나 타일셋을 읽지 못하면 false 입니다(타일셋을 못 읽은 맵을 "어디든 걸을 수 있는 맵"으로 두지 않는다).
         */
        [[nodiscard]] bool loadFromXML( string_view assetRelativePath );
        /**
         * @brief 이미 읽은 맵 데이터와 그 타일셋으로 채웁니다. 씬의 `TileMapRendererComponent` 가 읽은 것을 그대로 받아, 그리는 맵과 걷는 맵이 한 데이터가 되게 합니다.
         * @return 타일셋에 없는 브러시를 칠한 맵이면 false 입니다.
         */
        [[nodiscard]] bool assign( const TileMapXMLData& data, const TileSetAsset& tileSet );
        /**
         * @brief 같은 크기의 다른 타일 레이어(집 · 나무 같은 소품 레이어)에서 단단한(`solid`) 브러시 칸을 막습니다.
         * @details 막힘은 플래그 레이어(`Walkable`, 파일에 저장되는 값)가 아니라 따로 든 표에 쌓는다 — 저장하면 손으로 칠한 플래그만 남는다.
         *          충돌의 원본은 타일셋의 `solid` 하나다(렌더러의 물리 충돌체와 같은 값). 유니티의 TilemapCollider2D, Godot TileSet physics layer 와 같다.
         * @return 크기가 다르거나 타일셋에 없는 브러시를 칠한 레이어면 false 이고, 막힘은 바뀌지 않습니다.
         */
        [[nodiscard]] bool addSolidLayer( const TileMapXMLData& layer, const TileSetAsset& tileSet );

        /** @brief Resource 상대 XML 로 타일맵을 저장합니다. */
        [[nodiscard]] bool saveToXML( string_view assetRelativePath ) const;
        /** @brief 맵 데이터를 비웁니다. */
        void clear();
        /** @brief 맵 크기를 변경합니다. */
        void resize( int32 width, int32 height );

        /** @brief 맵 너비(타일 수)를 반환합니다. */
        int32 getWidth() const { return _data._width; }
        /** @brief 맵 높이(타일 수)를 반환합니다. */
        int32 getHeight() const { return _data._height; }
        /** @brief 맵 표시 이름을 반환합니다. */
        const string& getName() const { return _data._name; }
        /** @brief 맵 표시 이름을 설정합니다. */
        void setName( string_view name ) { _data._name = name; }
        /** @brief 원본 XML 경로를 반환합니다. */
        const string& getSourcePath() const { return _data._sourcePath; }
        /** @brief 이 맵에 해당하는 씬 경로를 반환합니다. */
        const string& getScenePath() const { return _data._scenePath; }
        /** @brief 이 맵에 해당하는 씬 경로를 설정합니다. */
        void setScenePath( string_view path ) { _data._scenePath = path; }
        /** @brief 존 태그 문자열(`<role>`)을 반환합니다. 쉼표나 공백으로 나눈 태그이며 `ZoneTracker::setFromMap` 이 읽습니다. */
        const string& getRole() const { return _data._role; }
        /** @brief 존 태그 문자열을 설정합니다. */
        void setRole( string_view role ) { _data._role = role; }
        /** @brief 기본 스폰 X를 반환합니다. */
        int32 getSpawnX() const { return _data._spawnX; }
        /** @brief 기본 스폰 Y를 반환합니다. */
        int32 getSpawnY() const { return _data._spawnY; }
        /** @brief 기본 스폰 좌표를 설정합니다. */
        void setSpawn( int32 x, int32 y )
        {
            _data._spawnX = x;
            _data._spawnY = y;
        }

        /** @brief 그 칸에서 레이어가 켜져 있는지 반환합니다. 맵 밖이면 false 입니다. */
        bool isFlagSet( TileFlagLayer layer, int32 x, int32 y ) const;
        /** @brief 보행 가능 여부를 반환합니다. `Walkable` 플래그가 켜져 있고 어느 타일 레이어의 단단한 브러시도 칠해지지 않은 칸입니다. */
        bool isWalkable( int32 x, int32 y ) const;
        /** @brief 조우 타일 여부를 반환합니다. */
        bool isEncounterTile( int32 x, int32 y ) const;
        /** @brief 통과 타일 여부를 반환합니다. */
        bool isPassThrough( int32 x, int32 y ) const;
        /** @brief 솔리드 타일 여부를 반환합니다. 보행할 수 없거나 맵 밖이면 true 입니다. */
        bool isSolid( int32 x, int32 y ) const;
        /** @brief 해당 좌표의 워프를 찾습니다. */
        const TileWarp* findWarp( int32 x, int32 y ) const;
        /** @brief 타일 비주얼을 반환합니다. */
        OverworldTileVisual getTileVisual( int32 x, int32 y ) const;

        /** @brief 그 칸의 레이어 값을 설정합니다. 맵 밖이면 무시합니다. */
        void setFlag( TileFlagLayer layer, int32 x, int32 y, bool bSet );
        /** @brief 보행 가능 여부를 설정합니다. */
        void setWalkable( int32 x, int32 y, bool bWalkable );
        /** @brief 조우 타일 여부를 설정합니다. */
        void setEncounter( int32 x, int32 y, bool bEncounter );
        /** @brief 통과 타일 여부를 설정합니다. */
        void setPassThrough( int32 x, int32 y, bool bPassThrough );
        /** @brief 타일 비주얼을 설정합니다. */
        void setTileVisual( int32 x, int32 y, const OverworldTileVisual& visual );
        /** @brief 워프를 추가하거나 갱신합니다. */
        void setOrUpdateWarp( const TileWarp& warp );
        /** @brief 워프를 제거합니다. */
        void removeWarp( int32 x, int32 y );
        /** @brief 맵 가장자리에 워프 프리셋을 칠합니다. edge 는 0이 북, 1이 동, 2가 남, 3이 서쪽입니다. */
        void paintEdgeWarpPreset( int32 edge /*0=N,1=E,2=S,3=W*/, string_view targetMap, int32 tx, int32 ty );

        /** @brief HD-2D 타일 디버그 로그를 남깁니다. */
        void debugLogTileHd2d( int32 x, int32 y ) const;

        /**
         * @brief 맵의 셀 격자(셀 번호 · 경계)를 반환합니다. 크기의 원본은 파일 스키마 `_data` 이고, 이것은 그 값으로 만든 뷰입니다.
         * @details 셀 수는 `TileMapXMLData::kMaxTileCount` 로 막혀 있어 int32 셀 번호가 넘치지 않습니다.
         */
        GridTopology getTopology() const { return GridTopology{ _data._width, _data._height }; }

    private:
        uint64 getWarpKey( int32 x, int32 y ) const { return ( static_cast<uint64>( static_cast<uint32>( x ) ) << 32 ) | static_cast<uint32>( y ); }
        void   rebuildWarpIndex();

        TileMapXMLData                        _data;          ///< 맵 데이터. 파일 스키마 그대로 보관한다
        vector<uint8>                         _listTileSolid; ///< 셀마다 단단한 브러시가 칠해졌으면 1 이다(`addSolidLayer`). 저장하지 않는다
        mutable unordered_map<uint64, size_t> _mapWarpIndex;
    };

} // namespace sw
