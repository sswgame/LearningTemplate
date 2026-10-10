/**
 * @file RTSSelection.h
 * @brief 고르기 — 끌어 고르기(내 유닛 먼저) · 클릭 · 같은 종류 · 부대 지정(0..9)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"

namespace sw
{
    /**
     * @class RTSSelection
     * @brief 한 플레이어의 고른 유닛과 부대입니다. 화면 좌표 → 월드 자리는 게임이 바꿔 넘깁니다.
     * @details 끌어 고르기는 스타크래프트 규칙입니다 — 사각형에 내 움직이는 유닛이 있으면 그것만, 없으면 내 건물 하나, 없으면 보이는 남의 것 하나.
     *          남의 것을 고르면 명령을 줄 수 없습니다(`isCommandable`). 죽은 유닛은 `prune` 이 뺍니다.
     */
    class SW_GF_API RTSSelection
    {
    public:
        static constexpr int32 kGroupCount = 10;

        RTSSelection();

        /** @brief 한 번에 고를 수 있는 수(0 = 제한 없음 · 스타크래프트 1 은 12)입니다. */
        void setMaxCount( int32 maxCount ) { _maxCount = maxCount; }
        void setPlayer( int32 player ) { _player = player; }

        /** @brief 사각형(XZ, 두 모서리) 안을 고릅니다. @p bAdd 면 지금 고른 것에 더합니다(쉬프트). */
        void selectInRect( const RTSWorld& world, const float3& cornerA, const float3& cornerB, bool bAdd );
        /** @brief 유닛 하나를 고릅니다. @p bToggle 이면 이미 골랐을 때 뺍니다. */
        void selectUnit( const RTSWorld& world, RTSUnitId unitId, bool bToggle );
        /** @brief 사각형 안의 @p unitId 와 같은 종류 내 유닛을 고릅니다(두 번 클릭). */
        void selectSameType( const RTSWorld& world, RTSUnitId unitId, const float3& cornerA, const float3& cornerB );
        void clear() { _listSelected.clear(); }

        /** @brief 지금 고른 것을 부대로 정합니다(Ctrl + 숫자). */
        void assignGroup( int32 group );
        /** @brief 지금 고른 것을 부대에 더합니다(Shift + 숫자). */
        void addToGroup( int32 group );
        /** @brief 부대를 고릅니다(숫자). 부대가 비었으면 false 입니다. */
        bool recallGroup( const RTSWorld& world, int32 group );

        /** @brief 죽은 유닛을 고른 것 · 부대에서 뺍니다. */
        void prune( const RTSWorld& world );

        const vector<RTSUnitId>& getSelected() const { return _listSelected; }
        const vector<RTSUnitId>& getGroup( int32 group ) const;
        /** @brief 처음 고른 것(명령 카드 · 초상화)입니다. */
        RTSUnitId getPrimary() const { return _listSelected.empty() ? RTSUnitId{} : _listSelected.front(); }
        bool      isSelected( RTSUnitId unitId ) const;
        /** @brief 고른 것이 모두 내 것이라 명령을 줄 수 있는가입니다. */
        bool  isCommandable( const RTSWorld& world ) const;
        int32 getPlayer() const { return _player; }

        /** @brief 고른 유닛 · 부대를 유닛 id(세대 포함)로 씁니다(핫 리로드 · 세이브 — 같은 id 를 되살린 월드와 함께 읽는다). */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다. 죽은 유닛은 다음 `prune` 이 거른다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        void addUnit( RTSUnitId unitId );

        vector<RTSUnitId> _listSelected;
        vector<RTSUnitId> _arrGroup[kGroupCount];
        int32             _player;
        int32             _maxCount;
    };
} // namespace sw
