/**
 * @file ModPackBuilder.h
 * @brief 소스 모드 폴더(`<모드>/content/`, 리소스 경로 그대로의 낱개 파일)를 모드 팩(도메인 없는 패치 팩)으로 묶습니다. 모드 제작 도구와 개발 실행이 같이 쓴다.
 * @details 팩 바이트는 쿠커와 같은 쓰는 곳 하나(`PackWriter`)가 만든다. 묶은 팩도 마운트 전에 같은 검사(`ModSession`)를 지난다 — 여기서는 경로 꼴과 크기 한계만 본다.
 */
#pragma once
#include "Core/Container/string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModSupportSettings.h"

namespace sw
{
    /**
     * @struct ModPackBuilder
     * @brief 소스 폴더를 모드 팩 하나로 씁니다.
     */
    struct SW_KIT_API ModPackBuilder
    {
        /** @brief 소스 모드의 낱개 파일 폴더 이름입니다(`<모드 폴더>/content/`). */
        static constexpr const utf8* kSourceFolderName = "content";
        /** @brief 묶은 모드 팩의 파일 이름입니다(`<모드 폴더>/content.pack`). */
        static constexpr const utf8* kPackFileName = "content.pack";

        /**
         * @brief @p contentFolder 아래 파일 모두를 그 상대 경로(리소스 id)로 담아 @p packPath 에 씁니다.
         * @return 폴더를 못 읽거나, 상대 경로가 정규형(소문자 · `/`)이 아니거나, 파일 수 · 파일 크기가 @p settings 의 한계를 넘거나, 쓰기가 실패하면 이유를 오류로 남기고 false 입니다.
         */
        [[nodiscard]] static bool buildFromFolder( string_view contentFolder, string_view packPath, const ModSupportSettings& settings );
    };
} // namespace sw
