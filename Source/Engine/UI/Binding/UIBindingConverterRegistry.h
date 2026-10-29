/**
 * @file UIBindingConverterRegistry.h
 * @brief 바인딩 변환기(`{bind:필드, converter=Percent}`)의 이름 → 함수 레지스트리입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/RegistrationList.h"
#include "Core/String/hashed_string.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/UI/Binding/UIBindingValue.h"

namespace sw
{
    /** @brief 변환기 함수입니다. @p in 을 바꿔 @p outValue 에 둡니다. 바꿀 수 없으면 false 를 반환하고 위젯 필드는 그대로 둡니다. */
    using UIBindingConvertFunction = bool ( * )( const UIBindingValue& in, UIBindingValue& outValue );

    /** @struct UIBindingConverter @brief 변환기 하나입니다. 받는 종류, 내는 종류, 함수를 가집니다(종류는 바인딩을 연결할 때 타입 검사에 씁니다). */
    struct UIBindingConverter
    {
        hashed_string            _name{};
        UIBindingConvertFunction _pConvert{ nullptr };
        UIBindingValueKind       _inputKind{ UIBindingValueKind::None };
        UIBindingValueKind       _outputKind{ UIBindingValueKind::None };
    };
} // namespace sw

namespace sw
{
    /**
     * @class UIBindingConverterRegistry
     * @brief 이름으로 찾는 변환기 레지스트리입니다(엔진 범위 서브시스템 `UIBindingConverters`). 엔진 기본 변환기는 `Percent`(0..1 → "75%"), `Invert`(불리언 뒤집기),
     *        `NotEmpty`(텍스트가 비지 않았는지), `IsZero`(숫자가 0 인지), `Seconds`(초 → "m:ss")입니다.
     * @details 게임과 키트가 자기 변환기를 추가하고 돌려받은 구독을 든다 — 구독이 사라지면(모듈의 객체와 함께) 거두고, 엔진 기본을 덮었던 이름이면 기본으로 돌아간다.
     *          게임 스레드에서만 쓴다.
     */
    class SW_API UIBindingConverterRegistry final : public ISubsystem
    {
    public:
        UIBindingConverterRegistry();

        // ISubsystem — 엔진 범위의 서브시스템이다(`engine::getSubsystem<UIBindingConverterRegistry>()`, 게임은 `game::getSubsystem<UIBindingConverterRegistry>()`).
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "UIBindingConverters";
        SubsystemInitResult          initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                         shutdownSubsystem() override {}
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        /**
         * @brief 변환기를 등록하고 그 구독을 돌려줍니다. 같은 이름이 있으면 교체합니다(엔진 기본을 같은 이름으로 덮을 수 있다). 구독이 사라지면 거둔다.
         * @details 교체된 옛 등록은 그림자로 남아 옛 구독이 놓으면 버려진다(`NameRegistry::addOwned`) — 핫 리로드는 새 인스턴스를 먼저 세우고 옛 인스턴스를 내린다.
         */
        [[nodiscard]] Subscription registerConverter( const UIBindingConverter& converter );
        /** @brief 이름으로 찾습니다. 없으면(거둔 이름 포함) nullptr 입니다. */
        const UIBindingConverter* findConverter( const hashed_string& name ) const;
        /** @brief 표가 바뀔 때마다 오르는 번호입니다. 바인딩은 변환기를 값으로 복사하므로, 바뀌면 다시 묶어야 한다(`UISystem::updateBindings`). */
        uint32 getRevision() const { return _registry.getRevision(); }

    private:
        /** @brief 엔진 기본 변환기를 올립니다(생성자가 부른다 — 주인 없이 늘 있다). */
        void registerEngineConverters();

        NameRegistry<UIBindingConverter> _registry; ///< 엔진 기본은 주인 없는 값, 게임 · 키트 것은 주인 있는 값(`addOwned` — 풀리면 덮었던 기본으로 돌아간다)
    };
} // namespace sw
