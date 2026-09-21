/**
 * @file ShaderBakeDriver.cpp
 * @brief 오프라인 베이크의 **정책** — 레시피 전부를 네 RHI 포맷으로 굽고 리플렉션 매니페스트를 쓴다.
 * @details 한 장을 굽는 법(`ShaderBaker::bakeShader`)과 이름 짓기·최신 판정은 `Shader/Compile` 의 메커니즘이고,
 *          "무엇을 굽는가" 는 파이프라인 XML 과 패스 종류를 아는 렌더러의 지식이다. 그래서 이 파일은
 *          `Renderer/Bake` 에 있고 `Shader/` 는 `Renderer/` 를 include 하지 않는다.
 */
#include "pch.h"

#include "Engine/Graphics/Renderer/Bake/ShaderBakeDriver.h"

#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingContract.h"
#include "Engine/Graphics/Shader/Compile/ShaderBaker.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "ShaderBaker" );

    uint32 ShaderBakeDriver::bakeAllShaders( string_view        resourceRoot,
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

        if ( FileUtil::directoryExists( rootDir ) == false )
        {
            SW_LOG_ERROR( "Resource root directory does not exist: %#", rootDir.c_str() );
            return 0;
        }

        SW_LOG_INFO( "Starting batch shader bake across all domains in '%#'...", rootDir.c_str() );

        // 1) 컴파일 대상 타깃 포맷 목록 구성
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

        // 2) 렌더 파이프라인 에셋 및 엔진 데이터 기반 레시피 일괄 수집
        vector<ShaderBakeRecipe> listRecipe;
        collectAllRecipes( rootDir, listRecipe );

        uint32 totalBaked = 0;

        // 리플렉션은 RHI 폴더마다 파일 하나로 모은다 — 셰이더마다 사이드카를 두면 팩 엔트리와
        // 압축 해제가 셰이더 수만큼 늘어난다(상용 엔진의 셰이더 라이브러리와 같은 이유).
        unordered_map<string, ShaderReflectionLibrary::EntryMap> mapManifest;
        uint32                                                   contractViolationCount{ 0 };

        // 3) 각 레시피 및 타깃 포맷별로 베이킹
        for ( const ShaderBakeRecipe& recipe : listRecipe )
        {
            string absPath;
            if ( FileUtil::fileExists( recipe._shaderPath ) )
                absPath = recipe._shaderPath;
            else
                absPath = ResourceUtil::getResourcePath( recipe._shaderPath );

            if ( FileUtil::fileExists( absPath ) == false )
                continue;

            const string normPath  = FileUtil::normalizeSeparators( absPath );
            const size_t shaderPos = normPath.find( "/shaders/" );
            if ( shaderPos == string::npos )
                continue;

            const string shaderDir = normPath.substr( 0, shaderPos + sizeof( "/shaders" ) - 1 );
            const string stemLower = ShaderBaker::getStemLower( normPath );

            for ( ShaderTargetFormat fmt : listTargetFormat )
            {
                const string_view subfolder = ShaderBaker::getSubfolderForFormat( fmt );
                const string_view ext       = ShaderBaker::getExtensionForFormat( fmt );
                const string      outDir    = FileUtil::joinPath( FileUtil::joinPath( shaderDir, "bin" ), subfolder );
                const string      fileName  = ShaderBaker::computeBinaryFileName( stemLower, recipe._stage, recipe._entryPoint, recipe._permHash, ext );
                const string      outPath   = FileUtil::joinPath( outDir, fileName );

                // **파일 시간이 아니라 내용 해시로 판정한다.** 이 저장소는 구운 바이너리까지 커밋하므로
                // `git pull` 이 소스와 산출물의 mtime 을 임의의 순서로 덮어쓴다 — 소스가 바뀌었는데도
                // "산출물이 더 새것" 이 되어 그대로 넘어간다. 실제로 `forwardlit` 이 라이트 버퍼 이전
                // 바이너리로 커밋됐고, Vulkan 만 다른 그림을 내는 것을 백엔드 버그로 오인했다.
                const bool bUpToDate = ( bForceAll == false ) && FileUtil::fileExists( outPath ) &&
                                       ShaderBaker::isBakedOutputCurrent( outDir, normPath );

                if ( bUpToDate == false )
                {
                    ShaderBakeResult result{};
                    if ( ShaderBaker::bakeShader( absPath, outPath, recipe._entryPoint, recipe._stage, fmt, &recipe._listPermutation, &result ) )
                        ++totalBaked;
                }

                // 새로 구웠든 이미 최신이든 매니페스트에는 항상 넣는다 — 바이너리만 최신이고
                // 리플렉션이 빠지면 런타임이 조용히 폴백하고, 배포 빌드에서는 그대로 실패한다.
                if ( mapManifest[outDir].find( fileName ) == mapManifest[outDir].end() )
                {
                    vector<uint8> bytecode;
                    if ( FileUtil::readFile( outPath, bytecode ) && bytecode.empty() == false )
                    {
                        ShaderReflectionData reflection = ShaderReflection::reflect( bytecode, fmt );
                        // 구운 바이너리를 계약과 대조한다 — 셰이더 헤더와 백엔드 상수가 한쪽만 바뀌면 여기서 이름·숫자로 드러난다.
                        contractViolationCount += ShaderBindingContract::validate( reflection, fmt, outPath );
                        mapManifest[outDir].emplace( fileName, std::move( reflection ) );
                    }
                }
            }
        }

        // 4) RHI 폴더별 리플렉션 매니페스트 기록
        for ( const auto& manifestPair : mapManifest )
        {
            ShaderReflectionLibrary::save( manifestPair.second, manifestPair.first );
            ShaderBaker::writeBakeStamp( manifestPair.first );
        }
        ShaderReflectionLibrary::clearCache();

        if ( contractViolationCount > 0 )
            SW_LOG_ERROR( "바인딩 계약 위반 %#건 — 위 [바인딩 계약] 로그를 보고 셰이더 선언이나 bindingslots.hlsli 를 고치십시오.", contractViolationCount );
        SW_LOG_INFO( "Shader baking completed: %# binaries generated/updated.", totalBaked );
        return totalBaked;
    }
} // namespace sw
