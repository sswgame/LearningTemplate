/**
 * @file WitcherMutagens.h
 * @brief 강화 · 변이 슬롯 — 스킬 슬롯 묶음(레벨로 열림)마다 변이원 슬롯 하나, 같은 묶음에 색이 맞는 스킬마다 변이원 보너스가 오릅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class StatBlock;
    class WitcherCatalog;

    /** @brief 슬롯에 끼우기 결과입니다. */
    enum class WitcherSlotResult : uint8
    {
        Ok = 0,
        InvalidSlot,
        Locked,          ///< 레벨이 모자라 묶음이 닫혀 있다
        AlreadyEquipped, ///< 그 스킬은 다른 슬롯에 있다
        UnknownMutagen
    };

    SW_GF_API const utf8* toString( WitcherSlotResult result );

    /**
     * @class WitcherMutagens
     * @brief 묶음 배치는 카탈로그의 `<SlotGroup>` 순서입니다. 스킬을 배웠는지(랭크)는 게임이 `SkillTreeState` 로 보고 끼웁니다 — 여기는 자리와 색만 봅니다.
     *        레벨이 내려가 묶음이 닫혀도 끼운 것은 남지만 능력치에는 들어가지 않습니다.
     */
    class SW_GF_API WitcherMutagens
    {
    public:
        WitcherMutagens();

        void              initialize( const WitcherCatalog* pCatalog, int32 characterLevel );
        void              setCharacterLevel( int32 characterLevel ) { _characterLevel = characterLevel; }
        WitcherSlotResult equipSkill( int32 group, int32 slot, const hashed_string& skillId );
        WitcherSlotResult equipMutagen( int32 group, const hashed_string& mutagenId );
        void              clearSlot( int32 group, int32 slot );

        bool          isGroupOpen( int32 group ) const;
        int32         getGroupCount() const { return static_cast<int32>( _listGroup.size() ); }
        hashed_string getSkill( int32 group, int32 slot ) const;
        hashed_string getMutagen( int32 group ) const;
        /** @brief 그 묶음에서 변이원과 색이 맞는 스킬 수입니다. */
        int32 countMatches( int32 group ) const;
        /** @brief 열린 묶음의 변이원 능력치(기본 + 맞은 스킬 × 맞춤 값)를 더합니다. */
        void computeStats( StatBlock& outStats ) const;
        /** @brief 열린 슬롯에 끼운 스킬입니다(능동 스킬 목록). */
        void collectEquippedSkills( vector<hashed_string>& outListSkill ) const;

    private:
        struct Group
        {
            vector<hashed_string> _listSkill{};
            hashed_string         _mutagenId{};
        };

        bool isValidSlot( int32 group, int32 slot ) const;

        vector<Group>         _listGroup;
        const WitcherCatalog* _pCatalog;
        int32                 _characterLevel;
    };
} // namespace sw
