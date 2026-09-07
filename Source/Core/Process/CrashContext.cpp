#include "pch.h"

#include "Core/Process/CrashContext.h"

#include "Core/String/StringUtil.h"

#include <chrono>
#include <cstdio>
#include <random>

namespace sw
{
    namespace
    {
        /// @brief 덤프·리포트를 쓸 폴더. 부팅 때 한 번 정하고 크래시 경로에서는 읽기만 한다.
        fixed_string<constant::kMaxBuffer1024> s_reportFolder{};

        /**
         * @brief 세션 ID 를 한 번 만든다 — 시각과 난수를 섞은 16진 문자열.
         * @details GUID API 를 쓰지 않는 이유는 플랫폼마다 헤더가 갈리고, 여기서 필요한 것은
         *          "이 실행을 다른 실행과 구분" 뿐이라서다. 로그 파일 이름에도 같은 값이 들어간다.
         */
        const utf8* makeSessionId()
        {
            static utf8 s_arrSession[24]{};
            if ( s_arrSession[0] != '\0' )
                return s_arrSession;

            const uint64 nowTicks =
                static_cast<uint64>( std::chrono::steady_clock::now().time_since_epoch().count() );
            const uint64 wallSeconds =
                static_cast<uint64>( std::chrono::system_clock::now().time_since_epoch().count() );
            std::random_device randomDevice;
            const uint64       mixed = nowTicks ^ ( wallSeconds << 16 ) ^ ( static_cast<uint64>( randomDevice() ) << 32 );

            static const utf8 kHex[] = "0123456789abcdef";
            for ( uint32 digit = 0; digit < 16; ++digit )
                s_arrSession[digit] = kHex[( mixed >> ( digit * 4 ) ) & 0xFu];
            s_arrSession[16] = '\0';
            return s_arrSession;
        }
    } // namespace

    CrashContextStore& CrashContextStore::get()
    {
        static CrashContextStore s_store{};
        return s_store;
    }

    void CrashContextStore::set( string_view key, string_view value )
    {
        if ( key.empty() )
            return;

        for ( uint32 entryIndex = 0; entryIndex < _entryCount; ++entryIndex )
        {
            if ( _arrEntry[entryIndex]._key.view() == key )
            {
                _arrEntry[entryIndex]._value = value;
                return;
            }
        }
        // 자리가 없으면 조용히 버린다 — 크래시 진단을 돕자고 넣은 것이 실패를 키우면 안 된다.
        if ( _entryCount >= CrashHandler::kMaxContextEntry )
            return;

        _arrEntry[_entryCount]._key   = key;
        _arrEntry[_entryCount]._value = value;
        ++_entryCount;
    }

    const utf8* getCrashSessionId()
    {
        return makeSessionId();
    }

    const utf8* getCrashReportFolder()
    {
        return s_reportFolder.c_str();
    }

    void setCrashReportFolder( string_view folderPath )
    {
        s_reportFolder = folderPath;
    }

    void buildCrashReportPath( utf8* pOutPath, uint32 outSize, const utf8* pExtension )
    {
        if ( pOutPath == nullptr || outSize == 0 )
            return;
        const utf8* pFolder = getCrashReportFolder();
        const utf8* pPrefix = ( pFolder != nullptr && pFolder[0] != '\0' ) ? pFolder : ".";
        std::snprintf( pOutPath, outSize, "%s/crash_%s.%s", pPrefix, getCrashSessionId(),
                       ( pExtension != nullptr ) ? pExtension : "txt" );
    }

    void writeCrashContextFile( const utf8* pReason, const void* pFaultAddress, uint64 processId, uint64 threadId )
    {
        utf8 arrPath[constant::kMaxBuffer1024]{};
        buildCrashReportPath( arrPath, constant::kMaxBuffer1024, "txt" );

        std::FILE* pFile = std::fopen( arrPath, "wb" );
        if ( pFile == nullptr )
            return;

        std::fprintf( pFile, "session   : %s\n", getCrashSessionId() );
        std::fprintf( pFile, "reason    : %s\n", ( pReason != nullptr ) ? pReason : "unknown" );
        // uint64 는 %llu 와 폭이 같다(Types.h). 캐스팅 없이 그대로 넘긴다 — 기본 자료형 이름을 쓰지 않는다.
        std::fprintf( pFile, "address   : 0x%llx\n", static_cast<uint64>( reinterpret_cast<uintptr_t>( pFaultAddress ) ) );
        std::fprintf( pFile, "processId : %llu\n", processId );
        std::fprintf( pFile, "threadId  : %llu\n", threadId );

        const CrashContextStore& store = CrashContextStore::get();
        for ( uint32 entryIndex = 0; entryIndex < store._entryCount; ++entryIndex )
            std::fprintf( pFile, "%-10s: %s\n", store._arrEntry[entryIndex]._key.c_str(), store._arrEntry[entryIndex]._value.c_str() );

        std::fclose( pFile );
    }

    void writeCrashStackFile( const utf8* pStackText )
    {
        if ( pStackText == nullptr )
            return;
        utf8 arrPath[constant::kMaxBuffer1024]{};
        buildCrashReportPath( arrPath, constant::kMaxBuffer1024, "stack.txt" );

        std::FILE* pFile = std::fopen( arrPath, "wb" );
        if ( pFile == nullptr )
            return;
        std::fputs( pStackText, pFile );
        std::fclose( pFile );
    }

    void CrashHandler::setContextValue( string_view key, string_view value )
    {
        CrashContextStore::get().set( key, value );
    }

    const utf8* CrashHandler::getSessionId()
    {
        return getCrashSessionId();
    }

    void CrashHandler::setReportFolder( string_view folderPath )
    {
        setCrashReportFolder( folderPath );
    }
} // namespace sw
