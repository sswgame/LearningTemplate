#include "pch.h"

#include "Engine/Text/TextBidi.h"

#include "Engine/Localization/PseudoLocalizer.h"
#include "Engine/Text/TextItemizer.h"

namespace sw
{
    namespace
    {
        struct TextBidiInternal
        {
            /** @brief LRO 는 덮어쓰기(왼쪽에서 오른쪽)의 시작입니다. RLO 와 PDF 는 의사 로컬라이저의 것을 씁니다. */
            static constexpr uint32 kLeftToRightOverride = 0x202D;

            /** @brief 단순 버전의 양방향 분류입니다(UAX #9 의 L, R, EN/AN, NI, BN, B). */
            enum class BidiClass : uint8
            {
                Left,     ///< 강한 왼쪽에서 오른쪽입니다.
                Right,    ///< 강한 오른쪽에서 왼쪽입니다.
                Number,   ///< 숫자입니다(유럽 숫자와 아랍 숫자를 가르지 않습니다).
                Neutral,  ///< 공백, 구두점, 기호입니다.
                Boundary, ///< 폭 없는 문자와 방향 제어입니다(앞 글자의 수준을 따릅니다).
                Separator ///< 단락 구분(문단 수준, 앞뒤를 끊는다)
            };

            static bool isDigit( uint32 codepoint )
            {
                return ( '0' <= codepoint && codepoint <= '9' ) || ( 0x0660u <= codepoint && codepoint <= 0x0669u ) || ( 0x06F0u <= codepoint && codepoint <= 0x06F9u );
            }

            static bool isParagraphSeparator( uint32 codepoint ) { return codepoint == '\n' || codepoint == '\r' || codepoint == 0x2029u; }

            static BidiClass classify( uint32 codepoint )
            {
                if ( isParagraphSeparator( codepoint ) )
                    return BidiClass::Separator;
                if ( TextItemizer::isZeroWidth( codepoint ) )
                    return BidiClass::Boundary;
                if ( isDigit( codepoint ) )
                    return BidiClass::Number;
                if ( TextItemizer::isStrongRightToLeft( codepoint ) )
                    return BidiClass::Right;
                if ( TextItemizer::isNeutral( codepoint ) )
                    return BidiClass::Neutral;
                return BidiClass::Left;
            }

            /** @brief 중립 판정에서 보는 강한 방향입니다. 숫자는 R 처럼 봅니다(N1). L 이면 false 입니다. */
            static bool isRightForNeutrals( BidiClass bidiClass ) { return bidiClass == BidiClass::Right || bidiClass == BidiClass::Number; }

            static bool isStrongOrNumber( BidiClass bidiClass )
            {
                return bidiClass == BidiClass::Left || bidiClass == BidiClass::Right || bidiClass == BidiClass::Number;
            }

