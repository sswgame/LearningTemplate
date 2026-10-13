/**
 * @file AdminToolSelfTest.cpp
 * @brief GM 도구 확장의 에디터 자체 테스트입니다. 패널이 에디터의 ImGui 컨텍스트에 그려지는지, 메모리 제공자로 조회 · 지급 · 회수 · 제재 · 감사 열람이 돌고 규칙(메모 필수, 잔액 부족, 등급)이 지켜지는지 확인합니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/SelfTest/EditorSelfTest.h"

#include "GameFramework/PlugIn/Editor/Admin/AdminToolSession.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
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
                EditorContext* pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return EditorSelfTestStep::Done;
                if ( context.getStepIndex() == 0 )
                {
                    context.expect( pContext->getSubsystem<EditorPanelManager>().setPanelOpen( "admin.tool", true ), "GM tool panel is not registered" );
                    return EditorSelfTestStep::Continue;
                }
                if ( context.getStepIndex() < 3 )
                    return EditorSelfTestStep::Continue;
                const ImGuiWindow* pWindow = ImGui::FindWindowByName( "GM Tool" );
                context.expect( pWindow != nullptr && pWindow->DrawList->VtxBuffer.Size > 0, "GM tool panel drew nothing in the editor's ImGui context" );
                (void)pContext->getSubsystem<EditorPanelManager>().setPanelOpen( "admin.tool", false ); // 닫기 실패는 위의 expect 가 이미 알렸다
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
        };
    } // namespace

    SW_EDITOR_SELF_TEST( AdminToolPanel, "admin.panelDraws", 9100, &AdminToolSelfTestInternal::runPanelDraws );
    SW_EDITOR_SELF_TEST( AdminToolSession, "admin.fakeRoundTrip", 9110, &AdminToolSelfTestInternal::runFakeRoundTrip );
} // namespace sw::editor
