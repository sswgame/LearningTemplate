/**
 * @file GameFramework/Runtime/Foundation/Framework/Flow/GameInstanceClientHook.h
 * @brief 게임 인스턴스에 클라이언트 전용 부분(로딩 화면 · UI 전역 명령)을 붙이는 자리입니다 — 게임 인스턴스는 UI 를 모르고 이 창구만 부릅니다.
 * @details 구현은 클라이언트 전용 소스(`GameFramework/Client/Flow`)가 정적 등록으로 하나 건다. 전용 서버는 그 소스를 짓지 않아 창구가 없고, 게임 인스턴스는 로딩 화면 없이 선다.
 *          언리얼의 `UGameInstance` 가 로딩 화면 모듈(클라이언트에만 싣는 MoviePlayer · CommonLoadingScreen)을 몰라도 도는 갈림과 같다(기억).
 *          GameFramework 는 리로드하지 않는 공유 모듈이라 등록은 이미지 수명 동안 하나다.
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameSceneFlow;
    class ScreenFade;
    class SubsystemScope;

    /** @class ILoadingScreen @brief 씬을 비동기로 바꾸는 동안의 로딩 화면입니다(구현은 클라이언트 전용 `LoadingScreenController`). 게임 스레드에서 부릅니다. */
    class ILoadingScreen
    {
    public:
        virtual ~ILoadingScreen() = default;

        /** @brief gamesettings 가 로딩 화면 문서를 적었으면 true 입니다(없으면 로딩 화면 없이 씬을 바꾼다). */
        [[nodiscard]] virtual bool hasLoadingScreenDocument() const = 0;
        /** @brief 로딩 화면을 띄웁니다. 띄우지 못하면(UI 가 없다) false 입니다. */
        virtual bool beginLoading() = 0;
        /** @brief 매 프레임 로딩 상태 · 진행률과 화면 페이드를 반영합니다. */
        virtual void update( float32 deltaSeconds, bool bLoading, optional<float32> loadProgress, ScreenFade& fade ) = 0;
    };

    /** @class IGameInstanceClientHook @brief 게임 인스턴스의 클라이언트 전용 부분을 세우는 쪽입니다. */
    class SW_GF_API IGameInstanceClientHook
    {
    public:
        virtual ~IGameInstanceClientHook() = default;

        /** @brief 게임 범위 @p gameScope 에 로딩 화면을 세우고 돌려줍니다(게임 인스턴스 생성자가 씬 흐름보다 먼저 부른다). */
        virtual ILoadingScreen* emplaceLoadingScreen( SubsystemScope& gameScope ) = 0;
        /** @brief UI 가 있으면 씬 흐름 @p flow 의 세션 명령(타이틀로)을 UI 전역 명령 표에 겁니다(`onInitialize` 뒤). 거둘 때는 `GameSceneFlow::unregisterSessionCommands`. */
        virtual void bindSessionCommands( GameSceneFlow& flow ) = 0;

        /** @brief 등록된 창구입니다. 클라이언트 전용 소스가 없으면(전용 서버) nullptr 입니다. */
        [[nodiscard]] static IGameInstanceClientHook* find();

        /** @brief 창구를 겁니다(정적 등록자가 부른다). 이미 다른 창구가 걸려 있으면 공용 거절 로그를 남기고 false 입니다 — 이미지에 하나다. */
        [[nodiscard]] static bool attach( IGameInstanceClientHook* pHook );
        /** @brief 걸린 창구를 뗍니다(건 이미지가 내려갈 때). 걸려 있지 않으면 할 일이 없다. */
        static void detach( IGameInstanceClientHook* pHook );

        /** @struct Registration @brief 클라이언트 전용 소스가 정적 객체로 창구를 겁니다(이미지에 하나) — 공통 틀 `StaticRegistrar` 라 건 이미지가 내려가면 뗀다. */
        struct Registration final : StaticRegistrar<IGameInstanceClientHook*>
        {
            explicit Registration( IGameInstanceClientHook& hook )
                : StaticRegistrar{ &hook, &IGameInstanceClientHook::attach, &IGameInstanceClientHook::detach }
            {
            }
        };
    };
} // namespace sw
