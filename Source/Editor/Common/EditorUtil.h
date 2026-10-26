/**
 * @file EditorUtil.h
 * @brief EditorModule 유틸리티입니다(파이썬 스크립트 명령, 계층 뱃지). 경로는 EditorPaths. UI 에 의존하지 않습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Process/Process.h"

namespace sw::editor
{
    /** @brief 에디터 설정 경로 해석과 공통 유틸리티입니다. ImGui 에 의존하지 않습니다(폰트는 GUI/EditorFontSetup). */
    class EditorUtil
    {
    public:
        /**
         * @brief 저장소 파이썬 스크립트 @p scriptPath 를 띄우는 명령입니다. 스크립트 인자는 돌려받은 `_listArgument` 뒤에 덧붙인다.
         * @details 실행기는 저장소 스크립트가 쓰는 것과 같다(Windows 는 런처 `py -3`, 그 밖은 `python3`).
         */
        static ProcessCommand makePythonScriptCommand( string scriptPath )
        {
#if defined( SW_PLATFORM_WINDOWS )
            return ProcessCommand{
                "py",
                { "-3", std::move( scriptPath ) }
            };
#else
            return ProcessCommand{ "python3", { std::move( scriptPath ) } };
#endif
        }

        /**
         * @brief 계층 라벨에 붙일 `[Category]` 뱃지를 덧붙입니다.
         * @param category 컴포넌트 타입의 리플렉션 Category (`TypeInfo::getCategory`).
         * @param inoutBadge 누적 중인 뱃지 문자열. 비어 있지 않으면 앞에 공백이 붙습니다.
         * @details 타입 이름이 아니라 Category 로 고르므로 게임이 넣은 컴포넌트도 뱃지가 붙고, 엔진이 컴포넌트를 늘려도 패널을
         *          고칠 필요가 없습니다("컴포넌트 추가" 메뉴와 같은 기준). 같은 Category 는 한 번만 넣습니다.
         */
        static void appendCategoryBadge( string_view category, string& inoutBadge );
    };
} // namespace sw::editor
