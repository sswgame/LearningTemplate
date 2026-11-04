/**
 * @file EditorDataText.h
 * @brief 에디터 데이터 파일(명령 · 패널 이름)이 든 영어 원문을 지금 언어로 풉니다. 원문이 현지화 키다(`Meta = "Localizable"` 프로퍼티를 수집기가 모은다 — `TextWidget._text` 와 같은 길).
 * @details 번역이 없으면 원문 그대로다(번역 없는 언어 · 원문 언어). 언어가 바뀌면 부르는 쪽이 현지화 글 판(`getEditorDataTextRevision`)을 보고 다시 푼다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    struct SW_EDITOR_API EditorDataText
    {
        /** @brief @p source 의 지금 언어 글입니다. 빈 글은 빈 글이다. */
        static string localize( string_view source );
        /** @brief 현지화 글 판입니다. 언어 · 표가 바뀌면 오른다(현지화 서비스가 없으면 0). */
        static uint32 getRevision();
    };
} // namespace sw::editor
