/**
 * @file UIBindingConverterRegistry.h
 * @brief 바인딩 변환기(`{bind:필드, converter=Percent}`)의 이름 → 함수 레지스트리입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/RegistrationList.h"
#include "Core/String/hashed_string.h"

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
     * @brief 이름으로 찾는 변환기 레지스트리입니다(`UISystem::getBindingConverters`). 엔진 기본 변환기는 `Percent`(0..1 → "75%"), `Invert`(불리언 뒤집기),
     *        `NotEmpty`(텍스트가 비지 않았는지), `IsZero`(숫자가 0 인지), `Seconds`(초 → "m:ss")입니다.
     * @details 게임과 키트가 자기 변환기를 추가하고 돌려받은 구독을 든다 — 구독이 사라지면(모듈의 객체와 함께) 거두고, 엔진 기본을 덮었던 이름이면 기본으로 돌아간다.
     *          게임 스레드에서만 쓴다.
     */
    class SW_API UIBindingConverterRegistry
    {
    public:
        UIBindingConverterRegistry();

        /**
         * @brief 변환기를 등록하고 그 구독을 돌려줍니다. 같은 이름이 있으면 교체합니다(엔진 기본을 같은 이름으로 덮을 수 있다). 구독이 사라지면 거둔다.
         * @details 교체된 옛 구독은 놓아도 지금 등록을 지우지 않는다 — 핫 리로드는 새 인스턴스를 먼저 세우고 옛 인스턴스를 내린다.
         */
        [[nodiscard]] Subscription registerConverter( const UIBindingConverter& converter );
        /** @brief 이름으로 찾습니다. 없으면(거둔 이름 포함) nullptr 입니다. */
        const UIBindingConverter* findConverter( const hashed_string& name ) const;
        /** @brief 표가 바뀔 때마다 오르는 번호입니다. 바인딩은 변환기를 값으로 복사하므로, 바뀌면 다시 묶어야 한다(`UISystem::updateBindings`). */
        uint32 getRevision() const { return _revision; }

    private:
        /** @brief 이름 하나의 지금 등록 구독 번호입니다. 교체되면 새 번호로 바뀐다. */
        struct ConverterOwner
        {
            hashed_string _name{};
            uint64        _id{ 0 };
        };
        /** @brief 엔진 기본 변환기를 올립니다(생성자가 부른다 — 구독 없이 늘 있다). */
        void registerEngineConverters();
        /** @brief 등록 구독 @p id 가 풀렸습니다. 그 이름의 지금 등록이 이 구독이면 거두고, 엔진 기본이 있던 이름이면 기본으로 돌린다. */
        void releaseSubscription( uint64 id );

        NameRegistry<UIBindingConverter> _registry;
        vector<UIBindingConverter>       _listEngineConverter; ///< 엔진 기본(덮였다 풀리면 돌아간다)
        vector<ConverterOwner>           _listOwner;           ///< 이름마다 지금 등록의 구독 번호
        uint64                           _nextOwnerID;         ///< 다음 등록 구독 번호(0 은 쓰지 않는다)
        uint32                           _revision;            ///< `getRevision`
        /** @brief 이 객체가 내준 구독입니다. 맨 끝 멤버라 먼저 사라져 남은 구독을 무효화한다. */
        SubscriptionList _subscriptions{ SubscriptionList::ReleaseFunction::create<&UIBindingConverterRegistry::releaseSubscription>( this ) };
    };
} // namespace sw
