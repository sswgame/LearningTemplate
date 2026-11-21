// 문서 편집의 상호작용 구간(위젯 활성 ~ 해제 동안 쥐고 끝에 한 단계)입니다.
#include "pch.h"

#include "Editor/Common/Workspace/Undo/DocumentEditSpan.h"

namespace sw::editor
{
    DocumentEditSpan::DocumentEditSpan()
        : _pBaseline{ make_shared<const string>() }
        , _pendingLabel{}
        , _bHolding{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void DocumentEditSpan::resetBaseline( string text )
    {
        _pBaseline = make_shared<const string>( std::move( text ) );
        _pendingLabel.clear();
        _bHolding = SW_FALSE;
    }

    optional<DocumentEdit> DocumentEditSpan::notifyEditedText( string_view label, string after, bool bInteracting )
    {
        if ( after == *_pBaseline )
        {
            _bHolding = SW_FALSE; // 쥔 구간이 처음 글로 돌아왔다 — 기록할 것이 없다
            return nullopt;
        }
        if ( bInteracting )
        {
            _pendingLabel = string{ label };
            _bHolding     = SW_TRUE;
            return nullopt;
        }
        _bHolding = SW_FALSE;
        _pendingLabel.clear();
        DocumentEdit edit{ std::move( _pBaseline ), make_shared<const string>( std::move( after ) ), string{ label } };
        _pBaseline = edit._pAfter; // 끝 글이 다음 기준선이다 — 복사하지 않고 같은 버퍼를 든다
        return edit;
    }

    optional<DocumentEdit> DocumentEditSpan::finish( string after )
    {
        if ( _bHolding == SW_FALSE )
            return nullopt;
        _bHolding = SW_FALSE;
        if ( after == *_pBaseline )
            return nullopt;
        DocumentEdit edit{ std::move( _pBaseline ), make_shared<const string>( std::move( after ) ), std::move( _pendingLabel ) };
        _pendingLabel.clear();
        _pBaseline = edit._pAfter; // 끝 글이 다음 기준선이다 — 복사하지 않고 같은 버퍼를 든다
        return edit;
    }
} // namespace sw::editor
