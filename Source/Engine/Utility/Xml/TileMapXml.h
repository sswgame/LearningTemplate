/**
 * @file TileMapXml.h
 * @brief 타일맵 XML 문서 스키마 (Engine 소유, Editor/GameFramework 공용)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief TileMap XML 전체 문서 */
    struct TileMapXmlData
    {
        /**
         * @brief 타일맵 하나가 가질 수 있는 최대 칸 수입니다 (2048 x 2048).
         * @details 칸 수는 `_width x _height` 이고, 칸마다 네 배열에 8 바이트가 든다 — 이 상한이
         *          곧 한 맵이 잡을 수 있는 메모리의 상한(약 32 MiB)이다. 크기는 **파일이나 사용자
         *          입력에서** 오는데(XML 의 `<width>`, 에디터의 Width/Height 칸) 둘 다 int32 를
         *          그대로 받으므로, 상한이 없으면 `100000 x 100000` 한 번에 10^10 칸을 잡으려다
         *          죽는다. 실제 맵은 한 변이 수십 칸이라 실사용을 자르지 않는다.
         */
        static constexpr int32 kMaxTileCount = 1 << 22;

        /** @brief 이 크기가 다룰 수 있는 범위인지 여부입니다. 곱은 int64 로 낸다 — int32 로는 넘친다. */
        static bool isSizeSupported( int32 width, int32 height )
        {
            if ( width <= 0 || height <= 0 )
                return false;
            return ( static_cast<int64>( width ) * static_cast<int64>( height ) ) <= static_cast<int64>( kMaxTileCount );
        }

        /** @brief 타일 한 칸의 가짜 높이·틴트·아틀라스 */
        struct Visual
        {
            uint8 _height{ 0 };
            uint8 _tintR{ 180 };
            uint8 _tintG{ 200 };
            uint8 _tintB{ 160 };
            uint8 _atlasId{ 0 };
        };

        /** @brief 타일 좌표에서 다른 맵으로 보내는 워프 */
        struct Warp
        {
            int32  _tileX{ 0 };
            int32  _tileY{ 0 };
            string _targetMap{};
            int32  _targetTileX{ 1 };
            int32  _targetTileY{ 1 };
            string _pairId{};
        };

        /** @brief 맵 조우 테이블의 한 행 */
        struct Encounter
        {
            string  _speciesId{};
            float32 _weight{ 1.0f };
        };

        string            _name{};
        string            _sourcePath{};
        string            _scenePath{};
        string            _role{};
        int32             _width{ 0 };
        int32             _height{ 0 };
        int32             _spawnX{ 1 };
        int32             _spawnY{ 1 };
        vector<uint8>     _listWalkable{};
        vector<uint8>     _listEncounter{};
        vector<uint8>     _listPassThrough{};
        vector<Visual>    _listVisual{};
        vector<Warp>      _listWarp{};
        vector<Encounter> _listEncounterEntry{};

        /** @brief Resource 상대 또는 절대 경로에서 타일맵 XML을 읽습니다. */
        SW_API bool load( string_view path );
        /** @brief XML 본문에서 타일맵을 읽습니다. */
        SW_API bool loadFromXml( string_view xml );
        /** @brief Resource 상대 또는 절대 경로로 타일맵 XML을 씁니다. */
        SW_API bool save( string_view path ) const;
        /**
         * @brief 타일맵 XML 본문을 만듭니다.
         * @details `<t>` 는 언제나 `_width × _height` 개를 적는다. 네 타일 배열이 그보다 짧으면
         *          (크기만 바꾸고 칸을 안 늘린 상태) 모자란 칸은 **읽기 쪽 기본값**으로 적고
         *          경고를 남긴다 — 배열 밖을 읽지 않으면서 왕복이 어긋나지도 않는 쪽이다.
         */
        SW_API string toXml() const;
    };
} // namespace sw
