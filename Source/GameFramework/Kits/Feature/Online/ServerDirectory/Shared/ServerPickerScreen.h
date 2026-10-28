/**
 * @file ServerPickerScreen.h
 * @brief 서버 고르기 화면입니다. `ServerListModel` 의 서버 목록을 줄마다 단추 하나로 보이고, 받는 중이면 상태 글을 보이며, 고른 서버를 델리게이트로 알립니다(온라인 키트 U1).
 * @details 문서(`.ui.xml`)는 호출하는 게임이 경로를 준다(코드에 기본 경로가 없다). 문서가 갖춰야 할 위젯: 서버 단추를 담을 패널 `Servers`, 상태 글 `Status`,
 *          명령이 `Refresh` 인 단추와 `Close` 인 단추(견본: `Content/common/ui/online/serverpicker.ui.xml`). 줄 단추의 이름은 `Server.<번호>` 이고 명령은 `Pick` 이다.
 *          받을 수 없는 상태(`Open` 이 아닌 서버)의 단추는 눌리지 않는다. 목록이 비면 이유를 `Status` 에 적는다 — 빈 화면으로 두지 않는다.
 *          화면은 요청을 보내지 않고 모델을 읽는다. 모델이 화면보다 오래 살아야 한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Screen/UIScreen.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Shared/ServerListModel.h"

namespace sw
{
    class UISystem;

    /**
     * @class ServerPickerScreen
     * @brief 서버 목록 화면입니다.
     */
    class SW_KIT_API ServerPickerScreen : public UIScreen
    {
    public:
        /** @brief 서버를 골랐을 때 불립니다. 화면은 곧 닫힌다. */
        using ServerPickedDelegate = Delegate<void( const ServerListEntry& server )>;

        ServerPickerScreen( const UIScreenDesc& desc, unique_ptr<Widget> root );
        ~ServerPickerScreen() override;

        /**
         * @brief 문서 @p documentPath 로 화면을 열고 모델을 연결합니다. 문서를 열지 못하면 무효 핸들입니다. 모델이 한 번도 묻지 않았으면(`Idle`) @p kind 로 목록을 묻습니다.
         */
        [[nodiscard]] static UIScreenHandle open( UISystem& ui, string_view documentPath, ServerListModel& model, string_view kind, const ServerPickedDelegate& onPicked );

        bool onCommand( const hashed_string& command, Widget& source ) override;
        /** @brief 모델의 목록이 바뀌었으면 줄과 상태 글을 다시 만듭니다. */
        void onTick( float32 deltaSeconds ) override;

    private:
        void rebuild();
        /** @brief 줄 단추 이름 `Server.<번호>` 에서 번호를 읽습니다. 형식이 다르면 false 입니다. */
        [[nodiscard]] static bool parseRowIndex( string_view widgetName, uint32& outIndex );

        ServerPickedDelegate _onPicked;
        string               _kind;
        ServerListModel*     _pModel;
        uint64               _shownRevision;
        bool                 _bBuilt;
    };
} // namespace sw
