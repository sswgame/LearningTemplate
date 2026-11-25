/**
 * @file ShopViewModel.h
 * @brief 상점 문서가 `{bind:_balance}` · `{bind:_status}` 로 읽는 값입니다.
 */
#pragma once
#include "Core/Container/string.h"

#include "Engine/Presentation/UIViewModel.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @class ShopViewModel @brief 상점 문서가 `{bind:_balance}` · `{bind:_status}` 로 읽는 값입니다. */
    REFLECT()
    class SW_GF_API ShopViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        ShopViewModel();
        ~ShopViewModel() override;

        const TypeInfo* getTypeInfo() const override;
        void            setBalance( const string& text ) { (void)setField( _balance, text, "_balance" ); }
        void            setStatus( const string& text ) { (void)setField( _status, text, "_status" ); }

    private:
        PROPERTY( DisplayName = "Balance" )
        string _balance;
        PROPERTY( DisplayName = "Status" )
        string _status;
    };
} // namespace sw
