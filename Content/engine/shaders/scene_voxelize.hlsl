#include "common.hlsli"

/**
 * scene_voxelize.hlsl — 실시간 프로브가 광선을 쏠 점유 복셀을 메시의 GPU 원본 정점에서 칠하는 컴퓨트(RenderPassKind::SceneVoxelize).
 *
 * 한 패스가 두 번 디스패치한다(같은 PSO, 입력 행만 다르다 — 사이에 UAV 배리어).
 *   (1) 지우기: 스레드 하나가 칸 하나다. 다시 칠할 구간의 점유 비트 · 발광 순위를 0 으로 되돌린다.
 *   (2) 칠하기: 스레드 하나가 삼각형 하나다. 노멀의 가장 큰 축으로 투영한 칸(3 축 투영 — 기하 셰이더 없음)을 돌며, 칸 기둥에서 삼각형 평면이 지나는 층만 삼각형-상자 분리축
 *       판정(Akenine-Möller 2001)으로 켠다 — 얇은 면도 빠짐없이 켜는 보수적 복셀화다. 겹치는 칸은 원자 OR, 발광은 순위의 원자 최댓값(더 밝은 쪽이 남는다).
 *
 * 바인딩 계약: 읽기 t0 입력 행(g_VoxelJobs) · t1 위치(float 셋씩 — 렌더 프록시 정점 버퍼의 위치 흐름을 GPU 복사한 것) · t2 인덱스(LOD 0), 쓰기 u0 복셀 단어.
 * 입력 행 배치는 아래 kRow* · kJob*(C++ SceneVoxelizer::planPaint 가 같은 순서로 짠다), 디스패치는 C++ RealtimeProbeUpdater::dispatchVoxelJobs — 스레드가 한 줄 상한을 넘으면 y 축으로 접는다.
 * 식을 고치면 CPU 기준 구현(테스트 SceneVoxelReference.h)도 함께 고친다.
 */

SW_DECLARE_STRUCTURED_BUFFER( float4, g_VoxelJobs, 0 );
SW_DECLARE_STRUCTURED_BUFFER( float, g_VoxelPositions, 1 );
SW_DECLARE_STRUCTURED_BUFFER( uint, g_VoxelIndices, 2 );
SW_DECLARE_RW_STRUCTURED_BUFFER( uint, g_VoxelRW, 0 );

static const uint kRowHeader      = 0u; // asuint( 일 수, 스레드 수, 지우기면 1, 디스패치 한 줄의 스레드 수 )
static const uint kRowFirstJob    = 1u;
static const uint kJobRowCount    = 9u;          // SceneVoxelizer::kJobRowCount
static const uint kJobThreads     = 0u;          // asuint( 첫 스레드, 스레드 수, 점유 단어의 첫 단어, 발광 단어의 첫 단어(없으면 kNoEmission) )
static const uint kJobDimension   = 1u;          // asuint( 격자 칸 수 X, Y, Z, 발광 순위(0 이면 발광 없음) )
static const uint kJobClipFirst   = 2u;          // asuint( 구간 처음 칸 X, Y, Z, 인덱스 버퍼 안의 첫 인덱스 )
static const uint kJobClipLast    = 3u;          // asuint( 구간 끝 칸(포함) X, Y, Z, 위치 버퍼 안의 첫 정점 )
static const uint kJobOrigin      = 4u;          // 격자 시작점 xyz, 칸 크기
static const uint kJobWorld       = 5u;          // 월드 행렬 네 행(행 벡터)
static const uint kNoEmission     = 0xFFFFFFFFu; // SceneVoxelizer::kNoEmission
static const uint kMaxSearchSteps = 32u;

static const float kParallelEpsilon = 1.0e-8f; // 외적 길이의 제곱이 이보다 작으면 모서리가 상자 축과 평행이다
static const float kOverlapSlack    = 1.0e-4f; // 상자를 살짝 키우는 비율 — 면에 정확히 닿는 삼각형이 이웃 칸에서 빠지지 않게 한다
static const float kLayerSlack      = 1.0e-2f; // 평면이 지나는 층을 고를 때의 여유(칸 크기 비율) — 거르는 것은 분리축 판정이다

struct VoxelJob
{
    uint   _firstThread;
    uint   _wordOffset;
    uint   _emissionOffset;
    uint3  _dimension;
    uint   _emissionRank;
    uint3  _clipFirst;
    uint   _firstIndex;
    uint3  _clipLast;
    uint   _firstVertex;
    float3 _origin;
    float  _cellSize;
};

