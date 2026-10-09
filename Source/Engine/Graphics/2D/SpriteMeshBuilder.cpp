#include "pch.h"

#include "Engine/Graphics/2D/SpriteMeshBuilder.h"

#include "Core/Common/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Resource/Cache/WeakInternCache.h"

namespace sw
{
    namespace
    {
        struct SpriteMeshBuilderInternal
        {
            /** @brief 한 축의 구간 하나 — 위치 [start, end] 와 그 구간이 읽는 프레임 좌표 [uvStart, uvEnd] 입니다. */
            struct Span
            {
                float32 _start;
                float32 _end;
                float32 _uvStart;
                float32 _uvEnd;
            };

            /**
             * @brief 한 축을 구간으로 나눕니다(가로는 왼쪽 → 오른쪽, 세로는 아래 → 위). 시작 테두리 · 가운데(늘이거나 되풀이) · 끝 테두리 순입니다.
             * @param length      그 축의 월드 길이
             * @param borderStart 시작 쪽 테두리(프레임 비율 = 자연 크기의 월드 길이)
             * @param borderEnd   끝 쪽 테두리
             * @param bFlipUv     좌표가 축과 반대로 자라는가(세로: v 는 위가 0)
             */
            static void makeSpans( float32 length, float32 borderStart, float32 borderEnd, bool bTiled, bool bFlipUv, vector<Span>& outListSpan )
            {
                outListSpan.clear();
                const float32 half = length * 0.5f;
                // 크기가 테두리 합보다 작으면 테두리를 비율대로 줄인다 — 모서리 텍스처는 그대로 다 보인다(유니티와 같음).
                float32       worldStart = borderStart;
                float32       worldEnd   = borderEnd;
                const float32 borderSum  = borderStart + borderEnd;
                if ( borderSum > length && borderSum > 0.0f )
                {
                    const float32 scale = length / borderSum;
                    worldStart *= scale;
                    worldEnd *= scale;
                }

                float32 cursor = -half;
                if ( worldStart > 0.0f )
                {
                    outListSpan.push_back( Span{ cursor, cursor + worldStart, toUv( bFlipUv, 0.0f ), toUv( bFlipUv, borderStart ) } );
                    cursor += worldStart;
                }
                const float32 centerEnd    = half - worldEnd;
                const float32 centerLength = centerEnd - cursor;
                const float32 naturalTile  = 1.0f - borderStart - borderEnd;
                if ( centerLength > 1e-6f )
                {
                    if ( bTiled == false || naturalTile <= 1e-4f )
                    {
                        outListSpan.push_back( Span{ cursor, centerEnd, toUv( bFlipUv, borderStart ), toUv( bFlipUv, 1.0f - borderEnd ) } );
                    }
                    else
                    {
                        // 자연 크기 칸으로 되풀이한다. 마지막 칸은 남은 길이만큼 잘라 UV 도 그만큼만 쓴다. 칸이 상한을 넘으면 칸 크기를 키워 상한에 맞춘다.
                        float32      tileLength = naturalTile;
                        const uint32 tileCount  = static_cast<uint32>( MathUtil::ceil( centerLength / tileLength - 1e-4f ) );
                        if ( tileCount > SpriteMeshBuilder::kMaxTileCountPerAxis )
                            tileLength = centerLength / static_cast<float32>( SpriteMeshBuilder::kMaxTileCountPerAxis );
                        while ( centerEnd - cursor > 1e-6f )
                        {
                            const float32 step     = MathUtil::min( tileLength, centerEnd - cursor );
                            const float32 fraction = step / tileLength;
                            outListSpan.push_back( Span{ cursor, cursor + step, toUv( bFlipUv, borderStart ), toUv( bFlipUv, borderStart + naturalTile * fraction ) } );
                            cursor += step;
                        }
                    }
                }
                cursor = centerEnd;
                if ( worldEnd > 0.0f )
                    outListSpan.push_back( Span{ cursor, half, toUv( bFlipUv, 1.0f - borderEnd ), toUv( bFlipUv, 1.0f ) } );
            }

            /** @brief 프레임 좌표(축 방향으로 자람)를 UV 로 옮깁니다. 세로는 v 가 위에서 0 이라 뒤집습니다. */
            static float32 toUv( bool bFlipUv, float32 frameCoord ) { return bFlipUv ? ( 1.0f - frameCoord ) : frameCoord; }

            static RHIVertex makeVertex( float32 x, float32 y, float32 normalZ, float32 u, float32 v )
            {
                return RHIVertex{
                    { x, y, 0.0f },
                    { 0.0f, 0.0f, normalZ },
                    { u, v },
                    { 1.0f, 1.0f, 1.0f, 1.0f }
                };
            }

            /**
             * @brief 사각형 하나를 삼각형 둘로 붙입니다. @p mirror 가 -1 이면 X 를 거울에 비춘 뒷면입니다.
             * @details 감김은 `createSpriteQuad` 와 같다 — 앞면 (왼아래, 오른위, 오른아래) · (왼아래, 왼위, 오른위). X 를 뒤집으면 감김이 뒤집혀 노멀이 +Z 가 된다.
             */
            static void appendQuad( const Span& column, const Span& row, float32 mirror, vector<RHIVertex>& outListVertex )
            {
                const float32 normalZ = ( mirror > 0.0f ) ? -1.0f : 1.0f;
                const float32 left    = column._start * mirror;
                const float32 right   = column._end * mirror;
                outListVertex.push_back( makeVertex( left, row._start, normalZ, column._uvStart, row._uvStart ) );
                outListVertex.push_back( makeVertex( right, row._end, normalZ, column._uvEnd, row._uvEnd ) );
                outListVertex.push_back( makeVertex( right, row._start, normalZ, column._uvEnd, row._uvStart ) );
                outListVertex.push_back( makeVertex( left, row._start, normalZ, column._uvStart, row._uvStart ) );
                outListVertex.push_back( makeVertex( left, row._end, normalZ, column._uvStart, row._uvEnd ) );
                outListVertex.push_back( makeVertex( right, row._end, normalZ, column._uvEnd, row._uvEnd ) );
            }

