/**
 * @file ProjectSettingsTableView.h
 * @brief 프로젝트 설정 창의 표 페이지(반사 형식 없는 설정 JSON)를 그립니다. 키 열 + 값 칸은 프로퍼티 그리드와 같은 모양이고, 배열은 `+ Add` · `x`, 객체는 키 칸에 치고 Enter 로 더합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

namespace sw::editor
{
    class ProjectSettingsJSONDocument;

    /**
     * @class ProjectSettingsTableView
     * @brief 표 문서 하나를 트리 행으로 그리고 편집을 문서에 넣습니다. 더하기 · 빼기는 행을 다 그린 뒤에 적용한다(그리는 중에 자식 목록을 바꾸지 않는다 — `EditorPropertyGrid` 와 같다).
     * @details 글은 칸에서 바로 반영하고, 수는 고치는 동안 초안에 두었다가 칸을 떠날 때 검사해 넣는다 — 치는 도중의 `0.` · `-` 를 거절해 글자가 사라지지 않게 한다.
     *          자체 테스트 표식은 `projectsettings.table.<키 경로>`(경로는 점으로 잇고 배열 원소는 번호) · `….add` · `….remove` 다.
     */
    class ProjectSettingsTableView
    {
    public:
        ProjectSettingsTableView();

        /** @brief @p document 를 그립니다. 값을 바꿨으면 true 입니다. 넣지 못한 수 글이 있으면 @p outStatus 에 그 이유를 적는다. */
        bool draw( ProjectSettingsJSONDocument& document, string& outStatus );
        /** @brief 다른 페이지를 고르거나 되돌릴 때 초안을 버립니다. */
        void resetDraft();

    private:
        enum class PendingEdit : uint8
        {
            None,
            AddItem,
            AddMember,
            Remove,
        };

        void drawNode( ProjectSettingsJSONDocument& document, uint32 node, const string& label, const string& path, uint32 parentNode, size_t childIndex );
        void drawLeaf( ProjectSettingsJSONDocument& document, uint32 node, const string& path );
        void drawRemoveButton( const ProjectSettingsJSONDocument& document, uint32 parentNode, size_t childIndex, const string& path );
        bool applyPendingEdit( ProjectSettingsJSONDocument& document );

    private:
        string                                                         _draftText;     ///< 고치는 중인 수 칸의 글
        string                                                         _pendingKey;    ///< 더할 객체 멤버 키
        string                                                         _status;        ///< 이번 그리기의 상태 글(넣지 못한 수)
        uint32                                                         _draftNode;     ///< 초안이 가리키는 수 노드(없으면 `kNoNode`)
        uint32                                                         _pendingNode;   ///< 더하기 · 빼기의 대상 컨테이너
        size_t                                                         _pendingIndex;  ///< 뺄 자식 번호
        unordered_map<uint32, fixed_string<constant::buffer::kMax256>> _mapNewKeyText; ///< 객체마다 "새 키" 칸의 글(키는 객체 노드 번호 — 칸마다 따로다)
        PendingEdit                                                    _pendingEdit;
        uint8                                                          _bEdited : 1; ///< 이번 그리기에 글 · 불을 바꿨다

        static constexpr uint32 kNoNode = ~0u;
    };
} // namespace sw::editor
