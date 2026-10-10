/**
 * @file TileMap.h
 * @brief 오버월드 타일맵 — Engine 타일맵 스키마(`TileMapXMLData`) 위의 칸 조회(걷기 · 조우 칸 · 통과 · 워프)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/TileMap/TileMapXML.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 워프 · HD-2D 비주얼 — 파일 스키마(`TileMapXMLData`)의 타입을 그대로 쓴다
    // ------------------------------------------------------------------------------
    /** @brief 타일 좌표에서 다른 맵으로 보내는 워프입니다. */
    using TileWarp = TileMapXMLData::Warp;
    /** @brief HD-2D 1차: 타일별 가짜 높이 + 틴트입니다(메시 패스 전까지 소프트웨어 · 디버그). */
    using OverworldTileVisual = TileMapXMLData::Visual;

    // ------------------------------------------------------------------------------
    // 2) TileMap — XML 그리드 + 워프 목록
    // ------------------------------------------------------------------------------
    /**
     * @brief HD-2D 오버월드 타일 그리드입니다.
     * @details 맵 데이터는 파일 스키마 `TileMapXMLData` 를 그대로 듭니다 — 플래그 레이어 · 비주얼 · 워프가 필드마다 복사되지 않습니다.
     *          칸 속성은 Engine 레이어 표(`kArrTileFlagLayerInfo`)의 레이어로 묻습니다(`isFlagSet`). 조우가 **일어나는 칸**은 `TileFlagLayer::Encounter` 이고,
     *          무엇을 만나는지는 장르 키트의 지역 표가 정합니다(지역 = 존 id · 태그 — `ZoneTracker`).
     */
    class SW_GF_API TileMap
    {
    public:
        TileMap();

        /** @brief Resource 상대 XML 에서 타일맵을 불러옵니다. */
        [[nodiscard]] bool loadFromXML( string_view assetRelativePath );

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
        /** @brief 대응 씬 경로를 반환합니다. */
        const string& getScenePath() const { return _data._scenePath; }
        /** @brief 대응 씬 경로를 설정합니다. */
        void setScenePath( string_view path ) { _data._scenePath = path; }
        /** @brief 존 태그 글(`<role>` — 쉼표 · 공백으로 나눈 태그, `ZoneTracker::setFromMap` 이 읽는다)을 반환합니다. */
        const string& getRole() const { return _data._role; }
        /** @brief 존 태그 글을 설정합니다. */
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
        /** @brief 보행 가능 여부를 반환합니다. */
        bool isWalkable( int32 x, int32 y ) const;
        /** @brief 조우 타일 여부를 반환합니다. */
        bool isEncounterTile( int32 x, int32 y ) const;
        /** @brief 통과 타일 여부를 반환합니다. */
        bool isPassThrough( int32 x, int32 y ) const;
        /** @brief 솔리드 타일 여부를 반환합니다(보행 불가 · 맵 밖). */
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
        /** @brief 가장자리 워프 프리셋을 칠합니다(0=N, 1=E, 2=S, 3=W). */
        void paintEdgeWarpPreset( int32 edge /*0=N,1=E,2=S,3=W*/, string_view targetMap, int32 tx, int32 ty );

        /** @brief HD-2D 타일 디버그 로그를 남깁니다. */
        void debugLogTileHd2d( int32 x, int32 y ) const;

    private:
        /** @brief 좌표가 맵 범위 안인지 반환합니다. */
        bool isInBounds( int32 x, int32 y ) const;
        /** @brief (x, y) 의 행 우선 1차원 인덱스를 반환합니다. */
        size_t indexOf( int32 x, int32 y ) const
        {
            // 곱셈을 size_t 로 한다. int 로 곱하면 큰 맵에서 넘친 뒤에 확대되므로, 캐스트가 값을
            // 지켜 주는 것처럼 보이지만 이미 틀린 값이다. (좌표 유효성은 isInBounds 가 본다.)
            return static_cast<size_t>( y ) * static_cast<size_t>( _data._width ) + static_cast<size_t>( x );
        }
        uint64 getWarpKey( int32 x, int32 y ) const { return ( static_cast<uint64>( static_cast<uint32>( x ) ) << 32 ) | static_cast<uint32>( y ); }
        void   rebuildWarpIndex();

        TileMapXMLData                        _data; ///< 맵 데이터(파일 스키마 그대로)
        mutable unordered_map<uint64, size_t> _mapWarpIndex;
    };

} // namespace sw
