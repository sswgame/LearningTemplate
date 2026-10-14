/**
 * @file TerrainHeightfield.h
 * @brief 지형의 CPU 질의입니다. 월드 높이, 노멀, 구멍, 레이어 가중치를 제공하며, 렌더 메시, 배치, 물리 충돌 메시(`TerrainCollisionUtil`)가 같은 값을 읽습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Environment/Placement/PlacementRule.h"

namespace sw
{
    struct HeightfieldData;

    /**
     * @struct TerrainSampleRect
     * @brief 샘플(또는 스플랫 텍셀) 좌표의 닫힌 사각형입니다(양 끝 포함). 쓰기 API 가 바뀐 범위를 알리는 데 쓰며, 기본값은 빈 사각형입니다.
     */
    struct SW_API TerrainSampleRect
    {
        uint32 _minX{ 1 };
        uint32 _minZ{ 1 };
        uint32 _maxX{ 0 };
        uint32 _maxZ{ 0 };

        bool isEmpty() const { return _minX > _maxX || _minZ > _maxZ; }
        /** @brief 점 (x, z) 를 포함하도록 넓힙니다. */
        void include( uint32 sampleX, uint32 sampleZ );
    };
} // namespace sw

namespace sw
{
    /**
     * @class TerrainHeightfield
     * @brief 높이장을 월드에 놓은 것입니다. 샘플 (0, 0) 이 `_origin` 이고 x 와 z 방향으로 `_size` 만큼 펼쳐집니다(회전과 크기 변환은 없습니다).
     * @details 셀 하나는 대각선 (x, z)-(x+1, z+1) 로 나눈 삼각형 둘이고, 높이 질의는 **렌더 메시(LOD 0)와 같은 삼각형**으로 보간한다.
     *          그래서 물리와 배치가 보는 땅과 그려진 땅이 같다. 노멀은 샘플마다 중앙 차분이고 질의는 그것을 쌍선형으로 섞는다(메시 정점 노멀과 같은 값이다).
     *          레이어 가중치는 스플랫 맵(RGBA8)을 쌍선형으로 읽어 합이 1 이 되게 나눈다. 셰이더(`terrain.hlsl`)와 같은 규칙이다.
     *          스레드: 질의는 읽기만 한다(병렬 틱과 배치 계산이 동시에 읽는다). 쓰기(`write*`, `setSplat`, `initialize`)는 읽는 쪽이 없는 때(틱 밖)에만 부른다 — 락이 없다.
     */
    class SW_API TerrainHeightfield
    {
    public:
        TerrainHeightfield();

        /**
         * @brief 원시 높이장을 월드 값으로 풉니다. 높이 = @p heightMin + 원시 / 65535 × ( @p heightMax − @p heightMin ).
         * @return 데이터가 맞지 않거나 넓이가 0 이면 false 이고 비어 있게 됩니다.
         */
        [[nodiscard]] bool initialize( const HeightfieldData& data, const float3& origin, const float2& size, float32 heightMin, float32 heightMax );
        /** @brief 비웁니다. */
        void shutdown();
        /**
         * @brief 레이어 가중치 맵(RGBA8, 행 우선이며 텍스처의 v 가 지형 z)을 지정합니다. 비우면 레이어 0 이 가득입니다.
         * @details 텍셀 중심이 지형 가장자리에 맞는다. 지형 u ∈ [0, 1] 이 텍셀 좌표 u × (너비 - 1) 이다.
         */
        void setSplat( uint32 width, uint32 height, const vector<uint8>& rgbaBytes );

        /**
         * @brief 높이 샘플 전부(N × N, `getHeightSamples` 와 같은 배치)에서 @p rect 의 샘플만 복사합니다. 에디터 브러시가 사본을 고쳐 바뀐 범위만 넘기는 길입니다.
         * @return 크기가 N × N 이 아니거나 @p rect 가 비었거나 격자를 벗어나면 false 이고 아무것도 바꾸지 않습니다.
         */
        [[nodiscard]] bool writeHeightSamples( const vector<float32>& listHeight, const TerrainSampleRect& rect );
        /**
         * @brief 스플랫 가중치 전부(너비 × 높이, `readSplatWeights` 와 같은 배치)에서 @p rect(텍셀 좌표)의 텍셀만 RGBA8 로 양자화해 씁니다.
         * @return 스플랫이 없거나 크기가 틀리거나 @p rect 가 비었거나 벗어나면 false 이고 아무것도 바꾸지 않습니다.
         */
        [[nodiscard]] bool writeSplatWeights( const vector<float4>& listWeight, const TerrainSampleRect& rect );
        /** @brief 스플랫 텍셀 전부를 가중치(합 1)로 풉니다. 스플랫이 없으면 false 입니다. */
        [[nodiscard]] bool readSplatWeights( vector<float4>& outListWeight ) const;
        /** @brief 지금 높이를 원시 높이장으로 되돌립니다(높이 = `heightMin` + 원시 / 65535 × 범위의 역변환). 구멍 마스크는 그대로 담습니다. */
        void makeHeightfieldData( float32 heightMin, float32 heightMax, HeightfieldData& outData ) const;

