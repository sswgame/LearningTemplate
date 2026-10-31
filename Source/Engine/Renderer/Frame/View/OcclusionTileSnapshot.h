/**
 * @file OcclusionTileSnapshot.h
 * @brief 렌더러의 가림 타일(지난 프레임 장면 깊이의 타일 최대 깊이)을 CPU 로 되읽은 사본과, 게임 스레드가 그 사본으로 구를 가림 판정하는 함수입니다.
 * @details 카메라의 "보이는가"(`RenderViewCollector`)가 가려짐까지 보게 하는 길이다. GPU 컬링은 가림 타일(`FramePassExecutorOcclusion.cpp`)로 가려진 인스턴스를 거르는데,
 *          게임 스레드는 그 결과를 모른다. 렌더러가 타일을 몇 프레임 늦게 되읽어(`AsyncBufferReadback`) 여기 `OcclusionTileMailbox` 에 올리고,
 *          수집기가 모니터(보임 오브젝트)의 경계 구를 `OcclusionTileQuery::isSphereOccluded` 로 같은 식에 넣는다 — 언리얼 `WasRecentlyRendered` 의 역할이다.
 *          판정 식은 gpu_cull.hlsl 의 `isOccluded` 와 같다(바꾸면 두 곳을 함께 고친다).
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @struct OcclusionTileSnapshot
     * @brief 가림 타일 한 벌과 그것을 읽는 데 필요한 격자 · 뷰-투영입니다. 렌더러가 만들고 게임 스레드가 읽기만 합니다(`shared_ptr<const>` 로 건넨다).
     */
    struct SW_API OcclusionTileSnapshot
    {
        float4x4        _viewProj{};         ///< 타일의 깊이를 그린 프레임의 뷰-투영(행 벡터 규약)
        uint32          _columns{ 0 };       ///< 타일 격자 열
        uint32          _rows{ 0 };          ///< 타일 격자 행
        uint32          _tileSize{ 0 };      ///< 타일 한 변(px)
        float32         _depthWidth{ 0.0f }; ///< 깊이 히스토리 크기(px)
        float32         _depthHeight{ 0.0f };
        uint64          _serial{ 0 };     ///< 이 사본을 요청한 번호(커질수록 최근)
        vector<float32> _listTileDepth{}; ///< 타일마다 최대 깊이, 행 우선(`_columns` x `_rows`)

        /** @brief 격자와 타일 수가 맞아 판정에 쓸 수 있는가. */
        bool isValid() const
        {
            const bool bShapeOk = _columns > 0 && _rows > 0 && _tileSize > 0;
            return bShapeOk && _listTileDepth.size() == static_cast<size_t>( _columns ) * _rows;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct OcclusionTileQuery
     * @brief 사본으로 구가 가려졌는지 묻습니다.
     */
    struct SW_API OcclusionTileQuery
    {
        /// 판정하는 타일 줄의 한 축 상한입니다 — 화면에서 이보다 넓게 덮는 물체는 판정하지 않는다(gpu_cull.hlsl `kMaxOcclusionTestTiles`).
        static constexpr uint32 kMaxTestTiles = 8;
        /// 이보다 작은 클립 w 는 카메라 뒤 · 눈 가까이로 본다(gpu_cull.hlsl `kMinOcclusionClipW`).
        static constexpr float32 kMinClipW = 1.0e-4f;

        /**
         * @brief 월드 구가 사본의 깊이 뒤에 완전히 가려졌으면 true 입니다. 모를 때(경계 상자 모서리가 카메라 뒤 · 화면 밖, 타일을 넓게 덮음, 사본이 쓸 수 없음)는 false(보인다)입니다.
         */
        static bool isSphereOccluded( const OcclusionTileSnapshot& snapshot, const float3& center, float32 radius );
    };
} // namespace sw

namespace sw
{
    /**
     * @class OcclusionTileMailbox
     * @brief 렌더 스레드가 올린 가장 최근 사본을 게임 스레드가 가져가는 우편함입니다. 락은 포인터 하나를 바꾸는 동안만 잡습니다.
     */
    class SW_API OcclusionTileMailbox
    {
    public:
        /** @brief 새 사본으로 바꿉니다. */
        void publish( shared_ptr<const OcclusionTileSnapshot> pSnapshot );
        /** @brief 사본을 비웁니다 — 기록을 버리는 프레임(컷 · 처음)이라 타일이 맞지 않을 때. */
        void clear();
        /** @brief 가장 최근 사본입니다. 없으면 nullptr(가림을 보지 않는다). */
        shared_ptr<const OcclusionTileSnapshot> getLatest() const;

    private:
        mutable mutex                                    _mutex;
        shared_ptr<const OcclusionTileSnapshot> _pLatest SW_GUARDED_BY( _mutex );
    };
} // namespace sw
