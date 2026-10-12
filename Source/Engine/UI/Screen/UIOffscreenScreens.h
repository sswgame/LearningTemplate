/**
 * @file UIOffscreenScreens.h
 * @brief 화면 스택 밖에서 렌더 텍스처에 그리는 화면들(에디터 UI 미리보기)입니다(`UISystem` 의 값 멤버).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/UI/Base/WidgetTypes.h"
#include "Engine/UI/Screen/UIScreen.h"

namespace sw
{
    struct UILayoutContext;
    struct UIPaintContext;

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
    };
} // namespace sw

namespace sw
{
    /**
     * @class UIOffscreenScreens
     * @brief 오프스크린 화면 목록입니다. 입력과 포커스가 없고, 화면마다 자기 뷰포트로 스타일, 레이아웃, 그리기를 돌려 렌더 텍스처 대상 목록을 내놓습니다.
     * @details 문서 생성, 핸들 발급, 스타일 세트 적용은 스택 화면과 같은 경로라 `UISystem` 이 한다. 이 클래스는 목록과 화면마다의 프레임 처리만 맡는다.
     */
    class SW_API UIOffscreenScreens
    {
    public:
        UIOffscreenScreens();
        ~UIOffscreenScreens();
        UIOffscreenScreens( const UIOffscreenScreens& )            = delete;
        UIOffscreenScreens& operator=( const UIOffscreenScreens& ) = delete;

        /** @brief 화면 @p screen 을 렌더 텍스처 @p targetPath 대상으로 목록 끝에 넣고 그 화면을 반환합니다(뷰포트는 `setView` 전까지 비어 있어 그리지 않습니다). */
        UIScreen& add( unique_ptr<UIScreen> screen, const hashed_string& targetPath );
        /** @brief 핸들 @p handle 의 화면을 지웁니다. 없으면 아무것도 하지 않습니다. */
        void remove( UIScreenHandle handle );
        void clear() { _listOffscreen.clear(); }
        /** @brief 핸들 @p handle 의 화면입니다. 없으면 nullptr 입니다. */
        UIScreen* findScreen( UIScreenHandle handle ) const;
        /**
         * @brief 화면 @p handle 의 뷰포트, 글자 배율(0 이하면 1), 테마를 정합니다.
         * @return 테마가 바뀌었으면 true 입니다. 부르는 쪽이 그 화면의 스타일 세트를 새 테마로 다시 적용합니다.
         */
        [[nodiscard]] bool setView( UIScreenHandle handle, const UIViewport& viewport, float32 textScale, const hashed_string& theme );
        /** @brief 목록입니다. 테마와 시트, 문서 핫 리로드가 모든 화면을 돌 때 씁니다. */
        vector<UIOffscreenScreen>&       getScreens() { return _listOffscreen; }
        const vector<UIOffscreenScreen>& getScreens() const { return _listOffscreen; }
        bool                             isEmpty() const { return _listOffscreen.empty(); }

        /**
         * @brief 화면마다 스타일, 레이아웃, 그리기를 자기 뷰포트로 돌립니다(`UISystem::update` 끝).
         * @details 컨텍스트는 화면 UI 의 것(@p baseLayout, @p basePaint)에서 이 화면의 뷰포트와 글자 배율만 바꿔 쓴다.
         */
        void update( const UILayoutContext& baseLayout, const UIPaintContext& basePaint );
        /** @brief 뷰포트가 있는 화면마다 렌더 텍스처 대상 그리기 목록을 덧붙입니다(불투명 바탕으로 지웁니다). */
        void appendCanvases( vector<CanvasTargetDrawList>& inoutListTarget ) const;

    private:
        vector<UIOffscreenScreen> _listOffscreen;
    };
} // namespace sw
