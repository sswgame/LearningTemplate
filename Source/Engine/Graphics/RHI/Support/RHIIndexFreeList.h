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
#include "Core/Log/Logger.h"

namespace sw
{
    /**
     * @brief 빈 슬롯(freeList)을 재사용하거나 새 인덱스를 계산만 합니다 — 저장은 호출 측이 합니다.
     * @details 등록 값을 만들기 전에 인덱스부터 알아야 하는 경우(예: 인덱스 범위 검사 후 분기,
     *          두 병렬 벡터에 나눠 쓰기)에 allocateFreeListIndex 대신 이걸 씁니다.
     */
    template <typename T>
    uint32 reserveFreeListIndex( const vector<T>& listRegistered, vector<uint32>& listFree )
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
        const uint32 index = reserveFreeListIndex( listRegistered, listFree );
        if ( index >= listRegistered.size() )
            listRegistered.resize( index + 1 );
        listRegistered[index] = std::move( value );
        return index;
    }

    /**
     * @brief 범위 안이면 이전 값을 반환하고 슬롯을 clearValue로 비운 뒤 freeList에 반환합니다.
     * @param pKindName 로그에 쓸 종류 이름("texture" 등). 비면 "resource" 로 적는다.
     * @return 비우기 전의 값. 범위 밖이거나 **이미 비어 있으면** clearValue.
     * @details **이미 빈 슬롯을 다시 반납하면 안 된다.** 그러면 프리리스트에 같은 인덱스가 두 번
     *          들어가고, 다음 두 번의 할당이 그 인덱스를 **서로 다른 리소스에 발급한다.**
     *
     *          이 가드는 원래 부르는 쪽마다 손으로 적혀 있었고 종류마다 모양이 달랐다 —
     *          DX11 · GL 의 buffer/texture 는 가드 + 에러 로그, DX11 의 uav 는 조용한 반환,
     *          **GL 의 uav 는 가드가 아예 없었다.** 그래서 GL 에서만 UAV 이중 해제가 통과했다.
     *          가드를 여기 한 자리에 두면 지금 쓰는 곳과 앞으로 쓸 곳이 같이 막힌다.
     * @note `T` 는 `clearValue` 와 `==` 로 견줄 수 있어야 한다 — "비었다" 를 알아야 하기 때문이다.
     */
    template <typename T>
    T releaseFreeListIndex( vector<T>& listRegistered, vector<uint32>& listFree, uint32 index, T clearValue = T{},
                            const utf8* pKindName = nullptr )
    {
        if ( index >= listRegistered.size() )
            return clearValue;

        if ( listRegistered[index] == clearValue )
        {
            SW_LOG_ERROR( "Bindless %# index %# is already free; ignoring the duplicate release.",
                          pKindName != nullptr ? pKindName : "resource", index );
            return clearValue;
        }

        T old                 = std::move( listRegistered[index] );
        listRegistered[index] = clearValue;
        listFree.push_back( index );
        return old;
    }
} // namespace sw
