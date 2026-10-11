#include "pch.h"

#include "ModuleHost/ShadowCopyName.h"

#include "Core/Container/StringUtil.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"

namespace sw
{
    namespace
    {
        struct ShadowCopyNameInternal
        {
            /** @brief @p path 가 복사본(파일 · 폴더)이고, 다른 살아 있는 프로세스의 것이 아니면 true 입니다. */
            static bool isRemovable( string_view path, int32 currentProcessID )
            {
                int32 ownerProcessID{ 0 };
                if ( ShadowCopyName::parse( path, ownerProcessID ) == false )
                    return false;
                // 다른 프로세스가 막 써 두고 아직 올리지 않은 복사본일 수 있다. 그 프로세스가 끝난 뒤의 정리가 지운다.
                const bool bOwnedByOtherLiveProcess = ownerProcessID != 0 && ownerProcessID != currentProcessID && Process::isProcessAlive( ownerProcessID );
                return bOwnedByOtherLiveProcess == false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string ShadowCopyName::make( string_view moduleName, int32 processID, uint32 serial, uint64 sourceMtime )
    {
        StringBuilder<constant::kMaxPathSize> nameBuilder;
        nameBuilder.append( moduleName ).append( kMarker ).append( kProcessPrefix ).append( processID ).append( '_' ).append( serial ).append( '_' ).append( sourceMtime );
        return string{ nameBuilder.c_str() };
    }

    bool ShadowCopyName::parse( string_view filePath, int32& outProcessID )
    {
        outProcessID = 0;
        string_view fileName;
        FileUtil::getFileNamePart( filePath, fileName );
        const string_view marker{ kMarker };
        const size_t      markerPos = fileName.rfind( marker );
        if ( markerPos == string_view::npos )
            return false;

        const string_view rest = fileName.substr( markerPos + marker.size() );
        if ( rest.empty() )
            return false;
        // 프로세스 ID 를 넣기 전 형식은 표식 바로 뒤가 번호다.
        if ( rest[0] != kProcessPrefix )
            return '0' <= rest[0] && rest[0] <= '9';

        const size_t idEnd = rest.find( '_' );
        if ( idEnd == string_view::npos || idEnd < 2 )
            return false;
        const string_view idToken = rest.substr( 1, idEnd - 1 );
        for ( const utf8 character : idToken )
        {
            if ( character < '0' || '9' < character )
                return false;
        }
        int32 processID{ 0 };
        if ( StringUtil::parseInt( idToken, processID ) == false || processID <= 0 )
            return false;
        outProcessID = processID;
        return true;
    }

    uint32 ShadowCopyName::removeStaleCopies( string_view directoryPath )
    {
        vector<string> listFile;
        if ( FileUtil::collectFiles( directoryPath, "", listFile, false ) == false )
            return 0;
        vector<string> listFolder;
        (void)FileUtil::collectFolders( directoryPath, listFolder, false ); // 폴더가 없는 자리(리눅스)도 있다 — 파일 정리는 그대로 한다

        const int32 currentProcessID = Process::getCurrentProcessID();
        uint32      removedCount{ 0 };
        for ( const string& filePath : listFile )
        {
            if ( ShadowCopyNameInternal::isRemovable( filePath, currentProcessID ) == false )
                continue;
            // 이 프로세스가 아직 올려 둔 사본은 지워지지 않는다(예상된 실패 — 알리지 않는다).
            if ( FileUtil::tryRemoveFile( filePath ) )
                ++removedCount;
        }

        // Windows 복사본은 제 폴더(`<모듈>_temp_p<pid>_…/<세대 이름>.dll`)에 있다 — 안의 파일을 다 지웠을 때만 폴더를 지운다(아직 매핑된 사본은 남는다).
        for ( const string& folderPath : listFolder )
        {
            if ( ShadowCopyNameInternal::isRemovable( folderPath, currentProcessID ) == false )
                continue;
            vector<string> listCopyFile;
            if ( FileUtil::collectFiles( folderPath, "", listCopyFile, false ) == false )
                continue;
            bool bEmptied{ true };
            for ( const string& copyFilePath : listCopyFile )
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
