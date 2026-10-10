/**
 * @file WitcherBestiary.h
 * @brief 괴물 도감 — 읽기 · 처치 · 조사로 오르는 지식 단계, 해금된 약점만 보여 주기, 기반 `ElementChart` 로 오일 · 폭탄 · 표식의 속성 배율입니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct WitcherWeakness;

    class Archive;
    class ElementChart;
    class WitcherCatalog;

    /** @brief 도감에 생긴 일입니다. */
    struct WitcherBestiaryEvent
    {
        enum class Kind : uint8
        {
            KnowledgeRaised = 0, ///< _value = 새 단계
            WeaknessRevealed     ///< _weaknessID
        };
        hashed_string _monsterID{};
        hashed_string _weaknessID{};
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
        static constexpr uint32 kStateTag     = FourCcUtil::make( "WBST" );
        static constexpr uint32 kStateVersion = 1;

        WitcherBestiary();

        void initialize( const WitcherCatalog* pCatalog, const ElementChart* pChart );
        /** @brief 괴물 책을 읽었습니다. 단계가 올랐으면 true 입니다. */
        [[nodiscard]] bool readBook( const hashed_string& monsterID );
        /** @brief 처치했습니다. 단계가 올랐으면 true 입니다. */
        bool recordKill( const hashed_string& monsterID );
        /** @brief 흔적 · 시체를 조사했습니다(계약). 단계가 올랐으면 true 입니다. */
        bool investigate( const hashed_string& monsterID );

        int32 getKnowledge( const hashed_string& monsterID ) const;
        int32 getKillCount( const hashed_string& monsterID ) const;
        /** @brief 지금 지식으로 보이는 약점입니다(적힌 순서). */
        void collectKnownWeaknesses( const hashed_string& monsterID, vector<const WitcherWeakness*>& outListWeakness ) const;
        /** @brief 공격 속성 @p attackElement 가 그 괴물에게 주는 배율입니다(방어 속성 모두의 곱 — 모르는 괴물 · 표가 없으면 1). */
        float32 computeMultiplier( const hashed_string& monsterID, const hashed_string& attackElement ) const;
        void    drainEvents( vector<WitcherBestiaryEvent>& outListEvent );
        /** @brief 괴물마다 지식 단계 · 처치 수를 씁니다. 카탈로그 · 상성표는 싣지 않는다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 모르는 괴물이거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        struct Entry
        {
            hashed_string _monsterID{};
            int32         _knowledge{ 0 };
            int32         _killCount{ 0 };
        };

        Entry*       findEntryMutable( const hashed_string& monsterID );
        const Entry* findEntry( const hashed_string& monsterID ) const;
        bool         raiseKnowledge( const hashed_string& monsterID, int32 level );

        vector<Entry>                     _listEntry;
        EventBuffer<WitcherBestiaryEvent> _eventBuffer;
        const WitcherCatalog*             _pCatalog;
        const ElementChart*               _pChart;
    };
} // namespace sw
