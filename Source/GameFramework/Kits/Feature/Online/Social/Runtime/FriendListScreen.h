/**
 * @file FriendListScreen.h
 * @brief 친구 목록 화면입니다. `FriendListModel` 의 친구를 줄마다 글 하나로 보이고(계정, 접속 상태, 활동), 받는 중이면 상태 글을 보입니다(온라인 키트 U1).
 * @details 문서(`.ui.xml`)는 호출하는 게임이 경로를 준다(코드에 기본 경로가 없다). 문서가 갖춰야 할 위젯: 친구 줄을 담을 패널 `Friends`, 상태 글 `Status`,
 *          명령이 `Refresh` 인 단추와 `Close` 인 단추(견본: `Content/common/ui/online/friendlist.ui.xml`). 줄 글의 이름은 `Friend.<번호>` 이다.
 *          목록이 비거나 받지 못하면 이유를 `Status` 에 적는다 — 빈 화면으로 두지 않는다. 받은 신청이 있으면 `Status` 에 개수를 함께 적는다.
 *          화면은 요청을 보내지 않고 모델을 읽는다(틱마다 `FriendListModel::update` 로 알림을 반영한다). 모델이 화면보다 오래 살아야 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Screen/UIScreen.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Social/Runtime/FriendListModel.h"

namespace sw
{
    class UISystem;

    /**
     * @class FriendListScreen
     * @brief 친구 목록 화면입니다.
     */
    REFLECT( Abstract )
    class SW_KIT_API FriendListScreen : public UIScreen
    {
    public:
        REFLECT_BODY();

        const TypeInfo* getTypeInfo() const override { return StaticType(); }
        FriendListScreen( const UIScreenDesc& desc, unique_ptr<Widget> root );
        ~FriendListScreen() override;

        /** @brief 문서 @p documentPath 로 화면을 열고 모델을 연결합니다. 문서를 열지 못하면 무효 핸들입니다. 모델이 한 번도 묻지 않았으면(`Idle`) 목록을 묻습니다. */
        [[nodiscard]] static UIScreenHandle open( UISystem& ui, string_view documentPath, FriendListModel& model );

        bool onCommand( const hashed_string& command, Widget& source ) override;
        /** @brief 알림을 모델에 반영하고, 목록이 바뀌었으면 줄과 상태 글을 다시 만듭니다. */
        void onTick( float32 deltaSeconds ) override;

    private:
        void rebuild();

        FriendListModel* _pModel;
        uint64           _shownRevision;
        bool             _bBuilt;
    };
} // namespace sw
