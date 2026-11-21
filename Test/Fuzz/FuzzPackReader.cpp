// PackReader 퍼저 — 받은 패치 · DLC · 모드 팩의 머리 · 색인 · 스트링 풀을 읽는 `PackReader::open` 과 항목 읽기에 임의 바이트를 넣는다.
// `open` 이 파일 경로만 받으므로 입력을 프로세스마다 하나인 임시 파일(`Saved/Fuzz/PackReader.<pid>.pack`)에 쓴다. 거절은 정상이고, 크래시 · 새니타이저 보고만 결함이다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Process/Process.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/Pack/PackReader.h"

namespace
{
    /** @brief 이 프로세스의 입력 파일 경로입니다 — 여러 퍼저 작업(`-jobs`)이 같은 파일을 쓰지 않게 pid 를 넣는다. */
    const sw::string& getInputPath()
    {
        static const sw::string path = sw::FileUtil::joinPath( sw::ProcessRoots::get()._saved.str(),
                                                               "Fuzz/PackReader." + sw::to_string( sw::Process::getCurrentProcessID() ) + ".pack" );
        return path;
    }
} // namespace

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* pData, size_t size )
{
    const sw::string& path = getInputPath();
    if ( sw::FileUtil::ensureParentDirectoryExists( sw::FileUtil::toDiskPath( path ) ) == false || sw::FileUtil::writeFile( sw::FileUtil::toDiskPath( path ), pData, size ) == false )
        std::abort(); // 입력을 못 쓰면 퍼징이 아무것도 보지 않는다 — 조용히 넘어가지 않는다
    sw::PackReader reader;
    if ( reader.open( path ) == false )
        return 0;
    // 색인이 받아들여졌으면 항목마다 읽어 압축 해제 · CRC 갈래까지 지난다.
    sw::vector<sw::string> listPath;
    reader.collectAllEntryPaths( listPath );
    for ( const sw::string& entryPath : listPath )
    {
        sw::vector<uint8> bytes;
        (void)reader.readFile( entryPath, bytes );
    }
    return 0;
}
