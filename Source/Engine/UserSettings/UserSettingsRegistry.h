/**
 * @file UserSettingsRegistry.h
 * @brief 사용자 설정이 값을 넘기는 이름 붙인 적용기와, 런타임 선택지 공급자의 등록부입니다.
 * @details 스키마(데이터)는 `target="applier:<name>"` · `optionsFrom="<name>"` 으로 여기 이름을 고릅니다. 코드는 이름과 동작만 가집니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct UserSettingDef;
    struct UserSettingOption;

    /** @brief 적용기 한 번 부름의 인자입니다. */
    struct UserSettingApplyContext
    {
        const UserSettingDef* _pDef{ nullptr };   ///< 적용하는 설정
        string_view           _value{};           ///< 정규화한 값
        string_view           _param{};           ///< 스키마의 `param`
        bool                  _bStartup{ false }; ///< 기동 · 다시 적용(`reapplyAll`)이면 true, 메뉴에서 바꾼 값이면 false
        bool                  _bDefault{ false }; ///< 값이 기본값과 같으면 true(플레이어가 고르지 않은 값)
    };

    /** @brief 값을 대상에 넣습니다. 넣지 못하면 false 입니다(경고는 적용기가 남깁니다). */
    SW_DECLARE_DELEGATE( bool, UserSettingApplierDelegate, const UserSettingApplyContext& );
    /** @brief 적용기가 받는 값인지 봅니다(스키마 검사가 기본값 · 선택지를 대조합니다). */
    SW_DECLARE_DELEGATE( bool, UserSettingValueFilterDelegate, string_view );
    /** @brief 지금 고를 수 있는 선택지를 채웁니다(언어 목록 등). */
    SW_DECLARE_DELEGATE( void, UserSettingOptionProviderDelegate, vector<UserSettingOption>& );
} // namespace sw

namespace sw
{
    /**
     * @class UserSettingApplierRegistry
     * @brief 이름 → 적용기 · 선택지 공급자 표입니다.
     * @details 모듈(게임 · 키트)도 올릴 수 있습니다 — 모듈 이미지를 내리기 전에 `UserSettingsManager`(IModuleUnloadListener)가 그 범위의 것을
     *          뗍니다(`removeCodeWithin`). 다만 스키마 검사는 스키마를 읽는 때 한 번이므로, 엔진 기동 때 읽는 스키마가 쓰는 이름은 엔진이 올려야 합니다.
     */
    class SW_API UserSettingApplierRegistry
    {
    public:
        UserSettingApplierRegistry();

        /** @brief 적용기를 올립니다. 같은 이름이 있으면 바꿉니다. @p filter 는 받는 값을 아는 적용기만 줍니다. */
        void registerApplier( const hashed_string& name, const UserSettingApplierDelegate& applier, const UserSettingValueFilterDelegate& filter = {} );
        /** @brief 적용기를 내립니다. */
        void unregisterApplier( const hashed_string& name );
        /** @brief 이름의 적용기가 있는지 봅니다. */
        bool hasApplier( const hashed_string& name ) const;
        /** @brief 적용기를 부릅니다. 없으면 false 입니다. */
        bool invokeApplier( const hashed_string& name, const UserSettingApplyContext& context ) const;
        /** @brief 적용기가 @p value 를 받는지 봅니다. 거르개가 없는 적용기 · 없는 적용기는 true 입니다. */
        bool acceptsValue( const hashed_string& name, string_view value ) const;

        /** @brief 선택지 공급자를 올립니다. 같은 이름이 있으면 바꿉니다. */
        void registerOptionProvider( const hashed_string& name, const UserSettingOptionProviderDelegate& provider );
        /** @brief 이름의 공급자가 있는지 봅니다. */
        bool hasOptionProvider( const hashed_string& name ) const;
        /** @brief 공급자의 선택지를 채웁니다. 없으면 false 이고 @p outListOption 은 비어 있습니다. */
        bool collectOptions( const hashed_string& name, vector<UserSettingOption>& outListOption ) const;

        /** @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 적용기 · 거르개 · 공급자를 떼고 뗀 수를 반환합니다. */
        uint32 removeCodeWithin( const void* pBegin, const void* pEnd );
        /** @brief 모두 비웁니다. */
        void clear();

    private:
        /** @brief 적용기 한 줄입니다. */
        struct ApplierEntry
        {
            hashed_string                  _name;
            UserSettingApplierDelegate     _applier;
            UserSettingValueFilterDelegate _filter;
        };
        /** @brief 선택지 공급자 한 줄입니다. */
        struct ProviderEntry
        {
            hashed_string                     _name;
            UserSettingOptionProviderDelegate _provider;
        };

        const ApplierEntry*  findApplierEntry( const hashed_string& name ) const;
        const ProviderEntry* findProviderEntry( const hashed_string& name ) const;

    private:
        vector<ApplierEntry>  _listApplier;
        vector<ProviderEntry> _listProvider;
    };
} // namespace sw
