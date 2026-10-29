/**
 * @file UIOffscreenScreens.h
 * @brief 화면 스택 밖에서 렌더 텍스처에 그리는 화면들(에디터 UI 미리보기)입니다(엔진 범위 서브시스템 `UIOffscreenScreens`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/WidgetTree/WidgetTypes.h"

namespace sw
{
    struct UILayoutContext;
    struct UIPaintContext;

    class LocalizationManager;
    class UISystem;
    class UIThemeController;

    /** @struct UIOffscreenScreen @brief 화면 스택 밖의 화면 하나입니다. 렌더 텍스처 경로, 뷰포트, 그리기 목록을 가집니다(에디터 미리보기). */
    struct UIOffscreenScreen
    {
        unique_ptr<UIScreen> _screen{};
        hashed_string        _targetPath{};
        hashed_string        _theme{}; ///< 빈 이름이면 지금 테마입니다.
        UIViewport           _viewport{};
        CanvasDrawList       _canvas{};
        CanvasDrawList       _scratch{};
        uint64               _revision{ 0 };
        float32              _textScale{ 1.0f };
        /// 이 화면의 글을 해석하는 문화권입니다(소유하지 않는다 — 부르는 쪽이 화면보다 오래 둡니다). nullptr 이면 게임 UI 와 같은 문화권입니다.
        const LocalizationManager* _pLocalization{ nullptr };
        bool                       _bNavigationMode{ false }; ///< 탐색 방식이면 포커스 테두리(`:focus-visible`)를 보입니다(미리보기의 탐색 흉내).
    };
} // namespace sw

namespace sw
{
    /**
     * @class UIOffscreenScreens
     * @brief 오프스크린 화면 목록입니다(엔진 범위 서브시스템 `UIOffscreenScreens`, 에디터는 `editor::findSubsystem<UIOffscreenScreens>()`). 입력과 포커스가 없고,
     *        화면마다 자기 뷰포트로 스타일, 레이아웃, 그리기를 돌려 렌더 텍스처 대상 목록을 내놓습니다.
     * @details 화면 스택에 들지 않으므로 화면 스택의 값 멤버가 아니다(Godot `SubViewport` · 언리얼 위젯 미리보기의 렌더 타깃처럼 따로 선다).
     *          문서 생성과 핸들 발급은 스택 화면과 같은 경로라 화면 스택에 묻고(`UISystem::createDocumentScreen`), 스타일 세트는 테마(`UIThemeController`)가 짓는다.
     *          문서 · 스타일 시트 다시 읽기와 테마 바뀜은 그 서비스의 알림을 직접 받아 자기 화면만 다시 맞춘다.
     *          프레임 처리는 UI 갱신의 그리기 뒤 칸(`UISystem::onUpdateAfterPaint`)에서 돈다 — 화면 그리기를 막은 동안에도 미리보기는 계속 그린다.
     */
    class SW_API UIOffscreenScreens final : public ISubsystem
    {
    public:
        UIOffscreenScreens();
        ~UIOffscreenScreens() override;

        // ISubsystem — 화면 스택(문서 생성 · 컨텍스트) · 테마 · 두 캐시(다시 읽기 알림)에 의존한다. 종료는 화면을 모두 지운다.
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "UIOffscreenScreens";
        SubsystemInitResult          initializeSubsystem() override;
        void                         shutdownSubsystem() override;
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const>      getSubsystemDependencies() const override;
        SubsystemHostMask            getSubsystemHost() const override { return EngineSubsystemHost::kClient; }

