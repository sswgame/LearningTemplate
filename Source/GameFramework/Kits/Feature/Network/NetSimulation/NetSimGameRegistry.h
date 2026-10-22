/**
 * @file NetSimGameRegistry.h
 * @brief 한 프로세스 재생(에디터의 In process 모드)에 쓸 수 있는 게임의 목록입니다. 키트 세션(`INetSimSession`)을 구현한 게임만 이름과 만드는 함수를 올립니다.
 * @details 게임 모듈은 활성 씬 하나를 가정하므로, 월드마다 씬을 따로 쓰는 세션을 직접 구현한 게임만 이 모드를 고를 수 있다(ServerDebug S8).
 *          항목(`NetSimGameEntry`)은 올린 모듈이 소유하고(정적 등록자 객체의 멤버), 모듈이 내려가기 전에 같은 객체로 `unregisterGame` 한다(만드는 함수가 그 모듈의 코드다).
 *          목록은 `RegistrationList` 라 핫 리로드의 교체 이미지가 같은 이름을 대체하고, 옛 이미지가 내려가며 뺀 항목은 새 등록을 건드리지 않는다.
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

    /** @brief 등록된 게임 하나 — 이름과 게임 객체를 만드는 함수입니다. 올린 모듈이 소유합니다. */
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
        /**
         * @brief 게임을 올립니다. 목록은 @p entry 의 주소를 쥐므로 올린 쪽이 뺄 때까지 살려 둡니다.
         * @details 이름이나 함수가 비거나, 같은 이름의 다른 항목이 있으면 오류를 남기고 false 입니다(핫 리로드의 교체 이미지는 대체 — `RegistrationReloadScope`).
         */
        [[nodiscard]] static bool registerGame( const NetSimGameEntry& entry );
        /** @brief 올린 그 항목을 뺍니다. 없으면 false 입니다. */
        static bool unregisterGame( const NetSimGameEntry& entry );
        /** @brief 올라온 게임입니다(이름 순이 아니라 올린 순서). */
        static const vector<const NetSimGameEntry*>& getGames();
        /** @brief 이름으로 찾습니다. 없으면 nullptr 입니다. */
        static const NetSimGameEntry* findGame( string_view name );
    };
} // namespace sw
