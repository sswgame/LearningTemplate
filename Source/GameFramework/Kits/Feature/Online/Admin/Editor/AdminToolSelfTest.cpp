/**
 * @file AdminToolSelfTest.cpp
 * @brief GM 도구 확장의 에디터 자체 테스트입니다. 패널이 에디터의 ImGui 컨텍스트에 그려지는지, 메모리 제공자로 조회 · 지급 · 회수 · 제재 · 감사 열람이 돌고 규칙(메모 필수, 잔액 부족, 등급)이 지켜지는지 확인합니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTest.h"

#include "GameFramework/Kits/Feature/Online/Admin/Editor/AdminToolPanel.h"
#include "GameFramework/Kits/Feature/Online/Admin/Editor/AdminToolSession.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 계정 이름 · 비밀번호 처리기 자리에 거는 기록 장치입니다. */
        struct CredentialsRecorder
        {
            string _loginName{};
            string _password{};
            int32  _callCount{ 0 };
            bool   _bAccept{ true };

            bool accept( string_view loginName, string_view password )
            {
                _loginName = string( loginName );
                _password  = string( password );
                ++_callCount;
                return _bAccept;
            }
        };

        struct AdminToolSelfTestInternal
        {
            static int64 findGold( const AdminReply& lookup )
            {
                for ( const LedgerBalance& balance : lookup._listBalance )
                {
                    if ( balance._assetID == "cur.gold" )
                        return balance._amount;
                }
                return 0;
            }

            /** @brief 확장 패널이 등록 목록에 올라 그려지는지 확인합니다. 결속기가 틀리면 확장의 ImGui::Begin 이 다른 컨텍스트로 가서 이 창이 에디터에 없습니다. */
            static EditorSelfTestStep runPanelDraws( EditorSelfTestContext& context )
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return EditorSelfTestStep::Done;
                if ( context.getStepIndex() == 0 )
                {
                    context.expect( findSubsystem<AdminToolSession>() != nullptr, "the extension subsystem AdminToolSession is not bound in the editor scope" );
                    context.expect( pContext->getSubsystem<EditorPanelManager>().setPanelOpen<AdminToolPanel>( true ), "GM tool panel is not registered" );
                    return EditorSelfTestStep::Continue;
                }
                if ( context.getStepIndex() < 3 )
                    return EditorSelfTestStep::Continue;
                const ImGuiWindow* pWindow = ImGui::FindWindowByName( AdminToolPanel::kPanelTitle );
                context.expect( pWindow != nullptr && pWindow->DrawList->VtxBuffer.Size > 0, "GM tool panel drew nothing in the editor's ImGui context" );
                (void)pContext->getSubsystem<EditorPanelManager>().setPanelOpen<AdminToolPanel>( false ); // 닫기 실패는 위의 expect 가 이미 알렸다
                return EditorSelfTestStep::Done;
            }

            /** @brief 메모리 제공자로 한 바퀴를 돌립니다. 지급이 잔액에 반영되고, 메모가 없으면 거절되고, 잔액보다 큰 회수는 거절되고, 감사 줄이 쌓여야 합니다. */
            static EditorSelfTestStep runFakeRoundTrip( EditorSelfTestContext& context )
            {
                AdminToolSession session; // 패널이 쓰는 세션과 따로 둔다
                const AccountID  playerID = FakeAdminProvider::kSeedPlayerID;

                session.requestLookup( playerID );
                context.expect( session.hasLookup() && AdminToolSelfTestInternal::findGold( session.getLookup() ) == 100, "seed account should start with 100 gold" );

                session.requestAdjust( playerID, "cur.gold", 50, false, "" );
                context.expect( session.getLastResult() == AdminResult::InvalidRequest, "a grant without a memo must be rejected" );
                session.requestAdjust( playerID, "cur.gold", 50, false, "selftest" );
                context.expect( session.getLastResult() == AdminResult::Ok && AdminToolSelfTestInternal::findGold( session.getLookup() ) == 150,
                                "a grant should raise the refreshed balance" );
                session.requestAdjust( playerID, "cur.gold", -1000, false, "selftest" );
                context.expect( session.getLastResult() == AdminResult::InsufficientFunds, "a revoke above the balance must be rejected" );

                session.requestSanction( playerID, ServiceSanctionKind::ChatMute, 600, "gm.notice", "selftest" );
                context.expect( session.getLastResult() == AdminResult::Ok && session.getLookup()._sanction._arrUntilMs[0] != 0, "a chat mute should be set" );

                session.requestAudit( "" );
                context.expect( session.getAuditRows().size() == 2, "the grant and the sanction should be in the audit log" );

                session.getFakeProvider().setActingRole( AdminRole::Viewer );
                session.requestAdjust( playerID, "cur.gold", 1, false, "selftest" );
                context.expect( session.getLastResult() == AdminResult::Forbidden, "a viewer must not be able to grant" );
                return EditorSelfTestStep::Done;
            }

            /**
             * @brief 이름 · 비밀번호 설정 화면의 규칙을 확인합니다 — 처리기가 없거나 이름 · 비밀번호가 비었거나 처리기가 거절하면 연결하지 않고(로그인한 척하지 않는다),
             *        받아들여지면 처리기가 받은 값이 입력 그대로이며 포트 없는 주소는 연결 단계에서 거절되어 메모리 제공자로 돌아온다(네트워크 없이 확인하려고 일부러 포트를 뺐다).
             */
            static EditorSelfTestStep runCredentialsHandoff( EditorSelfTestContext& context )
            {
                AdminToolSession session;
                string           error;
                context.expect( session.connectWithLogin( "nohost", "editor", "gm", "pw", error ) == false && error.empty() == false, "without a login handler the connection must be refused" );

                CredentialsRecorder recorder;
                const Subscription  subscription = session.bindCredentialsHandler( AdminToolSession::CredentialsHandler::create<&CredentialsRecorder::accept>( &recorder ) );
                context.expect( session.hasCredentialsHandler(), "the credentials handler should be bound" );
                context.expect( session.connectWithLogin( "nohost", "editor", "", "pw", error ) == false && recorder._callCount == 0, "an empty account name must not reach the handler" );
                context.expect( session.connectWithLogin( "nohost", "editor", "gm", "", error ) == false && recorder._callCount == 0, "an empty password must not reach the handler" );

                recorder._bAccept = false;
                context.expect( session.connectWithLogin( "nohost", "editor", "gm", "pw", error ) == false && recorder._callCount == 1, "a refused login must not connect" );
                recorder._bAccept     = true;
                const bool bConnected = session.connectWithLogin( "nohost", "editor", "gm", "pw", error );
                context.expect( bConnected == false && recorder._callCount == 2, "an address without a port is refused at the connection step, after the handler took the credentials" );
                context.expect( recorder._loginName == "gm" && recorder._password == "pw", "the handler should receive the typed account name and password" );
                context.expect( session.getProvider().isLive() == false, "a failed connection falls back to the in-memory provider" );
                return EditorSelfTestStep::Done;
            }
        };
    } // namespace

    SW_EDITOR_SELF_TEST( AdminToolPanel, "admin.panelDraws", 9100, &AdminToolSelfTestInternal::runPanelDraws );
    SW_EDITOR_SELF_TEST( AdminToolFakeRoundTrip, "admin.fakeRoundTrip", 9110, &AdminToolSelfTestInternal::runFakeRoundTrip );
    SW_EDITOR_SELF_TEST( AdminToolCredentialsHandoff, "admin.credentialsHandoff", 9120, &AdminToolSelfTestInternal::runCredentialsHandoff );
} // namespace sw::editor
