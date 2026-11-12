/**
 * @file Engine/Config/IGameAutoplayQuery.h
 * @brief 개발 스위치가 게임의 자동 플레이를 켰는지 묻는 창구입니다 — 게임 모드는 개발 장치(`Engine/Config/GameAutoplay.h`)를 모르고 이 칸만 읽습니다.
 * @details 구현은 개발 구성에만 있는 `GameAutoplaySubsystem`(DevTools/Diagnostics/GameAutoplaySwitch — 배포 구성이 짓지 않는다)이고, 배포본에서는 칸이 비어 게임 모드는 씬 데이터(`_bAutoPlay`)만 봅니다.
 *          언리얼에서 치트 매니저(`UCheatManager`, 배포본에 없음)가 게임 상태를 바꾸고 게임 코드는 그 값만 읽는 갈림과 같은 자리입니다.
 *          서비스 칸의 정의는 이 폴더에 있어 DevTools 를 짓지 않는 구성에서도 칸을 읽는 쪽이 링크됩니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Subsystem/ServiceSlot.h"

namespace sw
{
    /**
     * @class IGameAutoplayQuery
     * @brief 개발 스위치(`-gv_<게임>AutoPlay` · 에디터 툴바 · 콘솔 `autoplay`)의 자동 플레이 질의입니다. 게임 스레드와 틱 워커에서 읽습니다(값 하나를 읽을 뿐이다).
     */
    class SW_API IGameAutoplayQuery
    {
    public:
        virtual ~IGameAutoplayQuery() = default;

        /** @brief 지금 올라온 게임의 개발 스위치가 자동 플레이를 켰으면 true 입니다(등록이 없으면 false). */
        [[nodiscard]] virtual bool isGameAutoplayOn() const = 0;

        SW_SERVICE_SLOT( IGameAutoplayQuery, Optional );
    };
} // namespace sw
