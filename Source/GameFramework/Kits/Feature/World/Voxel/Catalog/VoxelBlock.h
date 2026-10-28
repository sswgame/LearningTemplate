/**
 * @file VoxelBlock.h
 * @brief 블록 종류를 정의합니다(blocks.xml). 면마다 아틀라스 타일, 색, 단단함, 통과 여부를 가집니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Base/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 월드에 들어가는 블록 번호입니다. 0 은 공기, 나머지는 카탈로그 순서(1 부터)입니다. */
    using VoxelBlockIndex = uint8;

    /** @brief 공기 블록 번호입니다. */
    constexpr VoxelBlockIndex kVoxelAirBlock = 0;

    /** @brief 블록 면입니다. 순서가 곧 `VoxelBlockDef::_arrFaceTile` 의 인덱스입니다. */
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

    /** @brief 정수 블록 좌표입니다. 월드 단위이며 블록 하나가 1입니다. */
    struct VoxelCoord
    {
        int32 _x{ 0 };
        int32 _y{ 0 };
        int32 _z{ 0 };

        constexpr bool       operator==( const VoxelCoord& other ) const { return _x == other._x && _y == other._y && _z == other._z; }
        constexpr bool       operator!=( const VoxelCoord& other ) const { return ( *this == other ) == false; }
        constexpr VoxelCoord operator+( const VoxelCoord& other ) const { return VoxelCoord{ _x + other._x, _y + other._y, _z + other._z }; }
    };
} // namespace sw

namespace sw
{
    /** @brief 면이 바라보는 방향(단위 정수 벡터)입니다. */
    SW_KIT_API VoxelCoord getVoxelFaceOffset( VoxelFace face );

    /**
     * @brief 블록 한 종입니다.
     * @details 면마다 아틀라스의 타일 번호를 따로 가집니다. 풀 블록은 윗면이 풀, 옆면이 풀 섞인 흙, 아랫면이 흙입니다.
     *          색은 정점 색에 곱해지는 틴트입니다. 그래서 같은 회색 타일을 돌과 자갈에 다른 색으로 나눠 쓸 수 있습니다.
     *          `_bOpaque` 가 아니면 이웃 면을 가리지 않습니다. 잎과 유리가 해당합니다.
     */
    REFLECT()
    struct VoxelBlockDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string          _name{};
        VoxelBlockIndex _index{ kVoxelAirBlock };                          ///< 카탈로그가 붙인다(읽은 순서)
        int32           _arrFaceTile[kVoxelFaceCount]{ 0, 0, 0, 0, 0, 0 }; ///< `tile` 하나 또는 `top` · `bottom` · `side` 셋을 손으로 읽는다
        PROPERTY( Meta = "Color" )
        float4 _color{ 1.0f, 1.0f, 1.0f, 1.0f }; ///< `r,g,b,a` 네 성분을 모두 적는다
        PROPERTY( Min = 0.0 )
        float32 _hardness{ 0.5f }; ///< 맨손으로 부수는 데 걸리는 시간(초)
        PROPERTY()
        bool _bSolid{ true }; ///< 몸이 통과하지 못한다
        PROPERTY()
        bool _bOpaque{ true }; ///< 이웃 면을 가린다
        PROPERTY()
        bool _bBreakable{ true };
        PROPERTY( Name = "drop" )
        hashed_string _dropID{}; ///< 부수면 얻는 블록 id(`drop`). 비면 자기 자신이다. 같은 카탈로그의 블록이어야 한다(읽을 때 본다)
    };
} // namespace sw

namespace sw
{
    /** @brief `<BlockCatalog>` 루트 속성의 스키마입니다. */
    REFLECT()
    struct VoxelBlockCatalogRootDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 1 )
        int32 _atlasColumns{ 8 };
        PROPERTY( Min = 1 )
        int32 _atlasRows{ 8 };
        PROPERTY( Min = 1 )
        int32 _tileTexels{ 16 }; ///< 아틀라스 타일 한 변의 텍셀 수
    };
} // namespace sw

