/**
 * @file WitcherBestiary.h
 * @brief 괴물 도감 — 읽기 · 처치 · 조사로 오르는 지식 단계, 해금된 약점만 보여 주기, 기반 `ElementChart` 로 오일 · 폭탄 · 표식의 속성 배율입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct WitcherWeakness;

    class ElementChart;
    class WitcherCatalog;

    /** @brief 도감에 생긴 일입니다. */
    struct WitcherBestiaryEvent
    {
        enum class Kind : uint8
        {
            KnowledgeRaised = 0, ///< _value = 새 단계
            WeaknessRevealed     ///< _weaknessId
        };
        hashed_string _monsterId{};
        hashed_string _weaknessId{};
        int32         _value{ 0 };
        Kind          _kind{ Kind::KnowledgeRaised };
    };
} // namespace sw

namespace sw
{
    /**
     * @class WitcherBestiary
     * @brief 한 위쳐의 도감입니다. 지식은 내려가지 않습니다 — 책은 그 괴물의 읽기 단계까지, 처치는 정한 수마다 한 단계씩(처치 상한까지),
     *        조사는 조사 단계까지 올립니다. 약점의 실제 배율은 지식과 상관없이 늘 적용되고, 지식은 **보여 주는 것**만 정합니다.
     */
    class SW_GF_API WitcherBestiary
    {
    public:
        WitcherBestiary();

        void initialize( const WitcherCatalog* pCatalog, const ElementChart* pChart );
        /** @brief 괴물 책을 읽었습니다. 단계가 올랐으면 true 입니다. */
        [[nodiscard]] bool readBook( const hashed_string& monsterId );
        /** @brief 처치했습니다. 단계가 올랐으면 true 입니다. */
        bool recordKill( const hashed_string& monsterId );
        /** @brief 흔적 · 시체를 조사했습니다(계약). 단계가 올랐으면 true 입니다. */
        bool investigate( const hashed_string& monsterId );

        int32 getKnowledge( const hashed_string& monsterId ) const;
        int32 getKillCount( const hashed_string& monsterId ) const;
        /** @brief 지금 지식으로 보이는 약점입니다(적힌 순서). */
        void collectKnownWeaknesses( const hashed_string& monsterId, vector<const WitcherWeakness*>& outListWeakness ) const;
        /** @brief 공격 속성 @p attackElement 가 그 괴물에게 주는 배율입니다(방어 속성 모두의 곱 — 모르는 괴물 · 표가 없으면 1). */
        float32 computeMultiplier( const hashed_string& monsterId, const hashed_string& attackElement ) const;
        void    drainEvents( vector<WitcherBestiaryEvent>& outListEvent );

    private:
        struct Entry
        {
            hashed_string _monsterId{};
            int32         _knowledge{ 0 };
            int32         _killCount{ 0 };
        };

        Entry*       findEntryMutable( const hashed_string& monsterId );
        const Entry* findEntry( const hashed_string& monsterId ) const;
        bool         raiseKnowledge( const hashed_string& monsterId, int32 level );

        vector<Entry>                _listEntry;
        vector<WitcherBestiaryEvent> _listEvent;
        const WitcherCatalog*        _pCatalog;
        const ElementChart*          _pChart;
    };
} // namespace sw
