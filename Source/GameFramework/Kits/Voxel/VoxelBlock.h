/**
 * @file VoxelBlock.h
 * @brief 블록 종류 — 면마다의 아틀라스 칸 · 색 · 단단함 · 통과 여부입니다(blocks.xml).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 월드에 들어가는 블록 번호입니다. 0 은 공기, 나머지는 카탈로그 순서(1 부터)입니다. */
    using VoxelBlockIndex = uint8;

    /** @brief 공기 블록 번호입니다. */
    constexpr VoxelBlockIndex kVoxelAirBlock = 0;

    /** @brief 블록 면입니다. 순서가 곧 `VoxelBlockDef::_arrFaceTile` 의 칸입니다. */
    enum class VoxelFace : uint8
    {
        PositiveX = 0,
        NegativeX,
        PositiveY, ///< 윗면
        NegativeY, ///< 아랫면
        PositiveZ,
        NegativeZ
    };

    /** @brief 면의 개수입니다. */
    constexpr int32 kVoxelFaceCount = 6;

    /** @brief 정수 블록 좌표입니다(월드 단위, 블록 하나 = 1). */
    struct VoxelCoord
    {
        int32 _x{ 0 };
        int32 _y{ 0 };
        int32 _z{ 0 };

        constexpr bool       operator==( const VoxelCoord& other ) const { return _x == other._x && _y == other._y && _z == other._z; }
        constexpr bool       operator!=( const VoxelCoord& other ) const { return ( *this == other ) == false; }
        constexpr VoxelCoord operator+( const VoxelCoord& other ) const { return VoxelCoord{ _x + other._x, _y + other._y, _z + other._z }; }
    };

    /** @brief 면이 바라보는 방향(단위 정수 벡터)입니다. */
    SW_GF_API VoxelCoord getVoxelFaceOffset( VoxelFace face );

    /**
     * @brief 블록 한 종입니다.
     * @details 면마다 아틀라스의 칸 번호를 따로 가집니다(풀 블록은 윗면이 풀, 옆면이 풀 섞인 흙, 아랫면이 흙). 색은 정점 색으로 곱해지는 틴트입니다 —
     *          같은 회색 칸을 돌 · 자갈에 다른 색으로 나눠 쓸 수 있습니다. `_bOpaque` 가 아니면 이웃 면을 가리지 않습니다(잎 · 유리).
     */
    struct VoxelBlockDef
    {
        hashed_string   _id{};
        string          _name{};
        VoxelBlockIndex _index{ kVoxelAirBlock };
        int32           _arrFaceTile[kVoxelFaceCount]{ 0, 0, 0, 0, 0, 0 };
        float4          _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float32         _hardness{ 0.5f };   ///< 맨손으로 부수는 데 걸리는 초
        uint8           _bSolid{ SW_TRUE };  ///< 몸이 통과하지 못한다
        uint8           _bOpaque{ SW_TRUE }; ///< 이웃 면을 가린다
        uint8           _bBreakable{ SW_TRUE };
    };

    /**
     * @class VoxelBlockCatalog
     * @brief `<BlockCatalog atlasColumns="8" atlasRows="8"><Block id="grass" name="Grass" tile="2" top="0" bottom="2" side="1" hardness="0.6"
     *        color="1 1 1 1" solid="true" opaque="true" breakable="true"/></BlockCatalog>` 를 읽습니다.
     * @details 블록 번호는 읽은 순서로 1 부터 붙습니다(최대 255). `tile` 은 여섯 면의 기본, `top` · `bottom` · `side` 가 그 면을 덮어씁니다.
     */
    class SW_GF_API VoxelBlockCatalog
    {
    public:
        VoxelBlockCatalog();

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );
        /** @brief 블록을 더하고 번호를 돌려줍니다. 같은 id 는 바꾸고 번호를 유지합니다. 255 개가 넘으면 공기(0)입니다. */
        VoxelBlockIndex addBlock( const VoxelBlockDef& block );

        /** @brief 번호로 찾습니다. 공기 · 모르는 번호는 nullptr 입니다. */
        const VoxelBlockDef* findBlock( VoxelBlockIndex index ) const;
        /** @brief id 로 번호를 찾습니다. 모르면 공기(0)입니다. */
        VoxelBlockIndex findBlockIndex( const hashed_string& blockId ) const;
        /** @brief 몸이 통과하지 못하는 블록이면 true 입니다(공기 · 모르는 번호는 false). */
        bool isSolid( VoxelBlockIndex index ) const;
        /** @brief 이웃 면을 가리는 블록이면 true 입니다. */
        bool isOpaque( VoxelBlockIndex index ) const;
        /** @brief 면의 아틀라스 칸 UV 사각형(왼위 · 오른아래)입니다. 칸 가장자리에서 반 텍셀 안으로 들여 이웃 칸이 번지지 않게 합니다. */
        void computeTileUv( int32 tile, float2& outMin, float2& outMax ) const;

        const vector<VoxelBlockDef>& getBlocks() const { return _listBlock; }
        int32                        getAtlasColumns() const { return _atlasColumns; }
        int32                        getAtlasRows() const { return _atlasRows; }
        /** @brief 아틀라스 칸 한 변의 텍셀 수입니다(UV 들여쓰기에 씁니다). */
        int32 getTileTexels() const { return _tileTexels; }

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName );

        vector<VoxelBlockDef> _listBlock; ///< 번호 - 1 자리
        int32                 _atlasColumns;
        int32                 _atlasRows;
        int32                 _tileTexels;
    };
} // namespace sw