        /**
         * @brief 문서 @p documentPath 를 화면 스택 밖에서 생성해 렌더 텍스처 @p targetPath(`rendertarget/...`)에 그립니다. 입력과 포커스가 없고 게임 UI 와 섞이지 않습니다.
         * @details 스타일, 테마, 핫 리로드는 스택의 화면과 같은 경로로 처리한다(문서를 고치면 미리보기도 다시 생성한다). 뷰포트는 `setView` 전까지 비어 있어 그리지 않는다.
         * @return 생성하지 못하면 무효 핸들입니다(오류는 로그에 남깁니다).
         */
        UIScreenHandle open( string_view documentPath, string_view targetPath );
        /** @brief 핸들 @p handle 의 화면을 지웁니다. 없으면 아무것도 하지 않습니다. */
        void remove( UIScreenHandle handle );
        /** @brief 핸들 @p handle 의 화면입니다. 없으면 nullptr 입니다. */
        UIScreen* findScreen( UIScreenHandle handle ) const;
        /**
         * @brief 화면 @p handle 의 뷰포트(UI 단위 크기, 배율, 안전 영역. 렌더 텍스처 크기가 물리 크기), 글자 배율(0 이하면 1), 테마(빈 이름이면 지금 테마)를 정합니다.
         * @details 테마가 바뀌면 그 화면의 스타일 시트만 다시 적용한다(게임 UI 의 테마는 그대로다).
         */
        void setView( UIScreenHandle handle, const UIViewport& viewport, float32 textScale, const hashed_string& theme );
        /**
         * @brief 화면 @p handle 이 글을 해석할 문화권을 정합니다(nullptr 이면 게임 UI 와 같은 문화권). 문화권이 전역이라 이 화면만 다른 언어로 보려는 길입니다.
         * @details 문화권이 다른 텍스트 버전을 가지므로, 바꾸면 모든 텍스트 위젯이 새 글을 해석하게 루트부터 다시 맞춥니다.
         */
        void setLocalization( UIScreenHandle handle, const LocalizationManager* pLocalization );
        /** @brief 화면 @p handle 이 그릴 렌더 텍스처 경로를 바꿉니다(해상도를 바꿨을 때 트리를 그대로 두고 대상만 옮긴다). */
        void setTargetPath( UIScreenHandle handle, const hashed_string& targetPath );
        /** @brief 화면 @p handle 의 입력 방식을 정합니다(탐색이면 `:focus-visible` 상태가 켜진다). 방식이 바뀌면 스타일을 다시 맞춥니다. */
        void setNavigationMode( UIScreenHandle handle, bool bNavigationMode );
        bool isEmpty() const { return _listOffscreen.empty(); }
        /** @brief 뷰포트가 있는 화면마다 렌더 텍스처 대상 그리기 목록을 덧붙입니다(불투명 바탕으로 지웁니다). 렌더 패킷이 캔버스 대상 목록에 넣습니다. */
        void appendCanvases( vector<CanvasTargetDrawList>& inoutListTarget ) const;

    private:
        /** @brief UI 갱신의 그리기 뒤 칸입니다 — 미리보기가 튼 애니메이션을 진행하고(스택 밖 화면은 `Open` 을 틀지 않는다) 화면마다 스타일, 레이아웃, 그리기를 돌립니다. */
        void update( float32 deltaSeconds );
        /** @brief 재생 중인 애니메이션이 있는 화면의 애니메이션을 @p deltaSeconds 만큼 진행합니다(미리보기의 애니메이션 재생). */
        void updateAnimations( float32 deltaSeconds );
        /**
         * @brief 화면마다 스타일, 레이아웃, 그리기를 자기 뷰포트로 돌립니다.
         * @details 컨텍스트는 화면 UI 의 것(@p baseLayout, @p basePaint)에서 이 화면의 뷰포트와 글자 배율만 바꿔 쓴다.
         */
        void paintScreens( const UILayoutContext& baseLayout, const UIPaintContext& basePaint );
        /** @brief 문서 @p documentPath(또는 그것을 조각으로 쓰는 문서)로 연 화면을 새로 생성하고 자기 테마로 다시 맞춥니다(문서 캐시의 다시 읽기 알림). */
        void onDocumentReloaded( string_view documentPath );
        /** @brief 시트 @p sheetPath 를 쓰는 화면의 스타일 시트를 자기 테마로 다시 적용합니다(스타일 시트 캐시의 다시 읽기 알림). */
        void onStyleSheetReloaded( string_view sheetPath );
        /** @brief 지금 테마가 바뀌었습니다 — 화면마다 자기 테마(빈 이름이면 지금 테마)로 다시 맞춥니다. */
        void onThemeChanged();

    private:
        vector<UIOffscreenScreen> _listOffscreen;
        UISystem*                 _pUI;                            ///< 화면 스택(문서 생성 · 컨텍스트, 기동이 찾는다)
        UIThemeController*        _pThemes;                        ///< 스타일 세트를 짓는 테마(기동이 찾는다)
        Subscription              _updateSubscription;             ///< UI 갱신의 그리기 뒤 칸(`UISystem::onUpdateAfterPaint`) 구독
        Subscription              _documentReloadedSubscription;   ///< 문서 캐시의 다시 읽기 알림 구독
        Subscription              _styleSheetReloadedSubscription; ///< 스타일 시트 캐시의 다시 읽기 알림 구독
        Subscription              _themeChangedSubscription;       ///< 테마가 바뀐 알림 구독
    };
} // namespace sw
