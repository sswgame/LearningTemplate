/**
 * @file ElementGrid.h
 * @brief 원소 규칙 테이블(`ElementRuleTable`)을 따르는 결정적 셀 오토마타입니다. 셀마다 재질 하나와 상태 몇 개를 가지며, 바람, 자극 대기, 고정 스텝을 지원합니다.
 * @details 한 스텝은 세 단계로 진행한다. (1) 셀마다 상태 시간을 진행한다(Age 는 +1, Countdown 은 -1). (2) 스텝 규칙을 **그 순간의 상태만 보고** 모은다. (3) 규칙 순서대로 한꺼번에
 *          적용한다. 그래서 셀을 도는 순서가 결과를 바꾸지 않는다. 같은 조작이면 같은 상태 해시가 나오므로 리플레이와 롤백에 쓸 수 있다.
 *          오브젝트 하나(횃불, 상자)는 1 × 1 격자로 같은 규칙을 쓴다.
 */
#pragma once
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"
#include "Core/Time/FixedStepTimer.h"

#include "GameFramework/Base/Foundation/Utility/Grid/GridTopology.h"
#include "GameFramework/Base/Gameplay/Gimmick/ElementRuleTable.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;

    /** @brief 원소 알림 하나입니다. 셀과 테이블이 지정한 알림 이름을 담습니다. */
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
     * @brief 가로 × 세로 셀로 이루어진 격자입니다. 좌표는 (x, y)이고, 바람도 같은 축의 (-1..1, -1..1) 입니다. 규칙 테이블은 소유하지 않고 참조만 하므로 격자보다 오래 살아 있어야 합니다.
     */
    class SW_GF_API ElementGrid
    {
    public:
        ElementGrid();

        /**
         * @brief 크기와 규칙 테이블을 정하고 모든 셀을 첫 재질(테이블의 0 번)로 비웁니다. 스텝 시간은 테이블의 값을 씁니다.
         * @return 크기가 1 보다 작거나, 테이블이 없거나, 테이블의 스텝 시간이 0 이하이면 오류를 남기고 false 입니다(크기를 올리거나 기본 스텝 시간으로 대신하지 않는다). 그러면 격자는 그대로입니다.
         */
        [[nodiscard]] bool initialize( int32 width, int32 height, const ElementRuleTable* pTable );
        /** @brief 셀은 그대로 두고 규칙 테이블만 바꿔서 가리킵니다. 같은 구조의 테이블로 바꿀 때 쓰며, 테이블을 보관한 쪽을 복사했을 때가 해당합니다. */
        void setTable( const ElementRuleTable* pTable ) { _pTable = pTable; }

        /** @brief 셀의 재질을 정하고 상태를 모두 제거합니다. */
        void  setMaterial( const int2& cell, int32 material );
        int32 getMaterial( const int2& cell ) const;
        /** @brief 바람 방향입니다. 성분은 -1..1 로 자릅니다. */
        void        setWind( const int2& wind );
        const int2& getWind() const { return _wind; }

        /**
         * @brief 자극(테이블의 번호)을 셀에 가합니다. 처음 맞는 규칙 하나를 적용합니다.
         * @return 바뀐 셀 수입니다. 퍼뜨리기는 이어진 셀을 모두 셉니다. 맞는 규칙이 없으면 0 입니다.
         */
        int32 applyStimulus( const int2& cell, int32 stimulus );
        /** @brief 한 스텝 진행합니다. */
        void step();
        /** @brief 시간을 진행해서 고정 스텝을 실행합니다. 실행한 스텝 수를 반환합니다. */
        int32 update( float32 deltaTime );
        /** @brief 쌓인 알림을 @p outListEvent 뒤에 추가하고 비웁니다. */
        void drainEvents( vector<ElementEvent>& outListEvent );

        bool  isInside( const int2& cell ) const { return _topology.isInside( cell ); }
        bool  hasStatus( const int2& cell, int32 status ) const;
        int32 getStatusValue( const int2& cell, int32 status ) const;
        bool  hasFlag( const int2& cell, int32 flag ) const;
        int32 countStatus( int32 status ) const;
        /** @brief 모든 셀 상태, 바람, 스텝 수를 합친 해시입니다. */
        uint32                  computeStateHash() const;
        int32                   getWidth() const { return _topology._width; }
        int32                   getHeight() const { return _topology._height; }
        uint32                  getStepCount() const { return _stepCount; }
        const ElementRuleTable* getTable() const { return _pTable; }

        /** @brief 크기, 셀(재질, 상태 비트, 상태 값), 바람, 고정 스텝의 남은 시간, 스텝 수를 씁니다. 규칙 테이블은 `initialize` 가 받은 것이므로 저장하지 않습니다. 스텝 안의 대기 변화와 번짐 스크래치는 스텝 사이에 비어 있으므로 저장하지 않고, 알림은 읽을 때 비웁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 상태를 복원합니다. 크기가 `initialize` 의 크기와 다르거나 데이터가 깨졌으면 false 를 반환하고 상태는 바뀌지 않습니다. 격자 크기는 맵이 정하므로 바이트로 바꾸지 않습니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 셀 하나입니다. 재질과 상태(비트와 값)를 담습니다. */
        struct Cell
        {
            uint8 _material{ 0 };
            uint8 _statusBits{ 0 };
            uint8 _arrStatusValue[ElementRuleTable::kMaxStatusCount]{};

            /** @brief `write` 가 쓰는 바이트 수입니다. 필드의 크기를 더해 만들고, 필드를 늘리고 여기에 더하지 않으면 `write` 의 정적 검사가 막습니다. */
            static constexpr uint64 kStateBytes = sizeof( _material ) + sizeof( _statusBits ) + sizeof( _arrStatusValue );

            /** @brief 재질, 상태 비트, 상태 값(남은 시간, 세기)을 씁니다. 비트만 실으면 복원한 불이 처음부터 다시 탄다. */
            void write( Archive& outArchive ) const;
            /** @brief `write` 가 쓴 바이트를 읽습니다. 오류는 아카이브가 기억합니다. */
            void read( Archive& archive );
        };

        /** @brief 스텝 규칙이 모은 작업 하나입니다. */
        struct PendingChange
        {
            int2  _cell{};
            int32 _rule{ 0 };
        };

        int32 toIndex( const int2& cell ) const { return _topology.toIndex( cell ); }
        Cell& getCell( const int2& cell ) { return _listCell[static_cast<size_t>( toIndex( cell ) )]; }
        bool  matchesRule( const Cell& cell, const ElementStimulusRule& rule ) const;
        void  addStatus( Cell& cell, int32 status ) const;
        void  removeStatus( Cell& cell, int32 status ) const;
        void  pushEvent( const hashed_string& name, const int2& cell );
        void  collectSpread( const ElementStepRule& rule, int32 ruleIndex, const int2& position );
        /** @brief `step` 1 단계입니다. 상태의 시간을 넘깁니다. 셀마다 따로 처리하므로 순서와 상관없습니다. */
        void advanceStatusTimers();
        /** @brief `step` 2 단계입니다. 스텝 규칙을 지금 상태만 보고 `_listPending` 에 모읍니다. 규칙 순서가 먼저이고 그다음이 행 우선입니다. */
        void collectStepChanges();
        /**
         * @brief `step` 3 단계입니다. 모은 순서대로 적용합니다.
         * @details 앞의 것이 바꾼 셀은 뒤의 것이 다시 확인합니다. 이렇게 하지 않으면 두 불이 같은 얼음을 녹이는 일이 생깁니다.
         */
        void applyStepChanges();

        vector<Cell>              _listCell;
        EventBuffer<ElementEvent> _eventBuffer;
        vector<PendingChange>     _listPending; ///< 스텝 안에서 다시 쓰는 버퍼
        GridSearchScratch         _floodSearch; ///< 번짐(`Flood` 규칙)에 쓰는 버퍼. 셀 표시와 큐를 규칙마다 다시 쓴다
        const ElementRuleTable*   _pTable;
        FixedStepTimer            _timer;
        int2                      _wind;
        GridTopology              _topology; ///< 셀 격자의 너비, 높이, 셀 번호 계산
        uint32                    _stepCount;
    };
} // namespace sw
