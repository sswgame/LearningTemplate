#include "pch.h"

#include "GameFramework/PlugIn/Editor/Admin/AdminToolPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/WallClock.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorTextExtentImGui.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Reflection/EnumName.h"

#include "GameFramework/PlugIn/Editor/Admin/AdminToolSession.h"

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

            /** @brief 편집 칸의 16 진 문자열을 계정 id 로 바꿉니다. 비었거나 16 진이 아니면 false 입니다. */
            [[nodiscard]] static bool parseAccountID( const string& text, AccountID& outAccountID )
            {
                const string_view trimmed             = StringUtil::trim( string_view( text ) );
                uint64            value               = 0;
                const bool        bInvalidAccountText = trimmed.empty() || StringUtil::parseUint64( trimmed, value, 16 ) == false || value == kInvalidAccountID;
                if ( bInvalidAccountText )
                    return false;
                outAccountID = value;
                return true;
            }

            static string formatAccountID( AccountID accountID )
            {
                StringBuilder<constant::kMaxBuffer32> text;
                text.appendFormat( "%#", Fmt( accountID, Format( 16, Format::Padding::Zero ).hex() ) );
                return string( text.view() );
            }

            static string formatBalance( const LedgerBalance& balance )
            {
                StringBuilder<constant::kMaxBuffer64> text;
                text.appendFormat( "%#%#", balance._amount, balance.isDebt() ? " (debt)" : "" );
                return string( text.view() );
            }

            static string formatClock( int64 unixMs )
            {
                const int64                           secondOfDay = ( unixMs / constant::kMillisecondsPerSecond ) % constant::kSecondsPerDay;
                StringBuilder<constant::kMaxBuffer32> text;
                text.appendFormat( "%#:%#:%#", static_cast<int32>( secondOfDay / constant::kSecondsPerHour ),
                                   static_cast<int32>( ( secondOfDay % constant::kSecondsPerHour ) / constant::kSecondsPerMinute ), static_cast<int32>( secondOfDay % constant::kSecondsPerMinute ) );
                return string( text.view() );
            }

            static string formatSanction( int64 untilMs )
            {
                if ( untilMs == 0 )
                    return "none";
                if ( untilMs == ServiceSanctionState::kPermanentMs )
                    return "permanent";
                const int64 remainingSeconds = ( untilMs - WallClock::nowUnixMilliseconds() ) / constant::kMillisecondsPerSecond;
                if ( remainingSeconds <= 0 )
                    return "expired";
                StringBuilder<constant::kMaxBuffer64> text;
                text.appendFormat( "%# s left", remainingSeconds );
                return string( text.view() );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    AdminToolPanel::AdminToolPanel()
        : IEditorPanel( false )
        , _gmAddressText{}
        , _accountText{}
        , _assetText{ "cur.gold" }
        , _memoText{}
        , _reasonText{ "gm.notice" }
        , _auditSubjectText{}
        , _scopeText{ "all" }
        , _maintenanceKeyText{ "maintenance.default" }
        , _noticeText{}
        , _eventJSONText{}
        , _eventIDText{}
        , _amount{ AdminToolPanelInternal::kDefaultAmount }
        , _sanctionSeconds{ AdminToolPanelInternal::kDefaultSanctionSeconds }
        , _maintenanceSeconds{ AdminToolPanelInternal::kDefaultWindowSeconds }
        , _noticeSeconds{ AdminToolPanelInternal::kDefaultWindowSeconds }
        , _noticeID{ 1 }
        , _noticePriority{ 0 }
        , _sanctionKind{ ServiceSanctionKind::ChatMute }
        , _bRefund{ false }
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
        ImGui::Text( "Provider: %s", session.getProvider().getProviderName() );
        if ( session.getProvider().isLive() == false )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(no server needed)" );
            // 메모리 제공자일 때만 보내는 GM 의 등급을 고른다. 서버에서는 계정의 등급을 서버가 읽는다.
            FakeAdminProvider&  fake       = session.getFakeProvider();
            constexpr AdminRole kArrRole[] = { AdminRole::Viewer, AdminRole::Support, AdminRole::Operator, AdminRole::Super };
            EditorChrome::setNextItemWidthColumns( 19.0f );
            if ( ImGui::BeginCombo( "Acting role", toString( fake.getActingRole() ) ) )
            {
                for ( const AdminRole role : kArrRole )
                {
                    if ( ImGui::Selectable( toString( role ), role == fake.getActingRole() ) )
                        fake.setActingRole( role );
                }
                ImGui::EndCombo();
            }
            EditorSelfTestMarks::note( "admin.role" );
        }

        // 서버의 GM 포트(서버 설정 `_adminServicePort`)에 연결한다. 로그인은 `getConnection().bindLoginHook` 으로 끼운 쪽이 한다.
        EditorWidgets::drawTextField( "GM port (host:port)", _gmAddressText, 200.0f * EditorThemeUtil::getDpiScale() );
        ImGui::SameLine();
        const bool bConnected = session.getConnection().getState() != AdminToolConnectionState::Disconnected;
        if ( bConnected == false && ImGui::Button( "Connect" ) )
        {
            string error;
            if ( session.connectLive( _gmAddressText, "editor", error ) == false )
                SW_LOG_WARNING( "Admin tool: GM connection failed - %#", error.c_str() );
        }
        if ( bConnected && ImGui::Button( "Disconnect" ) )
            session.disconnectLive();
        ImGui::TextDisabled( "GM connection: %s", session.getConnection().getStateText() );
    }

    void AdminToolPanel::drawAccountSection()
    {
        AdminToolSession* pSession = findSubsystem<AdminToolSession>();
        if ( pSession == nullptr )
            return; // 이 확장의 서브시스템이 아직 걸리지 않았다(프레임 앞 sync 전)
        AdminToolSession& session = *pSession;
        EditorWidgets::drawSectionHeader( "Account" );

        EditorWidgets::drawTextField( "Account id (hex)", _accountText, 160.0f * EditorThemeUtil::getDpiScale() );
        EditorSelfTestMarks::note( "admin.account" );
        ImGui::SameLine();
        if ( ImGui::Button( "Lookup" ) )
        {
            AccountID accountID = kInvalidAccountID;
            if ( AdminToolPanelInternal::parseAccountID( _accountText, accountID ) )
                session.requestLookup( accountID );
            else
                SW_LOG_WARNING( "Admin tool: account id must be a non-zero hex number" );
        }
        EditorSelfTestMarks::note( "admin.lookup" );

        if ( session.hasLookup() == false )
        {
            EditorWidgets::drawEmptyHint( "Look up an account to see its balances and sanctions" );
            return;
        }

        const AdminReply& lookup = session.getLookup();
        ImGui::Text( "%s  (id %s)  %s", lookup._identity._displayName.c_str(), AdminToolPanelInternal::formatAccountID( lookup._identity._accountID ).c_str(),
                     lookup._bOnline == SW_TRUE ? "online" : "offline" );

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
                ImGui::TextUnformatted( AdminToolPanelInternal::formatBalance( balance ).c_str() );
            }
            ImGui::EndTable();
        }

        for ( int32 kindIndex = 0; kindIndex < static_cast<int32>( ServiceSanctionKind::Count ); ++kindIndex )
        {
            const string state = AdminToolPanelInternal::formatSanction( lookup._sanction._arrUntilMs[kindIndex] );
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
        EditorWidgets::drawSectionHeader( "Grant / Revoke" );

        EditorWidgets::drawTextField( "Asset id", _assetText, 160.0f * EditorThemeUtil::getDpiScale() );
        EditorChrome::setNextItemWidthColumns( 21.0f );
        ImGui::InputScalar( "Amount", ImGuiDataType_S64, &_amount );
        EditorSelfTestMarks::note( "admin.amount" );
        EditorWidgets::drawTextField( "Memo (ticket)", _memoText, 240.0f * EditorThemeUtil::getDpiScale() );
        EditorSelfTestMarks::note( "admin.memo" );
        ImGui::Checkbox( "Refund revoke (may leave a debt)", &_bRefund );

        AccountID  accountID     = kInvalidAccountID;
        const bool bAccountValid = AdminToolPanelInternal::parseAccountID( _accountText, accountID );
        ImGui::BeginDisabled( bAccountValid == false );
        if ( ImGui::Button( "Grant" ) )
            session.requestAdjust( accountID, _assetText, _amount, false, _memoText );
        EditorSelfTestMarks::note( "admin.grant" );
        ImGui::SameLine();
        if ( ImGui::Button( "Revoke" ) )
            session.requestAdjust( accountID, _assetText, -_amount, _bRefund, _memoText );
        EditorSelfTestMarks::note( "admin.revoke" );
        ImGui::EndDisabled();
    }

    void AdminToolPanel::drawSanctionSection()
    {
        AdminToolSession* pSession = findSubsystem<AdminToolSession>();
        if ( pSession == nullptr )
            return; // 이 확장의 서브시스템이 아직 걸리지 않았다(프레임 앞 sync 전)
        AdminToolSession& session = *pSession;
        EditorWidgets::drawSectionHeader( "Sanction" );

        constexpr ServiceSanctionKind kArrKind[] = { ServiceSanctionKind::ChatMute, ServiceSanctionKind::Suspend, ServiceSanctionKind::Ban };
        EditorChrome::setNextItemWidthColumns( 21.0f );
        if ( ImGui::BeginCombo( "Kind", getEnumName( _sanctionKind ) ) )
        {
            for ( const ServiceSanctionKind kind : kArrKind )
            {
                if ( ImGui::Selectable( getEnumName( kind ), kind == _sanctionKind ) )
                    _sanctionKind = kind;
            }
            ImGui::EndCombo();
        }
        EditorChrome::setNextItemWidthColumns( 21.0f );
        ImGui::InputScalar( "Seconds", ImGuiDataType_S64, &_sanctionSeconds );
        EditorWidgets::drawTextField( "Reason code", _reasonText, 160.0f * EditorThemeUtil::getDpiScale() );

        AccountID  accountID     = kInvalidAccountID;
        const bool bAccountValid = AdminToolPanelInternal::parseAccountID( _accountText, accountID );
        ImGui::BeginDisabled( bAccountValid == false );
        if ( ImGui::Button( "Apply sanction" ) )
            session.requestSanction( accountID, _sanctionKind, _sanctionSeconds, _reasonText, _memoText );
        EditorSelfTestMarks::note( "admin.sanction.apply" );
        ImGui::SameLine();
        if ( ImGui::Button( "Lift sanction" ) )
            session.requestSanction( accountID, _sanctionKind, 0, _reasonText, _memoText );
        EditorSelfTestMarks::note( "admin.sanction.lift" );
        ImGui::EndDisabled();
    }

    void AdminToolPanel::drawAuditSection()
    {
        AdminToolSession* pSession = findSubsystem<AdminToolSession>();
        if ( pSession == nullptr )
            return; // 이 확장의 서브시스템이 아직 걸리지 않았다(프레임 앞 sync 전)
        AdminToolSession& session = *pSession;
        EditorWidgets::drawSectionHeader( "Audit log" );

        EditorWidgets::drawTextField( "Subject prefix", _auditSubjectText, 200.0f * EditorThemeUtil::getDpiScale() );
        ImGui::SameLine();
        if ( ImGui::Button( "List audit" ) )
            session.requestAudit( _auditSubjectText );
        EditorSelfTestMarks::note( "admin.audit.list" );

        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##AdminAudit", 5, kTableFlags, measureExtent( EditorTextExtent::ofText( 0.0f, 6.0f ) ) ) == false )
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
            ImGui::TextUnformatted( AdminToolPanelInternal::formatClock( entry._timeMs ).c_str() );
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
        EditorWidgets::drawSectionHeader( "Reports" );
        if ( ImGui::Button( "Open reports" ) )
            session.requestReports( true );
        EditorSelfTestMarks::note( "admin.report.list" );
        ImGui::SameLine();
        if ( ImGui::Button( "Closed reports" ) )
            session.requestReports( false );

        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##AdminReport", 5, kTableFlags, measureExtent( EditorTextExtent::ofText( 0.0f, 7.0f ) ) ) == false )
            return;
        ImGui::TableSetupColumn( "Time (UTC)" );
        ImGui::TableSetupColumn( "Reason" );
        ImGui::TableSetupColumn( "Target" );
        ImGui::TableSetupColumn( "Reported line" );
        ImGui::TableSetupColumn( "Action" );
        ImGui::TableHeadersRow();
        for ( const ServiceReport& report : session.getReports() )
        {
            ImGui::PushID( report._reportKey.c_str() );
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex( 0 );
            ImGui::TextUnformatted( AdminToolPanelInternal::formatClock( report._createdMs ).c_str() );
            ImGui::TableSetColumnIndex( 1 );
            ImGui::TextUnformatted( report._reason.c_str() );
            ImGui::TableSetColumnIndex( 2 );
            ImGui::TextUnformatted( AdminToolPanelInternal::formatAccountID( report._targetID ).c_str() );
            ImGui::TableSetColumnIndex( 3 );
            ImGui::TextUnformatted( report._listLine.empty() ? "" : report._listLine.back()._text.c_str() ); // 마지막 줄이 신고 대상이다
            ImGui::TableSetColumnIndex( 4 );
            const bool bOpen = report._status == ServiceReportStatus::Open;
            ImGui::BeginDisabled( bOpen == false || _memoText.empty() );
            if ( ImGui::SmallButton( "Resolve" ) )
                session.requestResolveReport( report._reportKey, ServiceReportStatus::Resolved, _memoText );
            ImGui::SameLine();
            if ( ImGui::SmallButton( "Dismiss" ) )
                session.requestResolveReport( report._reportKey, ServiceReportStatus::Dismissed, _memoText );
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void AdminToolPanel::drawDirectorySection()
    {
        AdminToolSession* pSession = findSubsystem<AdminToolSession>();
        if ( pSession == nullptr )
            return; // 이 확장의 서브시스템이 아직 걸리지 않았다(프레임 앞 sync 전)
        AdminToolSession& session = *pSession;
        EditorWidgets::drawSectionHeader( "Maintenance / Notice" );

        EditorWidgets::drawTextField( "Scope (all or server kind)", _scopeText, 160.0f * EditorThemeUtil::getDpiScale() );
        EditorWidgets::drawTextField( "Message key", _maintenanceKeyText, 200.0f * EditorThemeUtil::getDpiScale() );
        EditorChrome::setNextItemWidthColumns( 21.0f );
        ImGui::InputScalar( "Maintenance seconds (0 = until cleared)", ImGuiDataType_S64, &_maintenanceSeconds );
        ImGui::BeginDisabled( _memoText.empty() );
        if ( ImGui::Button( "Start maintenance" ) )
            session.requestSetMaintenance( _scopeText, _maintenanceKeyText, _maintenanceSeconds, _memoText );
        EditorSelfTestMarks::note( "admin.maintenance.set" );
        ImGui::SameLine();
        if ( ImGui::Button( "End maintenance" ) )
            session.requestClearMaintenance( _scopeText, _memoText );
        ImGui::EndDisabled();

        EditorChrome::setNextItemWidthColumns( 16.0f );
        ImGui::InputScalar( "Notice id", ImGuiDataType_S64, &_noticeID );
        EditorChrome::setNextItemWidthColumns( 16.0f );
        ImGui::InputInt( "Priority", &_noticePriority );
        EditorChrome::setNextItemWidthColumns( 21.0f );
        ImGui::InputScalar( "Notice seconds (0 = until removed)", ImGuiDataType_S64, &_noticeSeconds );
        EditorWidgets::drawTextField( "Notice text", _noticeText, 360.0f * EditorThemeUtil::getDpiScale() );
        const bool bNoticeReady = _memoText.empty() == false && _noticeID > 0;
        ImGui::BeginDisabled( bNoticeReady == false );
        if ( ImGui::Button( "Post notice" ) )
            session.requestPostNotice( static_cast<uint64>( _noticeID ), _noticeText, _noticePriority, _noticeSeconds, _memoText );
        EditorSelfTestMarks::note( "admin.notice.post" );
        ImGui::SameLine();
        if ( ImGui::Button( "Remove notice" ) )
            session.requestRemoveNotice( static_cast<uint64>( _noticeID ), _memoText );
        ImGui::EndDisabled();
    }

    void AdminToolPanel::drawLiveEventSection()
    {
        AdminToolSession* pSession = findSubsystem<AdminToolSession>();
        if ( pSession == nullptr )
            return; // 이 확장의 서브시스템이 아직 걸리지 않았다(프레임 앞 sync 전)
        AdminToolSession& session = *pSession;
        EditorWidgets::drawSectionHeader( "Live events" );
        if ( ImGui::Button( "List live events" ) )
            session.requestLiveEvents();
        EditorSelfTestMarks::note( "admin.liveevent.list" );
        for ( const AdminLiveEventView& view : session.getLiveEvents() )
        {
            ImGui::PushID( view._definitionJSON.c_str() );
            if ( ImGui::SmallButton( "Edit" ) )
                _eventJSONText = view._definitionJSON; // 고쳐서 다시 올린다(같은 id 는 덮어쓴다)
            ImGui::SameLine();
            ImGui::TextUnformatted( view._definitionJSON.c_str() );
            ImGui::PopID();
        }
        EditorWidgets::drawTextFieldMultiline( "Event JSON", _eventJSONText, 6 );
        ImGui::BeginDisabled( _memoText.empty() || _eventJSONText.empty() );
        if ( ImGui::Button( "Put live event" ) )
            session.requestPutLiveEvent( _eventJSONText, _memoText );
        EditorSelfTestMarks::note( "admin.liveevent.put" );
        ImGui::EndDisabled();
        EditorWidgets::drawTextField( "Event id", _eventIDText, 200.0f * EditorThemeUtil::getDpiScale() );
        ImGui::SameLine();
        ImGui::BeginDisabled( _memoText.empty() || _eventIDText.empty() );
        if ( ImGui::Button( "Remove live event" ) )
            session.requestRemoveLiveEvent( _eventIDText, _memoText );
        ImGui::EndDisabled();
    }

    // 세션의 등록 줄은 여기 둔다 — EngineTest 가 세션 .cpp 를 에디터 없이 직접 컴파일한다.
    SW_EDITOR_SUBSYSTEM( AdminToolSession, 9200 );
    SW_EDITOR_PANEL( AdminToolPanel, AdminToolPanel::kPanelID, EditorPanelCategory::Custom, 9200 );
} // namespace sw::editor
