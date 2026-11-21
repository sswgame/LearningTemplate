/**
 * @file ModuleManager/IReloadableModuleClient.h
 * @brief 핫 리로드되는 모듈 하나의 주인(모듈 호스트)이 리로드 매니저에 거는 창구 `IReloadableModuleClient` 입니다.
 * @details 모듈마다의 칸 넷(리로드 직전 · 직후 · 결함 · 새 라이브러리 검사)을 따로 걸면 하나를 빠뜨려도 컴파일된다 — 키트는 결함 · 검사 칸이 비어 있었다.
 *          순수 가상 넷을 한 번에 걸게 해서 모듈 주인이 네 가지를 모두 정하게 한다. 언리얼 `IModuleInterface` 의 `PreUnloadCallback` · `PostLoadCallback` 자리다.
 *          호스트는 다중 상속을 하지 않고 이 창구를 구현한 멤버 객체를 건다(`ILiveReloadManager::setModuleClient`).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "ModuleManager/ModuleLibraryHandle.h"

namespace sw
{
    /**
     * @class IReloadableModuleClient
     * @brief 리로드 매니저가 모듈 하나의 교체 단계마다 부르는 주인 쪽 창구입니다. 게임 스레드에서 불립니다.
     */
    class IReloadableModuleClient
    {
    public:
        virtual ~IReloadableModuleClient() = default;

        /** @brief 옛 라이브러리를 언로드하기 직전에 불립니다(옛 라이브러리가 있을 때만). 받은 인스턴스를 놓습니다. */
        virtual void onBeforeReload() = 0;
        /** @brief 새 라이브러리로 바꾼 직후(또는 기동의 `runAfterReload`)에 `ModuleCallGuard` 안에서 불립니다. API 를 묶고 인스턴스를 세웁니다. */
        virtual void onAfterReload( ModuleLibraryHandle library ) = 0;
        /**
         * @brief `onAfterReload` 가 결함(접근 위반 등)을 냈을 때 불립니다. 인자는 예외 코드(Windows) · 시그널 번호(리눅스)입니다.
         * @details 그 모듈에서 받은 것(인스턴스, API 테이블)을 **그 모듈을 부르지 않고** 버린다 — 반쯤 만든 인스턴스를 파괴하는 코드도 같은 모듈에 있다.
         */
        virtual void onReloadFault( uint32 faultCode ) = 0;
        /** @brief 옛 라이브러리가 아직 도는 동안(prepare) 새 라이브러리를 받아들일지 정합니다. false 면 옛 라이브러리를 그대로 둡니다. */
        [[nodiscard]] virtual bool isLibraryUsable( ModuleLibraryHandle library ) = 0;
    };
} // namespace sw
