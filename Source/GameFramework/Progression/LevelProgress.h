/**
 * @file LevelProgress.h
 * @brief 경험치 곡선(공식 · 표)과 레벨 진행 — 경험치를 더하면 몇 레벨이 올랐는지, 최고 레벨에서 멈추기입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /**
     * @class ExperienceCurve
     * @brief 레벨 L → L+1 에 필요한 경험치입니다. 표가 있으면 표, 없으면 `base × L^exponent + linear × L`(반올림)입니다.
     * @details XML: `<ExperienceCurve maxLevel="50" base="100" exponent="1.5" linear="0"/>` 또는 `<ExperienceCurve><Level xp="100"/>...</ExperienceCurve>`
     *          (표는 레벨 1 부터 차례로 — 표 길이 + 1 이 최고 레벨).
     */
    class SW_GF_API ExperienceCurve
    {
    public:
        ExperienceCurve();

        void               setFormula( float32 base, float32 exponent, float32 linear, int32 maxLevel );
        void               setTable( const vector<int64>& listXpToNext );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );
        void               loadFromNode( const XmlNode& node );

        /** @brief @p level 에서 다음 레벨까지입니다. 최고 레벨이면 0 입니다. */
        int64 getXpToNext( int32 level ) const;
        /** @brief 레벨 1 에서 @p level 이 되기까지의 합입니다. */
        int64 computeTotalXp( int32 level ) const;
        int32 getMaxLevel() const { return _maxLevel; }

    private:
        vector<int64> _listXpToNext;
        float32       _base;
        float32       _exponent;
        float32       _linear;
        int32         _maxLevel;
    };

    /** @brief 캐릭터 · 직업 · 무기 숙련 하나의 레벨입니다. */
    class SW_GF_API LevelProgress
    {
    public:
        /** @brief 경험치를 더하고 오른 레벨 수를 돌려줍니다. 최고 레벨이면 경험치는 0 에 머뭅니다. */
        int32 addXp( const ExperienceCurve& curve, int64 amount );
        /** @brief 레벨을 정합니다(세이브 · 치트). 경험치는 0 입니다. */
        void setLevel( const ExperienceCurve& curve, int32 level );
        /** @brief 이 레벨 안에서 찬 비율(0..1, 최고 레벨이면 1)입니다 — 경험치 막대. */
        float32 computeRatio( const ExperienceCurve& curve ) const;

        int32 getLevel() const { return _level; }
        int64 getXp() const { return _xp; }
        int64 getTotalXp() const { return _totalXp; }

    private:
        int64 _xp{ 0 };      ///< 이 레벨 안에서 모은 것
        int64 _totalXp{ 0 }; ///< 모두
        int32 _level{ 1 };
    };
} // namespace sw
