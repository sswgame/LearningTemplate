/**
 * @file ModuleManager/Host/HostServiceUtil.h
 * @brief App 과 Server 가 호스트 서비스를 세우는 한 줄 — 의존 순서로 세우고, 선 순서를 남기고, 아무도 가져가지 않은 `-gv_*` 를 알립니다.
 * @details 예전에는 두 호스트가 이 세 걸음을 저마다 적어 Server 만 모르는 `-gv_*` 를 조용히 버렸다(App 만 경고). 세우는 길을 하나로 두어 갈라지지 않게 한다.
 *          언리얼은 `FEngineLoop::PreInit` 하나가 게임 · 서버 대상을 함께 세운다.
 */
#pragma once
#include "Engine/HostServiceSet.h"

#include "ModuleManager/ModuleManagerAPI.h"

namespace sw
{
    class CommandLineManager;

    /**
     * @struct HostServiceUtil
     * @brief 호스트 서비스 세우기의 공통 걸음입니다.
     */
    struct SW_MODULEMANAGER_API HostServiceUtil
    {
        /**
         * @brief @p listPart 를 의존 순서로 세웁니다(`startHostServices`). 서면 선 순서를 로그로 남기고, 모듈 · 게임이 모두 선 지금까지 가져간 곳이 없는 `-gv_*` 를 경고합니다.
         * @return 하나라도 서지 못하면 오류를 남기고 false 입니다(선 것은 부르는 쪽의 `destroyAll` 이 내린다).
         */
        [[nodiscard]] static bool startAll( SubsystemCollection& outParts, vector_reference<const HostServiceID> listPart, const HostServiceStartDelegate& start,
                                            const HostServiceStopDelegate& stop, const CommandLineManager& commandLine );

        /**
         * @brief 모르는 `-gv_*` 키를 경고합니다. 파서는 모듈이 선언하기 전이라 모르는 키를 보류 테이블에 남긴다 — 모두 선 뒤에도 가져간 곳이 없으면 오타다.
         * @details 테이블은 비우지 않는다. 핫 리로드로 나중에 올라오는 모듈이 여전히 가져갈 수 있다.
         */
        static void warnUnknownGlobalOverrides( const CommandLineManager& commandLine );
    };
} // namespace sw
