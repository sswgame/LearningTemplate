#include "pch.h"

#include "Editor/Common/Localization/TranslationMemory.h"

#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Localization/CultureInfo.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    namespace
    {
        struct TranslationMemoryInternal
        {
            static void decodeCodepoints( string_view text, vector<uint32>& outListCodepoint )
            {
                outListCodepoint.clear();
                for ( size_t offset = 0; offset < text.size(); )
                {
                    outListCodepoint.push_back( StringUtil::decodeUTF8( text, offset ) );
                }
            }

            static bool isTrailingPunctuation( utf8 character )
            {
                return character == '.' || character == '!' || character == '?' || character == ':' || character == ';' || character == ',';
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    expected<void> TranslationMemory::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        XMLDocument document;
        XMLNode     root;
        if ( XMLReadUtil::parseRoot( document, FileUtil::skipUTF8Bom( xmlText ), sourceName, "TranslationMemory", root ) != XMLRootStatus::Found )
        {
            if ( document.getLastError().empty() == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "the document is not well-formed XML", document.getLastError() } );
            return unexpected( Error{ ErrorKind::InvalidArgument, "root must be <TranslationMemory>", sourceName } );
        }
        const XMLNode entries = root.findChild( "entries" );
        if ( XMLReadUtil::hasOnlyKnownNames( root, { "culture" }, { "entries" }, sourceName ) == false || entries.isValid() == false || root.findAttribute( "culture" ) == nullptr )
            return unexpected( Error{ ErrorKind::InvalidArgument, "needs <TranslationMemory culture=\"…\"><entries>…</entries></TranslationMemory> and nothing else", sourceName } );

        map<string, string> mapPair;
        size_t              index{ 0 };
        for ( XMLNode entry = entries.findChild(); entry.isValid(); entry = entry.findNextSibling(), ++index )
        {
            const bool bMalformedEntry = string_view{ entry.getName() } != "entry" || XMLReadUtil::hasOnlyKnownNames( entry, { "source", "text" }, {}, sourceName ) == false ||
                                         entry.findAttribute( "source" ) == nullptr || entry.findAttribute( "text" ) == nullptr;
            if ( bMalformedEntry )
            {
                StringBuilder<constant::buffer::kMax256> detail;
                detail.appendFormat( "%#: entries[%#]", sourceName, static_cast<uint64>( index ) );
                return unexpected( Error{ ErrorKind::InvalidArgument, "an entry needs exactly \"source\" and \"text\"", detail.view() } );
            }
            mapPair[string{ entry.getAttributeText( "source" ) }] = string{ entry.getAttributeText( "text" ) };
        }
        _culture         = CultureTable::normalizeCode( root.getAttributeText( "culture" ) );
        _mapSourceToText = std::move( mapPair );
        return {};
    }

    expected<void> TranslationMemory::loadFromFile( string_view absolutePath )
    {
        string text;
        if ( FileUtil::readTextFile( absolutePath, text ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "cannot read the file", absolutePath } );
        return loadFromXMLText( text, absolutePath );
    }

    string TranslationMemory::saveToXMLText() const
    {
        XMLDocument   document;
        const XMLNode root = document.appendRoot( "TranslationMemory" );
        root.appendAttribute( "culture", string_view{ _culture } );
        const XMLNode entries = root.appendChild( "entries" );
        for ( const auto& [source, text] : _mapSourceToText )
        {
            const XMLNode entry = entries.appendChild( "entry" );
            entry.appendAttribute( "source", string_view{ source } );
            entry.appendAttribute( "text", string_view{ text } );
        }
        return document.saveToString();
    }

    bool TranslationMemory::saveToFile( string_view absolutePath ) const
    {
        const string text = saveToXMLText();
        string       existing;
        const bool   bFileContentUnchanged = FileUtil::readOptionalTextFile( absolutePath, existing ) == OptionalFileRead::Read && existing == text;
        if ( bFileContentUnchanged )
            return true;
        return FileUtil::ensureParentDirectoryExists( absolutePath ) && FileUtil::writeTextFile( absolutePath, text );
    }

    bool TranslationMemory::addPair( string_view source, string_view text )
    {
        if ( source.empty() || text.empty() )
            return false;
        string&    stored   = _mapSourceToText[string( source )];
        const bool bChanged = stored != text;
        stored              = string( text );
        return bChanged;
    }

    bool TranslationMemory::findBestMatch( string_view source, TranslationMemoryMatch& outMatch, float32 minScore ) const
    {
        outMatch      = TranslationMemoryMatch{};
        const auto it = _mapSourceToText.find( string( source ) );
        if ( it != _mapSourceToText.end() )
        {
            outMatch._text          = it->second;
            outMatch._matchedSource = it->first;
            outMatch._score         = 1.0f;
            outMatch._bExact        = true;
            return true;
        }
        const string normalized = normalizeSource( source );
        float32      bestScore{ 0.0f };
        for ( const auto& [candidateSource, text] : _mapSourceToText )
        {
            // 길이가 크게 다르면 편집 거리를 계산하지 않는다. 유사도의 상한이 짧은 쪽 / 긴 쪽이다.
            const bool    bCandidateShorter = candidateSource.size() < source.size();
            const float32 shorter           = static_cast<float32>( bCandidateShorter ? candidateSource.size() : source.size() );
            const float32 longer            = static_cast<float32>( bCandidateShorter ? source.size() : candidateSource.size() );
            if ( longer > 0.0f && shorter / longer < minScore )
                continue;
            const string  candidateNormalized = normalizeSource( candidateSource );
            const float32 score               = candidateNormalized == normalized ? 0.99f : computeSimilarity( normalized, candidateNormalized );
            if ( score > bestScore )
            {
                bestScore               = score;
                outMatch._text          = text;
                outMatch._matchedSource = candidateSource;
                outMatch._score         = score;
            }
        }
        return bestScore >= minScore;
    }

    string TranslationMemory::findSourceOfText( string_view text ) const
    {
        for ( const auto& [source, candidateText] : _mapSourceToText )
        {
            if ( candidateText == text )
                return source;
        }
        return {};
    }

    string TranslationMemory::normalizeSource( string_view source )
    {
        string normalized;
        bool   bPendingSpace{ false };
        for ( const utf8 character : StringUtil::trim( source ) )
        {
            const bool bSpace = character == ' ' || character == '\t' || character == '\n' || character == '\r';
            if ( bSpace )
            {
                bPendingSpace = normalized.empty() == false;
                continue;
            }
            if ( bPendingSpace )
                normalized.push_back( ' ' );
            bPendingSpace = false;
            normalized.push_back( StringUtil::toLowerChar( character ) );
        }
        while ( normalized.empty() == false && TranslationMemoryInternal::isTrailingPunctuation( normalized.back() ) )
        {
            normalized.pop_back();
        }
        return normalized;
    }

    float32 TranslationMemory::computeSimilarity( string_view lhs, string_view rhs )
    {
        vector<uint32> listLeft;
        vector<uint32> listRight;
        TranslationMemoryInternal::decodeCodepoints( lhs, listLeft );
        TranslationMemoryInternal::decodeCodepoints( rhs, listRight );
        const size_t longer = listLeft.size() > listRight.size() ? listLeft.size() : listRight.size();
        if ( longer == 0 )
            return 1.0f;

        // 두 줄짜리 편집 거리 표(레벤슈타인).
        vector<uint32> listLastRow( listRight.size() + 1 );
        vector<uint32> listThisRow( listRight.size() + 1 );
        for ( size_t column = 0; column <= listRight.size(); ++column )
        {
            listLastRow[column] = static_cast<uint32>( column );
        }
        for ( size_t row = 1; row <= listLeft.size(); ++row )
        {
            listThisRow[0] = static_cast<uint32>( row );
            for ( size_t column = 1; column <= listRight.size(); ++column )
            {
                const uint32 substitution = listLastRow[column - 1] + ( listLeft[row - 1] == listRight[column - 1] ? 0u : 1u );
                const uint32 deletion     = listLastRow[column] + 1u;
                const uint32 insertion    = listThisRow[column - 1] + 1u;
                uint32       best         = substitution < deletion ? substitution : deletion;
                best                      = best < insertion ? best : insertion;
                listThisRow[column]       = best;
            }
            listLastRow.swap( listThisRow );
        }
        const float32 distance = static_cast<float32>( listLastRow[listRight.size()] );
        return 1.0f - distance / static_cast<float32>( longer );
    }

    void TranslationMemory::setCulture( string_view culture )
    {
        _culture = CultureTable::normalizeCode( culture );
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( TranslationMemory, TranslationMemory::kAssetType );
} // namespace sw
