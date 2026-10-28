/**
 * @file AdminToolPanel.h
 * @brief GM 도구 패널입니다. 계정 조회, 자산 지급과 회수, 제재, 감사 열람, 신고 처리, 점검과 공지, 라이브 이벤트 운영을 한 창에서 합니다(언리얼에는 없고, 운영 도구 · Nakama 콘솔의 계정 화면에 해당합니다).
 * @details 명령은 `AdminToolSession` 이 제공자로 보냅니다. 서버가 없으면 메모리 제공자가 같은 모양의 응답을 돌려주므로 창이 비지 않습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Editor/Common/GUI/IEditorPanel.h"

#include "GameFramework/Kits/Feature/Online/Admin/Shared/AdminProtocol.h"

namespace sw::editor
{
    /**
     * @class AdminToolPanel
     * @brief GM 도구 패널입니다. 입력 칸의 값만 들고 있고 결과는 세션에서 읽어 그립니다.
     */
    class AdminToolPanel final : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 열기(`EditorPanelManager::setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "admin.tool";
        /** @brief 창 제목입니다(ImGui 창 이름 — 자체 테스트가 창을 찾을 때 이 상수를 쓴다). */
        static constexpr const utf8* kPanelTitle = "GM Tool";
        /** @brief 비밀번호 입력 칸의 크기(끝 0 포함)입니다. */
        static constexpr size_t kPasswordCapacity = 128;

        AdminToolPanel();

        const utf8*      getPanelTitle() const override { return kPanelTitle; }
        bool             isToolPanel() const override { return true; }
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 96.0f, 45.0f ); }
        void             drawContent() override;

    private:
        void drawProviderBar();
        void drawAccountSection();
        void drawAdjustSection();
        void drawSanctionSection();
        void drawAuditSection();
        void drawReportSection();
        void drawDirectorySection();
        void drawLiveEventSection();

        string              _gmAddressText;
        string              _loginNameText;
        utf8                _arrPassword[kPasswordCapacity]; ///< 입력 중인 GM 비밀번호. 연결을 누르면 지운다
        string              _accountText;
        string              _assetText;
        string              _memoText;
        string              _reasonText;
        string              _auditSubjectText;
        string              _scopeText;
        string              _maintenanceKeyText;
        string              _noticeText;
        string              _eventJSONText;
        string              _eventIDText;
        int64               _amount;
        int64               _sanctionSeconds;
        int64               _maintenanceSeconds;
        int64               _noticeSeconds;
        int64               _noticeID;
        int32               _noticePriority;
        ServiceSanctionKind _sanctionKind;
        bool                _bRefund;
    };
} // namespace sw::editor