/** @brief 스레드 @p thread 의 일이 시작하는 행입니다 — 첫 스레드가 @p thread 이하인 마지막 일(첫 스레드는 오름차순이다). */
uint findJobRow( uint thread, uint jobCount )
{
    uint low  = 0u;
    uint high = jobCount;
    SW_LOOP for ( uint searchStep = 0u; searchStep < kMaxSearchSteps && high - low > 1u; ++searchStep )
    {
        const uint middle      = ( low + high ) >> 1u;
        const uint firstThread = asuint( g_VoxelJobs[kRowFirstJob + middle * kJobRowCount + kJobThreads].x );
        if ( firstThread <= thread )
            low = middle;
        else
            high = middle;
    }
    return kRowFirstJob + low * kJobRowCount;
}

VoxelJob loadJob( uint row )
{
    const uint4  threads   = asuint( g_VoxelJobs[row + kJobThreads] );
    const uint4  dimension = asuint( g_VoxelJobs[row + kJobDimension] );
    const uint4  clipFirst = asuint( g_VoxelJobs[row + kJobClipFirst] );
    const uint4  clipLast  = asuint( g_VoxelJobs[row + kJobClipLast] );
    const float4 origin    = g_VoxelJobs[row + kJobOrigin];
    VoxelJob     job;
    job._firstThread    = threads.x;
    job._wordOffset     = threads.z;
    job._emissionOffset = threads.w;
    job._dimension      = dimension.xyz;
    job._emissionRank   = dimension.w;
    job._clipFirst      = clipFirst.xyz;
    job._firstIndex     = clipFirst.w;
    job._clipLast       = clipLast.xyz;
    job._firstVertex    = clipLast.w;
    job._origin         = origin.xyz;
    job._cellSize       = origin.w;
    return job;
}

/** @brief 칸 번호입니다(SceneVoxelGrid::computeCellIndex 와 같다). */
uint computeCellIndex( VoxelJob job, uint3 cell )
{
    return ( cell.z * job._dimension.y + cell.y ) * job._dimension.x + cell.x;
}

/** @brief 지우기 — 구간 안의 칸 하나(@p local 번째)를 끄고 발광 순위를 0 으로 되돌립니다. */
void clearCell( VoxelJob job, uint local )
{
    const uint3 size      = job._clipLast - job._clipFirst + uint3( 1u, 1u, 1u );
    const uint3 cell      = job._clipFirst + uint3( local % size.x, ( local / size.x ) % size.y, local / ( size.x * size.y ) );
    const uint  cellIndex = computeCellIndex( job, cell );
    InterlockedAnd( g_VoxelRW[job._wordOffset + ( cellIndex >> 5u )], ~( 1u << ( cellIndex & 31u ) ) );
    if ( job._emissionOffset != kNoEmission )
        InterlockedAnd( g_VoxelRW[job._emissionOffset + cellIndex], 0u );
}

/** @brief 축 @p axis 를 마지막 성분으로 돌린 벡터입니다(축의 차례는 그대로 도는 순환이라 손잡이가 바뀌지 않는다). */
float3 toProjected( float3 value, uint axis )
{
    return axis == 0u ? value.yzx : ( axis == 1u ? value.zxy : value );
}

int3 toProjectedCell( int3 value, uint axis )
{
    return axis == 0u ? value.yzx : ( axis == 1u ? value.zxy : value );
}

/** @brief `toProjectedCell` 의 역입니다. */
uint3 fromProjectedCell( int3 value, uint axis )
{
    return (uint3)( axis == 0u ? value.zxy : ( axis == 1u ? value.yzx : value ) );
}

/** @brief 분리축 @p axis 위에서 삼각형의 투영 구간이 상자 반지름 밖이면 true 입니다. 축의 길이가 거의 0 이면 분리축이 아니다. */
bool isSeparatedOnEdgeAxis( float3 axis, float3 v0, float3 v1, float3 v2, float halfSize )
{
    const float3 projection = float3( dot( v0, axis ), dot( v1, axis ), dot( v2, axis ) );
    const float  radius     = halfSize * ( abs( axis.x ) + abs( axis.y ) + abs( axis.z ) );
    const float  low        = min( projection.x, min( projection.y, projection.z ) );
    const float  high       = max( projection.x, max( projection.y, projection.z ) );
    return dot( axis, axis ) > kParallelEpsilon && ( low > radius || high < -radius );
}

