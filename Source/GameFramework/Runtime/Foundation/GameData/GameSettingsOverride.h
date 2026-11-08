/**
 * @file GameFramework/Runtime/Foundation/GameData/GameSettingsOverride.h
 * @brief 게임 인스턴스가 읽은 gamesettings 를 서비스로 걸기 전에 고치는 자리입니다 — 게임 인스턴스는 무엇을 고치는지 모르고 이 창구만 부릅니다.
 * @details 구현은 개발 전용 소스(`GameData/Dev/GameSettingsLaunchOverride.cpp` — 로컬 수신기 · 로컬 서버로 끝점을 돌리는 실행 인자)가 정적 등록으로 하나 건다.
 *          배포는 그 소스를 짓지 않아 창구가 없고, 게임은 팩의 gamesettings 그대로 선다(`IGameInstanceClientHook` 과 같은 갈림).
 *          언리얼이 설정 값을 명령줄로 덮어쓰는 길(`-ini:`)을 배포 구성에서 닫는 것과 같은 자리다(기억에 의존).
 *          GameFramework 는 리로드하지 않는 공유 모듈이라 등록은 이미지 수명 동안 하나다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/String/RegistrationList.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct GameSettings;

    /** @class IGameSettingsOverride @brief 읽은 gamesettings 를 고치는 쪽입니다. 게임 스레드에서 부릅니다. */
    class SW_GF_API IGameSettingsOverride
    {
    public:
        virtual ~IGameSettingsOverride() = default;

        /** @brief 게임 인스턴스가 gamesettings 를 읽은 직후 · 게임 서비스로 걸기 전에 @p settings 를 고칩니다(인스턴스가 설 때마다 — 핫 리로드 포함). */
        virtual void apply( GameSettings& settings ) = 0;

        /** @brief 등록된 창구입니다. 개발 전용 소스가 없으면(배포) nullptr 입니다. */
        [[nodiscard]] static IGameSettingsOverride* find();

        /** @brief 창구를 겁니다(정적 등록자가 부른다). 이미 다른 창구가 걸려 있으면 공용 거절 로그를 남기고 false 입니다 — 이미지에 하나다. */
        [[nodiscard]] static bool attach( IGameSettingsOverride* pSettingsOverride );
        /** @brief 걸린 창구를 뗍니다(건 이미지가 내려갈 때). 걸려 있지 않으면 할 일이 없다. */
        static void detach( IGameSettingsOverride* pSettingsOverride );

        /** @struct Registration @brief 개발 전용 소스가 정적 객체로 창구를 겁니다(이미지에 하나) — 공통 틀 `StaticRegistrar` 라 건 이미지가 내려가면 뗀다. */
        struct Registration final : StaticRegistrar<IGameSettingsOverride*>
        {
            explicit Registration( IGameSettingsOverride& settingsOverride )
                : StaticRegistrar{ &settingsOverride, &IGameSettingsOverride::attach, &IGameSettingsOverride::detach }
            {
            }
        };
    };
} // namespace sw
