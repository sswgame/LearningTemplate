/**
 * @file OptionsMenuScreen.h
 * @brief 옵션 메뉴를 정의합니다. 사용자 설정 스키마(카테고리, 설정 정의)에서 탭과 행을 런타임에 만들고, 적용, 되돌리기, 기본값, 확인 카운트다운, 키 바인딩 창을 연결합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/UI/Screen/UIScreen.h"

SW_EXTERN_GLOBAL_VARIABLE( bool, gv_uiOptionsMenu );

namespace sw
{
    enum class UIInputMode : uint8;

    struct UserSettingDef;

    class PanelWidget;
    class UserSettingsManager;

    /**
     * @class OptionsMenuScreen
     * @brief 엔진 기본 옵션 메뉴입니다(언리얼 Lyra 의 `UGameSettingRegistry` 와 행 템플릿, 유니티 UI Toolkit 설정 화면에 해당). 게임은 문서와 스타일만 바꿉니다
     *        (팩 설정 `_uiOptionsMenu` 의 문서에서 이름 있는 위젯 `Tabs`, `Rows`, `RestartNotice` 와 버튼 명령 `Apply`, `Revert`, `Defaults`, `Close` 를 지키면 됩니다).
     * @details **구성**: 탭은 `getCategories()`(빈 카테고리는 뺀다)이고, 지금 탭의 행은 `collectSettings` 순서다. 행은 형식별 템플릿 조각(`engine/ui/parts/setting_*.ui.xml`)으로 만들어
     *          이름을 `<설정 id>.<안쪽 이름>` 으로 감싸고, 값 위젯(`Value`)에 `{setting:id}` 바인딩을 코드로 연결한다. 스키마가 원본이라 문서에는 행을 적지 않는다.
     *          값, 범위, 선택지, 사용 가능 여부(`enabledWhen`)는 설정 바인딩(6-3)이 채운다. 탭은 `UI.TabNext`, `UI.TabPrevious`(패드 LB, RB)와 탭 버튼으로 옮긴다.
     *          **적용**: `applyPending` 을 부른다. 확인 대기가 시작되면 카운트다운 창(`SettingsConfirmScreen`)을 띄우고, 다음 실행에 반영되는 값이면 다시 시작 알림을 띄운다.
     *          닫을 때(닫기, 뒤로)는 보류 값이 있으면 "적용, 버리기, 취소" 를 묻는다. 키 바인딩 행의 버튼은 `KeyRebindScreen` 을 연다.
     */
    class SW_API OptionsMenuScreen : public UIScreen
    {
    public:
        static constexpr utf8 kConfirmDocument[] = "engine/ui/confirm_countdown.ui.xml";
        static constexpr utf8 kUnsavedDocument[] = "engine/ui/confirm_unsaved.ui.xml";
        static constexpr utf8 kRebindDocument[]  = "engine/ui/key_rebind.ui.xml";

        OptionsMenuScreen( const UIScreenDesc& desc, unique_ptr<Widget> root );
        ~OptionsMenuScreen() override;

        /**
         * @brief UI 시스템의 옵션 메뉴 문서(`UISystem::getOptionsMenuDocument`)로 메뉴를 엽니다. 설정 출처(`UISystem::findUserSettings`)가 없거나
         *        문서를 만들지 못하면 오류를 남기고 무효 핸들을 반환합니다.
         */
        static UIScreenHandle open( UISystem& ui );
        /**
         * @brief 설정 @p setting 의 형식에 맞는 행 템플릿 문서 경로입니다(`engine/ui/parts/setting_*.ui.xml`).
         * @details 열거형 행은 입력 방식 @p mode 가 탐색(패드 · 키보드)이면 좌우로 고르는 `RotatorWidget` 견본(`setting_enum_pad.ui.xml`)이고, 포인터이면 펼치는 콤보 상자 견본이다.
         *          행은 탭을 고를 때 짓는다 — 행을 지은 뒤에 입력 방식이 바뀌어도 그 행은 그대로 쓸 수 있고(콤보는 `UI.Accept` 로, 로테이터는 클릭으로), 다음 탭 전환 때 바뀐다.
         */
        static const utf8* findRowDocument( const UserSettingDef& setting, UIInputMode mode );

        bool onCommand( const hashed_string& command, Widget& source ) override;
        bool onBack() override;
        bool onUnhandledAction( const hashed_string& action ) override;
        void onTick( float32 deltaSeconds ) override;
        void onTreeRebuilt() override;

        uint32 getTabCount() const { return static_cast<uint32>( _listCategory.size() ); }
        uint32 getSelectedTab() const { return _selectedTab; }
        /** @brief 탭 @p index 를 선택합니다. 행을 다시 만들고(탐색 방식이면 첫 행에 포커스) 탭 버튼의 `selected` 클래스를 옮깁니다. */
        void   selectTab( uint32 index );
        uint32 getRowCount() const { return static_cast<uint32>( _listRow.size() ); }
        /** @brief 행 @p index 의 설정 id 입니다. */
        const hashed_string& getRowSetting( uint32 index ) const { return _listRow[index]._settingID; }
        /** @brief 설정 @p settingID 행의 값 위젯(`Value`)입니다. 지금 탭에 없으면 nullptr 입니다. */
        Widget* findRowValueWidget( const hashed_string& settingID ) const;
        /** @brief 이 메뉴가 연 확인 창(카운트다운, 변경 확인, 키 받기)입니다. 없으면 무효입니다. */
        UIScreenHandle getPromptScreen() const { return _promptScreen; }

        /** @brief 보류 값을 적용합니다(확인 대기면 카운트다운 창). */
        void apply();
        /** @brief 닫기를 요청합니다. 보류 값이 있으면 "적용, 버리기, 취소" 창을 열고, 없으면 닫습니다. */
        void requestClose();

    private:
        /** @struct RowEntry @brief 지금 탭의 행 하나입니다. */
        struct RowEntry
        {
            hashed_string _settingID{};
            WidgetID      _value{ kInvalidWidgetID }; ///< 값 위젯입니다(키 바인딩이면 버튼).
            WidgetID      _glyph{ kInvalidWidgetID }; ///< 키 바인딩의 글리프 텍스트입니다(아니면 무효).
        };

        /** @brief 설정에서 탭을 다시 만들고 지금 탭(범위 밖이면 0)의 행을 만듭니다. */
        void rebuild();
        /** @brief 지금 탭의 행을 다시 만듭니다(기존 행과 그 바인딩을 지웁니다). */
        void populateRows();
        /** @brief 설정 하나의 행을 만들어 @p rows 에 붙입니다. 템플릿을 만들지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool createRow( PanelWidget& rows, const UserSettingDef& setting );
        /** @brief 키 바인딩 행의 글리프를 지금 입력 방식으로 다시 씁니다(바뀐 것만). */
        void refreshGlyphs();
        /** @brief 다시 시작 알림 줄을 `isRestartRequired` 에 맞춥니다. */
        void refreshRestartNotice();
        /** @brief 확인 창을 엽니다(이미 열려 있으면 그대로 둡니다). 무효 핸들이면 열지 못한 것입니다. */
        void openUnsavedPrompt();
        /** @brief 이 메뉴가 연 확인 창이 아직 떠 있으면 true 입니다. */
        bool isPromptOpen() const;

    private:
        vector<hashed_string> _listCategory;  ///< 탭 순서의 카테고리 id 입니다.
        vector<WidgetID>      _listTabButton; ///< 탭 버튼입니다(같은 순서).
        vector<RowEntry>      _listRow;       ///< 지금 탭의 행
        UserSettingsManager*  _pSettings;     ///< 설정 출처입니다(엔진 서비스나 테스트의 것이며, 메뉴보다 오래 삽니다).
        UIScreenHandle        _promptScreen;  ///< 이 메뉴가 연 확인 창입니다.
        uint32                _selectedTab;
    };
} // namespace sw
