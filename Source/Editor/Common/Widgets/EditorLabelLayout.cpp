#include "pch.h"

#include "Editor/Common/Widgets/EditorLabelLayout.h"

#include "Core/String/StringUtil.h"

namespace sw::editor
{
    namespace
    {
        struct EditorLabelLayoutInternal
        {
            /** @brief 이 글자 뒤에서 줄을 끊어도 되는가(단어 경계)입니다. */
            static bool isBreakAfter( utf8 character ) { return character == ' ' || character == '_' || character == '-' || character == '.'; }

            /** @brief 줄 머리의 공백을 건너뜁니다. */
            static string_view skipLeadingSpace( string_view text )
            {
                size_t offset = 0;
                while ( offset < text.size() && text[offset] == ' ' )
                    ++offset;
                return text.substr( offset );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorLabelLayoutUtil::breakLines( string_view text, float32 width, uint32 maxLineCount, MeasureFunc pfnMeasure, void* pUserData,
                                            vector<string_view>& outListLine )
    {
        outListLine.clear();
        if ( text.empty() || maxLineCount == 0 || pfnMeasure == nullptr )
        {
            outListLine.push_back( text );
            return;
        }

        string_view remaining = text;
        while ( outListLine.size() + 1 < maxLineCount && pfnMeasure( remaining, pUserData ) > width )
        {
            // 폭에 드는 가장 긴 앞부분(글자 경계)을 찾는다. 첫 글자는 넘쳐도 싣는다 — 빈 줄이 끝없이 생기지 않게.
            size_t fitEnd = 0;
            while ( fitEnd < remaining.size() )
            {
                size_t next = fitEnd;
                (void)StringUtil::decodeUtf8( remaining, next );
                const bool bOverflows = pfnMeasure( remaining.substr( 0, next ), pUserData ) > width;
                if ( fitEnd != 0 && bOverflows )
                    break;
                fitEnd = next;
            }

            // 그 안에서 마지막 단어 경계 뒤를 고른다. 없으면 글자 단위다.
            size_t cut = fitEnd;
            for ( size_t index = fitEnd; index > 0; --index )
            {
                if ( EditorLabelLayoutInternal::isBreakAfter( remaining[index - 1] ) )
                {
                    cut = index;
                    break;
                }
            }
            outListLine.push_back( remaining.substr( 0, cut ) );
            remaining = EditorLabelLayoutInternal::skipLeadingSpace( remaining.substr( cut ) );
            if ( remaining.empty() )
                return;
        }
        outListLine.push_back( remaining );
    }
} // namespace sw::editor
