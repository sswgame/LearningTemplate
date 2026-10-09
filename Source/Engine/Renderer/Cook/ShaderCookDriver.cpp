/**
 * @file ShaderCookDriver.cpp
 * @brief 오프라인 쿠킹의 **정책**입니다. 요청 모두를 네 RHI 포맷으로 쿠킹하고 리플렉션 매니페스트를 씁니다.
 * @details 한 장을 쿠킹하는 법(`ShaderCooker::cookShader`)과 이름 짓기 · 최신 판정은 `Shader/Compile` 의 메커니즘이고,
 *          "무엇을 쿠킹하는가" 는 파이프라인 XML 과 패스 종류를 아는 렌더러의 지식입니다. 그래서 이 파일은
 *          `Renderer/Cook` 에 있고 `Shader/` 는 `Renderer/` 를 include 하지 않습니다.
 */
#include "pch.h"

#include "Engine/Renderer/Cook/ShaderCookDriver.h"

#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingValidator.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "ShaderCooker" );

    ShaderCookSummary ShaderCookDriver::cookAllShaders( string_view        resourceRoot,
                                                        ShaderTargetFormat targetFormat,
                                                        bool               bForceAll )
    {
        string rootDir = string( resourceRoot );
        if ( rootDir.empty() )
        {
            rootDir = ResourceUtil::getRootFolderPath();
            if ( rootDir.empty() )
                rootDir = "Resource";
        }

        if ( FileUtil::isDirectory( rootDir ) == false )
        {
            SW_LOG_ERROR( "Resource root directory does not exist: %#", rootDir.c_str() );
            ShaderCookSummary nothingCooked{};
            nothingCooked._failedCount = 1; // 쿠킹하지 못했다 — 실패로 센다(`App --cook-shaders` 가 실패로 끝난다)
            return nothingCooked;
        }

        SW_LOG_INFO( "Starting batch shader cook across all domains in '%#'...", rootDir.c_str() );

        // 1) 컴파일 대상 타깃 포맷 목록을 만든다
        vector<ShaderTargetFormat> listTargetFormat;
        if ( targetFormat == ShaderTargetFormat::Count )
        {
            listTargetFormat.push_back( ShaderTargetFormat::DXBC_D3D11 );
            listTargetFormat.push_back( ShaderTargetFormat::DXIL_D3D12 );
            listTargetFormat.push_back( ShaderTargetFormat::SPIRV_Vulkan );
            listTargetFormat.push_back( ShaderTargetFormat::SPIRV_OpenGL );
        }
        else
        {
            listTargetFormat.push_back( targetFormat );
        }

        // 2) 렌더 파이프라인 에셋과 엔진 데이터로 요청을 한꺼번에 모은다
        vector<ShaderCookRequest> listRequest;
        collectAllRequests( rootDir, listRequest );

        ShaderCookSummary summary{};
        // 컴파일에 실패한 소스(정규화한 절대 경로)를 RHI 폴더마다 적는다 — 그 소스는 도장에서 빼서 다음 쿠킹이 다시 시도한다.
        unordered_map<string, unordered_set<string>> mapFailedSourceByDir;

        // 리플렉션은 RHI 폴더마다 파일 하나로 모은다. 셰이더마다 사이드카를 두면 팩 엔트리와
        // 압축 해제가 셰이더 수만큼 늘어난다(상용 엔진의 셰이더 라이브러리와 같은 이유).
        unordered_map<string, ShaderReflectionLibrary::EntryMap> mapManifest;

        // 3) 요청 · 타깃 포맷마다 쿠킹한다
        for ( const ShaderCookRequest& request : listRequest )
        {
            string absPath;
            if ( FileUtil::exists( request._shaderPath ) )
                absPath = request._shaderPath;
            else
                absPath = ResourceUtil::getResourcePath( request._shaderPath );

            if ( FileUtil::exists( absPath ) == false )
                continue;

            const string normPath  = FileUtil::normalizeSeparators( absPath );
            const size_t shaderPos = normPath.find( "/shaders/" );
            if ( shaderPos == string::npos )
                continue;

            const string shaderDir = normPath.substr( 0, shaderPos + sizeof( "/shaders" ) - 1 );
            const string stemLower = ShaderCooker::getStemLower( normPath );

            for ( ShaderTargetFormat fmt : listTargetFormat )
            {
                const string_view subfolder = ShaderCooker::getSubfolderForFormat( fmt );
                const string_view ext       = ShaderCooker::getExtensionForFormat( fmt );
                const string      outDir    = FileUtil::joinPath( FileUtil::joinPath( shaderDir, "bin" ), subfolder );
                const string      fileName  = ShaderCooker::computeBinaryFileName( stemLower, request._stage, request._entryPoint, request._permutationHash, ext );
                const string      outPath   = FileUtil::joinPath( outDir, fileName );

                // **파일 시간이 아니라 내용 해시로 판정한다.** 이 저장소는 쿠킹된 바이너리까지 커밋하므로
                // `git pull` 이 소스와 산출물의 mtime 을 임의의 순서로 덮어쓴다. 소스가 바뀌었는데도
                // "산출물이 더 새것" 이 되어 그대로 넘어간다. 낡은 바이너리는 한 백엔드만 다른 그림을 내
                // 백엔드 버그처럼 보인다.
                const bool bUpToDate = ( bForceAll == false ) && FileUtil::exists( outPath ) &&
                                       ShaderCooker::isCookedOutputCurrent( outDir, normPath );

                if ( bUpToDate == false )
                {
                    ShaderCookResult result{};
                    if ( ShaderCooker::cookShader( absPath, outPath, request._entryPoint, request._stage, fmt, &request._listPermutation, &result ) )
                    {
                        ++summary._cookedCount;
                    }
                    else
                    {
                        ++summary._failedCount;
                        mapFailedSourceByDir[outDir].insert( normPath );
                        SW_LOG_ERROR( "Shader cook failed: %# (%#) -> %#", normPath, request._entryPoint, outPath );
                    }
                }

                // 새로 쿠킹했든 이미 최신이든 매니페스트에는 항상 넣는다. 바이너리만 최신이고
                // 리플렉션이 빠지면 런타임이 조용히 폴백하고, 배포 빌드에서는 그대로 실패한다.
                if ( mapManifest[outDir].find( fileName ) == mapManifest[outDir].end() )
                {
                    vector<uint8> bytecode;
                    if ( FileUtil::readFile( outPath, bytecode ) && bytecode.empty() == false )
                    {
                        ShaderReflectionData reflection = ShaderReflection::reflect( bytecode, fmt );
                        // 쿠킹된 바이너리를 계약과 대조한다. 셰이더 헤더와 백엔드 상수가 한쪽만 바뀌면 여기서 이름 · 숫자로 드러난다.
                        summary._contractViolationCount += ShaderBindingValidator::validate( reflection, fmt, outPath );
                        mapManifest[outDir].emplace( fileName, std::move( reflection ) );
                    }
                }
            }
        }

        // 4) RHI 폴더별 리플렉션 매니페스트를 쓴다
        for ( const auto& manifestPair : mapManifest )
        {
            if ( ShaderReflectionLibrary::save( manifestPair.second, manifestPair.first ) == false )
            {
                // 매니페스트를 못 쓰면 이 폴더는 최신이 아니다 — 도장을 찍지 않는다(다음 쿠킹이 다시 판정한다).
                SW_LOG_ERROR( "Shader reflection manifest could not be written: %#", manifestPair.first );
                ++summary._failedCount;
                continue;
            }
            const auto failedIt = mapFailedSourceByDir.find( manifestPair.first );
            ShaderCooker::writeCookStamp( manifestPair.first, ( failedIt != mapFailedSourceByDir.end() ) ? &failedIt->second : nullptr );
        }
        ShaderReflectionLibrary::clearCache();

        if ( summary._contractViolationCount > 0 )
            SW_LOG_ERROR( "바인딩 계약 위반 %#건 — 위 [바인딩 계약] 로그를 보고 셰이더 선언이나 bindingslots.hlsli 를 고치십시오.", summary._contractViolationCount );
        if ( summary._failedCount > 0 )
            SW_LOG_ERROR( "Shader cooking failed for %# shader(s) - their sources are left out of cook.stamp so the next cook retries them.", summary._failedCount );
        SW_LOG_INFO( "Shader cooking completed: %# binaries generated/updated.", summary._cookedCount );
        return summary;
    }
} // namespace sw
