#include "pch.h"

#include "Engine/Window/WindowTextInput.h"

#include "Core/Container/StringUtil.h"

namespace sw
{
    size_t WindowTextComposition::findByteOffset( uint32 characterIndex ) const
    {
        size_t offset = 0;
        for ( uint32 index = 0; index < characterIndex && offset < _text.size(); ++index )
        {
            (void)StringUtil::decodeUTF8( _text, offset );
        }
        return offset;
    }

    void WindowTextComposition::applyEdit( uint32 firstCharacter, uint32 deleteCount, string_view insertedText )
    {
        const size_t firstByte = findByteOffset( firstCharacter );
        const size_t lastByte  = findByteOffset( firstCharacter + deleteCount );
        _text.replace( firstByte, lastByte - firstByte, insertedText.data(), insertedText.size() );
    }

    bool WindowTextInputFrame::finish( string& outCommit, string& outPreedit )
    {
        const bool bChanged = _pendingPreedit != _preedit;
        outCommit           = _pendingCommit;
        _preedit            = _pendingPreedit;
        outPreedit          = _preedit;
        _pendingCommit.clear();
        _pendingPreedit.clear();
        return bChanged;
    }

    bool WindowTextInputFrame::reset()
    {
        const bool bWasComposing = _preedit.empty() == false;
        _preedit.clear();
        _pendingPreedit.clear();
        _pendingCommit.clear();
        return bWasComposing;
    }
} // namespace sw
