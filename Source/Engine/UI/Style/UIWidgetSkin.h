/**
 * @file UIWidgetSkin.h
 * @brief 엔진 기본 스타일 시트(`EngineDefaultAssets::_uiWidgetSkin`)에서 푼 위젯 기본 겉모습과 그 값을 드는 서브시스템을 정의합니다.
 * @details 언리얼 `FCoreStyle` · 유니티 런타임 테마(`UnityDefaultRuntimeTheme`) · Godot 기본 테마의 자리다 — 위젯 코드는 겉모습 수치를 들지 않는다.
 *          위젯은 만들어질 때 자기 필드의 기본값을 여기서 받고(문서 · 코드가 적은 값이 이긴다), 필드가 없는 부품 크기는 그릴 때 읽는다.
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/Subsystem.h"
#include "Core/Subsystem/SubsystemSlot.h"

#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/UI/Document/UITextAssetCache.h"
#include "Engine/UI/Widgets/UIBrush.h"

namespace sw
{
    struct UIStyleSheetAsset;

    /**
     * @struct UIWidgetSkin
     * @brief 엔진 위젯들의 기본 겉모습입니다. 시트의 규칙(`ButtonWidget:hover`, 부품은 클래스 `SliderWidget.thumb`)에서 칸을 모두 채워야 만들어집니다.
     */
    struct SW_API UIWidgetSkin
    {
        /** @brief 상태마다의 배경 브러시입니다. */
        struct StateBrushes
        {
            UIBrush _normal{};
            UIBrush _hovered{};
            UIBrush _pressed{};
            UIBrush _disabled{};
        };

        struct Button
        {
            StateBrushes _brush{};
            float4       _padding{};
        };

        struct CheckBox
        {
            StateBrushes _brush{};
            float4       _padding{};
            float2       _boxSize{};
            UIBrush      _box{};
            float4       _boxPadding{}; ///< 상자 안에서 표시가 놓이는 안쪽 여백
            UIBrush      _mark{};
            float32      _labelGap{ 0.0f };      ///< 상자와 글 사이 틈(`.label` 의 왼쪽 여백)
            float32      _disabledAlpha{ 0.0f }; ///< 꺼진 상자의 색이 넘지 않는 불투명도
        };

        /** @brief 콤보 상자와 좌우 선택의 화살표입니다. */
        struct Arrow
        {
            float4  _color{};
            float32 _areaWidth{ 0.0f };     ///< 화살표가 차지하는 너비
            float32 _disabledAlpha{ 0.0f }; ///< 꺼진 화살표가 넘지 않는 불투명도
        };

        struct ComboBox
        {
            float4  _padding{};
            UIBrush _popup{};
            float4  _popupPadding{};
            float4  _popupShadowColor{};
            float2  _popupShadowOffset{};
            float32 _popupShadowBlur{ 0.0f };
            float32 _optionHeight{ 0.0f };
            float4  _optionPadding{};
            float32 _optionCornerRadius{ 0.0f };
            Arrow   _arrow{};
        };

        struct Rotator
        {
            float4 _padding{};
            Arrow  _arrow{};
        };

        struct Slider
        {
            float2  _size{};
            float4  _trackColor{};
            float32 _trackThickness{ 0.0f };
            float4  _fillColor{};
            float4  _thumbColor{};
            float32 _thumbSize{ 0.0f };
            float32 _disabledOpacity{ 0.0f }; ///< 꺼진 슬라이더의 불투명도 배율
        };

        struct ProgressBar
        {
            float2  _size{};
            float4  _backgroundColor{};
            float4  _fillColor{};
            float32 _cornerRadius{ 0.0f };
        };

        struct TextInput
        {
            UIBrush _background{};
            float4  _padding{};
            float4  _textColor{};
            float4  _hintColor{};
            float4  _selectionColor{};
            float4  _caretColor{};
            float32 _caretWidth{ 0.0f };
            float32 _underlineThickness{ 0.0f }; ///< 조합 중인 글 밑줄
        };

        struct FocusRing
        {
            float4  _color{};
            float4  _cornerRadius{};
            float32 _width{ 0.0f };
            float32 _outset{ 0.0f }; ///< 위젯 바깥으로 넓히는 길이(`_padding` 의 왼쪽)
        };

        Button      _button{};
        CheckBox    _checkBox{};
        ComboBox    _comboBox{};
        Rotator     _rotator{};
        Slider      _slider{};
        ProgressBar _progressBar{};
        TextInput   _textInput{};
        FocusRing   _focusRing{};

        /** @brief 시트의 규칙에서 칸을 모두 채웁니다. 빠진 칸이 있으면 첫 칸의 이름(`ButtonWidget:hover _backgroundColor`)을 적은 오류입니다 — 대신 쓸 값이 없다. */
        [[nodiscard]] static expected<UIWidgetSkin> build( const UIStyleSheetAsset& sheet );
        /** @brief 지금 엔진 겉모습입니다(`UIWidgetSkinService::getSkin`). 시트를 읽지 못했으면 멈춥니다. */
        static const UIWidgetSkin& get();
    };
} // namespace sw

