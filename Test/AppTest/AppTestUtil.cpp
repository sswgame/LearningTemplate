#include "pch.h"

#include "AppTest/AppTestUtil.h"

#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"

#include "Engine/Resource/ResourceUtil.h"

#include "sw/config/ConfigConstants.h"

namespace test
{
    namespace
    {
        struct AppTestUtilInternal
        {
            /** @brief 플랫폼별 실행 파일 이름입니다. */
            static const utf8* getAppExecutableName()
            {
#if defined( SW_PLATFORM_WINDOWS )
                return "App.exe";
#else
                return "App";
#endif
            }
        };
    } // namespace
} // namespace test

namespace test
{
    sw::string AppTestUtil::findAppExecutablePath()
    {
        // 반환은 이 변수 하나로만 한다 — 갈래마다 다른 객체를 돌려주면 NRVO 가 막힌다(-Wnrvo).
        sw::string candidate = sw::FileUtil::joinPath( sw::FileUtil::getCurrentPath(), AppTestUtilInternal::getAppExecutableName() );
        if ( sw::FileUtil::exists( candidate ) )
            return candidate;

        const sw::string executableFolder = sw::FileUtil::getDirectoryPart( sw::FileUtil::getExecutablePath() );
        candidate                         = sw::FileUtil::joinPath( executableFolder, AppTestUtilInternal::getAppExecutableName() );
        if ( sw::FileUtil::exists( candidate ) == false )
            candidate.clear();
        return candidate;
    }

    bool AppTestUtil::launchApp( sw::Process& outProcess, sw::string_view arguments )
    {
        const sw::string executablePath = findAppExecutablePath();
        if ( executablePath.empty() )
            return false;

        sw::string command{ "\"" };
        command += executablePath;
        command += "\" ";
        command += arguments;
        return outProcess.launch( command );
    }

    sw::string AppTestUtil::readActivePackRoot()
    {
        sw::string packRoot;
        if ( sw::ResourceUtil::initialize() == false )
            return packRoot;
        const sw::string repositoryRoot = sw::FileUtil::getDirectoryPart( sw::ResourceUtil::getRootFolderPath() );
        sw::string       text;
        if ( sw::FileUtil::readTextFile( sw::FileUtil::joinPath( repositoryRoot, sw::config::kFileRuntimeGameConfig ), text ) == false )
            return packRoot;
        const size_t keyIndex = text.find( "\"_packRoot\"" );
        const size_t open     = keyIndex == sw::string::npos ? sw::string::npos : text.find( '"', text.find( ':', keyIndex ) );
        const size_t close    = open == sw::string::npos ? sw::string::npos : text.find( '"', open + 1 );
        if ( close != sw::string::npos )
            packRoot = text.substr( open + 1, close - open - 1 );
        return packRoot;
    }
} // namespace test
