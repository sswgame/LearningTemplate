#include "pch.h"

#include "Editor/Panels/ContentBrowser/Model/ContentBrowserFileDrop.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"

namespace sw::editor
{
    namespace
    {
        struct ContentBrowserFileDropInternal
        {
            /** @brief 경로에 `..` 성분이 있으면 true 입니다(`/` 로 맞춘 경로). */
            static bool hasParentReference( string_view path )
            {
                size_t start{ 0 };
                while ( start <= path.size() )
                {
                    size_t end = path.find( '/', start );
                    if ( end == string_view::npos )
                        end = path.size();
                    if ( path.substr( start, end - start ) == ".." )
                        return true;
                    start = end + 1;
                }
                return false;
            }

            static bool hasAcceptedExtension( string_view path, const vector<string>& listExtension )
            {
                return VectorUtil::containsIf( listExtension, [path]( const string& extension )
                { return FileUtil::hasExtension( path, extension ); } );
            }
        };
    } // namespace

    void ContentBrowserFileDropPlan::make( const vector<ContentBrowserDroppedFile>& listFile, const ContentBrowserFileDropRules& rules, ContentBrowserFileDropPlan& outPlan )
    {
        using Internal = ContentBrowserFileDropInternal;
        outPlan._listImport.clear();
        outPlan._listRejected.clear();
        for ( const ContentBrowserDroppedFile& file : listFile )
        {
            if ( VectorUtil::contains( outPlan._listImport, file._path ) )
                continue; // 같은 파일을 두 번 놓았다 — 한 번만 받는다
            ContentBrowserFileDropRejection reason{ ContentBrowserFileDropRejection::NotRegularFile };
            bool                            bRejected = true;
            if ( FileUtil::isAbsolutePath( file._path ) == false )
                reason = ContentBrowserFileDropRejection::NotAbsolute;
            else if ( Internal::hasParentReference( file._path ) )
                reason = ContentBrowserFileDropRejection::ParentReference;
            else if ( file._bRegularFile == false )
                reason = ContentBrowserFileDropRejection::NotRegularFile;
            else if ( Internal::hasAcceptedExtension( file._path, rules._listExtension ) == false )
                reason = ContentBrowserFileDropRejection::UnknownExtension;
            else if ( file._byteSize > rules._maxBytes )
                reason = ContentBrowserFileDropRejection::TooLarge;
            else if ( outPlan._listImport.size() >= rules._maxCount )
                reason = ContentBrowserFileDropRejection::TooMany;
            else
                bRejected = false;
            if ( bRejected )
                outPlan._listRejected.push_back( ContentBrowserRejectedFile{ file._path, reason } );
            else
                outPlan._listImport.push_back( file._path );
        }
    }

    void ContentBrowserFileDropPlan::collectDroppedFiles( const vector<string>& listPath, vector<ContentBrowserDroppedFile>& outListFile )
    {
        outListFile.clear();
        outListFile.reserve( listPath.size() );
        for ( const string& path : listPath )
        {
            ContentBrowserDroppedFile file{};
            file._path         = FileUtil::normalizeSeparators( path );
            file._bRegularFile = FileUtil::isAbsolutePath( file._path ) && FileUtil::isRegularFile( FileUtil::toDiskPath( file._path ) ) && FileUtil::tryGetFileSize( FileUtil::toDiskPath( file._path ), file._byteSize );
            outListFile.push_back( std::move( file ) );
        }
    }

    const utf8* ContentBrowserFileDropPlan::describeRejection( ContentBrowserFileDropRejection reason )
    {
        switch ( reason )
        {
            case ContentBrowserFileDropRejection::NotAbsolute:
                return "not an absolute path";
            case ContentBrowserFileDropRejection::ParentReference:
                return "the path has a '..' part";
            case ContentBrowserFileDropRejection::NotRegularFile:
                return "not an existing regular file";
            case ContentBrowserFileDropRejection::UnknownExtension:
                return "not an importable asset type";
            case ContentBrowserFileDropRejection::TooLarge:
                return "larger than the drop size limit";
            case ContentBrowserFileDropRejection::TooMany:
                return "over the per-drop file count limit";
        }
        return "rejected";
    }
} // namespace sw::editor
