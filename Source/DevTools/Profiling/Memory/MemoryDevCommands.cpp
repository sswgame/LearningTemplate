/**
 * @file MemoryDevCommands.cpp
 * @brief 메모리 스냅숏 개발 명령(`memory.snapshot` · `memory.export` · `memory.exportinfo`)입니다(언리얼 `memreport` 자리). 실행 인자 `-memory-export` 는 `MemoryExportSubsystem` 이다.
 * @details 배포 호스트는 DevTools 를 링크하지 않아 명령도 인자 처리도 없다. 설정은 `Config/Dev/MemorySnapshot.json`(`MemorySnapshotPolicy`) — 없으면 명령이 이유를 답하고 아무것도 쓰지 않는다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "DevTools/Profiling/Memory/MemoryExportSubsystem.h"
#include "DevTools/Profiling/Memory/MemorySnapshotExport.h"
#include "DevTools/Profiling/Memory/MemorySnapshotSources.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    namespace
    {
        struct MemoryDevCommandsInternal
        {
            /** @brief 정책을 읽어 이 프로세스의 스냅숏을 찍습니다. 정책이 없으면 이유를 @p outReply 에 담고 false 입니다. */
            [[nodiscard]] static bool captureLocal( MemorySnapshotPolicy& outPolicy, MemorySnapshot& outSnapshot, string& outReply )
            {
                expected<MemorySnapshotPolicy> policy = MemorySnapshotPolicy::load();
                if ( policy.has_value() == false )
                {
                    outReply = "memory: ";
                    outReply += policy.error().message();
                    outReply += " - ";
                    outReply += policy.error().detail();
                    return false;
                }
                outPolicy   = *policy;
                outSnapshot = MemorySnapshotSources::makeEngine( outPolicy._allocationSiteLimit ).collect( MemorySnapshot::makeTimeLabel(), "local" );
                return true;
            }

            /** @brief `memory.snapshot`: 이 프로세스의 스냅숏을 전송 글로 답합니다 — 에디터의 원격 메모리 창이 디버그 채널로 부른다. */
            static bool runSnapshot( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                MemorySnapshotPolicy policy;
                MemorySnapshot       snapshot;
                if ( captureLocal( policy, snapshot, outReply ) == false )
                    return true; // 답이 이유다
                outReply = snapshot.writeText();
                return true;
            }

            /** @brief `memory.export [폴더]`: 스냅숏을 찍어 `<폴더>/<시각>/`(폴더가 없으면 `Saved/<_exportFolder>`)에 시트별 CSV 로 씁니다(워커). */
            static bool runExport( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                MemorySnapshotPolicy policy;
                MemorySnapshot       snapshot;
                if ( captureLocal( policy, snapshot, outReply ) == false )
                    return true;
                string root = MemorySnapshotExport::getDefaultRoot( policy );
                if ( listArgument.size() == 1 && DevCommandRegistry::readDiskPathArgument( listArgument[0], root, outReply ) == false )
                    return true; // 답이 이유다(절대 경로만 받는다)
                const string folder = MemorySnapshotExport::reserveFolder( root );
                if ( folder.empty() )
                {
                    StringBuilder<constant::buffer::kMax1024> reply;
                    reply.appendFormat( "memory.export: could not create a folder under '%#'", root );
                    outReply = string( reply.view() );
                    return true;
                }
                MemoryExportSubsystem* const pExport = engine::findSubsystem<MemoryExportSubsystem>(); // 개발 도구 클라이언트가 기동 범위에 세운다 — 엔진 서비스 칸이 없는 타입이다
                if ( pExport == nullptr )
                {
                    outReply = "memory.export: no memory export subsystem in this host";
                    return true;
                }
                pExport->writeAsync( make_shared<const MemorySnapshot>( std::move( snapshot ) ), folder );
                StringBuilder<constant::buffer::kMax1024> reply;
                reply.appendFormat( "memory.export -> %#", folder );
                outReply = string( reply.view() );
                return true;
            }

            /**
             * @brief `memory.exportinfo [시트]`: 가장 최근 내보내기 폴더의 `<시트>.csv`(없으면 Tags)를 읽어 BOM · 머리말 · 행 수를 답합니다 — 시나리오가 파일을 확인하는 자리.
             * @details 행 수는 머리말을 뺀 줄 수다(따옴표 안 줄바꿈은 세지 않는다 — 칸의 줄바꿈은 콜스택 글에만 있고 수집이 한 줄로 접는다).
             */
            static bool runExportInfo( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                const string                   sheetName = listArgument.empty() ? string{ getMemorySheetSchema( MemorySheetKind::Tags )._pName } : listArgument[0];
                expected<MemorySnapshotPolicy> policy    = MemorySnapshotPolicy::load();
                if ( policy.has_value() == false )
                {
                    outReply = "memory.exportinfo: no policy";
                    return true;
                }
                const string                            folder = MemorySnapshotExport::findLatestFolder( MemorySnapshotExport::getDefaultRoot( *policy ) );
                fixed_string<constant::buffer::kMax128> fileName;
                formatstring( fileName.data(), fileName.capacity(), "%#.csv", sheetName );
                const string  path = FileUtil::joinPath( folder, fileName.view() );
                vector<uint8> listByte;
                if ( folder.empty() || FileUtil::readFile( path, listByte ) == false )
                {
                    StringBuilder<constant::buffer::kMax1024> reply;
                    reply.appendFormat( "memory.exportinfo: no file '%#'", path );
                    outReply = string( reply.view() );
                    return true;
                }
                const string_view                         text{ reinterpret_cast<const utf8*>( listByte.data() ), listByte.size() };
                const bool                                bBOM      = StringUtil::startsWith( text, CSVTable::kByteOrderMark );
                const string_view                         body      = bBOM ? text.substr( CSVTable::kByteOrderMark.size() ) : text;
                const size_t                              headerEnd = body.find( "\r\n" );
                const uint32                              lineCount = StringUtil::countChar( body, '\n' );
                StringBuilder<constant::buffer::kMax1024> reply;
                reply.appendFormat( "memory.exportinfo: %#%# header=%# rows=%#", sheetName, bBOM ? " bom=yes" : " bom=no", body.substr( 0, headerEnd == string_view::npos ? body.size() : headerEnd ),
                                    lineCount > 0 ? lineCount - 1 : 0u );
                outReply = string( reply.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( MemorySnapshotText, "memory.snapshot", "memory.snapshot", "Reply with a memory snapshot as text (tags, sites, assets, GPU resources, types, caches)",
                    &MemoryDevCommandsInternal::runSnapshot );
    SW_DEV_COMMAND( MemoryExport, "memory.export", "memory.export [folder]", "Write a memory snapshot as CSV sheets under <folder or Saved/MemorySnapshots>/<time>",
                    &MemoryDevCommandsInternal::runExport );
    SW_DEV_COMMAND( MemoryExportInfo, "memory.exportinfo", "memory.exportinfo [sheet]", "Read the latest memory export's <sheet>.csv and reply with its BOM, header and row count",
                    &MemoryDevCommandsInternal::runExportInfo );
} // namespace sw
