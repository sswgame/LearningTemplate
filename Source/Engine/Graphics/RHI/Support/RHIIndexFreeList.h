/**
 * @file RHIIndexFreeList.h
 * @brief GPU에 그대로 넘기는 얕은 uint32 인덱스용 재사용 슬롯 할당 헬퍼 (bindless CB/텍스처/UAV 공용)
 * @details RHIHandleTable<T>(RHIHandleTable.h)는 64비트 generation-팩드 핸들이라 셰이더에 그대로
 *          넘기는 인덱스 용도로는 안 맞는다 — DX11/Vulkan/OpenGL이 각자 손으로 반복해 온
 *          "freeList에서 pop, 없으면 append; 해제 시 슬롯 비우고 freeList에 반환" 알고리즘만
 *          공유한다. 저장소(vector<T>·vector<uint32> 페어)는 백엔드가 그대로 소유한 채 참조로
 *          넘기므로, draw-time에 같은 벡터를 직접 읽는 기존 코드는 손댈 필요가 없다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @brief 빈 슬롯(freeList)을 재사용하거나 새 인덱스를 계산만 합니다 — 저장은 호출 측이 합니다.
     * @details 등록 값을 만들기 전에 인덱스부터 알아야 하는 경우(예: 인덱스 범위 검사 후 분기,
     *          두 병렬 벡터에 나눠 쓰기)에 allocateFreeListIndex 대신 이걸 씁니다.
     */
    template <typename T>
    uint32 resolveFreeListIndex( const vector<T>& listRegistered, vector<uint32>& listFree )
    {
        if ( listFree.empty() == false )
        {
            const uint32 index = listFree.back();
            listFree.pop_back();
            return index;
        }
        return static_cast<uint32>( listRegistered.size() );
    }

    /**
     * @brief 빈 슬롯을 재사용하거나(freeList) 새로 늘려서 value를 저장하고 인덱스를 반환합니다.
     */
    template <typename T>
    uint32 allocateFreeListIndex( vector<T>& listRegistered, vector<uint32>& listFree, T value )
    {
        const uint32 index = resolveFreeListIndex( listRegistered, listFree );
        if ( index >= listRegistered.size() )
            listRegistered.resize( index + 1 );
        listRegistered[index] = std::move( value );
        return index;
    }

    /**
     * @brief 범위 안이면 이전 값을 반환하고 슬롯을 clearValue로 비운 뒤 freeList에 반환합니다.
     *        범위 밖이면 아무 것도 하지 않고 clearValue를 반환합니다.
     */
    template <typename T>
    T releaseFreeListIndex( vector<T>& listRegistered, vector<uint32>& listFree, uint32 index, T clearValue = T{} )
    {
        if ( index >= listRegistered.size() )
            return clearValue;
        T old                 = std::move( listRegistered[index] );
        listRegistered[index] = clearValue;
        listFree.push_back( index );
        return old;
    }
} // namespace sw
