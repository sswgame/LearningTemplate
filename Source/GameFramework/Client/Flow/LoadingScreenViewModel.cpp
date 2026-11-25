#include "pch.h"

#include "GameFramework/Client/Flow/LoadingScreenViewModel.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    LoadingScreenViewModel::LoadingScreenViewModel()
        : UIViewModel{}
        , _progress{ 0.0f }
        , _progressVisibility{ WidgetVisibility::Collapsed }
        , _tip{}
        , _tipVisibility{ WidgetVisibility::Collapsed }
    {
    }

    LoadingScreenViewModel::~LoadingScreenViewModel() = default;

    const TypeInfo* LoadingScreenViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void LoadingScreenViewModel::setProgress( optional<float32> progress )
    {
        if ( progress.has_value() )
            (void)setField( _progress, MathUtil::clamp( *progress, 0.0f, 1.0f ), "_progress" );
        // 진행 막대 · 팁은 입력을 받지 않는다.
        (void)setField( _progressVisibility, progress.has_value() ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed, "_progressVisibility" );
    }

    void LoadingScreenViewModel::setTip( const string& tip )
    {
        (void)setField( _tip, tip, "_tip" ); // 팁은 현지화 키(원문)다 — 글 위젯이 그릴 때 푼다(표에 없으면 원문 그대로)
        (void)setField( _tipVisibility, tip.empty() ? WidgetVisibility::Collapsed : WidgetVisibility::HitTestInvisible, "_tipVisibility" );
    }
} // namespace sw
