/**
 * @file NetSimGameRegistry.h
 * @brief 한 프로세스 재생(에디터의 In process 모드)에 쓸 수 있는 게임의 목록입니다. 키트 세션(`INetSimSession`)을 구현한 게임만 이름과 만드는 함수를 올립니다.
 * @details 게임 모듈은 활성 씬 하나를 가정하므로, 월드마다 씬을 따로 쓰는 세션을 직접 구현한 게임만 이 모드를 고를 수 있다(ServerDebug S8).
 *          게임 모듈이 내려가기 전에 `unregisterGame` 으로 뺀다(만드는 함수가 그 모듈의 코드다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class INetSimGame;

    /** @brief 등록된 게임 하나 — 이름과 게임 객체를 만드는 함수입니다. */
    struct NetSimGameEntry
    {
        string _name;
        unique_ptr<INetSimGame> ( *_pfnCreate )(){ nullptr };
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetSimGameRegistry
     * @brief 한 프로세스 재생용 게임 목록입니다(전부 static, 게임 스레드에서만 부른다).
     */
    class SW_KIT_API NetSimGameRegistry
    {
    public:
        /** @brief 게임을 올립니다. 같은 이름이 이미 있거나 함수가 비면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool registerGame( string_view name, unique_ptr<INetSimGame> ( *pfnCreate )() );
        /** @brief 게임을 뺍니다. 없으면 false 입니다. */
        static bool unregisterGame( string_view name );
        /** @brief 올라온 게임입니다(이름 순이 아니라 올린 순서). */
        static const vector<NetSimGameEntry>& getGames();
        /** @brief 이름으로 찾습니다. 없으면 nullptr 입니다. */
        static const NetSimGameEntry* findGame( string_view name );
    };
} // namespace sw
