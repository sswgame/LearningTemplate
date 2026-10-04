/**
 * @file SaveGame.h
 * @brief TurnBattle 영속 세이브(맵 + 파티 + 스토리 플래그)입니다. 일시적인 전투 상태는 뺍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Framework/SaveGame.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Rpg/TurnBattle/SpeciesData.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) TurnBattleSaveGame — 리플렉션 기반 공통 슬롯 필드 + 파티
    // ------------------------------------------------------------------------------
    /** @brief 맵 · 좌표 · 플래그 · 파티를 리플렉션으로 파일에 저장합니다. */
    REFLECT()
    struct SW_GF_API TurnBattleSaveGame : public SaveGame, public IFlagStore
    {
    public:
        REFLECT_BODY();

        PROPERTY()
        string _mapPath{}; ///< 현재 맵. 비었으면 `ensureStartMap` 이 시작 맵(`GameSettings::_startMap`)으로 채운다(세이브를 읽은 뒤에도)

        PROPERTY()
        int32 _playerX{ 1 };

        PROPERTY()
        int32 _playerY{ 1 };

        PROPERTY()
        vector<PartyMember> _listParty{};

        PROPERTY()
        map<string, int32> _mapFlag{}; ///< 스토리 플래그

        /** @brief 파티를 비웁니다. */
        void clearParty();
        /** @brief 외부 파티로 교체합니다. */
        void setPartyFrom( const vector<PartyMember>& listParty );
        /** @brief 스타터 파티가 없으면 채웁니다. */
        void ensureStarterParty();
        /** @brief 맵이 비었으면 시작 맵(`GameSettings::_startMap`)으로 채웁니다. 세이브를 읽은 뒤에도 부릅니다(맵 없는 세이브는 시작 맵에서). */
        void ensureStartMap();

        /** @brief 플래그 값을 반환합니다. 없으면 defaultValue 입니다. */
        int32 getFlag( string_view key, int32 defaultValue = 0 ) const override;
        /** @brief 플래그 값을 설정합니다. */
        void setFlag( string_view key, int32 value ) override;

        /** @brief 세이브 데이터를 파일로 저장합니다. */
        [[nodiscard]] bool saveToFile( string_view path ) const override;
        /** @brief 파일에서 세이브 데이터를 불러옵니다. */
        [[nodiscard]] bool loadFromFile( string_view path ) override;
    };

    namespace turnbattle
    {
        using SaveGame = TurnBattleSaveGame;
    } // namespace turnbattle
} // namespace sw
