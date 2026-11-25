/**
 * @file LoadingScreenViewModel.h
 * @brief 로딩 화면 문서가 바인딩으로 읽는 값입니다 — 진행 막대(`{bind:_progress}` · `_progressVisibility`)와 팁(`{bind:_tip}` · `_tipVisibility`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Presentation/UIViewModel.h"
#include "Engine/Presentation/WidgetVisibility.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @class LoadingScreenViewModel @brief 파일 머리말 참고. 로딩 화면 제어(`LoadingScreenController`)가 쥐고 값을 넣는다. */
    REFLECT()
    class SW_GF_API LoadingScreenViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        LoadingScreenViewModel();
        ~LoadingScreenViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 로드 진행(0..1)을 보입니다. 진행을 모르면(없음) 막대를 접습니다. */
        void setProgress( optional<float32> progress );
        /** @brief 팁(현지화 키 · 원문)을 보입니다. 비면 접습니다. */
        void setTip( const string& tip );

    private:
        PROPERTY( DisplayName = "Progress", Min = 0.0, Max = 1.0 )
        float32 _progress;
        PROPERTY( DisplayName = "Progress Visibility" )
        WidgetVisibility _progressVisibility;
        PROPERTY( DisplayName = "Tip" )
        string _tip;
        PROPERTY( DisplayName = "Tip Visibility" )
        WidgetVisibility _tipVisibility;
    };
} // namespace sw
