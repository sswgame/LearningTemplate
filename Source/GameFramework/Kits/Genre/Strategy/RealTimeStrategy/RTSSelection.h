/**
 * @file RTSSelection.h
 * @brief 유닛 선택을 담당합니다. 끌어서 선택(내 유닛 우선), 클릭, 같은 종류 선택, 부대 지정(0..9)을 지원합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"

namespace sw
{
    /**
     * @class RTSSelection
     * @brief 한 플레이어가 선택한 유닛과 부대를 관리합니다. 화면 좌표를 월드 위치로 바꾸는 일은 게임이 합니다.
     * @details 끌어서 선택은 스타크래프트 규칙을 따른다. 사각형 안에 내 움직이는 유닛이 있으면 그것만 고르고, 없으면 내 건물 하나, 그것도 없으면 보이는 남의 유닛 하나를 고른다.
     *          남의 것을 고르면 명령을 줄 수 없다(`isCommandable`). 죽은 유닛은 `prune` 이 선택에서 뺀다.
     */
    class SW_KIT_API RTSSelection
    {
    public:
        static constexpr int32 kGroupCount = 10;

        RTSSelection();

        /** @brief 한 번에 고를 수 있는 최대 수를 정합니다. 0 이면 제한이 없고, 스타크래프트 1 은 12 입니다. */
        void setMaxCount( int32 maxCount ) { _maxCount = maxCount; }
        void setPlayer( int32 player ) { _player = player; }

        /** @brief XZ 평면의 사각형(두 모서리) 안을 고릅니다. @p bAdd 가 true 이면 지금 고른 것에 더합니다(쉬프트). */
        void selectInRect( const RTSWorld& world, const float3& cornerA, const float3& cornerB, bool bAdd );
        /** @brief 유닛 하나를 고릅니다. @p bToggle 이 true 이고 이미 골랐으면 선택에서 뺍니다. */
        void selectUnit( const RTSWorld& world, RTSUnitID unitID, bool bToggle );
        /** @brief 사각형 안에서 @p unitID 와 같은 종류인 내 유닛을 고릅니다(더블 클릭). */
        void selectSameType( const RTSWorld& world, RTSUnitID unitID, const float3& cornerA, const float3& cornerB );
        void clear() { _listSelected.clear(); }

        /** @brief 지금 고른 것을 부대로 지정합니다(Ctrl + 숫자). */
        void assignGroup( int32 group );
        /** @brief 지금 고른 것을 부대에 추가합니다(Shift + 숫자). */
        void addToGroup( int32 group );
        /** @brief 부대를 선택합니다(숫자). 부대가 비었으면 false 를 반환합니다. */
        bool recallGroup( const RTSWorld& world, int32 group );

        /** @brief 죽은 유닛을 선택과 부대에서 뺍니다. */
        void prune( const RTSWorld& world );

        const vector<RTSUnitID>& getSelected() const { return _listSelected; }
        const vector<RTSUnitID>& getGroup( int32 group ) const;
        /** @brief 가장 먼저 고른 유닛을 반환합니다(명령 카드와 초상화에 씁니다). */
        RTSUnitID getPrimary() const { return _listSelected.empty() ? RTSUnitID{} : _listSelected.front(); }
        bool      isSelected( RTSUnitID unitID ) const;
        /** @brief 고른 것이 모두 내 것이라 명령을 줄 수 있는지 반환합니다. */
        bool  isCommandable( const RTSWorld& world ) const;
        int32 getPlayer() const { return _player; }

        /** @brief 고른 유닛과 부대를 유닛 id(세대 포함)로 씁니다. 핫 리로드와 세이브에 쓰며, 같은 id 를 복원한 월드와 함께 읽어야 합니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다. 죽은 유닛은 다음 `prune` 이 걸러 냅니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        void addUnit( RTSUnitID unitID );

        vector<RTSUnitID> _listSelected;
        vector<RTSUnitID> _arrGroup[kGroupCount];
        int32             _player;
        int32             _maxCount;
    };
} // namespace sw
