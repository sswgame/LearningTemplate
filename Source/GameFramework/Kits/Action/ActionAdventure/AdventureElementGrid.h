/**
 * @file AdventureElementGrid.h
 * @brief 야생의 숨결의 화학 엔진 — 격자 셀(풀 · 나무 · 금속 · 물 · 얼음)과 불 · 전기 · 얼음의 결정적 셀 자동자입니다.
 * @details 기반의 원소 규칙표(`ElementRuleTable`) · 격자(`ElementGrid`) 위에 이 키트의 재질 열거와 설정을 얹은 것입니다 — 규칙표는 설정 값으로 지은
 *          `Resource/common/data/elements/default.elements.xml` 과 같은 모양입니다(`ElementRuleTest.AdventureGridMatchesDefaultTable`).
 *          규칙(한 걸음마다, 걸음 시작의 상태만 보고 정한 뒤 한꺼번에 바꾼다 — 셀을 도는 순서가 결과를 바꾸지 않는다):
 *          - 불은 풀 · 나무를 태운다. 탄 지 `_spreadDelaySteps` 걸음이 지나면 바람이 부는 쪽 이웃(대각 포함 한 칸)으로 옮겨 붙는다.
 *            바람이 없으면 네 이웃 모두로 붙는다. 바람을 거슬러서는 붙지 않는다(`_bCrosswindSpread` 면 바람의 옆 두 칸도).
 *          - 풀은 `_grassBurnSteps`, 나무는 `_woodBurnSteps` 걸음 타고 사라진다(빈 칸). 타는 풀은 위로 오르는 기류를 만든다(활공).
 *          - 타는 칸의 네 이웃 얼음은 녹아 물이 된다. 물 · 금속 · 얼음은 타지 않는다.
 *          - 전기는 닿은 칸에서 이어진(네 이웃) 금속 · 물을 따라 한 번에 퍼지고 `_chargeSteps` 걸음 남는다. 얼음은 전기를 막는다.
 *          - 얼음을 쓰면 물이 얼고, 타는 칸은 꺼진다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/Base/Gimmick/ElementGrid.h"
#include "GameFramework/Base/Gimmick/ElementRuleTable.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 셀의 재질입니다. */
    enum class AdventureMaterial : uint8
    {
        Empty = 0, ///< 맨땅 · 탄 자리
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
        int32   _spreadDelaySteps{ 1 }; ///< 붙은 뒤 이 걸음부터 옆으로 옮긴다
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
        Melted, ///< 얼음 → 물
        Frozen, ///< 물 → 얼음
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
     * @brief 가로 × 세로 셀입니다. 칸 좌표는 (x, y) 이고 바람도 같은 축의 (−1..1, −1..1) 입니다.
     */
    class SW_GF_API AdventureElementGrid
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "AELM" );
        static constexpr uint32 kStateVersion = 1;

        AdventureElementGrid();
        /** @brief 격자는 규칙표를 가리키므로 복사하면 새 규칙표를 가리키게 다시 잇습니다. */
        AdventureElementGrid( const AdventureElementGrid& other );
        AdventureElementGrid& operator=( const AdventureElementGrid& other );

        void initialize( int32 width, int32 height, const AdventureElementSettings& settings );

        void              setMaterial( const int2& cell, AdventureMaterial material );
        AdventureMaterial getMaterial( const int2& cell ) const;
        /** @brief 바람 방향입니다. 성분은 −1..1 로 자릅니다. (0, 0) 은 바람 없음입니다. */
        void        setWind( const int2& wind );
        const int2& getWind() const { return _grid.getWind(); }

        /** @brief 불을 댑니다(불화살 · 부싯돌). 풀 · 나무는 붙고 얼음은 녹습니다. 무언가 바뀌었으면 true 입니다. */
        [[nodiscard]] bool applyFire( const int2& cell );
        /** @brief 냉기를 댑니다. 물은 얼고 타는 칸은 꺼집니다. 무언가 바뀌었으면 true 입니다. */
        [[nodiscard]] bool applyIce( const int2& cell );
        /** @brief 전기를 댑니다. 금속 · 물이면 이어진 금속 · 물이 모두 대전됩니다. 대전된 칸 수입니다. */
        int32 applyElectric( const int2& cell );

        /** @brief 한 걸음 진행합니다. */
        void step();
        /** @brief 시간을 흘려 고정 걸음을 냅니다. 낸 걸음 수입니다. */
        int32 update( float32 deltaTime );
        /** @brief 쌓인 알림을 @p outListEvent 뒤에 붙이고 비웁니다. */
        void drainEvents( vector<AdventureElementEvent>& outListEvent );

        bool isInside( const int2& cell ) const { return _grid.isInside( cell ); }
        bool isBurning( const int2& cell ) const;
        bool isCharged( const int2& cell ) const;
        /** @brief 위로 오르는 기류가 있는가(타는 풀)입니다. */
        bool hasUpdraft( const int2& cell ) const;
        /** @brief 기류가 있는 칸들입니다(행 우선 순서). */
        void  collectUpdraft( vector<int2>& outListCell ) const;
        int32 countBurning() const;
        /** @brief 모든 셀 상태의 해시입니다(결정성 · 리플레이 비교). */
        uint32 computeStateHash() const;
        int32  getWidth() const { return _grid.getWidth(); }
        int32  getHeight() const { return _grid.getHeight(); }
        uint32 getStepCount() const { return _grid.getStepCount(); }

        /** @brief 기반 격자의 칸 · 바람 · 고정 걸음 · 걸음 수를 씁니다. 설정 · 규칙표는 `initialize` 의 것이라 싣지 않고, 알림은 읽을 때 비웁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 격자 크기가 다르거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 설정 값으로 규칙표를 짓습니다(`default.elements.xml` 과 같은 모양 — 재질 번호 = `AdventureMaterial`). */
        void buildRuleTable();

        AdventureElementSettings      _settings;
        ElementRuleTable              _table;
        ElementGrid                   _grid;
        vector<ElementEvent>          _listGridEvent; ///< 격자 알림을 옮기는 자리(재사용)
        vector<AdventureElementEvent> _listEvent;
        int32                         _burningStatus;
        int32                         _chargedStatus;
        int32                         _updraftFlag;
        int32                         _fireStimulus;
        int32                         _iceStimulus;
        int32                         _electricStimulus;
    };
} // namespace sw
