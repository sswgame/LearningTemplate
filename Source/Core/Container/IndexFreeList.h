/**
 * @file IndexFreeList.h
 * @brief 인덱스 슬롯을 재사용하는 빈 슬롯 스택(프리리스트) 도우미입니다.
 * @details "빈 슬롯이 있으면 마지막에 반납된 것부터(LIFO) 꺼내고, 없으면 새 인덱스를 늘린다" 를 한 곳에 둡니다.
 *          RHI bindless 인덱스(셰이더에 그대로 넘기는 uint32), 씬 · 공간 자료구조의 노드 슬롯, 게임 키트의 유닛 슬롯이 같이 씁니다.
 *          세대가 든 64비트 핸들이 필요하면 `SlotHandleTable` 을 씁니다.
 *
 *          저장소(등록 컨테이너 · 프리리스트 쌍)는 부르는 쪽이 그대로 소유한 채 참조로 넘깁니다. 그래서 같은 벡터를 직접 읽는
 *          코드(드로우 시점의 레지스트리 조회 등)는 손댈 필요가 없습니다. 잠금은 없습니다 — 스레드 안전은 부르는 쪽 책임입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @brief 프리리스트에 빈 슬롯이 있으면 마지막에 반납된 것을 꺼냅니다.
     * @return 꺼냈으면 true. 비어 있으면 false 이고 outIndex 는 그대로입니다.
     * @details 새 인덱스를 등록 컨테이너 크기가 아닌 다른 것(카운터 · 용량 검사)으로 정하는 쪽이 씁니다.
     */
    [[nodiscard]] inline bool tryPopFreeListIndex( vector<uint32>& listFree, uint32& outIndex )
    {
        if ( listFree.empty() )
            return false;
        outIndex = listFree.back();
        listFree.pop_back();
        return true;
    }

    /**
     * @brief 빈 슬롯을 재사용하거나 새 인덱스(등록 컨테이너의 크기)를 계산만 합니다. 저장은 부르는 쪽이 합니다.
     * @details 등록 값을 만들기 전에 인덱스부터 알아야 하는 경우(인덱스 범위 검사 뒤 분기, 병렬 컨테이너 여럿에 나눠 쓰기)에
     *          allocateFreeListIndex 대신 이것을 씁니다. 등록 컨테이너는 `size()` 만 읽으므로 vector 가 아니어도 됩니다.
     */
    template <typename TContainer>
    uint32 reserveFreeListIndex( const TContainer& listRegistered, vector<uint32>& listFree )
    {
        uint32 index{ 0 };
        if ( tryPopFreeListIndex( listFree, index ) )
            return index;
        return static_cast<uint32>( listRegistered.size() );
    }

    /**
     * @brief 빈 슬롯을 재사용하거나(freeList) 새로 늘려서 value 를 저장하고 인덱스를 반환합니다.
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
     * @brief 범위 안이면 이전 값을 반환하고 슬롯을 clearValue 로 비운 뒤 freeList 에 돌려줍니다.
     * @return 비우기 전의 값. 범위 밖이거나 **이미 비어 있으면** clearValue.
     * @details **이미 빈 슬롯을 다시 반납하면 안 됩니다.** 그러면 프리리스트에 같은 인덱스가 두 번 들어가고, 다음 두 번의
     *          할당이 그 인덱스를 **서로 다른 대상에 발급합니다.** 그래서 거절하고, Debug 에서는 단언으로 알립니다
     *          (Container 는 Log 아래 티어라 로그를 쓰지 않습니다).
     *
     *          가드를 부르는 쪽마다 손으로 적지 않고 여기 한 곳에 두어, 지금 쓰는 곳과 앞으로 쓸 곳이 같이 막히게 합니다.
     * @note `T` 는 `clearValue` 와 `==` 로 비교할 수 있어야 합니다. "비었다" 를 알아야 하기 때문입니다.
     */
    template <typename T>
    T releaseFreeListIndex( vector<T>& listRegistered, vector<uint32>& listFree, uint32 index, T clearValue = T{} )
    {
        if ( index >= listRegistered.size() )
            return clearValue;

        if ( listRegistered[index] == clearValue )
        {
            SW_ASSERT( false && "releaseFreeListIndex: the slot is already free; ignoring the duplicate release" );
            return clearValue;
        }

        T old                 = std::move( listRegistered[index] );
        listRegistered[index] = clearValue;
        listFree.push_back( index );
        return old;
    }
} // namespace sw