namespace sw
{
    /**
     * @class UIWidgetSkinService
     * @brief 엔진 기본 스타일 시트를 읽어 `UIWidgetSkin` 으로 드는 엔진 범위 서브시스템입니다. 기동이 읽고, 읽지 못하거나 칸이 빠지면 기동을 멈춥니다.
     * @details 시트는 에셋 캐시로 들어 핫 리로드된다 — 다시 읽기가 실패하면 앞 겉모습을 그대로 쓴다(`UITextAssetCache`). 바뀌면 `onSkinChanged` 를 알리고 문서로 연 화면이 다시 지어진다. 게임 스레드에서만 쓴다.
     */
    class SW_API UIWidgetSkinService final : public Subsystem
    {
    public:
        UIWidgetSkinService();
        ~UIWidgetSkinService() override;

        // Subsystem
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "UIWidgetSkin";
        SubsystemInitResult          initializeSubsystem() override;
        void                         shutdownSubsystem() override;
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const>      getSubsystemDependencies() const override;
        SubsystemHostMask            getSubsystemHost() const override { return EngineSubsystemHost::kClient; }

        /** @brief 지금 겉모습입니다. 서지 않았으면(시트를 읽지 못한 기동 · 클라이언트가 아닌 호스트) 멈춥니다. */
        const UIWidgetSkin& getSkin() const;
        /** @brief 시트를 다시 읽어 겉모습이 바뀌었다는 알림입니다. */
        MulticastDelegate<void()>& onSkinChanged() { return _onSkinChanged; }

    private:
        friend struct UIWidgetSkinTestAccess; // 테스트는 다른 시트의 겉모습을 잠깐 건다
        friend struct UIWidgetSkin;           // 기동 단계 앞에서 만든 위젯이 시트를 그 자리에서 읽는다(`UIWidgetSkin::get`)

        void onSheetReloaded( string_view sheetPath );
        /** @brief 캐시의 시트를 `_skin` 에 겁니다. 실패하면 오류를 남기고 false 입니다(앞 겉모습은 그대로). */
        [[nodiscard]] bool applySheet();

    private:
        UITextAssetCache               _cache; ///< 시트에서 푼 겉모습의 캐시(레지스트리에 오르는 에셋 캐시)
        shared_ptr<const UIWidgetSkin> _skin;
        MulticastDelegate<void()>      _onSkinChanged;
        Subscription                   _sheetReloadedSubscription;
        Subscription                   _assetCacheRegistration; ///< 에셋 캐시 레지스트리 등록 구독(캐시보다 먼저 풀린다, 나중에 선언)

        SW_SUBSYSTEM_SLOT( UIWidgetSkinService, Required );
    };
} // namespace sw
