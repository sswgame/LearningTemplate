/**
 * @file SceneVoxelScene.h
 * @brief 실시간 프로브 갱신이 광선을 쏠 장면입니다 — 복셀 격자의 모양과, 그 격자에 칠할 메시 목록입니다(칠한 칸은 GPU 버퍼에만 있다).
 * @details 게임 스레드(`SceneVoxelizer`)가 레지스트리에서 모아 만들고, 렌더 스레드(`RealtimeProbeUpdater`)가 메시의 GPU 원본 정점(렌더 프록시 버퍼의 복사)으로 칠한다 —
 *          CPU 정점을 읽지 않으므로 메시가 CPU 정점을 놓아도 된다(`MeshCPUAccess`, R-15). 만든 뒤에는 바뀌지 않는 공유 값이다(컴포넌트가 캐시로, 렌더 스레드가 지난 장면으로 든다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/AABB.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/IdentityToken.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Lighting/SceneVoxelGrid.h"

namespace sw
{
    class Mesh;

    /** @brief 격자에 칠할 메시 하나입니다(모은 때의 월드 행렬 · 인덱스 구간 · 발광). 레지스트리 메시 칸마다 하나이고, 칠하지 않는 칸은 메시가 비어 있습니다. */
    struct SW_API SceneVoxelSource
    {
        shared_ptr<Mesh> _mesh{};               ///< 칠하지 않는 칸(가리개가 아니거나 격자 밖)은 nullptr
        IdentityToken    _contentID{};          ///< 모은 때의 메시 내용(`Mesh::getContentID`) — 핫 리로드가 제자리에서 바꾼 메시를 가른다
        float4x4         _world{};              ///< 모은 때의 월드 행렬(행 벡터)
        AABB             _box{ AABB::empty() }; ///< 모은 때의 월드 상자
        uint32           _firstIndex{ 0 };      ///< 칠할 삼각형의 첫 인덱스(LOD 0 인덱스 안)
        uint32           _indexCount{ 0 };      ///< 칠할 인덱스 수(3 의 배수)
        uint32           _packedEmission{ 0 };  ///< 자체 발광 복사휘도(RGB9E5 — `PackUtil::packRGB9E5`). 발광하지 않으면 0

        /** @brief 격자에 칠하는 칸이면 true 입니다. */
        bool isPainted() const { return _mesh != nullptr; }
        /** @brief 칠한 결과가 같은 원천이면 true 입니다(메시 내용 · 자리 · 구간 · 발광). */
        bool isSamePaint( const SceneVoxelSource& other ) const;
    };

    /** @brief 움직이는 원천 하나 — 메시 칸과 지금 칠할 값입니다(격자 밖이면 칠하지 않는 원천). */
    struct SceneVoxelMovingSource
    {
        uint32           _slot{ 0 };
        SceneVoxelSource _source{};
    };

    /**
     * @brief 격자의 모양과 칠할 메시들입니다 — 정적 층(한 번도 바뀌지 않은 메시)과 움직이는 층(모은 뒤 움직였거나 모습이 바뀐 메시).
     * @details 정적 층은 버전끼리 나눠 든다(공유 값) — 물체 하나가 움직이는 프레임은 움직이는 목록만 새로 짓고, GPU 도 정적 사본에서 낡은 자리를 되돌린 뒤 움직이는 메시만 다시 칠한다
     *          (언리얼 Lumen 의 카드 캐시 · 가상 그림자 맵의 정적 · 동적 층과 같은 가름). 처음 바뀐 메시가 정적 층에서 빠질 때 한 번만 정적 구간을 다시 칠한다.
     */
    struct SW_API SceneVoxelScene
    {
        SceneVoxelGrid                             _grid{};
        shared_ptr<const vector<SceneVoxelSource>> _pListStaticSource{};      ///< 레지스트리 메시 칸 번호마다(칸이 그 뒤로 늘었으면 짧다). 없으면 nullptr — `getStaticSources` 로 읽는다
        vector<SceneVoxelMovingSource>             _listMovingSource{};       ///< 움직이는 원천들(칸 번호 오름차순)
        uint8                                      _bMovingLayer{ SW_FALSE }; ///< 움직이는 원천이 있었다 — 정적 사본 자리를 잡는다(다시 모을 때까지 켜져 있어 자리가 오가지 않는다)
        /**
         * @brief 이 장면의 서로 다른 발광(RGB9E5)을 어두운 것부터 세운 목록입니다. 발광 칸에는 색이 아니라 순위(목록 번호 + 1, 0 은 발광 없음)를 쓴다.
         * @details 여러 면이 한 칸을 덮으면 더 밝은 쪽이 남는다 — RGB9E5 비트는 밝기 차례가 아니라 원자 최댓값으로 고를 수 없으므로 순위를 쓴다(`InterlockedMax`, scene_voxelize.hlsl).
         *          읽는 쪽(probe_update.hlsl)은 순위로 이 목록(입력 행)을 찾는다.
         */
        vector<uint32> _listEmissionPalette{};

        /** @brief 발광 @p packedEmission 의 순위입니다. 0 이거나 목록에 없으면 0 입니다. */
        uint32 findEmissionRank( uint32 packedEmission ) const;
        /** @brief 정적 층의 원천들입니다(메시 칸 번호마다). */
        const vector<SceneVoxelSource>& getStaticSources() const;
        /** @brief 정적 층을 바꿉니다. */
        void setStaticSources( vector<SceneVoxelSource>&& listSource );
        /** @brief 칠할 메시들의 발광에서 `_listEmissionPalette` 를 다시 만듭니다. */
        void rebuildEmissionPalette();
    };
} // namespace sw
