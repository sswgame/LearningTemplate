/**
 * @file UserSettingsHandlerRegistry.h
 * @brief 사용자 설정이 값을 넘기는 이름 붙은 핸들러와, 런타임 선택지 공급자의 레지스트리를 정의합니다.
 * @details 이 폴더에서 "핸들러"는 코드의 `handler` 를 가리킨다(설정 값을 받아 엔진에 건네는 델리게이트 `UserSettingHandlerDelegate`).
 *          스키마(데이터)는 `target="handler:<name>"` 과 `optionsFrom="<name>"` 으로 여기의 이름을 고른다. 코드는 이름과 동작만 가진다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct UserSettingDef;
    struct UserSettingOption;

    /** @brief 핸들러 한 번 부름의 인자입니다. */
    struct UserSettingApplyParams
    {
        const UserSettingDef* _pDef{ nullptr };   ///< 적용하는 설정입니다.
        string_view           _value{};           ///< 정규화한 값
        string_view           _param{};           ///< 스키마의 `param`
        bool                  _bStartup{ false }; ///< 기동이나 다시 적용(`reapplyAll`)이면 true 이고, 메뉴에서 바꾼 값이면 false 입니다.
        bool                  _bDefault{ false }; ///< 값이 기본값과 같으면 true(플레이어가 고르지 않은 값)
    };

    /** @brief 값을 대상에 넣습니다. 넣지 못하면 false 입니다(경고는 핸들러가 남깁니다). */
    SW_DECLARE_DELEGATE( bool, UserSettingHandlerDelegate, const UserSettingApplyParams& );
    /** @brief 핸들러가 받는 값인지 확인합니다(스키마 검사가 기본값과 선택지를 대조합니다). */
    SW_DECLARE_DELEGATE( bool, UserSettingValueFilterDelegate, string_view );
    /** @brief 지금 고를 수 있는 선택지를 채웁니다(언어 목록 등). */
    SW_DECLARE_DELEGATE( void, UserSettingOptionProviderDelegate, vector<UserSettingOption>& );
} // namespace sw

namespace sw
{
    /**
     * @class UserSettingsHandlerRegistry
     * @brief 이름에서 핸들러와 선택지 공급자를 찾는 레지스트리입니다.
     * @details 모듈(게임, 키트)도 등록할 수 있다. 모듈 이미지를 언로드하기 전에 `UserSettingsManager`(언로드 처리기)가 그 범위의 것을
     *          제거한다(`removeCodeWithin`). 다만 스키마 검사는 스키마를 읽을 때 한 번만 하므로, 엔진 기동 때 읽는 스키마가 쓰는 이름은 엔진이 등록해야 한다.
     *          등록은 돌려준 구독(`Subscription`)이 소유한다 — 구독이 사라지면 그 줄이 빠지고, 레지스트리가 먼저 사라지면 구독이 무효가 된다.
     *          게임 스레드에서만 쓴다(설정 적용 · 메뉴 · 등록이 모두 게임 스레드라 잠금을 두지 않는다).
     */
    class SW_API UserSettingsHandlerRegistry : private SubscriptionSource
    {
    public:
        UserSettingsHandlerRegistry();

        /**
         * @brief 핸들러를 올리고 그 구독을 돌려줍니다. 같은 이름이 있으면 바꿉니다(앞 구독은 더 아무것도 떼지 않는다). @p filter 는 받는 값을 아는 핸들러만 줍니다.
         */
        [[nodiscard]] Subscription registerHandler( const hashed_string& name, const UserSettingHandlerDelegate& handler, const UserSettingValueFilterDelegate& filter = {} );
        /** @brief 이름의 핸들러가 있는지 봅니다. */
        bool hasHandler( const hashed_string& name ) const;
        /** @brief 핸들러를 부릅니다. 없으면 false 입니다. */
        bool invokeHandler( const hashed_string& name, const UserSettingApplyParams& context ) const;
        /** @brief 핸들러가 @p value 를 받는지 확인합니다. 필터가 없는 핸들러나 없는 핸들러는 true 입니다. */
        bool acceptsValue( const hashed_string& name, string_view value ) const;

        /** @brief 선택지 공급자를 올리고 그 구독을 돌려줍니다. 같은 이름이 있으면 바꿉니다(앞 구독은 더 아무것도 떼지 않는다). */
        [[nodiscard]] Subscription registerOptionProvider( const hashed_string& name, const UserSettingOptionProviderDelegate& provider );
        /** @brief 이름의 공급자가 있는지 봅니다. */
        bool hasOptionProvider( const hashed_string& name ) const;
        /** @brief 공급자의 선택지를 채웁니다. 없으면 false 이고 @p outListOption 은 비어 있습니다. */
        [[nodiscard]] bool collectOptions( const hashed_string& name, vector<UserSettingOption>& outListOption ) const;

        /** @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 핸들러, 필터, 공급자를 제거하고 제거한 개수를 반환합니다. */
        uint32 removeCodeWithin( const void* pBegin, const void* pEnd );
        /** @brief 모두 비웁니다. */
        void clear();

    private:
        /** @brief 핸들러 한 줄입니다. */
        struct HandlerEntry
        {
            hashed_string                  _name;
            UserSettingHandlerDelegate     _handler;
            UserSettingValueFilterDelegate _filter;
            uint64                         _subscriptionID{ 0 }; ///< 이 줄을 소유하는 구독의 번호(바꾸면 새 번호)
        };
        /** @brief 선택지 공급자 한 줄입니다. */
        struct ProviderEntry
        {
            hashed_string                     _name;
            UserSettingOptionProviderDelegate _provider;
            uint64                            _subscriptionID{ 0 }; ///< 이 줄을 소유하는 구독의 번호(바꾸면 새 번호)
        };

        const HandlerEntry*  findHandlerEntry( const hashed_string& name ) const;
        const ProviderEntry* findProviderEntry( const hashed_string& name ) const;
        /** @brief 구독 @p id 가 풀렸습니다. 그 번호의 핸들러 · 공급자 줄을 지웁니다(바뀐 줄이면 번호가 달라 남는다). */
        void releaseSubscription( uint64 id ) override;

    private:
        vector<HandlerEntry>  _listHandler;
        vector<ProviderEntry> _listProvider;
        uint64                _nextSubscriptionID; ///< 다음 구독 번호(0 은 쓰지 않는다)
    };
} // namespace sw
