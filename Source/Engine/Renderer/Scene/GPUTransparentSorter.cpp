#include "pch.h"

#include "Engine/Renderer/Scene/GPUTransparentSorter.h"

namespace sw
{
    vector<GPUTransparentSortKey>& GPUTransparentSorter::resizeKeys( size_t count )
    {
        _listKey.resize( count );
        return _listKey;
    }

    void GPUTransparentSorter::sortKeys( vector<uint32>& outListCandidateIndex )
    {
        const size_t transparentCount = _listKey.size();
        // 레이어 키, 먼 것, 후보 인덱스 순서다. 정렬이 결정적이어야 "순서가 그대로" 판정이 흔들리지 않는다.
        const auto isFartherFirst = &GPUTransparentSorter::isDrawnBefore;

        // **지난 프레임 순서에서 출발한다.** 나누기를 다시 하지 않은 프레임이면 키가 지난 정렬 결과 순서로 채워져,
        // 한 프레임에 조금씩 움직인 물체들은 거의 정렬되어 있다. 삽입 정렬은 (원소 수 + 뒤집힌 쌍 수)에 비례한다.
        // std::sort 는 입력이 거의 정렬되어 있어도 N log N 을 다 치른다. 이동 횟수가 예산을 넘으면(카메라가 크게 돌았거나
        // 나누기를 다시 해 후보 순서로 돌아갔을 때) std::sort 로 넘긴다. 순서는 전순서(거리, 후보 인덱스)라 **어느 쪽으로
        // 정렬해도 결과가 같다**. 반쯤 삽입 정렬된 배열을 넘겨받아도 std::sort 의 결과는 그대로다.
        const size_t moveBudget = transparentCount * kTransparentInsertionMovesPerElement;
        size_t       moveCount  = 0;
        for ( size_t sortIndex = 1; sortIndex < transparentCount && moveCount <= moveBudget; ++sortIndex )
        {
            if ( isFartherFirst( _listKey[sortIndex], _listKey[sortIndex - 1] ) == false )
                continue;
            const GPUTransparentSortKey key         = _listKey[sortIndex];
            size_t                      insertIndex = sortIndex;
            do
            {
                _listKey[insertIndex] = _listKey[insertIndex - 1];
                --insertIndex;
            } while ( insertIndex > 0 && isFartherFirst( key, _listKey[insertIndex - 1] ) );
            _listKey[insertIndex] = key;
            moveCount += sortIndex - insertIndex;
        }
        if ( moveCount > moveBudget )
            std::sort( _listKey.begin(), _listKey.end(), isFartherFirst );
        for ( size_t sortIndex = 0; sortIndex < transparentCount; ++sortIndex )
        {
            outListCandidateIndex[sortIndex] = _listKey[sortIndex]._candidateIndex;
        }
    }

    vector<GPUTransparentSortKey>& GPUTransparentSorter::resizeViewKeys( size_t count )
    {
        _listViewKey.resize( count );
        return _listViewKey;
    }

    GPUViewTransparentOrder GPUTransparentSorter::computeViewOrder( uint64 viewID, uint32 tailBase, const vector<GPUMeshBatch>& listBatch )
    {
        const uint32 tailCount = static_cast<uint32>( _listViewKey.size() );
        // 1) 이 뷰의 시점으로 꼬리 전체를 정렬한다. 같은 깊이는 주 순서(꼬리 번호)로 가른다. 전순서라 결정적이다.
        std::sort( _listViewKey.begin(), _listViewKey.end(), &GPUTransparentSorter::isDrawnBefore );

        GPUViewTransparentOrder order;
        order._viewID                       = viewID;
        order._tailBase                     = tailBase;
        shared_ptr<vector<uint32>> listRank = make_shared<vector<uint32>>( tailCount );
        for ( uint32 rank = 0; rank < tailCount; ++rank )
        {
            ( *listRank )[_listViewKey[rank]._candidateIndex] = rank;
        }

        // 2) 배치 안을 rank 순으로 다시 배치한 꼬리 슬롯(컬링 없는 백엔드의 인스턴스 슬롯 스트림)과, 배치마다 가장 먼저 그릴 인스턴스의 rank.
        shared_ptr<vector<uint32>> listTailSlot = make_shared<vector<uint32>>( tailCount );
        for ( uint32 tailIndex = 0; tailIndex < tailCount; ++tailIndex )
        {
            ( *listTailSlot )[tailIndex] = tailBase + tailIndex;
        }
        vector<uint32> listBatchFirstRank( listBatch.size(), MathUtil::kMaxUInt32 );
        for ( uint32 batchIndex = 0; batchIndex < static_cast<uint32>( listBatch.size() ); ++batchIndex )
        {
            const GPUMeshBatch& batch          = listBatch[batchIndex];
            const bool          bInsideTheTail = tailBase <= batch._instanceBase && batch._instanceBase + batch._instanceCount <= tailBase + tailCount;
            if ( bInsideTheTail == false )
                continue;
            const uint32 begin = batch._instanceBase - tailBase;
            const uint32 end   = begin + batch._instanceCount;
            for ( uint32 tailIndex = begin; tailIndex < end; ++tailIndex )
            {
                listBatchFirstRank[batchIndex] = MathUtil::min( listBatchFirstRank[batchIndex], ( *listRank )[tailIndex] );
            }
            std::sort( listTailSlot->begin() + begin, listTailSlot->begin() + end,
                       [&listRank, tailBase]( uint32 slotA, uint32 slotB )
            { return ( *listRank )[slotA - tailBase] < ( *listRank )[slotB - tailBase]; } );
        }
        order._listBatchOrder.resize( listBatch.size() );
        for ( uint32 batchIndex = 0; batchIndex < static_cast<uint32>( listBatch.size() ); ++batchIndex )
        {
            order._listBatchOrder[batchIndex] = batchIndex;
        }
        std::stable_sort( order._listBatchOrder.begin(), order._listBatchOrder.end(),
                          [&listBatchFirstRank]( uint32 batchA, uint32 batchB )
        { return listBatchFirstRank[batchA] < listBatchFirstRank[batchB]; } );
        order._pListRank     = std::move( listRank );
        order._pListTailSlot = std::move( listTailSlot );
        return order;
    }
} // namespace sw
