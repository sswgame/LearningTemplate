#include "pch.h"

#include "Engine/Graphics/Shader/ShaderReflectionLibrary.h"

#include "Core/Concurrency/mutex.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Graphics/Shader/ShaderBaker.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
    namespace
    {
        SW_LOG_CALLER( "ShaderReflectionLibrary" );

        /// @brief 'SRFM' 매니페스트 매직.
        constexpr uint32 kManifestMagic = 0x4D465253;
        /// @brief 매니페스트 포맷 버전. 굽는 쪽과 읽는 쪽이 같은 파일에 있으므로 한 곳만 올리면 된다.
        constexpr uint32 kManifestVersion = 1;

        void writeReflectionInternal( Archive& archive, const ShaderReflectionData& reflection )
        {
            archive << static_cast<uint32>( reflection._listConstantBuffer.size() );
            for ( const ShaderBufferInfo& buffer : reflection._listConstantBuffer )
            {
                archive << string_view( buffer._name );
                archive << buffer._registerSpace;
                archive << buffer._bindPoint;
                archive << buffer._totalSize;
                archive << static_cast<uint32>( buffer._listVariable.size() );
                for ( const ShaderVariableInfo& variable : buffer._listVariable )
                {
                    archive << string_view( variable._name );
                    archive << string_view( variable._type );
                    archive << variable._offset;
                    archive << variable._size;
                }
            }

            archive << static_cast<uint32>( reflection._listResource.size() );
            for ( const ShaderResourceBinding& resource : reflection._listResource )
            {
                archive << string_view( resource._name );
                archive << string_view( resource._type );
                archive << resource._registerSpace;
                archive << resource._bindPoint;
                archive << resource._bindCount;
            }
        }

        bool readReflectionInternal( Archive& archive, ShaderReflectionData& outReflection )
        {
            uint32 bufferCount{ 0 };
            archive >> bufferCount;
            if ( archive.isError() )
                return false;

            outReflection._listConstantBuffer.resize( bufferCount );
            for ( ShaderBufferInfo& buffer : outReflection._listConstantBuffer )
            {
                archive >> buffer._name;
                archive >> buffer._registerSpace;
                archive >> buffer._bindPoint;
                archive >> buffer._totalSize;

                uint32 variableCount{ 0 };
                archive >> variableCount;
                if ( archive.isError() )
                    return false;

                buffer._listVariable.resize( variableCount );
                for ( ShaderVariableInfo& variable : buffer._listVariable )
                {
                    archive >> variable._name;
                    archive >> variable._type;
                    archive >> variable._offset;
                    archive >> variable._size;
                }
            }

            uint32 resourceCount{ 0 };
            archive >> resourceCount;
            if ( archive.isError() )
                return false;

            outReflection._listResource.resize( resourceCount );
            for ( ShaderResourceBinding& resource : outReflection._listResource )
            {
                archive >> resource._name;
                archive >> resource._type;
                archive >> resource._registerSpace;
                archive >> resource._bindPoint;
                archive >> resource._bindCount;
            }

            return archive.isError() == false;
        }

        /**
         * @brief 셰이더 소스 경로에서 `<...>/shaders/bin/<rhi>/` 리소스 상대 경로를 만듭니다.
         * @details ShaderCache 의 사전 컴파일 바이너리 경로와 같은 규칙이라 매니페스트가 바이트코드
         *          바로 옆에 놓인다.
         */
        string makeBinDirRelativeInternal( string_view shaderFilePath, string_view rhiFolder )
        {
            const string norm = FileUtil::normalizeSeparators( shaderFilePath );
            const size_t pos  = norm.find( "shaders/" );
            if ( pos != string::npos )
            {
                const string prefix = norm.substr( 0, pos + sizeof( "shaders/" ) - 1 );
                return prefix + "bin/" + string( rhiFolder ) + "/";
            }
            return string( "shaders/bin/" ) + string( rhiFolder ) + "/";
        }

        /// @brief 소스 경로에서 소문자 스템을 뽑습니다 (베이커의 파일명 규칙과 같아야 한다).
        string getStemLowerInternal( string_view filePath )
        {
            const string fileName = FileUtil::getFileNamePart( filePath );
            const string stem     = FileUtil::removeExtension( fileName );
            return StringUtil::toLower( stem.c_str() );
        }

        mutex& manifestMutexInternal()
        {
            static mutex s_mutex;
            return s_mutex;
        }

        /// @brief 매니페스트 경로 → 항목. 없는 파일도 빈 채로 캐시해 매번 다시 열지 않는다.
        unordered_map<string, ShaderReflectionLibrary::EntryMap>& manifestCacheInternal()
        {
            static unordered_map<string, ShaderReflectionLibrary::EntryMap> s_mapManifest;
            return s_mapManifest;
        }
    } // namespace

    const utf8* ShaderReflectionLibrary::getManifestFileName()
    {
        return "reflection.manifest";
    }

    bool ShaderReflectionLibrary::save( const EntryMap& mapEntry, string_view absDirectory )
    {
        if ( absDirectory.empty() )
            return false;

        Archive archive;
        archive << kManifestMagic;
        archive << kManifestVersion;
        archive << static_cast<uint32>( mapEntry.size() );
        for ( const auto& pair : mapEntry )
        {
            archive << string_view( pair.first );
            writeReflectionInternal( archive, pair.second );
        }

        vector<uint8> bytes;
        archive.writeData( bytes );
        if ( bytes.empty() )
            return false;

        FileUtil::ensureDirectoryExists( absDirectory );
        const string outPath = FileUtil::joinPath( absDirectory, getManifestFileName() );
        if ( FileUtil::writeFile( outPath, bytes.data(), bytes.size() ) == false )
        {
            SW_LOG_ERROR( "리플렉션 매니페스트 쓰기 실패: %#", outPath.c_str() );
            return false;
        }

        SW_LOG_INFO( "리플렉션 매니페스트 %# (%# 항목, %# bytes)", outPath.c_str(),
                     static_cast<uint32>( mapEntry.size() ), static_cast<uint32>( bytes.size() ) );
        return true;
    }

    bool ShaderReflectionLibrary::tryGet( const ShaderCompileDesc& desc, ShaderReflectionData& outReflection )
    {
        const string_view rhiFolder = ShaderBaker::getSubfolderForFormat( desc._targetFormat );
        const string_view ext       = ShaderBaker::getExtensionForFormat( desc._targetFormat );
        const string      binDirRel = makeBinDirRelativeInternal( desc._filePath, rhiFolder );

        // 키는 베이커가 만든 바이너리 파일 이름 그대로다 — 진입점과 퍼뮤테이션 해시가 반영된다.
        const uint64 permHash = ShaderBaker::computePermutationHash( desc._listDefine );
        const string key      = ShaderBaker::computeBinaryFileName( getStemLowerInternal( desc._filePath ), desc._stage,
                                                                    desc._entryPoint, permHash, ext );

#if !defined( SW_SHIPPING )
        // **소스보다 오래된 매니페스트는 쓰지 않는다** — ShaderCache 의 베이크 바이너리와 같은 규칙이다
        // (71cd9755 가 바이너리에만 넣었고 여기엔 빠져 있었다). 둘이 어긋나면 증상이 아주 멀리서 난다:
        // 바이너리는 새로 컴파일돼 b1(MaterialCB)을 참조하는데 레이아웃은 낡은 매니페스트라 그 슬롯이
        // 없고, 바인더가 b1 을 아예 안 걸어서 DX12 가 빈 루트 디스크립터 테이블을 읽고 GPU 페이지
        // 폴트(DEVICE_HUNG)로 죽는다. 셰이더 한 줄 고쳤을 뿐인데 죽는 곳은 드로우라 추적이 오래 걸린다.
        {
            const string sourceAbs   = ResourceUtil::getResourcePath( desc._filePath );
            const string manifestAbs = ResourceUtil::getResourcePath( binDirRel + getManifestFileName() );
            if ( sourceAbs.empty() == false && manifestAbs.empty() == false )
            {
                const uint64 sourceMtime   = FileUtil::getFileTimestamp( sourceAbs );
                const uint64 manifestMtime = FileUtil::getFileTimestamp( manifestAbs );
                if ( sourceMtime != 0 && manifestMtime != 0 && manifestMtime < sourceMtime )
                {
                    SW_LOG_TRACE( "리플렉션 매니페스트가 소스보다 오래됨 — 런타임 리플렉션으로 폴백: %#",
                                  string( desc._filePath ).c_str() );
                    return false;
                }
            }
        }
#endif

        std::scoped_lock<mutex> lock{ manifestMutexInternal() };
        auto&                   mapManifest = manifestCacheInternal();

        auto manifestIter = mapManifest.find( binDirRel );
        if ( manifestIter == mapManifest.end() )
        {
            EntryMap     loaded;
            const string manifestRel = binDirRel + getManifestFileName();
            Archive      archive( manifestRel, true );
            if ( archive.isError() == false )
            {
                uint32 magic{ 0 };
                uint32 version{ 0 };
                uint32 entryCount{ 0 };
                archive >> magic;
                archive >> version;
                archive >> entryCount;

                if ( magic != kManifestMagic || version != kManifestVersion )
                {
                    SW_LOG_WARNING( "리플렉션 매니페스트 형식이 맞지 않습니다 (magic=%# version=%#): %#",
                                    magic, version, manifestRel.c_str() );
                    entryCount = 0;
                }

                for ( uint32 entryIndex = 0; entryIndex < entryCount; ++entryIndex )
                {
                    string               entryKey;
                    ShaderReflectionData entryData;
                    archive >> entryKey;
                    if ( readReflectionInternal( archive, entryData ) == false )
                    {
                        SW_LOG_WARNING( "리플렉션 매니페스트가 손상되었습니다: %#", manifestRel.c_str() );
                        loaded.clear();
                        break;
                    }
                    loaded.emplace( std::move( entryKey ), std::move( entryData ) );
                }
            }
            manifestIter = mapManifest.emplace( binDirRel, std::move( loaded ) ).first;
        }

        const auto entryIter = manifestIter->second.find( key );
        if ( entryIter == manifestIter->second.end() )
            return false;

        outReflection = entryIter->second;
        return true;
    }

    void ShaderReflectionLibrary::clearCache()
    {
        std::scoped_lock<mutex> lock{ manifestMutexInternal() };
        manifestCacheInternal().clear();
    }
} // namespace sw
