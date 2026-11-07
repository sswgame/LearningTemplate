/**
 * @file EditSnapshotHistory.h
 * @brief 편집 모델이 쓰는 스냅샷 되돌리기 기록입니다. 편집 직전 상태를 쌓고, 같은 키의 연속 편집(슬라이더 끌기)은 한 단계로 합칩니다.
 * @details 창마다 편집 대상이 달라 에디터 전역 `CommandStack` 에 섞지 않고 모델이 자기 기록을 가집니다(설계 문서 "Undo 는 창 안에서만 돈다").
 *          스냅샷은 값 타입이어야 하며 복사 비용이 작아야 한다(소켓 표, 외형 선택).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /**
     * @class EditSnapshotHistory
     * @brief 되돌리기 · 다시 하기 스택 한 쌍입니다. 기록은 최대 `kMaxCount` 단계이며 넘으면 가장 오래된 것부터 버립니다.
     */
    template <typename TSnapshot>
    class EditSnapshotHistory
    {
    public:
        static constexpr size_t kMaxCount = 64;

        /** @brief 모든 기록을 지웁니다(문서를 새로 읽을 때). */
        void clear()
        {
            _listUndo.clear();
            _listRedo.clear();
            _lastCoalesceKey.clear();
        }

        /**
         * @brief 편집 직전 상태를 기록합니다. 다시 하기 기록은 버립니다.
         * @param before 편집 직전 상태입니다.
         * @param coalesceKey 비어 있지 않고 직전 기록의 키와 같으면 새 단계를 만들지 않습니다(끌기 한 번이 한 단계). 비면 항상 새 단계입니다.
         */
        void record( const TSnapshot& before, string_view coalesceKey = {} )
        {
            _listRedo.clear();
            const bool bCoalesce = coalesceKey.empty() == false && string_view( _lastCoalesceKey ) == coalesceKey && _listUndo.empty() == false;
            _lastCoalesceKey     = string( coalesceKey );
            if ( bCoalesce )
                return;
            _listUndo.push_back( before );
            if ( _listUndo.size() > kMaxCount )
                _listUndo.erase( _listUndo.begin() );
        }

        /** @brief 끌기가 끝났음을 알립니다. 다음 편집은 같은 키여도 새 단계를 만듭니다. */
        void endCoalescing() { _lastCoalesceKey.clear(); }

        /** @brief 한 단계 되돌립니다. @p inoutCurrent 는 지금 상태를 받아 되돌린 상태로 바뀝니다. 기록이 없으면 false 입니다. */
        [[nodiscard]] bool undo( TSnapshot& inoutCurrent )
        {
            if ( _listUndo.empty() )
                return false;
            _listRedo.push_back( inoutCurrent );
            inoutCurrent = _listUndo.back();
            _listUndo.pop_back();
            _lastCoalesceKey.clear();
            return true;
        }

        /** @brief 되돌린 것을 다시 합니다. 기록이 없으면 false 입니다. */
        [[nodiscard]] bool redo( TSnapshot& inoutCurrent )
        {
            if ( _listRedo.empty() )
                return false;
            _listUndo.push_back( inoutCurrent );
            inoutCurrent = _listRedo.back();
            _listRedo.pop_back();
            _lastCoalesceKey.clear();
            return true;
        }

        bool   canUndo() const { return _listUndo.empty() == false; }
        bool   canRedo() const { return _listRedo.empty() == false; }
        size_t getUndoCount() const { return _listUndo.size(); }

    private:
        vector<TSnapshot> _listUndo{};
        vector<TSnapshot> _listRedo{};
        string            _lastCoalesceKey{};
    };
} // namespace sw::editor
