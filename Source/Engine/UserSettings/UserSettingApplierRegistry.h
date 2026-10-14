/**
 * @file UserSettingApplierRegistry.h
 * @brief 사용자 설정이 값을 넘기는 이름 붙은 적용 함수와, 런타임 선택지 공급자의 레지스트리를 정의합니다.
 * @details 스키마(데이터)는 `target="applier:<name>"` 과 `optionsFrom="<name>"` 으로 여기의 이름을 고른다. 코드는 이름과 동작만 가진다.
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

    /** @brief 적용 함수 한 번 부름의 인자입니다. */
    struct UserSettingApplyParams
    {
        const UserSettingDef* _pDef{ nullptr };   ///< 적용하는 설정입니다.
        string_view           _value{};           ///< 정규화한 값
        string_view           _param{};           ///< 스키마의 `param`
        bool                  _bStartup{ false }; ///< 기동이나 다시 적용(`reapplyAll`)이면 true 이고, 메뉴에서 바꾼 값이면 false 입니다.
        bool                  _bDefault{ false }; ///< 값이 기본값과 같으면 true(플레이어가 고르지 않은 값)
    };

    /** @brief 값을 대상에 넣습니다. 넣지 못하면 false 입니다(경고는 적용 함수가 남깁니다). */
    SW_DECLARE_DELEGATE( bool, UserSettingApplierDelegate, const UserSettingApplyParams& );
    /** @brief 적용 함수가 받는 값인지 확인합니다(스키마 검사가 기본값과 선택지를 대조합니다). */
    SW_DECLARE_DELEGATE( bool, UserSettingValueFilterDelegate, string_view );
    /** @brief 지금 고를 수 있는 선택지를 채웁니다(언어 목록 등). */
    SW_DECLARE_DELEGATE( void, UserSettingOptionProviderDelegate, vector<UserSettingOption>& );
} // namespace sw

namespace sw
{
    /**
     * @class UserSettingApplierRegistry
     * @brief 이름에서 적용 함수와 선택지 공급자를 찾는 레지스트리입니다.
     * @details 모듈(게임, 키트)도 등록할 수 있다. 모듈 이미지를 언로드하기 전에 `UserSettingsManager`(IModuleUnloadListener)가 그 범위의 것을
     *          제거한다(`removeCodeWithin`). 다만 스키마 검사는 스키마를 읽을 때 한 번만 하므로, 엔진 기동 때 읽는 스키마가 쓰는 이름은 엔진이 등록해야 한다.
     */
    class SW_API UserSettingApplierRegistry
    {
    public:
        UserSettingApplierRegistry();

        /** @brief 적용 함수를 올립니다. 같은 이름이 있으면 바꿉니다. @p filter 는 받는 값을 아는 적용 함수만 줍니다. */
        void registerApplier( const hashed_string& name, const UserSettingApplierDelegate& applier, const UserSettingValueFilterDelegate& filter = {} );
        /** @brief 적용 함수를 내립니다. */
        void unregisterApplier( const hashed_string& name );
        /** @brief 이름의 적용 함수가 있는지 봅니다. */
        bool hasApplier( const hashed_string& name ) const;
        /** @brief 적용 함수를 부릅니다. 없으면 false 입니다. */
        bool invokeApplier( const hashed_string& name, const UserSettingApplyParams& context ) const;
        /** @brief 적용 함수가 @p value 를 받는지 확인합니다. 필터가 없는 적용 함수나 없는 적용 함수는 true 입니다. */
        bool acceptsValue( const hashed_string& name, string_view value ) const;

        /** @brief 선택지 공급자를 올립니다. 같은 이름이 있으면 바꿉니다. */
        void registerOptionProvider( const hashed_string& name, const UserSettingOptionProviderDelegate& provider );
        /** @brief 이름의 공급자가 있는지 봅니다. */
        bool hasOptionProvider( const hashed_string& name ) const;
        /** @brief 공급자의 선택지를 채웁니다. 없으면 false 이고 @p outListOption 은 비어 있습니다. */
        bool collectOptions( const hashed_string& name, vector<UserSettingOption>& outListOption ) const;

        /** @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 적용 함수, 필터, 공급자를 제거하고 제거한 개수를 반환합니다. */
        uint32 removeCodeWithin( const void* pBegin, const void* pEnd );
        /** @brief 모두 비웁니다. */
        void clear();

    private:
        /** @brief 적용 함수 한 줄입니다. */
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
