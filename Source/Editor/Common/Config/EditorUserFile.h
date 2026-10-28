/**
 * @file EditorUserFile.h
 * @brief 사용자 파일(`Saved/Editor/` 의 환경설정 · 단축키)을 읽은 결과와, 깨진 파일을 치우는 복구 경로 하나입니다. ImGui 에 의존하지 않습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /**
     * @brief 사용자 파일 하나를 읽은 결과입니다.
     * @details 엔진 필수 데이터와 달리 사용자 파일은 앱이 쓰고 사람이 고치기도 하는 파일이라, 깨졌다고 기동을 멈추지 않는다. 대신 조용히 기본값으로
     *          가지 않는다: 깨진 파일을 `<이름>.corrupt` 로 치우고 오류를 남긴 뒤(`EditorUserFile::quarantineCorruptFile`) 새로 쓴다.
     */
    enum class EditorUserFileResult : uint8
    {
        Loaded = 0, ///< 읽었다
        Missing,    ///< 파일이 없다 — 처음 실행이다. 기본(덮어쓰기 없음)이 곧 그 파일의 내용이다
        Recovered,  ///< 파일이 깨져 `.corrupt` 로 치우고 오류를 남겼다. 부르는 쪽이 새 파일을 쓴다
    };

    /** @struct EditorUserFile @brief 깨진 사용자 파일을 치우는 한 경로입니다. 환경설정과 단축키가 함께 씁니다. */
    struct SW_EDITOR_API EditorUserFile
    {
        /** @brief 치운 파일에 붙는 접미사입니다(`EditorPreferences.json.corrupt`). */
        static constexpr const utf8* kCorruptSuffix = ".corrupt";

        /**
         * @brief 깨진 @p filePath 를 `<filePath>.corrupt` 로 옮기고, 어디로 옮겼는지와 @p reason 을 오류로 남깁니다. 이전 `.corrupt` 는 덮어씁니다.
         * @return 원래 위치에서 치웠으면 true. 옮기지 못했으면 false(그 이유도 오류로 남긴다) — 이때 새 파일을 쓰면 깨진 원본이 사라진다.
         */
        [[nodiscard]] static bool quarantineCorruptFile( string_view filePath, string_view reason );
    };
} // namespace sw::editor