/** @brief 모서리 @p edge 와 상자 축 셋의 외적 가운데 하나라도 분리축이면 true 입니다. */
bool isSeparatedOnEdge( float3 edge, float3 v0, float3 v1, float3 v2, float halfSize )
{
    const bool bAxisX = isSeparatedOnEdgeAxis( float3( 0.0f, -edge.z, edge.y ), v0, v1, v2, halfSize );
    const bool bAxisY = isSeparatedOnEdgeAxis( float3( edge.z, 0.0f, -edge.x ), v0, v1, v2, halfSize );
    const bool bAxisZ = isSeparatedOnEdgeAxis( float3( -edge.y, edge.x, 0.0f ), v0, v1, v2, halfSize );
    return bAxisX || bAxisY || bAxisZ;
}

/** @brief 중심 @p center, 반 변 @p halfSize 인 상자와 삼각형이 겹치는지입니다(분리축 13 개). 이른 반환이 없다(GL 드라이버 · FXC 루프 펼치기). */
bool overlapsTriangleBox( float3 center, float halfSize, float3 a, float3 b, float3 c, float3 normal )
{
    const float3 v0           = a - center;
    const float3 v1           = b - center;
    const float3 v2           = c - center;
    const float3 low          = min( v0, min( v1, v2 ) );
    const float3 high         = max( v0, max( v1, v2 ) );
    const bool   bOutsideBox  = any( low > float3( halfSize, halfSize, halfSize ) ) || any( high < float3( -halfSize, -halfSize, -halfSize ) );
    const float  planeRadius  = halfSize * ( abs( normal.x ) + abs( normal.y ) + abs( normal.z ) );
    const float3 planeOffset  = float3( dot( v0, normal ), dot( v1, normal ), dot( v2, normal ) );
    const float  planeLow     = min( planeOffset.x, min( planeOffset.y, planeOffset.z ) );
    const float  planeHigh    = max( planeOffset.x, max( planeOffset.y, planeOffset.z ) );
    const bool   bBesidePlane = planeLow > planeRadius || planeHigh < -planeRadius;
    const bool   bEdge0       = isSeparatedOnEdge( v1 - v0, v0, v1, v2, halfSize );
    const bool   bEdge1       = isSeparatedOnEdge( v2 - v1, v0, v1, v2, halfSize );
    const bool   bEdge2       = isSeparatedOnEdge( v0 - v2, v0, v1, v2, halfSize );
    return bOutsideBox == false && bBesidePlane == false && bEdge0 == false && bEdge1 == false && bEdge2 == false;
}

/** @brief 삼각형의 모서리 정점 하나를 격자 시작점 기준 위치로 읽습니다. */
float3 loadCorner( VoxelJob job, float4x4 world, uint cornerIndex )
{
    const uint   vertexIndex = job._firstVertex + g_VoxelIndices[job._firstIndex + cornerIndex];
    const float3 position    = float3( g_VoxelPositions[vertexIndex * 3u], g_VoxelPositions[vertexIndex * 3u + 1u], g_VoxelPositions[vertexIndex * 3u + 2u] );
    return mul( float4( position, 1.0f ), world ).xyz - job._origin;
}

