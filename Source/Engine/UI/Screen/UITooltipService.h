/**
 * @file UITooltipService.h
 * @brief 런타임 UI 의 툴팁과 커서 모양입니다. 호버 경로에서 가장 가까운 위젯의 설명 글(`Widget::getTooltip`)을 머문 뒤 띄우고, 커서 모양(`Widget::getCursorShape`)을 OS 커서로 냅니다.
 */
#pragma once
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/UI/Screen/UIScreenTypes.h"
#include "Engine/UI/WidgetTree/WidgetTypes.h"
#include "Engine/Window/CursorShape.h"

namespace sw
{
    struct UIWidgetPath;

    class UISystem;
    class Widget;
    class WidgetTree;

    /**
     * @class UITooltipService
     * @brief 툴팁 화면(`UIEngineDocuments::_tooltip`, 오버레이 레이어)을 열고 닫고, 커서 모양을 정합니다. 엔진 범위 서브시스템 `UITooltips` 입니다.
     * @details 언리얼 Slate 의 툴팁(`SToolTip`, 지연 후 커서 옆) · 위젯 `Cursor` 속성과 같은 자리다.
     *          - 툴팁: 호버 경로의 잎에서 루트로 올라가 처음 만난 비어 있지 않은 설명 글이 주인이다. 같은 주인 위에 `gv_uiTooltipDelay` 초 머물면 띄우고,
     *            주인이 바뀌거나 포인터 입력이 아니게 되면(탐색 방식) 닫는다. 위치는 포인터 오른쪽 아래(`kPointerOffset`, UI 단위)이며 문서의 `Body` 위젯을 옮긴다.
     *          - 커서: 호버 경로의 잎에서 올라가 처음 만난 `Inherit` 가 아닌 모양이다. 루트까지 모두 `Inherit` 면 화살표다. 호버가 없으면 화살표다.
     *          문서의 위젯 이름: `Body`(옮기는 바탕), `Text`(글). 게임 스레드에서만 쓴다.
     */
    class SW_API UITooltipService final : public ISubsystem
    {
    public:
        /** @brief 툴팁을 포인터에서 이만큼(UI 단위) 오른쪽 아래에 둡니다. */
        static constexpr float32 kPointerOffsetX = 16.0f;
        static constexpr float32 kPointerOffsetY = 20.0f;

        UITooltipService();

        // ISubsystem — 엔진 범위의 서브시스템이다(`engine::getSubsystem<UITooltipService>()`, 게임은 `game::getSubsystem<UITooltipService>()`). 화면 스택(`UISystem`)에 의존하고,
        // 기동이 UI 갱신 칸(`UISystem::onUpdateBeforeScreens`)에 걸고 종료가 줄을 비우고 푼다.
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "UITooltips";
        SubsystemInitResult          initializeSubsystem() override;
        void                         shutdownSubsystem() override;
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const>      getSubsystemDependencies() const override;
        SubsystemHostMask            getSubsystemHost() const override { return EngineSubsystemHost::kClient; }

        /** @brief 호버 경로로 커서 모양을 정하고, 머문 시간을 세어 툴팁을 열고 닫고 옮깁니다. UI 갱신의 화면 틱 앞 칸이 부릅니다(화면 그리기를 막은 동안은 건너뛴다). */
        void update( float32 deltaSeconds );
        /** @brief 툴팁을 닫고 머문 시간을 지웁니다(종료 `shutdownSubsystem`). */
        void clear();

        /**
         * @brief 툴팁 왼쪽 위 자리를 구합니다. 포인터 오른쪽 아래(`kPointerOffset`)가 기본이고, 화면 오른쪽 · 아래를 넘으면 포인터 왼쪽 · 위로 뒤집으며, 그래도 화면 밖이면 화면 안으로 민다.
         * @param pointer     UI 단위 포인터 자리
         * @param tooltipSize 툴팁 크기(UI 단위, 아직 배치 전이면 0)
         * @param screenSize  UI 뷰포트 크기(UI 단위)
         */
        static float2 computePlacement( const float2& pointer, const float2& tooltipSize, const float2& screenSize );
        /** @brief 호버 경로 @p path(@p tree 안)의 커서 모양입니다. 잎에서 올라가 처음 만난 `Inherit` 가 아닌 값, 없으면 `Arrow` 입니다. */
        static UICursorShape resolveCursorShape( const WidgetTree& tree, const UIWidgetPath& path );
        /** @brief 풀린 UI 커서 모양(`Inherit` 가 아님)을 OS 커서 모양으로 바꿉니다. `Inherit` 를 넘기면 어설트하고 `Arrow` 입니다. */
        static CursorShape toCursorShape( UICursorShape shape );
        /** @brief 호버 경로 @p path 에서 설명 글을 가진 가장 가까운 위젯입니다(잎부터). 없으면 nullptr 입니다. */
        static const Widget* findTooltipOwner( const WidgetTree& tree, const UIWidgetPath& path );

        /** @brief 지금 툴팁을 띄운 주인입니다(없으면 `kInvalidWidgetID`). */
        WidgetID getShownOwner() const { return _shownOwner; }
        /** @brief 툴팁 화면입니다(열려 있지 않으면 무효 핸들). */
        UIScreenHandle getScreen() const { return _screen; }

    private:
        /** @brief 툴팁 화면을 열고 글을 넣고 포인터 옆으로 옮깁니다. 문서를 열지 못하면 오류를 한 번 남기고 다시 시도하지 않습니다. */
        void show( const Widget& owner );
        /** @brief 툴팁 화면을 닫습니다. */
        void hide();

        UISystem*      _pUI;                ///< 화면 스택(기동이 찾는다, 그 전 · 종료 뒤에는 nullptr)
        Subscription   _updateSubscription; ///< UI 갱신 칸(`UISystem::onUpdateBeforeScreens`) 구독
        UIScreenHandle _screen;
        WidgetID       _hoverOwner;      ///< 지금 머무는 주인(설명 글을 가진 위젯)
        WidgetID       _shownOwner;      ///< 툴팁을 띄운 주인
        float32        _hoverSeconds;    ///< `_hoverOwner` 위에 머문 시간
        bool           _bDocumentFailed; ///< 문서를 열지 못했는지(같은 오류를 매 프레임 내지 않게)
    };
} // namespace sw
