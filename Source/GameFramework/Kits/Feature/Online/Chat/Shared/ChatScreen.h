/**
 * @file ChatScreen.h
 * @brief 채팅 화면입니다. `ChatLogModel` 의 기록에서 한 채널의 최근 줄(`kVisibleLines`)을 글로 보이고, 입력 줄의 글을 보내며, 마지막 보내기 결과를 상태 글에 적습니다(온라인 키트 U1).
 * @details 문서(`.ui.xml`)는 호출하는 게임이 경로를 준다(코드에 기본 경로가 없다). 문서가 갖춰야 할 위젯: 줄을 담을 패널 `Lines`, 상태 글 `Status`, 입력 줄 `Input`(`TextInputWidget`),
 *          명령이 `Send` 인 단추와 `Close` 인 단추(견본: `Resource/common/ui/online/chat.ui.xml`). 줄 글의 이름은 `Line.<번호>` 이다.
 *          보내기는 글을 요청으로 보낼 때 입력 줄을 비운다 — 서버가 거절하면(금칙어, 도배, 채팅 금지) 이유가 `Status` 에 적히고 글은 다시 쳐야 한다.
 *          화면은 요청을 직접 보내지 않고 모델로 보낸다(틱마다 `ChatLogModel::update` 로 받은 메시지를 반영한다). 모델이 화면보다 오래 살아야 한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Screen/UIScreen.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatLogModel.h"

namespace sw
{
    class UISystem;

    /**
     * @class ChatScreen
     * @brief 한 채널의 채팅 화면입니다.
     */
    class SW_KIT_API ChatScreen : public UIScreen
    {
    public:
        static constexpr size_t kVisibleLines = 15; ///< 화면에 보이는 줄 수의 상한이다

        ChatScreen( const UIScreenDesc& desc, unique_ptr<Widget> root );
        ~ChatScreen() override;

        /** @brief 문서 @p documentPath 로 @p channelID 의 채팅 화면을 열고 모델을 연결합니다. 문서를 열지 못하면 무효 핸들입니다. */
        [[nodiscard]] static UIScreenHandle open( UISystem& ui, string_view documentPath, ChatLogModel& model, string_view channelID );

        bool onCommand( const hashed_string& command, Widget& source ) override;
        /** @brief 받은 메시지를 모델에 반영하고, 기록이나 결과가 바뀌었으면 줄과 상태 글을 다시 만듭니다. */
        void onTick( float32 deltaSeconds ) override;

        /** @brief 보내기 결과를 상태 글로 바꿉니다. */
        [[nodiscard]] static string makeStatusText( ChatResult result, int64 retryAfterMs );

    private:
        void rebuild();

        string        _channelID;
        ChatLogModel* _pModel;
        uint64        _shownRevision;
        bool          _bBuilt;
    };
} // namespace sw
