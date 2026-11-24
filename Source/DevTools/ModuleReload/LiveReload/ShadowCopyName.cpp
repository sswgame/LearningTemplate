#include "pch.h"

#include "DevTools/ModuleReload/LiveReload/ShadowCopyName.h"

#include "Core/File/ProcessRoots.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"

namespace sw
{
    string ShadowCopyName::make( string_view moduleName, uint32 serial )
    {
        StringBuilder<constant::file::kMaxPathSize> nameBuilder;
        nameBuilder.append( moduleName ).append( '_' ).append( serial );
        return string{ nameBuilder.c_str() };
    }

    AbsolutePath ShadowCopyName::getDirectory()
    {
        StringBuilder<constant::buffer::kMax256> textBuilder;
        textBuilder.append( "ModuleShadow/" ).append( Process::getCurrentProcessID() );
        return ProcessRoots::get()._run.join( textBuilder.view() );
    }

    uint32 ShadowCopyName::removeCopies( const AbsolutePath& directoryPath )
    {
        vector<AbsolutePath> listFile;
        if ( FileUtil::collectFiles( directoryPath, "", listFile, false ) == false )
            return 0; // 아직 아무것도 쓰지 않았다
        vector<AbsolutePath> listFolder;
        (void)FileUtil::collectFolders( directoryPath, listFolder, false ); // 폴더가 없는 자리(리눅스)도 있다 — 파일 정리는 그대로 한다

        uint32 removedCount{ 0 };
        for ( const AbsolutePath& filePath : listFile )
        {
            // 아직 올려 둔 사본은 지워지지 않는다(예상된 실패 — 알리지 않는다).
            if ( FileUtil::tryRemoveFile( filePath ) )
                ++removedCount;
        }

        // Windows 복사본은 제 폴더(`<모듈>_<번호>/<세대 이름>.dll`)에 있다 — 안의 파일을 다 지웠을 때만 폴더를 지운다(아직 매핑된 사본은 남는다).
        for ( const AbsolutePath& folderPath : listFolder )
        {
            vector<AbsolutePath> listCopyFile;
            if ( FileUtil::collectFiles( folderPath, "", listCopyFile, false ) == false )
                continue;
            bool bEmptied{ true };
            for ( const AbsolutePath& copyFilePath : listCopyFile )
            {
                if ( FileUtil::tryRemoveFile( copyFilePath ) == false )
                    bEmptied = false;
            }
            if ( bEmptied && FileUtil::removeDirectory( folderPath ) )
                ++removedCount;
        }
        return removedCount;
    }
} // namespace sw
