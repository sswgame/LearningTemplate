/**
 * @file ElementGrid.h
 * @brief 원소 규칙표(`ElementRuleTable`)를 따르는 결정적 셀 자동자 — 칸마다 재질 하나와 상태 몇 개, 바람, 자극 대기, 고정 걸음입니다.
 * @details 한 걸음은 (1) 칸마다 상태 시간을 흘리고(Age +1 · Countdown −1) (2) 걸음 규칙을 **그 순간의 상태만 보고** 모은 뒤 (3) 규칙 순서대로 한꺼번에
 *          적용합니다 — 칸을 도는 순서가 결과를 바꾸지 않습니다. 같은 조작이면 같은 상태 해시입니다(리플레이 · 롤백). 오브젝트 하나(횃불 · 상자)는
 *          1 × 1 격자로 같은 규칙을 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Gimmick/ElementRuleTable.h"
#include "GameFramework/Base/Utility/EventBuffer.h"
#include "GameFramework/Base/Utility/FixedStepTimer.h"
#include "GameFramework/Base/Utility/GridTopology.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;

    /** @brief 원소 알림 하나 — 칸과 표가 적은 알림 이름입니다. */
    struct ElementEvent
    {
        hashed_string _name{};
        int2          _cell{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class ElementGrid
     * @brief 가로 × 세로 칸입니다. 좌표는 (x, y), 바람도 같은 축의 (−1..1, −1..1) 입니다. 규칙표는 빌려 들고 격자보다 오래 살아야 합니다.
     */
    class SW_GF_API ElementGrid
    {
    public:
        ElementGrid();

        /** @brief 크기와 규칙표를 정하고 모든 칸을 첫 재질(표의 0 번)로 비웁니다. 걸음 시간은 표의 것입니다. */
        void initialize( int32 width, int32 height, const ElementRuleTable* pTable );
        /** @brief 칸은 그대로 두고 규칙표만 바꿔 가리킵니다(같은 모양의 표 — 표를 든 쪽을 복사했을 때). */
        void setTable( const ElementRuleTable* pTable ) { _pTable = pTable; }

        /** @brief 칸의 재질을 정하고 상태를 모두 뗍니다. */
        void  setMaterial( const int2& cell, int32 material );
        int32 getMaterial( const int2& cell ) const;
        /** @brief 바람 방향입니다. 성분은 −1..1 로 자릅니다. */
        void        setWind( const int2& wind );
        const int2& getWind() const { return _wind; }

        /**
         * @brief 자극(표의 번호)을 칸에 댑니다 — 처음 맞는 규칙 한 줄을 적용합니다.
         * @return 바뀐 칸 수(퍼뜨리기는 이어진 칸 모두). 맞는 규칙이 없으면 0 입니다.
         */
        int32 applyStimulus( const int2& cell, int32 stimulus );
        /** @brief 한 걸음 진행합니다. */
        void step();
        /** @brief 시간을 흘려 고정 걸음을 냅니다. 낸 걸음 수입니다. */
        int32 update( float32 deltaTime );
        /** @brief 쌓인 알림을 @p outListEvent 뒤에 붙이고 비웁니다. */
        void drainEvents( vector<ElementEvent>& outListEvent );

        bool  isInside( const int2& cell ) const { return 0 <= cell._x && cell._x < _width && 0 <= cell._y && cell._y < _height; }
        bool  hasStatus( const int2& cell, int32 status ) const;
        int32 getStatusValue( const int2& cell, int32 status ) const;
        bool  hasFlag( const int2& cell, int32 flag ) const;
        int32 countStatus( int32 status ) const;
        /** @brief 모든 칸 상태 · 바람 · 걸음 수의 해시입니다. */
        uint32                  computeStateHash() const;
        int32                   getWidth() const { return _width; }
        int32                   getHeight() const { return _height; }
        uint32                  getStepCount() const { return _stepCount; }
        const ElementRuleTable* getTable() const { return _pTable; }

        /** @brief 크기 · 칸(재질 · 상태 비트 · 상태 값) · 바람 · 고정 걸음 남은 시간 · 걸음 수을 씁니다. 규칙 표는 `initialize` 의 것, 걸음 안의 대기 변화 · 번짐 스크래치는 걸음 사이에 비어 싣지 않고, 알림은 읽을 때 비웁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 크기가 `initialize` 의 크기와 다르거나 깨졌으면 false 이고 그대로입니다(격자 크기는 맵이 정한다 — 바이트가 바꾸지 않는다). */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 칸 하나 — 재질과 상태(비트 + 값)입니다. */
        struct Cell
        {
            uint8 _material{ 0 };
            uint8 _statusBits{ 0 };
            uint8 _arrStatusValue[ElementRuleTable::kMaxStatusCount]{};
        };

        /** @brief 걸음 규칙이 모은 할 일 하나입니다. */
        struct PendingChange
        {
            int2  _cell{};
            int32 _rule{ 0 };
        };

        int32 toIndex( const int2& cell ) const { return cell._y * _width + cell._x; }
        Cell& getCell( const int2& cell ) { return _listCell[static_cast<size_t>( toIndex( cell ) )]; }
        bool  matchesRule( const Cell& cell, const ElementStimulusRule& rule ) const;
        void  addStatus( Cell& cell, int32 status ) const;
        void  removeStatus( Cell& cell, int32 status ) const;
        void  pushEvent( const hashed_string& name, const int2& cell );
        void  collectSpread( const ElementStepRule& rule, int32 ruleIndex, const int2& position );

        vector<Cell>              _listCell;
        EventBuffer<ElementEvent> _eventBuffer;
        vector<PendingChange>     _listPending; ///< 걸음 안에서 다시 쓰는 자리
        GridSearchScratch         _floodSearch; ///< 번짐(`Flood` 규칙) — 칸 표시 · 큐를 규칙마다 다시 쓴다
        const ElementRuleTable*   _pTable;
        FixedStepTimer            _timer;
        int2                      _wind;
        int32                     _width;
        int32                     _height;
        uint32                    _stepCount;
    };
} // namespace sw
