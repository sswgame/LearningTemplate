/**
 * @file IndexFreeList.h
 * @brief 반납된 인덱스 슬롯을 다시 내주는 빈 슬롯 스택(프리리스트)입니다. 인덱스 재사용 정책(LIFO · 이중 반납 거절)은 여기 한 곳에 있습니다.
 * @details "빈 슬롯이 있으면 마지막에 반납된 것부터(LIFO) 꺼내고, 없으면 새 인덱스를 늘린다" 를 한 형식에 둡니다.
 *          RHI bindless 인덱스(셰이더에 그대로 넘기는 uint32), 씬 · 공간 자료구조의 노드 슬롯, 게임 키트의 유닛 슬롯, 그리고 세대가 든
 *          핸들 표(`SlotHandleTable`)의 빈 칸이 모두 이것을 씁니다 — 빈 칸 스택을 `vector<uint32>` 로 손으로 다루지 않습니다.
 *
 *          세 갈래 할당의 자리: 다시 쓰는 맨 인덱스는 이 형식, 다시 쓰되 옛 핸들을 가려야 하면 `SlotHandleTable`(이 형식 + 세대),
 *          한 번 내준 값을 다시 쓰면 안 되는 id 는 `IDAllocator`(재사용 없음). 언리얼 `TSparseArray` 의 빈 칸 목록 · `FRHIDescriptorAllocator` 와 같은 자리.
 *
 *          등록 컨테이너(인덱스가 가리키는 값의 배열)는 부르는 쪽이 그대로 소유하고 `allocate` · `release` 에 참조로 넘깁니다. 그래서 같은 벡터를
 *          직접 읽는 코드(드로우 시점의 레지스트리 조회 등)는 손댈 필요가 없습니다. 잠금은 없습니다 — 스레드 안전은 부르는 쪽 책임입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class IndexFreeList
     * @brief 빈 인덱스 슬롯 스택입니다. 마지막에 반납된 것부터 다시 나갑니다.
     */
    class IndexFreeList
    {
    public:
        IndexFreeList()
            : _listIndex{}
        {
        }

        /**
         * @brief 빈 슬롯이 있으면 마지막에 반납된 것을 꺼냅니다.
         * @return 꺼냈으면 true. 비어 있으면 false 이고 outIndex 는 그대로입니다.
         * @details 새 인덱스를 등록 컨테이너 크기가 아닌 다른 것(카운터 · 용량 검사)으로 정하는 쪽이 씁니다.
         */
        [[nodiscard]] bool tryPop( uint32& outIndex )
        {
            if ( _listIndex.empty() )
                return false;
            outIndex = _listIndex.back();
            _listIndex.pop_back();
            return true;
        }

        /**
         * @brief 빈 슬롯을 재사용하거나 새 인덱스(등록 컨테이너의 크기)를 계산만 합니다. 저장은 부르는 쪽이 합니다.
         * @details 등록 값을 만들기 전에 인덱스부터 알아야 하는 경우(인덱스 범위 검사 뒤 분기, 병렬 컨테이너 여럿에 나눠 쓰기)에
         *          `allocate` 대신 이것을 씁니다. 등록 컨테이너는 `size()` 만 읽으므로 vector 가 아니어도 됩니다.
         */
        template <typename TContainer>
        [[nodiscard]] uint32 reserve( const TContainer& listRegistered )
        {
            uint32 index{ 0 };
            if ( tryPop( index ) )
                return index;
            return static_cast<uint32>( listRegistered.size() );
        }

        /** @brief 빈 슬롯을 재사용하거나 새로 늘려서 value 를 저장하고 인덱스를 반환합니다. */
        template <typename T>
        uint32 allocate( vector<T>& listRegistered, T value )
        {
            const uint32 index = reserve( listRegistered );
            if ( index >= listRegistered.size() )
                listRegistered.resize( index + 1 );
            listRegistered[index] = std::move( value );
            return index;
        }

        /**
         * @brief 범위 안이면 이전 값을 반환하고 슬롯을 clearValue 로 비운 뒤 빈 슬롯으로 돌려받습니다.
         * @return 비우기 전의 값. 범위 밖이거나 **이미 비어 있으면** clearValue.
         * @details **이미 빈 슬롯을 다시 반납하면 안 됩니다.** 그러면 스택에 같은 인덱스가 두 번 들어가고, 다음 두 번의
         *          할당이 그 인덱스를 **서로 다른 대상에 발급합니다.** 그래서 거절하고, Debug 에서는 단언으로 알립니다
         *          (Container 는 Log 아래 티어라 로그를 쓰지 않습니다).
         * @note `T` 는 `clearValue` 와 `==` 로 비교할 수 있어야 합니다. "비었다" 를 알아야 하기 때문입니다.
         */
        template <typename T>
        T release( vector<T>& listRegistered, uint32 index, T clearValue = T{} )
        {
            if ( index >= listRegistered.size() )
                return clearValue;

            if ( listRegistered[index] == clearValue )
            {
                SW_ASSERT( false && "IndexFreeList::release: the slot is already free; ignoring the duplicate release" );
                return clearValue;
            }

            T old                 = std::move( listRegistered[index] );
            listRegistered[index] = clearValue;
            _listIndex.push_back( index );
            return old;
        }

        /**
         * @brief 슬롯 하나를 빈 슬롯으로 돌려받습니다.
         * @details "이미 비었는가" 를 등록 값으로 물을 수 없는 표(세대 · 점유 비트를 슬롯에 따로 드는 표, 등록 값이 여럿으로 갈린 표)가
         *          점유를 푼 바로 그 자리에서 부릅니다 — 이중 반납은 그 표의 세대 · 점유 검사가 막습니다. 등록 값으로 물을 수 있으면 `release` 를 씁니다.
         */
        void push( uint32 index ) { _listIndex.push_back( index ); }

        size_t size() const { return _listIndex.size(); }
        bool   empty() const { return _listIndex.empty(); }
        /** @brief 빈 슬롯을 모두 잊습니다(등록 컨테이너를 함께 비우는 쪽이 부릅니다). */
        void clear() { _listIndex.clear(); }

        /** @brief 빈 슬롯 스택(아래가 먼저 반납된 것)입니다. 저장 · 검사가 읽습니다. */
        const vector<uint32>& getIndices() const { return _listIndex; }
        /** @brief 저장본에서 읽은 스택으로 바꿉니다. 등록 컨테이너와 맞는지(범위 · 중복 · 빈 칸)는 부르는 쪽이 먼저 확인합니다. */
        void restore( vector<uint32>&& listIndex ) { _listIndex = std::move( listIndex ); }

    private:
        vector<uint32> _listIndex; ///< 빈 슬롯 인덱스. 끝이 다음에 나갈 것이다(LIFO)
    };
} // namespace sw
