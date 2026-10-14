/**
 * @file CharacterEditorPanel.h
 * @brief 캐릭터 편집 창의 첫 단계 패널 — 소켓 파일 편집(목록 · 추가 · 이름 바꾸기 · 변환 · 좌우 대칭 · 저장)과 외형 선택 편집(슬롯 · 체형 · 꾸미기 값). 미리보기 씬이 없다.
 * @details 데이터 편집은 모두 `SocketEditModel` · `AppearanceEditModel` 이 하고 이 패널은 그리기만 한다. 화면 전체 틀(미리보기 씬 · 뷰포트 기즈모 · 문서 패널 계약)은
 *          `EditorMultiWorld` 단계가 끝난 뒤 이어 붙인다(`docs/plans/EditorCharacterEditor.md` 단계 1).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/String/fixed_string.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/GUI/IEditorPanel.h"

#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/AppearanceEditModel.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/SocketEditModel.h"

namespace sw::editor
{
    /**
     * @class CharacterEditorPanel
     * @brief 캐릭터 편집 확장의 도구 패널입니다.
     */
    class CharacterEditorPanel final : public IEditorPanel
    {
    public:
        CharacterEditorPanel();

        const utf8*        getPanelTitle() const override { return "Character Editor"; }
        bool               isToolPanel() const override { return true; }
        void               drawContent() override;
        [[nodiscard]] bool saveDocument() override;
        bool               canSaveDocument() const override { return _sockets.isDirty(); }

        SocketEditModel&     getSocketModel() { return _sockets; }
        AppearanceEditModel& getAppearanceModel() { return _appearance; }

    private:
        void drawFileRow();
        void drawSocketTab();
        void drawSocketList();
        void drawSocketDetail( const SocketDef& socket );
        void drawAppearanceTab();
        void drawHistoryRow();
        void ensureKindTable();
        /** @brief 모델 편집 결과를 상태 줄에 보입니다. 실패하면 이유를, 성공하면 빈 줄을 둡니다. */
        void reportEdit( bool bSucceeded );

        SocketEditModel                       _sockets;
        AppearanceEditModel                   _appearance;
        fixed_string<constant::kMaxPathSize>  _path;
        fixed_string<constant::kMaxBuffer128> _newName;
        fixed_string<constant::kMaxBuffer128> _renameBuffer;
        string                                _status;
        hashed_string                         _selectedSocket;
        bool                                  _bKindTableReady;
    };
} // namespace sw::editor
