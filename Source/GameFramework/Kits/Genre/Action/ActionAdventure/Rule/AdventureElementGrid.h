/**
 * @file AdventureElementGrid.h
 * @brief 야생의 숨결의 화학 엔진입니다. 격자 셀(풀, 나무, 금속, 물, 얼음) 위에서 불, 전기, 얼음이 결정적으로 퍼지는 셀 자동자입니다.
 * @details 기반의 원소 규칙 테이블(`ElementRuleTable`)과 격자(`ElementGrid`) 위에 이 키트의 재질 열거형과 설정을 적용했다.
 *          규칙 테이블은 설정 값으로 만든다. 내용은 `Resource/common/data/elements/default.elements.xml` 과 같다(`ElementRuleTest.AdventureGridMatchesDefaultTable`).
 *          규칙은 한 스텝마다 스텝 시작 시점의 상태만 보고 정한 뒤 한꺼번에 바꾼다. 그래서 셀을 도는 순서가 결과를 바꾸지 않는다.
 *          - 불은 풀과 나무를 태운다. 탄 지 `_spreadDelaySteps` 스텝이 지나면 바람이 부는 쪽의 이웃(대각선 포함 한 칸)으로 옮겨 붙는다.
 *            바람이 없으면 네 이웃 모두에 붙는다. 바람을 거슬러서는 붙지 않는다(`_bCrosswindSpread` 가 true 면 바람의 옆 두 칸에도 붙는다).
 *          - 풀은 `_grassBurnSteps`, 나무는 `_woodBurnSteps` 스텝 동안 타고 사라진다(빈 칸이 된다). 타는 풀은 위로 오르는 기류를 만든다(활공에 쓴다).
 *          - 타는 칸의 네 이웃에 있는 얼음은 녹아서 물이 된다. 물, 금속, 얼음은 타지 않는다.
 *          - 전기는 닿은 칸에서 이어진(네 이웃) 금속과 물을 따라 한 번에 퍼지고 `_chargeSteps` 스텝 동안 남는다. 얼음은 전기를 막는다.
 *          - 얼음을 쓰면 물이 얼고, 타는 칸은 꺼진다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/Base/Gameplay/Gimmick/ElementGrid.h"
#include "GameFramework/Base/Gameplay/Gimmick/ElementRuleTable.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 셀의 재질입니다. */
    enum class AdventureMaterial : uint8
    {
        Empty = 0, ///< 맨땅이거나 탄 자리
        Grass,
        Wood,
        Metal,
        Water,
        Ice
    };

    /** @brief 화학 설정입니다. */
    struct AdventureElementSettings
    {
        float32 _stepTime{ 0.25f };
        int32   _grassBurnSteps{ 4 };
        int32   _woodBurnSteps{ 12 };
        int32   _spreadDelaySteps{ 1 }; ///< 불이 붙은 뒤 이 스텝부터 옆으로 옮겨 붙는다
        int32   _chargeSteps{ 2 };
        uint8   _bCrosswindSpread{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 화학 알림 종류입니다. */
    enum class AdventureElementEventType : uint8
    {
        Ignited = 0,
        BurnedOut, ///< 다 타서 빈 칸이 됐다
        Extinguished,
        Melted, ///< 얼음이 물이 됐다
        Frozen, ///< 물이 얼음이 됐다
        Electrified
    };

    /** @brief 화학 알림 하나입니다. */
    struct AdventureElementEvent
    {
        int2                      _cell{};
        AdventureElementEventType _type{ AdventureElementEventType::Ignited };
    };
} // namespace sw

namespace sw
{
    class Archive;

    /**
     * @class AdventureElementGrid
     * @brief 가로 × 세로 셀로 이루어진 격자입니다. 칸 좌표는 (x, y) 이고, 바람도 같은 축의 (−1..1, −1..1) 값입니다.
     */
    class SW_KIT_API AdventureElementGrid
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "AELM" );
        static constexpr uint32 kStateVersion = 1;

        AdventureElementGrid();
        /** @brief 격자는 규칙 테이블을 가리키므로, 복사하면 새 규칙 테이블을 가리키도록 다시 연결합니다. */
        AdventureElementGrid( const AdventureElementGrid& other );
        AdventureElementGrid& operator=( const AdventureElementGrid& other );

        void initialize( int32 width, int32 height, const AdventureElementSettings& settings );

        void              setMaterial( const int2& cell, AdventureMaterial material );
        AdventureMaterial getMaterial( const int2& cell ) const;
        /** @brief 바람 방향입니다. 성분은 −1..1 로 자르며, (0, 0) 은 바람 없음입니다. */
        void        setWind( const int2& wind );
        const int2& getWind() const { return _grid.getWind(); }

        /** @brief 불을 붙입니다(불화살, 부싯돌). 풀과 나무는 붙고 얼음은 녹습니다. 무언가 바뀌었으면 true 를 반환합니다. */
        [[nodiscard]] bool applyFire( const int2& cell );
        /** @brief 냉기를 가합니다. 물은 얼고 타는 칸은 꺼집니다. 무언가 바뀌었으면 true 를 반환합니다. */
        [[nodiscard]] bool applyIce( const int2& cell );
        /** @brief 전기를 가합니다. 금속이나 물에 닿으면 이어진 금속과 물이 모두 대전되고, 대전된 칸 수를 반환합니다. */
        int32 applyElectric( const int2& cell );

        /** @brief 한 스텝 진행합니다. */
        void step();
        /** @brief 시간을 흘려 고정 스텝을 진행하고, 진행한 스텝 수를 반환합니다. */
        int32 update( float32 deltaTime );
        /** @brief 쌓인 알림을 @p outListEvent 뒤에 붙이고 비웁니다. */
        void drainEvents( vector<AdventureElementEvent>& outListEvent );

        bool isInside( const int2& cell ) const { return _grid.isInside( cell ); }
        bool isBurning( const int2& cell ) const;
        bool isCharged( const int2& cell ) const;
        /** @brief 위로 오르는 기류가 있는지(타는 풀이 있는지) 반환합니다. */
        bool hasUpdraft( const int2& cell ) const;
        /** @brief 기류가 있는 칸을 행 우선 순서로 반환합니다. */
        void  collectUpdraft( vector<int2>& outListCell ) const;
        int32 countBurning() const;
        /** @brief 모든 셀 상태의 해시입니다. 결정성 검증과 리플레이 비교에 씁니다. */
        uint32 computeStateHash() const;
        int32  getWidth() const { return _grid.getWidth(); }
        int32  getHeight() const { return _grid.getHeight(); }
        uint32 getStepCount() const { return _grid.getStepCount(); }

        /** @brief 기반 격자의 칸, 바람, 고정 스텝, 스텝 수를 씁니다. 설정과 규칙 테이블은 `initialize` 에서 받은 것이라 싣지 않고, 알림은 읽을 때 비웁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 상태를 바꿉니다. 격자 크기가 다르거나 바이트가 깨졌으면 상태를 바꾸지 않고 false 를 반환합니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 설정 값으로 규칙 테이블을 만듭니다. 내용은 `default.elements.xml` 과 같고, 재질 번호는 `AdventureMaterial` 입니다. */
        void populateRuleTable();

        AdventureElementSettings      _settings;
        ElementRuleTable              _table;
        ElementGrid                   _grid;
        vector<ElementEvent>          _listGridEvent; ///< 격자 알림을 옮겨 담는 버퍼(재사용한다)
        vector<AdventureElementEvent> _listEvent;
        int32                         _burningStatus;
        int32                         _chargedStatus;
        int32                         _updraftFlag;
        int32                         _fireStimulus;
        int32                         _iceStimulus;
        int32                         _electricStimulus;
    };
} // namespace sw
