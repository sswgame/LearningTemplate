/**
 * @file ModPackBuilder.h
 * @brief 소스 모드 폴더(`<모드>/content/`, 리소스 경로 그대로의 낱개 파일)를 모드 팩(도메인 없는 패치 팩)으로 묶습니다. 모드 제작 도구와 개발 실행이 같이 쓴다.
 * @details 팩 바이트는 쿠커와 같은 쓰는 곳 하나(`PackStreamWriter`)가 만든다. 묶은 팩도 마운트 전에 같은 검사(`ModSession`)를 지난다 — 여기서는 경로 꼴과 크기 한계만 본다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModSupportSettings.h"

namespace sw
{
    struct AssetPackIndex;
    struct PackStorageSettings;

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
         * @return 폴더를 못 읽거나, 상대 경로가 정규형(소문자 · `/`)이 아니거나, 파일 수 · 파일 크기가 @p settings 의 한계를 넘거나, 쓰기가 실패하면(저장 방식은 쿠커와 같은 @p storage) 이유를 오류로 남기고 false 입니다.
         */
        [[nodiscard]] static bool buildFromFolder( const AbsolutePath& contentFolder, const AbsolutePath& packPath, const ModSupportSettings& settings, const PackStorageSettings& storage );

    private:
        /**
         * @brief 모드 파일 하나(@p relativePath, 바이트 @p bytes)의 정체성을 팩 색인 @p inoutIndex 에 더합니다 — 우리 형식 글은 머리 `<?asset guid?>`, 원본은 곁의 `.meta`.
         * @details 팩 루트의 `assetindex.xml` 로 실려 런타임 색인이 도메인 색인 뒤 층으로 합친다 — 모드가 새로 더한 에셋도 GUID 참조로 풀린다. 정체성이 없는 파일은 건너뛴다.
         */
        static void addIdentity( const string& relativePath, const vector<uint8>& bytes, AssetPackIndex& inoutIndex );
    };
} // namespace sw
