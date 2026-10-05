/**
 * @file OverworldSaveGame.h
 * @brief 오버월드 세이브 — 지금 맵 · 플레이어 타일 자리 · 월드 플래그를 SAV1 바이너리로 싣습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Framework/SaveGame.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/World/GameFlags.h"

namespace sw
{
    /**
     * @class OverworldSaveGame
     * @brief 오버월드의 영속 상태입니다. 게임 도중의 정본은 `GameFlags` · `PlayerController` 이고, 이 세이브는 저장할 때 그 값을 받아 둡니다(언리얼 `USaveGame` 처럼 사본).
     * @details 플래그는 `GameFlags::fillEntries` 의 이름 순 목록이라 같은 상태면 같은 바이트입니다. `saveToFile` · `loadFromFile` 은
     *          이 타입으로 `SaveGameSerializer` 를 부릅니다.
     */
    REFLECT()
    class SW_GF_API OverworldSaveGame : public SaveGame
    {
    public:
        REFLECT_BODY();

        PROPERTY()
        string _mapPath{}; ///< 지금 맵. 비었으면 읽은 뒤 `ensureStartMap` 이 시작 맵(`GameSettings::_startMap`)으로 채운다
        PROPERTY()
        int32 _playerX{ 1 };
        PROPERTY()
        int32 _playerY{ 1 };
        PROPERTY()
        vector<GameFlagEntry> _listFlag{}; ///< 월드 플래그(이름 순, 값 0 은 없다)

        /** @brief 맵이 비었으면 시작 맵(`GameSettings::_startMap`)으로 채웁니다. `loadFromFile` 이 읽은 뒤 부릅니다. */
        void ensureStartMap();
        /** @brief 지금 월드 플래그를 세이브에 받아 둡니다(이름 순). */
        void captureFlags( const GameFlags& flags );
        /** @brief 세이브의 플래그로 @p outFlags 를 되살립니다(지금 값은 모두 지운다). */
        void restoreFlags( GameFlags& outFlags ) const;

        /** @brief SAV1 바이너리 파일로 저장합니다. */
        [[nodiscard]] bool saveToFile( string_view path ) const override;
        /** @brief SAV1 바이너리 파일에서 읽고, 맵이 비었으면 시작 맵으로 채웁니다. */
        [[nodiscard]] bool loadFromFile( string_view path ) override;
    };
} // namespace sw
