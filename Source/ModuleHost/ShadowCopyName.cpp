#include "pch.h"

#include "ModuleHost/ShadowCopyName.h"

#include "Core/Container/StringUtil.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"

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

        const int32 currentProcessID = Process::getCurrentProcessID();
        uint32      removedCount{ 0 };
        for ( const string& filePath : listFile )
        {
            int32 ownerProcessID{ 0 };
            if ( parse( filePath, ownerProcessID ) == false )
                continue;
            // 다른 프로세스가 막 써 두고 아직 올리지 않은 복사본일 수 있다. 그 프로세스가 끝난 뒤의 정리가 지운다.
            const bool bOwnedByOtherLiveProcess = ownerProcessID != 0 && ownerProcessID != currentProcessID && Process::isProcessAlive( ownerProcessID );
            if ( bOwnedByOtherLiveProcess )
                continue;
            // 이 프로세스가 아직 올려 둔 사본은 지워지지 않는다(예상된 실패 — 알리지 않는다).
            if ( FileUtil::tryRemoveFile( filePath ) )
                ++removedCount;
        }
        return removedCount;
    }
} // namespace sw