        bool          isValid() const { return _resolution >= 2; }
        uint32        getResolution() const { return _resolution; }
        const float3& getOrigin() const { return _origin; }
        const float2& getSize() const { return _size; }
        /** @brief 셀 한 변의 길이(x, z)입니다. */
        const float2& getCellSize() const { return _cellSize; }
        /** @brief 월드 높이 샘플 전부(N × N, 행 우선)입니다. */
        const vector<float32>& getHeightSamples() const { return _listHeight; }
        /** @brief CPU 로 읽은 스플랫 맵의 너비입니다. 없으면 0 입니다. */
        uint32 getSplatWidth() const { return _splatWidth; }
        /** @brief CPU 로 읽은 스플랫 맵의 높이입니다. 없으면 0 입니다. */
        uint32 getSplatHeight() const { return _splatHeight; }
        /** @brief 스플랫 맵 바이트(RGBA8, 행 우선)입니다. */
        const vector<uint8>& getSplatBytes() const { return _splatBytes; }
        /** @brief 셀 구멍 마스크((N-1)², 비었으면 구멍 없음)입니다. */
        const vector<uint8>& getHoleCells() const { return _listHoleCell; }

        /** @brief 샘플 (x, z) 의 월드 높이를 반환합니다. 범위 밖이면 가장자리로 제한합니다. */
        float32 getSampleHeight( int32 sampleX, int32 sampleZ ) const;
        /** @brief 샘플 (x, z) 의 월드 위치를 반환합니다(가장자리로 제한하지 않습니다). */
        float3 getSamplePosition( uint32 sampleX, uint32 sampleZ ) const;
        /** @brief 샘플 (x, z) 의 노멀을 반환합니다(중앙 차분이며, 가장자리는 한쪽 차분입니다). */
        float3 computeSampleNormal( uint32 sampleX, uint32 sampleZ ) const;
        /** @brief 셀 (x, z) 가 구멍이면 true 를 반환합니다. */
        bool isHoleCell( uint32 cellX, uint32 cellZ ) const;

        /** @brief 월드 (x, z) 의 높이입니다. 지형 밖이거나 구멍이면 false 입니다. */
        [[nodiscard]] bool findHeightAt( float32 worldX, float32 worldZ, float32& outHeight ) const;
        /** @brief 월드 (x, z) 의 노멀을 구합니다. 지형 밖이면 false 를 반환합니다(구멍이어도 값은 줍니다). */
        [[nodiscard]] bool findNormalAt( float32 worldX, float32 worldZ, float3& outNormal ) const;
        /** @brief 월드 (x, z) 가 구멍 셀이면 true 를 반환합니다(지형 밖은 false 입니다). */
        bool isHoleAt( float32 worldX, float32 worldZ ) const;
        /** @brief 월드 (x, z) 의 레이어 가중치(합 1)를 반환합니다. 스플랫이 없거나 지형 밖이면 레이어 0 이 1 입니다. */
        float4 computeLayerWeightsAt( float32 worldX, float32 worldZ ) const;
        /** @brief 지형의 정규 좌표 (u, v) ∈ [0, 1]² 의 가중치(합 1)입니다. */
        float4 computeLayerWeightsAtUv( float32 terrainU, float32 terrainV ) const;

        /** @brief 가중치 넷을 합 1 로 나눕니다. 합이 0 이면 레이어 0 이 1 입니다. 셰이더 `normalizeWeights` 와 같은 식입니다. */
        static float4 normalizeWeights( const float4& weight );

    private:
        /** @brief 월드 (x, z) 를 샘플 공간 연속 좌표로 옮깁니다. 지형 밖이면 false 입니다. */
        bool toSampleSpace( float32 worldX, float32 worldZ, float32& outSampleX, float32& outSampleZ ) const;

    private:
        vector<float32> _listHeight;
        vector<uint8>   _listHoleCell;
        vector<uint8>   _splatBytes;
        float3          _origin;
        float2          _size;
        float2          _cellSize;
        uint32          _resolution;
        uint32          _splatWidth;
        uint32          _splatHeight;
    };
} // namespace sw

namespace sw
{
    /**
     * @class TerrainPlacementSurface
     * @brief 지형을 배치 표면으로 쓰는 어댑터입니다. 평면 좌표 (u, v) 가 월드 (x, z) 이며, 구멍은 놓을 수 없는 위치입니다.
     */
    class SW_API TerrainPlacementSurface final : public IPlacementSurface
    {
    public:
        explicit TerrainPlacementSurface( const TerrainHeightfield& heightfield );
        ~TerrainPlacementSurface() override = default;

        [[nodiscard]] bool sampleSurface( const float2& planePosition, PlacementSurfaceSample& outSample ) const override;

    private:
        const TerrainHeightfield& _heightfield;
    };
} // namespace sw
