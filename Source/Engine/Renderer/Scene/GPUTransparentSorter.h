/**
 * @file GPUTransparentSorter.h
 * @brief 투명 큐의 그리기 순서(정렬 레이어 키, 깊이, 후보 번호)를 정하는 규칙과 정렬 작업 배열을 가집니다.
 * @details `GPUSceneBuilder` 가 값 멤버로 두고 직접 부릅니다(가상 호출 없음). 키를 채우는 일은 후보를 아는 빌더가 하고,
 *          여기는 비교 규칙, 정렬 전략(삽입 정렬 → std::sort), 추가 뷰의 순번 테이블을 맡습니다. GPU 도 씬도 모릅니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Math/MathUtil.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"

namespace sw
{
    /**
     * @struct GPUTransparentSortKey
     * @brief 투명 정렬 키(정렬 레이어 키, 깊이, 후보 인덱스)입니다. 정렬 전에 한 번 계산해 둡니다.
     * @details 깊이는 정렬 축이 영벡터면 카메라까지의 거리², 아니면 그 축 위의 깊이(카메라 기준)입니다. 둘 다 클수록 먼저 그립니다.
     */
    struct GPUTransparentSortKey
    {
        uint32  _sortKey{ 0 };
        float32 _depth{ 0.0f };
        uint32  _candidateIndex{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class GPUTransparentSorter
     * @brief 투명 정렬의 규칙과 작업 배열입니다. 게임 스레드 전용입니다.
     */
    class SW_API GPUTransparentSorter
    {
    public:
        /**
         * @brief 투명 정렬이 삽입 정렬로 버틸 수 있는 이동 예산입니다(원소당 이동 횟수).
         * @details 한 프레임에 조금씩 움직이는 씬은 뒤집힌 쌍이 원소 수보다 훨씬 적습니다. 이 예산을 넘는 프레임(카메라가 크게
         *          돌았거나 나누기를 다시 했을 때)은 삽입 정렬을 멈추고 std::sort 가 마저 합니다. 최악의 경우 N log N 에 예산만큼 더한 값입니다.
         */
        static constexpr size_t kTransparentInsertionMovesPerElement = 8;

        /**
         * @brief 투명 정렬 순서입니다. 정렬 레이어 키가 작은 것, 깊이가 큰 것(먼 것), 후보 인덱스가 작은 것 순입니다. 전순서입니다.
         * @details 깊이가 같으면 후보 인덱스(등록 순서)로 가르므로 같은 Z 의 스프라이트가 카메라를 따라 앞뒤가 뒤집히지 않습니다.
         *          정렬 루프가 원소마다 부르므로 헤더에 둡니다(인라인).
         */
        static bool isDrawnBefore( const GPUTransparentSortKey& keyA, const GPUTransparentSortKey& keyB )
        {
            if ( keyA._sortKey != keyB._sortKey )
                return keyA._sortKey < keyB._sortKey;
            if ( keyA._depth != keyB._depth )
                return keyA._depth > keyB._depth;
            return keyA._candidateIndex < keyB._candidateIndex;
        }
        /**
         * @brief 메시 투명 배치(첫 인스턴스의 키)를 입자 그리기(방출기 위치의 키)보다 먼저 그리면 true 입니다. 레이어 키와 깊이가 같으면 배치가 먼저입니다.
         * @details 두 키의 후보 번호는 서로 다른 목록의 번호라 비교하지 않는다. 그래서 같은 레이어 · 깊이의 순서는 "메시 먼저" 로 고정한다(결정적).
         */
        static bool isBatchDrawnBeforeParticle( const GPUTransparentSortKey& batchKey, const GPUTransparentSortKey& particleKey )
        {
            if ( batchKey._sortKey != particleKey._sortKey )
                return batchKey._sortKey < particleKey._sortKey;
            return batchKey._depth >= particleKey._depth;
        }
        /**
         * @brief 입자 그리기마다 투명 배치 순서 안의 자리를 정합니다(메시 투명과 입자의 교차 정렬).
         * @param listBatchHeadKey 그리는 순서로 놓인 배치마다 가장 먼저 그리는 인스턴스의 키. 순서가 `isDrawnBefore` 와 맞아야 한다(정렬된 꼬리를 자른 배치라 그렇다).
         *                         키를 모르는 배치(꼬리 밖)는 `kNoBatchHeadLayer` 레이어로 둔다 — 모든 입자가 그 앞에 선다.
         * @param listParticleKey  `isDrawnBefore` 로 정렬된 입자 키. 후보 번호 칸이 입자 그리기 번호다.
         * @param outListSlot      입자 키 순서의 자리(앞 내용은 지운다)
         */
        static void placeParticles( vector_reference<const GPUTransparentSortKey> listBatchHeadKey, vector_reference<const GPUTransparentSortKey> listParticleKey,
                                    vector<GPUTransparentParticleSlot>& outListSlot );
        /** @brief 첫 인스턴스의 키를 모르는 배치에 주는 레이어 키입니다. 어떤 입자보다도 뒤에 그린다. */
        static constexpr uint32 kNoBatchHeadLayer = 0xFFFFFFFFu;
        /**
         * @brief 투명 인스턴스 하나의 정렬 키를 채웁니다. 주 뷰(`GPUSceneBuilder::sortTransparent`)와 추가 뷰가 같은 규칙을 씁니다.
         * @details 키를 채우는 루프가 원소마다 부르므로 헤더에 둡니다(인라인).
         */
        static GPUTransparentSortKey makeTransparentSortKey( uint32 sortLayer, const float3& center, const float3& eye, const float3& axis, uint32 orderIndex )
        {
            GPUTransparentSortKey key;
            key._sortKey        = sortLayer;
            key._depth          = axis.getLengthSquared() > MathUtil::kEpsilon ? ( center - eye ).dot( axis ) : float3::getDistanceSquared( center, eye );
            key._candidateIndex = orderIndex;
            return key;
        }

        /**
         * @brief 투명 정렬의 깊이를 측정할 축입니다. 영벡터면 카메라까지의 거리를 씁니다(`GPUSceneBuilder::setTransparentSortAxis`).
         */
        void setAxis( const float3& axis ) { _axis = axis; }
        /** @brief 투명 정렬의 깊이 축입니다. 영벡터면 거리를 씁니다. */
        const float3& getAxis() const { return _axis; }

        /**
         * @brief 주 뷰 정렬의 키 배열을 count 개로 맞추고 돌려줍니다. 부르는 쪽이 원소마다 키를 채운 뒤 `sortKeys` 를 부릅니다.
         * @details 배열은 프레임을 넘어 재사용합니다(프레임당 할당이 없습니다).
         */
        vector<GPUTransparentSortKey>& resizeKeys( size_t count );
        /**
         * @brief 채워 둔 키를 정렬하고, 정렬된 후보 인덱스를 outListCandidateIndex 의 앞 (키 수)개에 씁니다.
         * @details **키 배열의 지금 순서에서 출발합니다.** 지난 프레임 순서로 키를 채웠다면 삽입 정렬은 (원소 수 + 뒤집힌 쌍 수)에 비례합니다.
         *          이동 횟수가 원소당 `kTransparentInsertionMovesPerElement` 를 넘으면 std::sort 로 넘어갑니다. 순서는 전순서이므로 어느 쪽이든 결과가 같습니다.
         *          outListCandidateIndex 는 키 수 이상의 크기여야 합니다(재할당하지 않습니다).
         */
        void sortKeys( vector<uint32>& outListCandidateIndex );

        /** @brief 추가 뷰 정렬의 키 배열을 count 개로 맞추고 돌려줍니다. 뷰마다 다시 씁니다. 키의 후보 인덱스 필드에는 꼬리 번호를 넣습니다. */
        vector<GPUTransparentSortKey>& resizeViewKeys( size_t count );
        /**
         * @brief 채워 둔 추가 뷰 키(꼬리 번호 순)로 그 뷰의 투명 순서를 만듭니다.
         * @details 꼬리 전체를 정렬해 꼬리 번호마다 순번을 매기고, 배치 안을 순번 순으로 다시 놓은 꼬리 슬롯(컬링 없는 백엔드의 인스턴스 슬롯 스트림)과
         *          배치 순서(배치마다 가장 먼저 그릴 인스턴스의 순번 순, 같으면 원래 순서)를 만듭니다. 꼬리 안에 온전히 들지 않는 배치는 순서만 따릅니다.
         *          @p listParticleKey(이 뷰의 시점으로 정렬한 입자 키)는 배치마다 가장 먼저 그릴 인스턴스의 키와 비교해 자리를 받습니다(`placeParticles`).
         */
        GPUViewTransparentOrder computeViewOrder( uint64 viewID, uint32 tailBase, vector_reference<const GPUMeshBatch> listBatch,
                                                  vector_reference<const GPUTransparentSortKey> listParticleKey );

    private:
        /**
         * @brief 주 뷰 투명 정렬의 작업 배열입니다.
         * @details 비교 함수가 원소마다 raw 에서 바운드를 읽어 거리를 **다시** 구하면 투명 2000개에 비교 22000번,
         *          무작위 읽기 44000번이 발생합니다. 그 raw 는 다른 코어가 방금 쓴 것이라 원격 캐시에서 가져옵니다. 키를 한 번 계산해 두면 읽기는 2000번이고, 정렬은 12바이트 연속 배열 위에서 돕니다.
         */
        vector<GPUTransparentSortKey> _listKey;
        /// @brief 추가 뷰 정렬의 임시 배열입니다. 뷰마다 다시 씁니다.
        vector<GPUTransparentSortKey> _listViewKey;
        /// @brief 추가 뷰의 배치 순서마다 가장 먼저 그리는 인스턴스의 키입니다(입자 자리 정하기). 뷰마다 다시 씁니다.
        vector<GPUTransparentSortKey> _listViewBatchHeadKey;
        /** @brief 투명 정렬의 깊이 축입니다. 영벡터면 카메라까지의 거리를 씁니다. */
        float3 _axis{};
    };
} // namespace sw
