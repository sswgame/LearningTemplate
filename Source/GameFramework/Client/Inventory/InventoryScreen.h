/**
 * @file GameFramework/Client/Inventory/InventoryScreen.h
 * @brief 인벤토리 화면입니다. 캐릭터 장비 컴포넌트(`CharacterEquipmentComponent`)의 가방 칸 격자 · 장비 칸 · 능력치 · 무게를 보이고, 모든 조작을 `InventoryCommand` 로 냅니다.
 * @details 문서(`.ui.xml`)는 호출하는 게임이 준다(견본 `Content/common/ui/inventory/inventory.ui.xml`). 갖출 위젯: 칸을 담을 패널 `Grid`, 장비 칸 단추를 담을 패널 `Equipment`,
 *          글 `Details` · `Stats` · `Weight` · `Status`, 동작 메뉴 패널 `Actions`(명령 `ActUse` · `ActEquip` · `ActSplit` · `ActDrop` · `ActClose` 단추, 처음엔 접힘), 명령 `Sort` · `Filter` · `Close` 단추.
 */
#pragma once
#include "Core/Concurrency/DevCounter.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/WeakRef.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/UI/Menu/ConfirmDialogScreen.h"
#include "Engine/UI/Screen/UIScreen.h"

#include "GameFramework/Client/Inventory/InventoryViewModel.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Inventory/InventoryCommand.h"

namespace sw
{
    class CharacterEquipmentComponent;
    class InventorySlotWidget;
    class UISystem;

    /**
     * @class InventoryScreen
     * @brief 가방 · 장비 화면입니다(디아블로 · 와우 · 젤다의 가방, 언리얼 Lyra 인벤토리 위젯 자리).
     * @details **집기 · 놓기.** 칸을 누르거나 확인(패드 A)하면 그 칸을 집고, 다른 칸에서 다시 누르면 옮긴다(같은 아이템이면 겹치고 다르면 바꾼다). 마우스로 끌어 다른 칸에 놓아도 같다.
     *          장비 칸을 누르면 집은 칸을 그 칸에 끼고, 집은 것이 없으면 벗긴다. 패드 · 키보드는 포커스 이동(방향 이웃) + 확인으로 같은 일을 한다.
     *          **동작 메뉴.** 오른쪽 누름 · 길게 누르기 · `UI.ContextMenu`(패드 X · 메뉴 키)가 그 칸의 메뉴(사용 · 장착 · 나누기 · 버리기)를 연다. 나누기 · 여러 개 버리기는 수량 대화 상자다.
     *          **정렬 · 거르기.** 정렬은 `Sort` 명령, 거르기는 분류를 차례로 돌며 맞지 않는 칸을 끈다. **툴팁.** 칸의 툴팁 · `Details` 글은 이름 · 설명 · 수치이고 이름 · 설명은 현지화 키다.
     *          버린 조각은 캐릭터 앞에 월드 아이템(`PickupComponent`)으로 놓인다. 장비 컴포넌트가 먼저 사라지거나 끝나면 화면이 스스로 닫힙니다(`WeakRef`).
     */
    REFLECT( Abstract )
    class SW_GF_API InventoryScreen : public UIScreen
    {
    public:
        REFLECT_BODY();

        static constexpr const utf8* kGridName      = "Grid";
        static constexpr const utf8* kEquipmentName = "Equipment";
        static constexpr const utf8* kActionsName   = "Actions";
        static constexpr int32       kNoSlot        = InventoryCommand::kNoSlot;

        const TypeInfo* getTypeInfo() const override { return StaticType(); }

        InventoryScreen( const UIScreenDef& desc, unique_ptr<Widget> root );
        ~InventoryScreen() override;

        /** @brief 문서 @p documentPath 로 화면을 열고 장비 컴포넌트 @p character 를 겁니다. 문서를 열지 못하거나 컴포넌트가 시작 전이면 무효 핸들입니다. */
        [[nodiscard]] static UIScreenHandle open( UISystem& ui, string_view documentPath, CharacterEquipmentComponent& character );

        bool onCommand( const hashed_string& command, Widget& source ) override;
        /** @brief 동작 메뉴가 열려 있으면 닫고, 집은 것이 있으면 놓고, 아니면 화면을 닫습니다. */
        bool onBack() override;
        /** @brief `UI.ContextMenu` 는 포커스 칸의 동작 메뉴를 엽니다. */
        bool onUnhandledAction( const hashed_string& action ) override;
        /** @brief 가방 · 장비가 바뀌었으면 칸을 다시 그리고, 길게 누르기를 셉니다. */
        void onTick( float32 deltaSeconds ) override;

        // ---- 칸 단추(`InventorySlotWidget`)가 부른다 ----
        void onSlotPressed( int32 bagSlot );
        /** @brief 누른 칸을 @p position(UI 단위)에서 뗐습니다. 그 자리가 다른 칸이면 옮깁니다. */
        void onSlotReleased( int32 bagSlot, const float2& position );
        void onSlotSecondary( int32 bagSlot );

        int32                  getHeldSlot() const { return _heldSlot; }
        int32                  getMenuSlot() const { return _menuSlot; }
        InventoryCommandResult getLastResult() const { return _lastResult; }
        const DevCounter&      getCommandCount() const { return _commandCount; } ///< 감시 값만 읽는 개발 전용 칸(배포는 0)
        const hashed_string&   getFilter() const { return _filter; }

    private:
        /** @brief 명령을 적용하고 결과 글을 보입니다. 버린 조각은 캐릭터 앞에 놓습니다. */
        InventoryCommandResult run( const InventoryCommand& command );
        void                   onSlotActivated( int32 bagSlot );
        void                   openActions( int32 bagSlot );
        void                   closeActions();
        void                   buildWidgets();
        void                   refresh();
        void                   refreshDetails();
        void                   setStatus( const string& text );
        void                   cycleFilter();
        /** @brief @p position 아래의 칸 번호입니다. 없으면 `kNoSlot` 입니다. */
        int32 findSlotAt( const float2& position ) const;
        /** @brief 수량 대화 상자의 답(나누기 · 버리기)입니다. 화면이 남아 있으면 명령을 냅니다. */
        static void onQuantityChosen( UISystem& ui, UIScreenHandle screen, InventoryCommand command, ConfirmDialogResult result, int32 count );

    private:
        vector<InventorySlotWidget*>         _listSlotWidget; ///< 가방 칸 순서(트리가 소유)
        WeakRef<CharacterEquipmentComponent> _character;      ///< 장비 컴포넌트(씬 소유) — 먼저 사라지면 화면이 닫힌다
        hashed_string                        _filter;         ///< 거르는 분류(비면 모두)
        float32                              _pressSeconds;   ///< 지금 누른 칸을 누른 시간(길게 누르기)
        int32                                _pressedSlot;    ///< 포인터로 누른 칸
        int32                                _heldSlot;       ///< 집은 칸(놓을 곳을 기다린다)
        int32                                _menuSlot;       ///< 동작 메뉴가 열린 칸
        uint32                               _shownBagRevision;
        uint32                               _shownEquipmentRevision;
        DevCounter                           _commandCount; ///< 낸 명령 수(성공 · 실패 모두 — 감시 값)
        InventoryCommandResult               _lastResult;
        uint8                                _bLongPressFired; ///< 길게 누르기가 메뉴를 열었다 — 이어 오는 클릭을 버린다
        uint8                                _bBuilt;
        InventoryViewModel                   _viewModel; ///< 문서가 바인딩으로 읽는 글(`{bind:…}`) — 화면이 위젯 이름으로 찾아 쓰지 않는다
    };
} // namespace sw