            /** @brief 메시 표의 키 해시입니다(값의 비트를 섞습니다). */
            struct DescHash
            {
                size_t operator()( const SlicedSpriteDesc& desc ) const noexcept
                {
                    uint32 arrWord[7]{};
                    Memory::copy( &arrWord[0], &desc._size, sizeof( float32 ) * 2 );
                    Memory::copy( &arrWord[2], &desc._border, sizeof( float32 ) * 4 );
                    arrWord[6]  = desc._bTiled;
                    uint64 hash = HashUtil::kFnvOffset64;
                    for ( const uint32 word : arrWord )
                    {
                        hash = ( hash ^ word ) * HashUtil::kFnvPrime64;
                    }
                    return static_cast<size_t>( hash );
                }
            };

            using SlicedMeshCache = WeakInternCache<SlicedSpriteDesc, Mesh, DescHash>;

            /** @brief 값의 정점을 지어 메시 하나로 만듭니다. 정점이 없으면(크기 0) nullptr 입니다. */
            static shared_ptr<Mesh> createSlicedMesh( const SlicedSpriteDesc& desc )
            {
                vector<RHIVertex> listVertex;
                SpriteMeshBuilder::buildSlicedVertices( desc, listVertex );
                if ( listVertex.empty() )
                    return {};
                shared_ptr<Mesh> mesh = Mesh::create();
                mesh->setVertices( std::move( listVertex ) );
                return mesh;
            }

            /** @brief 9-슬라이스 · 타일 메시 표입니다. 사라진 칸은 새 칸을 넣을 때 걷는다 — 크기를 끌어 바꾸는 편집은 크기마다 칸을 남긴다. */
            static SlicedMeshCache& getSlicedMeshCache()
            {
                static SlicedMeshCache s_cache{ "SlicedSpriteMesh" };
                return s_cache;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool SlicedSpriteDesc::operator==( const SlicedSpriteDesc& other ) const
    {
        return Memory::compare( &_size, &other._size, sizeof( _size ) ) == 0 && Memory::compare( &_border, &other._border, sizeof( _border ) ) == 0 &&
               _bTiled == other._bTiled;
    }

    void SpriteMeshBuilder::buildSlicedVertices( const SlicedSpriteDesc& desc, vector<RHIVertex>& outListVertex )
    {
        using Internal = SpriteMeshBuilderInternal;
        outListVertex.clear();
        const float32 width  = MathUtil::max( desc._size._x, 0.0f );
        const float32 height = MathUtil::max( desc._size._y, 0.0f );
        if ( width <= 0.0f || height <= 0.0f )
            return;

        // 테두리는 0..1 로 묶고, 마주 보는 둘의 합이 1 을 넘으면 비율대로 줄인다(가운데가 음수가 되지 않게).
        float32 left   = MathUtil::saturate( desc._border._x );
        float32 bottom = MathUtil::saturate( desc._border._y );
        float32 right  = MathUtil::saturate( desc._border._z );
        float32 top    = MathUtil::saturate( desc._border._w );
        if ( left + right > 1.0f )
        {
            const float32 scale = 1.0f / ( left + right );
            left *= scale;
            right *= scale;
        }
        if ( bottom + top > 1.0f )
        {
            const float32 scale = 1.0f / ( bottom + top );
            bottom *= scale;
            top *= scale;
        }

        vector<Internal::Span> listColumn;
        vector<Internal::Span> listRow;
        Internal::makeSpans( width, left, right, desc._bTiled != SW_FALSE, false, listColumn );
        // 세로는 아래 → 위로 자라고 v 는 위가 0 이라 좌표를 뒤집는다. 아래 테두리가 시작 쪽이다.
        Internal::makeSpans( height, bottom, top, desc._bTiled != SW_FALSE, true, listRow );

        outListVertex.reserve( listColumn.size() * listRow.size() * 12 );
        const float32 arrMirror[2] = { 1.0f, -1.0f };
        for ( const float32 mirror : arrMirror )
        {
            for ( const Internal::Span& row : listRow )
            {
                for ( const Internal::Span& column : listColumn )
                {
                    Internal::appendQuad( column, row, mirror, outListVertex );
                }
            }
        }
    }

    shared_ptr<Mesh> SpriteMeshBuilder::acquireSlicedMesh( const SlicedSpriteDesc& desc )
    {
        SW_MEMORY_SCOPE( Mesh );
        return SpriteMeshBuilderInternal::getSlicedMeshCache().acquire( desc, &SpriteMeshBuilderInternal::createSlicedMesh );
    }

    IAssetCache& SpriteMeshBuilder::getSlicedMeshCache()
    {
        return SpriteMeshBuilderInternal::getSlicedMeshCache();
    }
} // namespace sw
