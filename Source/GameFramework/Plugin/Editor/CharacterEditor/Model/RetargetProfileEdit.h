/**
 * @file RetargetProfileEdit.h
 * @brief 리타깃 프로파일 창의 이름 규칙입니다. ImGui 없이 값만 다룹니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Retarget/RetargetProfile.h"

namespace sw::editor
{
    /**
     * @struct RetargetProfileEdit
     * @brief 본 이름 글과 새 항목 규칙입니다.
     */
    struct RetargetProfileEdit
    {
        /** @brief "a, b" 를 이름 목록으로 읽습니다. */
        static vector<hashed_string> parseNames( string_view text );
        /** @brief 이름 목록을 "a, b" 로 잇습니다. */
        static string joinNames( const vector<hashed_string>& listName );
        /** @brief 새 사슬 이름으로 쓸 수 있으면 true 입니다. 비었거나 겹친 이름은 다시 읽을 때 오류입니다. */
        static bool canAddChain( const vector<RetargetChain>& listChain, const hashed_string& name );
        /** @brief 목표 자세에 더할 수 있는 본이면 true 입니다. 같은 본을 두 번 적으면 다시 읽을 때 오류입니다. */
        static bool canAddPoseBone( const vector<RetargetPoseBone>& listPose, const hashed_string& bone );
    };
} // namespace sw::editor
