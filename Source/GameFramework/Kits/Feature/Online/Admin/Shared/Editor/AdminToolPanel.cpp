#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Shared/Editor/AdminToolPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/WallClock.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "GameFramework/Kits/Feature/Online/Admin/Shared/Editor/AdminToolSession.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct AdminToolPanelInternal
        {
            static constexpr int64 kSecondsPerDay          = 86400;
            static constexpr int64 kDefaultAmount          = 100;
            static constexpr int64 kDefaultSanctionSeconds = 3600;
            static constexpr int32 kSecondsPerMinute       = 60;
            static constexpr int32 kSecondsPerHour         = 3600;

            /** @brief 편집 칸의 16 진 문자열을 계정 id 로 바꿉니다. 비었거나 16 진이 아니면 false 입니다. */
            [[nodiscard]] static bool parseAccountID( const string& text, AccountID& outAccountID )
            {
                const string_view trimmed = StringUtil::trim( string_view( text ) );
                uint64            value   = 0;
                if ( trimmed.empty() || StringUtil::parseUint64( trimmed, value, 16 ) == false || value == kInvalidAccountID )
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
                const int64                           secondOfDay = ( unixMs / 1000 ) % kSecondsPerDay;
                StringBuilder<constant::kMaxBuffer32> text;
                text.appendFormat( "%#:%#:%#", static_cast<int32>( secondOfDay / kSecondsPerHour ),
                                   static_cast<int32>( ( secondOfDay % kSecondsPerHour ) / kSecondsPerMinute ), static_cast<int32>( secondOfDay % kSecondsPerMinute ) );
                return string( text.view() );
            }

            static string formatSanction( int64 untilMs )
            {
                if ( untilMs == 0 )
                    return "none";
                if ( untilMs == ServiceSanctionState::kPermanentMs )
                    return "permanent";
                const int64 remainingSeconds = ( untilMs - WallClock::nowUnixMilliseconds() ) / 1000;
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
        , _accountText{}
        , _assetText{ "cur.gold" }
        , _memoText{}
        , _reasonText{ "gm.notice" }
        , _auditSubjectText{}
        , _amount{ AdminToolPanelInternal::kDefaultAmount }
        , _sanctionSeconds{ AdminToolPanelInternal::kDefaultSanctionSeconds }
        , _sanctionKind{ ServiceSanctionKind::ChatMute }
        , _bRefund{ false }
    {
        _accountText = "100"; // 메모리 제공자의 첫 계정(`FakeAdminProvider::kSeedPlayerID`)이다
    }

    void AdminToolPanel::drawContent()
    {
        drawProviderBar();
        ImGui::Separator();
        drawAccountSection();
        drawAdjustSection();
        drawSanctionSection();
        drawAuditSection();

        const AdminToolSession& session = AdminToolSession::get();
        ImGui::Separator();
        EditorWidgets::drawPanelStatus( session.getStatusLine().empty() ? "Ready" : session.getStatusLine().c_str() );
    }

    void AdminToolPanel::drawProviderBar()
    {
        AdminToolSession& session = AdminToolSession::get();
        ImGui::Text( "Provider: %s", session.getProvider().getProviderName() );
        if ( session.getProvider().isLive() == false )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(no server needed)" );
            // 메모리 제공자일 때만 보내는 GM 의 등급을 고른다. 서버에서는 계정의 등급을 서버가 읽는다.
            FakeAdminProvider&  fake       = session.getFakeProvider();
            constexpr AdminRole kArrRole[] = { AdminRole::Viewer, AdminRole::Support, AdminRole::Operator, AdminRole::Super };
            ImGui::SetNextItemWidth( 140.0f * EditorThemeUtil::getDpiScale() );
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
    }

    void AdminToolPanel::drawAccountSection()
    {
        AdminToolSession& session = AdminToolSession::get();
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
            ImGui::Text( "%s: %s", toString( static_cast<ServiceSanctionKind>( kindIndex ) ), state.c_str() );
            if ( kindIndex + 1 < static_cast<int32>( ServiceSanctionKind::Count ) )
                ImGui::SameLine();
        }
    }

    void AdminToolPanel::drawAdjustSection()
    {
        AdminToolSession& session = AdminToolSession::get();
        EditorWidgets::drawSectionHeader( "Grant / Revoke" );

        EditorWidgets::drawTextField( "Asset id", _assetText, 160.0f * EditorThemeUtil::getDpiScale() );
        ImGui::SetNextItemWidth( 160.0f * EditorThemeUtil::getDpiScale() );
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
        AdminToolSession& session = AdminToolSession::get();
        EditorWidgets::drawSectionHeader( "Sanction" );

        constexpr ServiceSanctionKind kArrKind[] = { ServiceSanctionKind::ChatMute, ServiceSanctionKind::Suspend, ServiceSanctionKind::Ban };
        ImGui::SetNextItemWidth( 160.0f * EditorThemeUtil::getDpiScale() );
        if ( ImGui::BeginCombo( "Kind", toString( _sanctionKind ) ) )
        {
            for ( const ServiceSanctionKind kind : kArrKind )
            {
                if ( ImGui::Selectable( toString( kind ), kind == _sanctionKind ) )
                    _sanctionKind = kind;
            }
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth( 160.0f * EditorThemeUtil::getDpiScale() );
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
        AdminToolSession& session = AdminToolSession::get();
        EditorWidgets::drawSectionHeader( "Audit log" );

        EditorWidgets::drawTextField( "Subject prefix", _auditSubjectText, 200.0f * EditorThemeUtil::getDpiScale() );
        ImGui::SameLine();
        if ( ImGui::Button( "List audit" ) )
            session.requestAudit( _auditSubjectText );
        EditorSelfTestMarks::note( "admin.audit.list" );

        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##AdminAudit", 5, kTableFlags, ImVec2{ 0.0f, 120.0f * EditorThemeUtil::getDpiScale() } ) == false )
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

    SW_EDITOR_PANEL( AdminToolPanel, "admin.tool", EditorPanelCategory::Custom, 9200 );
} // namespace sw::editor
