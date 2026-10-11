/**
 * @file MultiplayerPanel.h
 * @brief Multiplayer 창 — 멀티 프로세스 재생의 Session 탭(구성 · 프로세스 목록 · 서버 일시정지 · 한 틱 진행 · 출처별 로그)입니다.
 * @details 언리얼 Network Insights · 유니티 Multiplayer Play Mode 창의 자리입니다. 세션은 `EditorContext::getNetPlaySession()` 하나이고,
 *          툴바의 Net Mode 드롭다운과 이 창이 같은 세션을 봅니다. 회선 조절 · 서버 검사기 · 네트워크 프로파일러 · 어긋남 찾기 탭은
 *          `docs/plans/ServerDebug.md` 의 S4 · S3 · S5 · S7 이 이 창에 더합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/IEditorPanel.h"

namespace sw::editor
{
    class EditorNetPlaySession;

    /**
     * @class MultiplayerPanel
     * @brief Session 탭을 그립니다. 세션 갱신(`EditorNetPlaySession::update`)은 이 창이 아니라 툴바가 프레임마다 한다(창을 닫아도 로그가 모인다).
     */
    class MultiplayerPanel final : public IEditorPanel
    {
    public:
        MultiplayerPanel();

        const utf8* getPanelTitle() const override { return "Multiplayer"; }
        bool        isToolPanel() const override { return true; }
        void        drawContent() override;
        float2      getInitialPanelSize() const override { return float2{ 760.0f, 560.0f }; }

        /** @brief 툴바가 쓰는 Net Mode 고르기(Standalone · 전용 서버 · 리슨 호스트 + 플레이어 수)입니다. 바꿨으면 true 입니다. */
        static bool drawNetModeCombo( EditorNetPlaySession& session );
        /** @brief 지금 구성으로 세션을 시작합니다(로그에 결과). Play 단추 · `netplay.start` 가 부릅니다. */
        static bool startSession( EditorNetPlaySession& session );

    private:
        void drawSessionTab( EditorNetPlaySession& session );
        void drawProcessTable( EditorNetPlaySession& session );
        void drawLog( EditorNetPlaySession& session );

        fixed_string<constant::kMaxBuffer256> _clientArguments; ///< 클라이언트마다 붙일 인자(편집 칸)
        int32                                 _stepTickCount;   ///< "Step" 이 서버를 돌릴 틱 수
        int32                                 _logSource;       ///< 로그 거르기 — -1 이면 모두, 아니면 프로세스 번호
        bool                                  _bFollowLog;      ///< 새 줄이 오면 끝으로 스크롤
    };
} // namespace sw::editor
