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
            (void)SW_SET_VIEW_MODEL_FIELD( _progress, MathUtil::clamp( *progress, 0.0f, 1.0f ) );
        // 진행 막대 · 팁은 입력을 받지 않는다.
        (void)SW_SET_VIEW_MODEL_FIELD( _progressVisibility, toHUDVisibility( progress.has_value() ) );
    }

    void LoadingScreenViewModel::setTip( const string& tip )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _tip, tip ); // 팁은 현지화 키(원문)다 — 글 위젯이 그릴 때 푼다(표에 없으면 원문 그대로)
        (void)SW_SET_VIEW_MODEL_FIELD( _tipVisibility, toHUDVisibility( tip.empty() == false ) );
    }
} // namespace sw
