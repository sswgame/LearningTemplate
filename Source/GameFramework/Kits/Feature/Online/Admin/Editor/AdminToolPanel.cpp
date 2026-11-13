#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Editor/AdminToolPanel.h"

#include "Core/Memory/Memory.h"
#include "Core/Time/WallClock.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Kits/Feature/Online/Admin/Editor/AdminToolSession.h"
#include "GameFramework/Kits/Feature/Online/Admin/Editor/Model/AdminToolModel.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct AdminToolPanelInternal
        {
            static constexpr int64 kDefaultAmount          = 100;
            static constexpr int64 kDefaultSanctionSeconds = 3600;
            static constexpr int64 kDefaultWindowSeconds   = 3600;
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    AdminToolPanel::AdminToolPanel()
        : IEditorPanel( false )
        , _gmAddressText{}
        , _loginNameText{}
        , _arrPassword{}
        , _accountText{}
        , _assetText{ "cur.gold" }
        , _memoText{}
        , _reasonText{ "gm.notice" }
        , _auditSubjectText{}
        , _scopeText{ "all" }
        , _maintenanceKeyText{ "maintenance.default" }
        , _announcementText{}
        , _eventJSONText{}
        , _eventIDText{}
        , _amount{ AdminToolPanelInternal::kDefaultAmount }
        , _sanctionSeconds{ AdminToolPanelInternal::kDefaultSanctionSeconds }
        , _maintenanceSeconds{ AdminToolPanelInternal::kDefaultWindowSeconds }
        , _announcementSeconds{ AdminToolPanelInternal::kDefaultWindowSeconds }
        , _announcementID{ 1 }
        , _announcementPriority{ 0 }
        , _sanctionKind{ ServiceSanctionKind::ChatMute }
        , _bRefund{ SW_FALSE }
    {
        _accountText = "100"; // 메모리 제공자의 첫 계정(`FakeAdminProvider::kSeedPlayerID`)이다
    }

    void AdminToolPanel::drawContent()
    {
        if ( AdminToolSession* pConnectionSession = findSubsystem<AdminToolSession>() )
            pConnectionSession->updateConnection();
        drawProviderBar();
        ImGui::Separator();
        drawAccountSection();
        drawAdjustSection();
        drawSanctionSection();
        drawAuditSection();
        drawReportSection();
        drawDirectorySection();
        drawLiveEventSection();

        const AdminToolSession* pSession = findSubsystem<AdminToolSession>();
        if ( pSession == nullptr )
            return; // 이 확장의 서브시스템이 아직 걸리지 않았다(프레임 앞 sync 전)
        const AdminToolSession& session = *pSession;
        ImGui::Separator();
        EditorWidgets::drawPanelStatus( session.getStatusLine().empty() ? "Ready" : session.getStatusLine().c_str() );
    }

    void AdminToolPanel::drawProviderBar()
    {
        AdminToolSession* pSession = findSubsystem<AdminToolSession>();
        if ( pSession == nullptr )
            return; // 이 확장의 서브시스템이 아직 걸리지 않았다(프레임 앞 sync 전)
        AdminToolSession& session = *pSession;
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdPluginFormat", "Provider", TextArgumentList().addText( "providerName", session.getProvider().getProviderName() ) ).c_str() );
        if ( session.getProvider().isLive() == false )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(no server needed)" );
            // 메모리 제공자일 때만 보내는 GM 의 등급을 고른다. 서버에서는 계정의 등급을 서버가 읽는다.
            FakeAdminProvider&  fake       = session.getFakeProvider();
            constexpr AdminRole kArrRole[] = { AdminRole::Viewer, AdminRole::Support, AdminRole::Operator, AdminRole::Super };
            EditorChrome::setNextItemWidthColumns( 19.0f );
            if ( EditorWidgets::beginCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "ActingRole" ), "Acting role" ), toString( fake.getActingRole() ) ) )
            {
                for ( const AdminRole role : kArrRole )
                {
                    if ( ImGui::Selectable( toString( role ), role == fake.getActingRole() ) )
                        fake.setActingRole( role );
                }
                ImGui::EndCombo();
            }
            EditorSelfTestMarks::record( "admin.role" );
        }

        // 서버의 GM 포트(서버 설정 `_adminServicePort`)에 연결한다. 로그인은 `getConnection().bindLoginHook` 으로 끼운 쪽이 한다.
        EditorWidgets::drawTextField( "GM port (host:port)", _gmAddressText, 200.0f * EditorThemeUtil::getDPIScale() );
        // 계정 이름 · 비밀번호는 로그인을 맡은 쪽(조립)이 처리기를 걸었을 때만 받는다. 없으면 로그인 없이 연결하고 서버가 GM 명령을 거절한다는 것을 알린다.
        const bool bNeedsLogin = session.hasCredentialsHandler();
        if ( bNeedsLogin )
        {
            EditorWidgets::drawTextField( "GM account", _loginNameText, 160.0f * EditorThemeUtil::getDPIScale() );
            EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdPluginField", "GMPassword" ) );
            EditorChrome::setNextItemWidthColumns( 19.0f );
            EditorWidgets::inputText( SW_WIDGET_ID( "##GMpassword" ), _arrPassword, kPasswordCapacity, ImGuiInputTextFlags_Password );
        }
        else
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "NoLoginHandlerIsBoundGMCommandsAre" ) );
        }
        // 실패한 연결도 다시 걸 수 있다 - 예전에는 Failed 를 연결된 것으로 보아 Connect 단추가 사라져 Disconnect 를 먼저 눌러야 했다.
        const bool bCanOpen = session.getConnection().canOpen();
        if ( bCanOpen && ImGui::Button( SW_LOCKEY( "EdPlugin", "Connect" ) ) )
        {
            const expected<void> opened =
                bNeedsLogin ? session.connectWithLogin( _gmAddressText, "editor", _loginNameText, _arrPassword ) : session.connectLive( _gmAddressText, "editor" );
            Memory::set( _arrPassword, 0, sizeof( _arrPassword ) ); // 비밀번호를 입력 칸에 남기지 않는다(로그인을 맡은 쪽이 받아 갔다)
            if ( opened.has_value() == false )
                SW_LOG_WARNING_ID( "AdminToolPanel.AdminToolGMConnection", "Admin tool: GM connection failed - %# (%#)", opened.error().message(), opened.error().detail() );
        }
        if ( bCanOpen == false && ImGui::Button( SW_LOCKEY( "EdPlugin", "Disconnect" ) ) )
            session.disconnectLive();
        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdPluginFormat", "GMConnection", TextArgumentList().addText( "stateText", session.getConnection().getStateText() ) ).c_str() );
    }

    void AdminToolPanel::drawAccountSection()
    {
        AdminToolSession* pSession = findSubsystem<AdminToolSession>();
        if ( pSession == nullptr )
            return; // 이 확장의 서브시스템이 아직 걸리지 않았다(프레임 앞 sync 전)
        AdminToolSession& session = *pSession;
        EditorWidgets::drawSectionHeader( SW_LOCKEY( "EdPluginWidget", "Account" ) );

        EditorWidgets::drawTextField( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "AccountIdHex" ), "Account id (hex)" ), _accountText, 160.0f * EditorThemeUtil::getDPIScale() );
        EditorSelfTestMarks::record( "admin.account" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Lookup" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "Lookup" ) ) )
        {
            AccountID accountID = kInvalidAccountID;
            if ( AdminToolModel::parseAccountID( _accountText, accountID ) )
                session.requestLookup( accountID );
            else
                SW_LOG_WARNING_ID( "AdminToolPanel.AdminToolAccountId", "Admin tool: account id must be a non-zero hex number" );
        }
        EditorSelfTestMarks::record( "admin.lookup" );

        if ( session.hasLookup() == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdPlugin", "LookUpAnAccountToSeeItsBalances" ) );
            return;
        }

        const AdminReply& lookup = session.getLookup();
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdPluginFormat", "Id", TextArgumentList().addText( "displayName", lookup._identity._displayName.c_str() ).addText( "accountID", AdminToolModel::formatAccountID( lookup._identity._accountID ).c_str() ).addText( "presence", lookup._bOnline == SW_TRUE ? "online" : "offline" ) ).c_str() );

        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##AdminBalance", 2, kTableFlags ) )
        {
            ImGui::TableSetupColumn( "Asset" );
            ImGui::TableSetupColumn( "Amount" );
            ImGui::TableHeadersRow();
            for ( const LedgerBalance& balance : lookup._listBalance )
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                ImGui::TextUnformatted( balance._assetID.c_str() );
                ImGui::TableSetColumnIndex( 1 );
                ImGui::TextUnformatted( AdminToolModel::formatBalance( balance ).c_str() );
            }
            ImGui::EndTable();
        }

        for ( int32 kindIndex = 0; kindIndex < static_cast<int32>( ServiceSanctionKind::Count ); ++kindIndex )
        {
            const string state = AdminToolModel::formatSanction( lookup._sanction._arrUntilMs[kindIndex], WallClock::nowUnixMilliseconds().count() );
            ImGui::Text( "%s: %s", getEnumName( static_cast<ServiceSanctionKind>( kindIndex ) ), state.c_str() );
            if ( kindIndex + 1 < static_cast<int32>( ServiceSanctionKind::Count ) )
                ImGui::SameLine();
        }
    }

    void AdminToolPanel::drawAdjustSection()
    {
        AdminToolSession* pSession = findSubsystem<AdminToolSession>();
        if ( pSession == nullptr )
            return; // 이 확장의 서브시스템이 아직 걸리지 않았다(프레임 앞 sync 전)
        AdminToolSession& session = *pSession;
        EditorWidgets::drawSectionHeader( SW_LOCKEY( "EdPluginWidget", "GrantRevoke" ) );

        EditorWidgets::drawTextField( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "AssetId" ), "Asset id" ), _assetText, 160.0f * EditorThemeUtil::getDPIScale() );
        EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdPluginField", "Amount" ) );
        EditorChrome::setNextItemWidthColumns( 21.0f );
        EditorWidgets::inputScalar( SW_WIDGET_ID( "##Amount" ), ImGuiDataType_S64, &_amount, nullptr, nullptr, nullptr, 0, "admin.amount" );
        EditorWidgets::drawTextField( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "MemoTicket" ), "Memo (ticket)" ), _memoText, 240.0f * EditorThemeUtil::getDPIScale() );
        EditorSelfTestMarks::record( "admin.memo" );
        ImGui::Checkbox( SW_LOCKEY( "EdPlugin", "RefundRevokeMayLeaveADebt" ), &_bRefund );

        AccountID  accountID     = kInvalidAccountID;
        const bool bAccountValid = AdminToolModel::parseAccountID( _accountText, accountID );
        // 칸은 크기다 - 음수를 그대로 두면 Grant 가 회수하고 Revoke 가 지급했다.
        ImGui::BeginDisabled( bAccountValid == false || AdminToolModel::isAdjustAmountValid( _amount ) == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "Grant" ) ) )
            session.requestAdjust( accountID, _assetText, _amount, false, _memoText );
        EditorSelfTestMarks::record( "admin.grant" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Revoke" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "Revoke" ) ) )
            session.requestAdjust( accountID, _assetText, -_amount, _bRefund, _memoText );
        EditorSelfTestMarks::record( "admin.revoke" );
        ImGui::EndDisabled();
    }

    void AdminToolPanel::drawSanctionSection()
    {
        AdminToolSession* pSession = findSubsystem<AdminToolSession>();
        if ( pSession == nullptr )
            return; // 이 확장의 서브시스템이 아직 걸리지 않았다(프레임 앞 sync 전)
        AdminToolSession& session = *pSession;
        EditorWidgets::drawSectionHeader( SW_LOCKEY( "EdPluginWidget", "Sanction" ) );

        constexpr ServiceSanctionKind kArrKind[] = { ServiceSanctionKind::ChatMute, ServiceSanctionKind::Suspend, ServiceSanctionKind::Ban };
        EditorChrome::setNextItemWidthColumns( 21.0f );
        if ( EditorWidgets::beginCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "Kind" ), "Kind" ), getEnumName( _sanctionKind ) ) )
        {
            for ( const ServiceSanctionKind kind : kArrKind )
            {
                if ( ImGui::Selectable( getEnumName( kind ), kind == _sanctionKind ) )
                    _sanctionKind = kind;
            }
            ImGui::EndCombo();
        }
        EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdPluginField", "Seconds" ) );
        EditorChrome::setNextItemWidthColumns( 21.0f );
        EditorWidgets::inputScalar( SW_WIDGET_ID( "##Seconds" ), ImGuiDataType_S64, &_sanctionSeconds );
        EditorWidgets::drawTextField( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "ReasonCode" ), "Reason code" ), _reasonText, 160.0f * EditorThemeUtil::getDPIScale() );

        AccountID  accountID     = kInvalidAccountID;
        const bool bAccountValid = AdminToolModel::parseAccountID( _accountText, accountID );
        ImGui::BeginDisabled( bAccountValid == false );
        // 기간 0 은 해제의 뜻이다 - 거는 단추가 해제를 보내지 않게 한다.
        ImGui::BeginDisabled( AdminToolModel::isSanctionDurationValid( _sanctionSeconds ) == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "ApplySanction" ) ) )
            session.requestSanction( accountID, _sanctionKind, _sanctionSeconds, _reasonText, _memoText );
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "admin.sanction.apply" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "LiftSanction" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "LiftSanction" ) ) )
            session.requestSanction( accountID, _sanctionKind, 0, _reasonText, _memoText );
        EditorSelfTestMarks::record( "admin.sanction.lift" );
        ImGui::EndDisabled();
    }

    void AdminToolPanel::drawAuditSection()
    {
        AdminToolSession* pSession = findSubsystem<AdminToolSession>();
        if ( pSession == nullptr )
            return; // 이 확장의 서브시스템이 아직 걸리지 않았다(프레임 앞 sync 전)
        AdminToolSession& session = *pSession;
        EditorWidgets::drawSectionHeader( SW_LOCKEY( "EdPluginWidget", "AuditLog" ) );

        EditorWidgets::drawTextField( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "SubjectPrefix" ), "Subject prefix" ), _auditSubjectText, 200.0f * EditorThemeUtil::getDPIScale() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "ListAudit" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "ListAudit" ) ) )
            session.requestAudit( _auditSubjectText );
        EditorSelfTestMarks::record( "admin.audit.list" );

        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##AdminAudit", 5, kTableFlags, EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, 6.0f ) ) ) == false )
            return;
        ImGui::TableSetupColumn( "Time (UTC)" );
        ImGui::TableSetupColumn( "Action" );
        ImGui::TableSetupColumn( "Subject" );
        ImGui::TableSetupColumn( "Change" );
        ImGui::TableSetupColumn( "Memo" );
        ImGui::TableHeadersRow();
        for ( const ServiceAuditEntry& entry : session.getAuditRows() )
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            ImGui::TextUnformatted( AdminToolModel::formatClock( entry._timeMs ).c_str() );
            ImGui::TableSetColumnIndex( 1 );
            ImGui::TextUnformatted( entry._action.c_str() );
            ImGui::TableSetColumnIndex( 2 );
            ImGui::TextUnformatted( entry._subject.c_str() );
            ImGui::TableSetColumnIndex( 3 );
            ImGui::Text( "%s -> %s", entry._before.c_str(), entry._after.c_str() );
            ImGui::TableSetColumnIndex( 4 );
            ImGui::TextUnformatted( entry._memo.c_str() );
        }
        ImGui::EndTable();
    }

    void AdminToolPanel::drawReportSection()
    {
        AdminToolSession* pSession = findSubsystem<AdminToolSession>();
        if ( pSession == nullptr )
            return; // 이 확장의 서브시스템이 아직 걸리지 않았다(프레임 앞 sync 전)
        AdminToolSession& session = *pSession;
        EditorWidgets::drawSectionHeader( SW_LOCKEY( "EdPluginWidget", "Reports" ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "OpenReports" ) ) )
            session.requestReports( true );
        EditorSelfTestMarks::record( "admin.report.list" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "ClosedReports" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "ClosedReports" ) ) )
            session.requestReports( false );

        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##AdminReport", 5, kTableFlags, EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, 7.0f ) ) ) == false )
            return;
        ImGui::TableSetupColumn( "Time (UTC)" );
        ImGui::TableSetupColumn( "Reason" );
        ImGui::TableSetupColumn( "Target" );
        ImGui::TableSetupColumn( "Reported line" );
        ImGui::TableSetupColumn( "Action" );
        ImGui::TableHeadersRow();
        // 처리 명령은 표를 다 그린 뒤 보낸다 — 메모리 제공자는 그 자리에서 답하고, 답을 받은 세션이 신고 목록을 다시 받아 바꾼다.
        // 그리는 중에 보내면 지금 도는 목록(`getReports`)이 바뀌어 반복자가 풀린 메모리를 가리킨다.
        string              resolveKey;
        ServiceReportStatus resolveStatus = ServiceReportStatus::Resolved;
        for ( const ServiceReport& report : session.getReports() )
        {
            ImGui::PushID( report._reportKey.c_str() );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            ImGui::TextUnformatted( AdminToolModel::formatClock( report._createdMs ).c_str() );
            ImGui::TableSetColumnIndex( 1 );
            ImGui::TextUnformatted( report._reason.c_str() );
            ImGui::TableSetColumnIndex( 2 );
            ImGui::TextUnformatted( AdminToolModel::formatAccountID( report._targetID ).c_str() );
            ImGui::TableSetColumnIndex( 3 );
            ImGui::TextUnformatted( report._listLine.empty() ? "" : report._listLine.back()._text.c_str() ); // 마지막 줄이 신고 대상이다
            ImGui::TableSetColumnIndex( 4 );
            const bool bOpen = report._status == ServiceReportStatus::Open;
            ImGui::BeginDisabled( bOpen == false || _memoText.empty() );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "Resolve" ) ) )
            {
                resolveKey    = report._reportKey;
                resolveStatus = ServiceReportStatus::Resolved;
            }
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Dismiss" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "Dismiss" ) ) )
            {
                resolveKey    = report._reportKey;
                resolveStatus = ServiceReportStatus::Dismissed;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
        if ( resolveKey.empty() == false )
            session.requestResolveReport( resolveKey, resolveStatus, _memoText );
    }

    void AdminToolPanel::drawDirectorySection()
    {
        AdminToolSession* pSession = findSubsystem<AdminToolSession>();
        if ( pSession == nullptr )
            return; // 이 확장의 서브시스템이 아직 걸리지 않았다(프레임 앞 sync 전)
        AdminToolSession& session = *pSession;
        EditorWidgets::drawSectionHeader( SW_LOCKEY( "EdPluginWidget", "MaintenanceNotice" ) );

        EditorWidgets::drawTextField( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "ScopeAllOrServerKind" ), "Scope (all or server kind)" ), _scopeText, 160.0f * EditorThemeUtil::getDPIScale() );
        EditorWidgets::drawTextField( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "MessageKey" ), "Message key" ), _maintenanceKeyText, 200.0f * EditorThemeUtil::getDPIScale() );
        EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdPluginField", "MaintenanceSeconds0UntilCleared" ) );
        EditorChrome::setNextItemWidthColumns( 21.0f );
        EditorWidgets::inputScalar( SW_WIDGET_ID( "##Maintenanceseconds0untilcleared" ), ImGuiDataType_S64, &_maintenanceSeconds );
        ImGui::BeginDisabled( _memoText.empty() );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "StartMaintenance" ) ) )
            session.requestSetMaintenance( _scopeText, _maintenanceKeyText, _maintenanceSeconds, _memoText );
        EditorSelfTestMarks::record( "admin.maintenance.set" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "EndMaintenance" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "EndMaintenance" ) ) )
            session.requestClearMaintenance( _scopeText, _memoText );
        ImGui::EndDisabled();

        EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdPluginField", "NoticeId" ) );
        EditorChrome::setNextItemWidthColumns( 16.0f );
        EditorWidgets::inputScalar( SW_WIDGET_ID( "##Noticeid" ), ImGuiDataType_S64, &_announcementID );
        EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdPluginField", "Priority" ) );
        EditorChrome::setNextItemWidthColumns( 16.0f );
        EditorWidgets::inputInt( SW_WIDGET_ID( "##Priority" ), &_announcementPriority );
        EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdPluginField", "NoticeSeconds0UntilRemoved" ) );
        EditorChrome::setNextItemWidthColumns( 21.0f );
        EditorWidgets::inputScalar( SW_WIDGET_ID( "##Noticeseconds0untilremoved" ), ImGuiDataType_S64, &_announcementSeconds );
        EditorWidgets::drawTextField( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "NoticeText" ), "Notice text" ), _announcementText, 360.0f * EditorThemeUtil::getDPIScale() );
        const bool bAnnouncementReady = _memoText.empty() == false && _announcementID > 0;
        ImGui::BeginDisabled( bAnnouncementReady == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "PostAnnouncement" ) ) )
            session.requestPostAnnouncement( static_cast<uint64>( _announcementID ), _announcementText, _announcementPriority, _announcementSeconds, _memoText );
        EditorSelfTestMarks::record( "admin.notice.post" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "RemoveAnnouncement" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "RemoveAnnouncement" ) ) )
            session.requestRemoveAnnouncement( static_cast<uint64>( _announcementID ), _memoText );
        ImGui::EndDisabled();
    }

    void AdminToolPanel::drawLiveEventSection()
    {
        AdminToolSession* pSession = findSubsystem<AdminToolSession>();
        if ( pSession == nullptr )
            return; // 이 확장의 서브시스템이 아직 걸리지 않았다(프레임 앞 sync 전)
        AdminToolSession& session = *pSession;
        EditorWidgets::drawSectionHeader( SW_LOCKEY( "EdPluginWidget", "LiveEvents" ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "ListLiveEvents" ) ) )
            session.requestLiveEvents();
        EditorSelfTestMarks::record( "admin.liveevent.list" );
        for ( const AdminLiveEventView& view : session.getLiveEvents() )
        {
            ImGui::PushID( view._definitionJSON.c_str() );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "Edit" ) ) )
                _eventJSONText = view._definitionJSON; // 고쳐서 다시 올린다(같은 id 는 덮어쓴다)
            ImGui::SameLine();
            ImGui::TextUnformatted( view._definitionJSON.c_str() );
            ImGui::PopID();
        }
        EditorWidgets::drawTextFieldMultiline( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "EventJson" ), "Event JSON" ), _eventJSONText, 6 );
        ImGui::BeginDisabled( _memoText.empty() || _eventJSONText.empty() );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "PutLiveEvent" ) ) )
            session.requestPutLiveEvent( _eventJSONText, _memoText );
        EditorSelfTestMarks::record( "admin.liveevent.put" );
        ImGui::EndDisabled();
        EditorWidgets::drawTextField( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "EventId" ), "Event id" ), _eventIDText, 200.0f * EditorThemeUtil::getDPIScale() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "RemoveLiveEvent" ) ) );
        ImGui::BeginDisabled( _memoText.empty() || _eventIDText.empty() );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "RemoveLiveEvent" ) ) )
            session.requestRemoveLiveEvent( _eventIDText, _memoText );
        ImGui::EndDisabled();
    }

    SW_EDITOR_SUBSYSTEM( AdminToolSession, 9200 );
    SW_EDITOR_PANEL( AdminToolPanel, AdminToolPanel::kPanelID, EditorPanelCategory::Custom, 9200 );
} // namespace sw::editor