            /** @brief 해석이 끝난 분류의 수준입니다(I1, I2). */
            static uint8 computeLevel( BidiClass bidiClass, uint8 paragraphLevel )
            {
                const bool bRightParagraph = TextBidi::isRightToLeftLevel( paragraphLevel );
                switch ( bidiClass )
                {
                    case BidiClass::Left:
                        return bRightParagraph ? 2 : 0;
                    case BidiClass::Right:
                        return 1;
                    case BidiClass::Number:
                        return 2;
                    case BidiClass::Neutral:
                    case BidiClass::Boundary:
                    case BidiClass::Separator:
                        return paragraphLevel;
                }
                return paragraphLevel;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void TextBidi::resolveLevels( const vector<uint32>& listCodepoint, TextDirection paragraphDirection, vector<uint8>& outListLevel )
    {
        using Internal          = TextBidiInternal;
        using BidiClass         = Internal::BidiClass;
        const size_t count      = listCodepoint.size();
        const uint8  paragraph  = getParagraphLevel( paragraphDirection );
        const bool   bRightPara = isRightToLeftLevel( paragraph );
        outListLevel.assign( count, paragraph );

        // 1) 분류 + 덮어쓰기 한 겹(X4, X5 의 단순 버전) + W7(앞 강한 문자가 L 인 숫자는 L). 단락 구분에서 상태를 되돌린다.
        vector<BidiClass> listClass( count, BidiClass::Neutral );
        BidiClass         overrideClass = BidiClass::Neutral; // Neutral 이면 덮어쓰기 없음
        bool              bPreviousLeft = bRightPara == false;
        for ( size_t index = 0; index < count; ++index )
        {
            const uint32 codepoint = listCodepoint[index];
            BidiClass    bidiClass = Internal::classify( codepoint );
            if ( codepoint == PseudoLocalizer::kRightToLeftOverride )
                overrideClass = BidiClass::Right;
            else if ( codepoint == Internal::kLeftToRightOverride )
                overrideClass = BidiClass::Left;
            else if ( codepoint == PseudoLocalizer::kPopDirectionalFormat )
                overrideClass = BidiClass::Neutral;

            if ( bidiClass == BidiClass::Separator )
            {
                overrideClass = BidiClass::Neutral;
                bPreviousLeft = bRightPara == false;
            }
            else if ( bidiClass != BidiClass::Boundary && overrideClass != BidiClass::Neutral )
            {
                bidiClass = overrideClass;
            }

            if ( bidiClass == BidiClass::Left || bidiClass == BidiClass::Right )
                bPreviousLeft = bidiClass == BidiClass::Left;
            else if ( bidiClass == BidiClass::Number && bPreviousLeft )
                bidiClass = BidiClass::Left;
            listClass[index] = bidiClass;
        }

        // 2) 중립 구간(N1, N2): 앞뒤 강한 방향(숫자는 R, 단락 끝과 텍스트 끝은 문단 방향)이 같으면 그 방향, 다르면 문단 방향이다. 폭 없는 문자는 구간을 끊지 않는다.
        size_t index = 0;
        while ( index < count )
        {
            if ( listClass[index] != BidiClass::Neutral )
            {
                ++index;
                continue;
            }
            const size_t runStart = index;
            while ( index < count && ( listClass[index] == BidiClass::Neutral || listClass[index] == BidiClass::Boundary ) )
            {
                ++index;
            }
            bool bBeforeRight = bRightPara;
            for ( size_t before = runStart; before > 0; --before )
            {
                const BidiClass previous = listClass[before - 1];
                if ( previous == BidiClass::Separator )
                    break;
                if ( Internal::isStrongOrNumber( previous ) )
                {
                    bBeforeRight = Internal::isRightForNeutrals( previous );
                    break;
                }
            }
            const bool      bAfterRight = index < count && Internal::isStrongOrNumber( listClass[index] ) ? Internal::isRightForNeutrals( listClass[index] ) : bRightPara;
            const BidiClass resolved    = bBeforeRight == bAfterRight ? ( bBeforeRight ? BidiClass::Right : BidiClass::Left )
                                                                      : ( bRightPara ? BidiClass::Right : BidiClass::Left );
            for ( size_t neutral = runStart; neutral < index; ++neutral )
            {
                if ( listClass[neutral] == BidiClass::Neutral )
                    listClass[neutral] = resolved;
            }
        }

        // 3) 수준(I1, I2). 폭 없는 문자는 앞 글자의 수준이다(X9 로 지운 것과 같은 효과).
        for ( size_t position = 0; position < count; ++position )
        {
            const BidiClass bidiClass = listClass[position];
            if ( bidiClass == BidiClass::Boundary )
                outListLevel[position] = position > 0 && listClass[position - 1] != BidiClass::Separator ? outListLevel[position - 1] : paragraph;
            else
                outListLevel[position] = Internal::computeLevel( bidiClass, paragraph );
        }
    }

    void TextBidi::reorderVisually( const vector<uint8>& listLevel, vector<uint32>& outListIndex )
    {
        const uint32 count = static_cast<uint32>( listLevel.size() );
        outListIndex.resize( count );
        uint8 highest = 0;
        uint8 lowest  = count > 0 ? listLevel[0] : 0;
        for ( uint32 index = 0; index < count; ++index )
        {
            outListIndex[index] = index;
            highest             = listLevel[index] > highest ? listLevel[index] : highest;
            lowest              = listLevel[index] < lowest ? listLevel[index] : lowest;
        }
        // 가장 낮은 홀수 수준은 가장 낮은 수준을 홀수로 올린 것이다. 짝수(2)만 있는 줄은 뒤집지 않는다(두 번 뒤집혀 제자리가 된다).
        const uint8 lowestOdd = static_cast<uint8>( lowest | 1u );
        for ( uint32 level = highest; level >= lowestOdd; --level )
        {
            uint32 index = 0;
            while ( index < count )
            {
                // 눈에 보이는 위치 index 의 수준은 그 위치에 놓인 논리 번호의 수준이다.
                if ( listLevel[outListIndex[index]] < level )
                {
                    ++index;
                    continue;
                }
                uint32 runEnd = index;
                while ( runEnd < count && listLevel[outListIndex[runEnd]] >= level )
                {
                    ++runEnd;
                }
                for ( uint32 left = index, right = runEnd - 1; left < right; ++left, --right )
                {
                    const uint32 swapped = outListIndex[left];
                    outListIndex[left]   = outListIndex[right];
                    outListIndex[right]  = swapped;
                }
                index = runEnd;
            }
        }
    }

    uint32 TextBidi::getMirroredCodepoint( uint32 codepoint )
    {
        switch ( codepoint )
        {
            case '(':
                return ')';
            case ')':
                return '(';
            case '<':
                return '>';
            case '>':
                return '<';
            case '[':
                return ']';
            case ']':
                return '[';
            case '{':
                return '}';
            case '}':
                return '{';
            case 0x00ABu: // «
                return 0x00BBu;
            case 0x00BBu:
                return 0x00ABu;
            case 0x2039u: // ‹
                return 0x203Au;
            case 0x203Au:
                return 0x2039u;
            default:
                return codepoint;
        }
    }
} // namespace sw
