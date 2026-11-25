/**
 * @file InventoryViewModel.h
 * @brief 인벤토리 문서가 `{bind:_details}` · `_stats` · `_weight` · `_status` 로 읽는 값입니다.
 */
#pragma once
#include "Core/Container/string.h"

#include "Engine/Presentation/UIViewModel.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @class InventoryViewModel @brief 인벤토리 문서가 `{bind:_details}` · `_stats` · `_weight` · `_status` 로 읽는 값입니다. */
    REFLECT()
    class SW_GF_API InventoryViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        InventoryViewModel();
        ~InventoryViewModel() override;

        const TypeInfo* getTypeInfo() const override;
        void            setDetails( const string& text ) { (void)setField( _details, text, "_details" ); }
        void            setStats( const string& text ) { (void)setField( _stats, text, "_stats" ); }
        void            setWeight( const string& text ) { (void)setField( _weight, text, "_weight" ); }
        void            setStatus( const string& text ) { (void)setField( _status, text, "_status" ); }

    private:
        PROPERTY( DisplayName = "Details" )
        string _details;
        PROPERTY( DisplayName = "Stats" )
        string _stats;
        PROPERTY( DisplayName = "Weight" )
        string _weight;
        PROPERTY( DisplayName = "Status" )
        string _status;
    };
} // namespace sw
