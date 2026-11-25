/**
 * @file UIStatusViewModel.h
 * @brief 상태 줄 하나만 보이는 화면의 뷰모델입니다 — 문서가 `{bind:_status}` 로 읽는다(목록 화면의 불러오는 중 · 실패 · 빈 목록 글).
 */
#pragma once
#include "Core/Container/string.h"

#include "Engine/Presentation/UIViewModel.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @class UIStatusViewModel @brief 상태 줄 하나만 보이는 화면의 뷰모델입니다 — 문서가 `{bind:_status}` 로 읽는다(목록 화면의 불러오는 중 · 실패 · 빈 목록 글). */
    REFLECT()
    class SW_API UIStatusViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        UIStatusViewModel();
        ~UIStatusViewModel() override;

        const TypeInfo* getTypeInfo() const override;
        void            setStatus( const string& text ) { (void)setField( _status, text, "_status" ); }

    private:
        PROPERTY( DisplayName = "Status" )
        string _status;
    };
} // namespace sw
