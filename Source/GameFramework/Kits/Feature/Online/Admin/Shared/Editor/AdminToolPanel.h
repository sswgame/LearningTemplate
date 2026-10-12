/**
 * @file AdminToolPanel.h
 * @brief GM 도구 패널입니다. 계정 조회, 자산 지급과 회수, 제재, 감사 열람을 한 창에서 합니다(언리얼에는 없고, 운영 도구 · Nakama 콘솔의 계정 화면에 해당합니다).
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
        AdminToolPanel();

        const utf8* getPanelTitle() const override { return "GM Tool"; }
        bool        isToolPanel() const override { return true; }
        float2      getInitialPanelSize() const override { return float2{ 720.0f, 860.0f }; }
        void        drawContent() override;

    private:
        void drawProviderBar();
        void drawAccountSection();
        void drawAdjustSection();
        void drawSanctionSection();
        void drawAuditSection();

        string              _accountText;
        string              _assetText;
        string              _memoText;
        string              _reasonText;
        string              _auditSubjectText;
        int64               _amount;
        int64               _sanctionSeconds;
        ServiceSanctionKind _sanctionKind;
        bool                _bRefund;
    };
} // namespace sw::editor