/** @brief 칠하기 — 삼각형 하나(@p triangleIndex 번째)와 겹치는 구간 안의 칸을 모두 켭니다. */
void paintTriangle( VoxelJob job, float4x4 world, uint triangleIndex )
{
    const float3 cornerA = loadCorner( job, world, triangleIndex * 3u );
    const float3 cornerB = loadCorner( job, world, triangleIndex * 3u + 1u );
    const float3 cornerC = loadCorner( job, world, triangleIndex * 3u + 2u );

    // 삼각형 상자가 덮는 칸을 구간으로 자른다. 정수로 바꾸기 전에 격자 밖 값을 가둔다(먼 삼각형의 정수 넘침).
    const float3 low       = min( cornerA, min( cornerB, cornerC ) );
    const float3 high      = max( cornerA, max( cornerB, cornerC ) );
    const float3 dimension = (float3)job._dimension;
    const float3 extent    = dimension * job._cellSize;
    const bool   bInGrid   = all( high >= float3( 0.0f, 0.0f, 0.0f ) ) && all( low <= extent );
    const int3   cellFirst = max( (int3)clamp( floor( low / job._cellSize ), float3( 0.0f, 0.0f, 0.0f ), dimension ), (int3)job._clipFirst );
    const int3   cellLast  = min( (int3)clamp( floor( high / job._cellSize ), float3( 0.0f, 0.0f, 0.0f ), dimension - 1.0f ), (int3)job._clipLast );

    // 노멀의 가장 큰 축으로 투영한다 — 기울기가 1 이하라 칸 기둥 하나가 평면과 만나는 층이 몇 개뿐이다. 넓이 0 인 삼각형은 면이 아니다.
    const float3 normal    = cross( cornerB - cornerA, cornerC - cornerB );
    const float3 magnitude = abs( normal );
    const uint   axis      = ( magnitude.x >= magnitude.y && magnitude.x >= magnitude.z ) ? 0u : ( magnitude.y >= magnitude.z ? 1u : 2u );
    const float3 a         = toProjected( cornerA, axis );
    const float3 b         = toProjected( cornerB, axis );
    const float3 c         = toProjected( cornerC, axis );
    const float3 n         = toProjected( normal, axis );
    const int3   first     = toProjectedCell( cellFirst, axis );
    const int3   last      = toProjectedCell( cellLast, axis );
    const float  halfSize  = job._cellSize * 0.5f * ( 1.0f + kOverlapSlack );
    const bool   bPaint    = bInGrid && abs( n.z ) > 0.0f;
    const float2 slope     = bPaint ? n.xy / n.z : float2( 0.0f, 0.0f );
    const float  spread    = halfSize * ( abs( slope.x ) + abs( slope.y ) ) + job._cellSize * kLayerSlack;
    const int2   columnEnd = bPaint ? last.xy : first.xy - int2( 1, 1 );

    SW_LOOP for ( int columnV = first.y; columnV <= columnEnd.y; ++columnV )
    {
        SW_LOOP for ( int columnU = first.x; columnU <= columnEnd.x; ++columnU )
        {
            const float2 columnCenter = ( float2( (float)columnU, (float)columnV ) + 0.5f ) * job._cellSize;
            const float  planeDepth   = a.z - dot( slope, columnCenter - a.xy );
            const int    layerFirst   = (int)clamp( floor( ( planeDepth - spread ) / job._cellSize ), (float)first.z, (float)last.z + 1.0f );
            const int    layerLast    = (int)clamp( floor( ( planeDepth + spread ) / job._cellSize ), (float)first.z - 1.0f, (float)last.z );
            SW_LOOP for ( int layer = layerFirst; layer <= layerLast; ++layer )
            {
                const float3 center = float3( columnCenter, ( (float)layer + 0.5f ) * job._cellSize );
                if ( overlapsTriangleBox( center, halfSize, a, b, c, n ) )
                {
                    const uint cellIndex = computeCellIndex( job, fromProjectedCell( int3( columnU, columnV, layer ), axis ) );
                    InterlockedOr( g_VoxelRW[job._wordOffset + ( cellIndex >> 5u )], 1u << ( cellIndex & 31u ) );
                    if ( job._emissionRank != 0u && job._emissionOffset != kNoEmission )
                        InterlockedMax( g_VoxelRW[job._emissionOffset + cellIndex], job._emissionRank );
                }
            }
        }
    }
}

SW_NUMTHREADS( 64, 1, 1 )
void CSMain( uint3 dispatchID : SV_DispatchThreadID )
{
    const uint4 header = asuint( g_VoxelJobs[kRowHeader] );
    const uint  thread = dispatchID.y * header.w + dispatchID.x;
    // 올림한 남는 스레드는 아무것도 하지 않는다.
    if ( thread < header.y )
    {
        const uint     row = findJobRow( thread, header.x );
        const VoxelJob job = loadJob( row );
        if ( header.z != 0u )
        {
            clearCell( job, thread - job._firstThread );
        }
        else
        {
            const float4x4 world = float4x4( g_VoxelJobs[row + kJobWorld], g_VoxelJobs[row + kJobWorld + 1u], g_VoxelJobs[row + kJobWorld + 2u], g_VoxelJobs[row + kJobWorld + 3u] );
            paintTriangle( job, world, thread - job._firstThread );
        }
    }
}
