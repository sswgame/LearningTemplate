// 에디터 글꼴 구성의 파일 찾기 · 글꼴 치수 읽기입니다(ImGui 없음). 아틀라스 구성은 `EditorFontSetup.cpp`(MVVM View)다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/GUI/Theme/EditorFontSetup.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"

#include "Engine/Text/FontSubsystem.h"

namespace sw::editor
{
    AbsolutePath EditorFontSetup::findFontFile( const vector<string>& listFileName )
    {
        // 에디터 팩의 글꼴 폴더가 먼저다(예전에는 같은 폴더를 두 길 - 도메인 폴더 · 리소스 루트 + 도메인 - 로 두 번 봤다).
        const AbsolutePath fontsFolder = EditorPaths::findContentFolder( FileUtil::joinPath( constant::path::kEditorPack, EditorPaths::kFontsFolderName ) );
        for ( const string& name : listFileName )
        {
            if ( name.empty() )
                continue;
            if ( fontsFolder.empty() == false )
            {
                const AbsolutePath candidate = fontsFolder.join( name );
                if ( FileUtil::isRegularFile( candidate ) )
                    return candidate;
            }
            // 결함: OS 폴더 바로 아래만 봐 리눅스(`truetype/<패밀리>/`)에서 글꼴을 못 찾고 에디터가 서지 못했다 - 엔진의 찾기를 그대로 쓴다.
            const AbsolutePath systemFont = getSubsystem<FontSubsystem>().findSystemFontFile( name.c_str() );
            if ( systemFont.empty() == false )
                return systemFont;
        }
        return {};
    }

    bool EditorFontSetup::readEmRatio( const AbsolutePath& path, float32& outRatio )
    {
        vector<uint8> bytes;
        if ( FileUtil::readFile( path, bytes ) == false )
            return false;
        IFontRasterizer& rasterizer = getSubsystem<FontSubsystem>().getRasterizer();
        const FontFaceID face       = rasterizer.loadFace( std::move( bytes ), 0, path.str() );
        FontFaceMetrics  metrics{};
        const bool       bRead = face != kInvalidFontFaceID && rasterizer.findFaceMetrics( face, metrics );
        if ( face != kInvalidFontFaceID )
            rasterizer.unloadFace( face );
        outRatio = metrics._ascender - metrics._descender;
        if ( bRead == false || outRatio <= 0.0f )
        {
            SW_LOG_ERROR_ID( "EditorFontSetup.FontNoUsableAscender", "Font %# has no usable ascender/descender metrics", path.c_str() );
            return false;
        }
        return true;
    }
} // namespace sw::editor