namespace sw
{
    /**
     * @class VoxelBlockCatalog
     * @brief 블록 카탈로그입니다. 아래 형식의 XML 을 읽습니다.
     *        `<BlockCatalog atlasColumns="8" atlasRows="8"><Block id="grass" name="Grass" top="0" bottom="2" side="1" hardness="0.6"
     *        color="1,1,1,1" solid="true" opaque="true" breakable="true" drop="dirt"/><Block id="dirt" name="Dirt" tile="2"/></BlockCatalog>`
     * @details 블록 번호는 읽은 순서대로 1부터 붙으며 최대 255입니다. 면은 `tile` 하나(여섯 면이 같다)로 적거나 `top` · `bottom` · `side` 셋을 모두 적습니다.
     *          둘을 섞거나 셋 중 하나를 빼면 다른 면 값으로 채우지 않고 그 블록을 넣지 않는 오류입니다. 칸 번호는 아틀라스 안이어야 합니다.
     */
    class SW_KIT_API VoxelBlockCatalog : public XMLCatalog<VoxelBlockCatalog>
    {
        friend class XMLCatalog<VoxelBlockCatalog>;

    public:
        /** @brief 데이터 종류 이름입니다(`SW_RESOURCE_DATA_KIND` 등록 줄과 팩 경로 `GamePackDataUtil::tryMakeKindPath` 가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "voxelblocks";

        VoxelBlockCatalog();

        /** @brief 블록을 추가하고 번호를 반환합니다. 같은 id 는 내용을 바꾸고 번호를 유지합니다. 255개를 넘으면 공기(0)를 반환합니다. */
        VoxelBlockIndex addBlock( const VoxelBlockDef& block );

        /** @brief 번호로 찾습니다. 공기이거나 모르는 번호이면 nullptr 입니다. */
        const VoxelBlockDef* findBlock( VoxelBlockIndex index ) const;
        /** @brief id 로 번호를 찾습니다. 모르는 id 이면 공기(0)를 반환합니다. */
        VoxelBlockIndex findBlockIndex( const hashed_string& blockID ) const;
        /** @brief 몸이 통과하지 못하는 블록이면 true 입니다. 공기이거나 모르는 번호이면 false 입니다. */
        bool isSolid( VoxelBlockIndex index ) const;
        /** @brief 이웃 면을 가리는 블록이면 true 입니다. */
        bool isOpaque( VoxelBlockIndex index ) const;
        /** @brief 면의 아틀라스 타일 UV 사각형(왼쪽 위와 오른쪽 아래)입니다. 타일 가장자리에서 반 텍셀 안쪽으로 들여 이웃 타일의 색이 번지지 않게 합니다. */
        void computeTileUv( int32 tile, float2& outMin, float2& outMax ) const;

        const vector<VoxelBlockDef>& getBlocks() const { return _catalog.getAll(); }
        int32                        getAtlasColumns() const { return _atlasColumns; }
        int32                        getAtlasRows() const { return _atlasRows; }
        /** @brief 아틀라스 타일 한 변의 텍셀 수입니다. UV 를 안쪽으로 들일 때 씁니다. */
        int32 getTileTexels() const { return _tileTexels; }

    private:
        static constexpr const utf8* kXMLRootName = "BlockCatalog"; ///< 루트 요소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief `<Block>` 하나를 스키마와 면 칸으로 읽습니다. 오류면 넣지 않고 false 입니다(오류는 이름과 함께 남긴다). */
        [[nodiscard]] bool loadBlock( const XMLNode& node, string_view sourceName );

        GameCatalog<VoxelBlockDef> _catalog; ///< 인덱스가 블록 번호 - 1 이다. id 조회는 해시 한 번으로 끝난다
        int32                      _atlasColumns;
        int32                      _atlasRows;
        int32                      _tileTexels;
    };
} // namespace sw
