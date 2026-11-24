/**
 * @file SimulationViewSync.h
 * @brief 규칙(시뮬레이션) 슬롯 → 뷰 오브젝트 동기화입니다. 틱(워커)이 슬롯마다 보일 id 를 알리고, 틱이 끝난 뒤(게임 스레드) 바뀐 슬롯만 지우고 다시 스폰합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"

namespace sw
{
    /**
     * @class SimulationViewSync
     * @brief 슬롯 번호(유닛 · 건물 · 타일 번호)마다 지금 보이는 규칙 id @p TID 와 뷰 오브젝트 하나를 둡니다. `TID{}` 는 "보일 것 없음" 이다.
     * @details 게임마다 따로 들던 "보일 것과 스폰한 것을 비교해 바뀐 슬롯을 쌓고 틱 뒤에 맞추는" 코드(Nile 건물 · 도로, Skirmish 유닛)를 하나로 모은다(감사 K-10).
     *          언리얼 Mass Representation 프로세서 하나가 엔티티 → 액터를 맞추는 자리다. 틱(워커)에서는 `beginCollect` · `show` · `endCollect` 만, 틱 뒤에는 `flush` 만 부른다
     *          (구조 쓰기는 틱 밖 — `scheduleFlush` 계약). 단계 · 색 같은 같은 오브젝트 안의 변화는 뷰 컴포넌트가 맞추고, 여기는 오브젝트를 바꿀 때만 쌓는다.
     */
    template <typename TID>
    class SimulationViewSync
    {
    public:
        /** @brief 슬롯 하나입니다. */
        struct Slot
        {
            TID              _shown{};  ///< 스폰했거나 스폰할 규칙 id(`TID{}` 면 없다). 틱이 쓴다
            GameObjectHandle _object{}; ///< 틱 뒤에만 바뀐다
            uint32           _stamp{ 0 };
        };

        /** @brief 틱: 이번 수집을 시작합니다. 이번에 `show` 하지 않은 슬롯은 `endCollect` 가 비운다. */
        void beginCollect() { ++_stamp; }

        /** @brief 틱: 슬롯 @p index 가 이번에 @p id 로 보여야 합니다. 지금 모습과 다르면 맞출 목록에 쌓는다. */
        void show( size_t index, const TID& id )
        {
            if ( _listSlot.size() <= index )
                _listSlot.resize( index + 1 );
            Slot& slot  = _listSlot[index];
            slot._stamp = _stamp;
            if ( slot._shown == id )
                return;
            slot._shown = id;
            _listPending.push_back( index );
        }

        /** @brief 틱: 이번 수집에서 보이지 않은 슬롯(죽음 · 안개 · 허물림)을 비웁니다. 모든 슬롯을 매번 `show` 하는 쓰는 쪽은 부르지 않아도 된다. */
        void endCollect()
        {
            for ( size_t index = 0; index < _listSlot.size(); ++index )
            {
                Slot& slot = _listSlot[index];
                if ( slot._stamp == _stamp || slot._shown == TID{} )
                    continue;
                slot._shown = TID{};
                _listPending.push_back( index );
            }
        }

        bool hasPending() const { return _listPending.empty() == false; }

        /**
         * @brief 틱 뒤(게임 스레드): 바뀐 슬롯마다 옛 오브젝트를 @p destroy( GameObjectHandle& ) 로 지우고, 보일 id 가 있으면 @p spawn( index, id ) 가 낸 핸들을 둡니다.
         * @details 같은 슬롯이 두 번 쌓였어도(플러시 전에 틱이 여럿) 지우고 다시 스폰하므로 오브젝트는 하나다.
         */
        template <typename TDestroy, typename TSpawn>
        void flush( TDestroy&& destroy, TSpawn&& spawn )
        {
            for ( const size_t index : _listPending )
            {
                Slot& slot = _listSlot[index];
                destroy( slot._object );
                if ( ( slot._shown == TID{} ) == false )
                    slot._object = spawn( index, slot._shown );
            }
            _listPending.clear();
        }

        /** @brief 뷰 오브젝트를 모두 정리한 뒤(핸들이 이미 무효) 슬롯과 쌓인 목록을 비웁니다. 다음 수집이 보일 것을 모두 다시 쌓는다. */
        void clear()
        {
            _listSlot.clear();
            _listPending.clear();
        }

        const vector<Slot>& getSlots() const { return _listSlot; }

    private:
        vector<Slot>   _listSlot;
        vector<size_t> _listPending; ///< 틱 뒤에 맞출 슬롯(틱이 쌓는다)
        uint32         _stamp{ 0 };
    };
} // namespace sw
