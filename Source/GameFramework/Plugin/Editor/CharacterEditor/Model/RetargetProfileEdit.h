/**
 * @file RetargetProfileEdit.h
 * @brief 리타깃 프로파일 창의 이름 규칙입니다. ImGui 없이 값만 다룹니다.
 * @details 창이 만드는 모든 중간 상태는 `RetargetProfile::toXML` 이 쓴 글을 `loadFromXMLText` 이 다시 읽을 수 있어야 한다 — 되돌리기가 그 글로 돌아오기 때문이다.
 *          본 목록이 빈 사슬과 겹친 사슬 이름은 다시 읽을 때 오류이므로 여기서 애초에 만들지 못하게 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Name/hashed_string.h"

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
        /** @brief 이름과 두 본 목록 글로 새 사슬을 만듭니다. 이름을 쓸 수 없거나 한쪽 본 목록이 비면 false 입니다(빈 본 목록은 다시 읽을 때 오류입니다). */
        [[nodiscard]] static bool makeChain( const vector<RetargetChain>& listChain, string_view name, string_view sourceText, string_view targetText, RetargetChain& outChain );
        /** @brief 사슬 이름을 바꿉니다. 범위 밖이거나 쓸 수 없는 이름이면 false 이고 그대로입니다. */
        [[nodiscard]] static bool renameChain( vector<RetargetChain>& inoutListChain, size_t chainIndex, const hashed_string& name );
        /** @brief 목표 자세에 더할 수 있는 본이면 true 입니다. 이름이 비었거나 같은 본을 두 번 적으면 다시 읽을 때 오류입니다. */
        static bool canAddPoseBone( const vector<RetargetPoseBone>& listPose, const hashed_string& bone );
        /** @brief 본 이름 테이블에 더할 수 있는 짝이면 true 입니다. 이미 짝이 있는 대상 본을 또 적으면 앞 짝이 이기고 새 줄은 말없이 버려지므로 받지 않습니다. */
        static bool canAddBonePair( const vector<RetargetBonePair>& listPair, const hashed_string& source, const hashed_string& target );
    };
} // namespace sw::editor
