#include "pch.h"

#if defined( SW_TEXT_COMPLEX_SHAPING )
    #include "Engine/Text/SheenBidi/ComplexBidi.h"

    #include <SheenBidi/SheenBidi.h>

namespace sw
{
    void ComplexBidiUtil::resolveLevels( const vector<uint32>& listCodepoint, TextDirection paragraphDirection, vector<uint8>& outListLevel )
    {
        outListLevel.assign( listCodepoint.size(), paragraphDirection == TextDirection::RightToLeft ? uint8{ 1 } : uint8{ 0 } );
        if ( listCodepoint.empty() )
            return;

        // 코드 포인트 열을 UTF-32 로 넘기면 SheenBidi 의 위치가 곧 코드 포인트 번호다(바이트 위치로 바꿀 일이 없다).
        SBCodepointSequence sequence{};
        sequence.stringEncoding  = SBStringEncodingUTF32;
        sequence.stringBuffer    = const_cast<uint32*>( listCodepoint.data() );
        sequence.stringLength    = static_cast<SBUInteger>( listCodepoint.size() );
        SBAlgorithmRef algorithm = SBAlgorithmCreate( &sequence );
        const SBLevel  baseLevel = paragraphDirection == TextDirection::RightToLeft ? SBLevel{ 1 } : SBLevel{ 0 };

        // 단락 구분(B 부류 — `\n` 포함)마다 새 문단이다. 문단 수준은 위젯의 흐름 방향이 정한다(첫 강한 문자로 정하지 않는다 — 단순 버전과 같은 계약).
        SBUInteger offset = 0;
        while ( offset < sequence.stringLength )
        {
            SBParagraphRef   paragraph = SBAlgorithmCreateParagraph( algorithm, offset, sequence.stringLength - offset, baseLevel );
            const SBUInteger length    = SBParagraphGetLength( paragraph );
            const SBLevel*   pLevel    = SBParagraphGetLevelsPtr( paragraph );
            for ( SBUInteger index = 0; index < length; ++index )
            {
                outListLevel[static_cast<size_t>( offset + index )] = static_cast<uint8>( pLevel[index] );
            }
            SBParagraphRelease( paragraph );
            SW_ASSERT( length > 0 ); // 문단은 적어도 한 코드 포인트다 — 아니면 이 반복이 끝나지 않는다
            if ( length == 0 )
                break;
            offset += length;
        }
        SBAlgorithmRelease( algorithm );
    }
} // namespace sw
#endif
