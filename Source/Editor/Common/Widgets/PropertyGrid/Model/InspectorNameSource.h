/**
 * @file InspectorNameSource.h
 * @brief 인스펙터 이름 칸의 고르기 목록 출처입니다 — `PROPERTY( Meta = "NameSource=<출처>" )` 인 `hashed_string` 칸을 글 대신 목록에서 고르게 합니다.
 * @details 출처는 데이터 표의 이름이다(충돌 프로필 · 충돌 채널 — `physicssettings.xml`). 언리얼 `GetOptions` 메타 · 충돌 프리셋 고르기 목록과 같은 자리다.
 *          모르는 출처는 오류 한 번이고 글 칸으로 그린다(이름은 PROPERTY 검증이 따로 본다). ImGui 를 모른다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/String/Name/hashed_string.h"

namespace sw::editor
{
    /** @brief 이름 칸의 고르기 목록 출처입니다(파일 머리말). */
    struct InspectorNameSource
    {
        /** @brief 출처 @p source 의 이름을 표 차례로 @p outListName 에 담습니다(먼저 비운다). 아는 출처면 true, 모르면 오류를 한 번 남기고 false 입니다. */
        [[nodiscard]] static bool collectNames( const hashed_string& source, vector<hashed_string>& outListName );
    };
} // namespace sw::editor
