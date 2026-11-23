#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/RHIBackendType.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/ContentHash.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingValidatorIssue.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionData.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "sw/config/CookContract.gen.h"

namespace sw
{
    namespace
    {
        struct ShaderCookerInternal
        {
            static constexpr uint32 kPermutationHashHexDigits = 16; ///< 바이너리 이름에 적는 퍼뮤테이션 해시 자릿수 — 64 비트를 다 적는다
            /** @brief 백엔드 하나의 셰이더 타깃과 바이너리 폴더입니다. 쿠킹 테이블(`SW_RHI_BACKEND_TABLE`)의 항목마다 하나입니다. */
            struct BackendFolder
            {
                string_view        _folder;
                ShaderTargetFormat _format;
            };

            static constexpr BackendFolder kArrBackendFolder[] = {
#define SW_SHADER_COOKER_BACKEND_ROW( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) { ShaderFolder, ShaderTargetFormat::ShaderTarget },
                SW_RHI_BACKEND_TABLE( SW_SHADER_COOKER_BACKEND_ROW )
#undef SW_SHADER_COOKER_BACKEND_ROW
            };
            static_assert( SW_COUNT_OF( kArrBackendFolder ) == static_cast<size_t>( ShaderTargetFormat::Count ),
                           "Config/Engine/CookContract.json needs one rhi_backends row per ShaderTargetFormat" );

            static string getStemLower( string_view filePath )
            {
                const string fileName = FileUtil::getFileNamePart( filePath );
                const string stem     = FileUtil::removeExtension( fileName );
                return StringUtil::toLower( stem.c_str() );
            }

            /** @brief 모든 형식이 표에 한 번씩 있는지 - 수만 맞고 한 형식이 두 번이면 다른 형식의 폴더를 찾지 못한다. */
            static constexpr bool isEveryFormatCovered()
            {
                for ( size_t formatIndex = 0; formatIndex < SW_COUNT_OF( kArrBackendFolder ); ++formatIndex )
                {
                    size_t matchCount = 0;
                    for ( const BackendFolder& row : kArrBackendFolder )
                    {
                        if ( static_cast<size_t>( row._format ) == formatIndex )
                            ++matchCount;
                    }
                    if ( matchCount != 1 )
                        return false;
                }
                return true;
            }
        };

    } // namespace
} // namespace sw

namespace sw
{
    string ShaderCooker::getStemLower( string_view filePath )
    {
        return ShaderCookerInternal::getStemLower( filePath );
    }

    uint64 ShaderCooker::computePermutationHash( const vector<string>& listPermutation )
    {
        const vector<string> listSorted = makeCanonicalDefines( listPermutation );
        if ( listSorted.empty() )
            return 0;

        uint64 hash{ HashUtil::kFNVOffset64 }; // FNV-1a 64비트 오프셋 기저값
        for ( const string& define : listSorted )
        {
            hash = HashUtil::computeStringHash64( define, false, hash );
        }
        return hash;
    }

    vector<string> ShaderCooker::makeCanonicalDefines( const vector<string>& listPermutation )
    {
        // `FOO` 와 `FOO=1` 은 컴파일러에게 같은 것이다. 런타임은 ShaderMacroDefine::parse 로 값 없는
        // define 에 "1" 을 채운 **뒤** 해시하므로, 여기서 원문 그대로 해시하면 같은 퍼뮤테이션이
        // 쿠킹과 런타임에서 서로 다른 해시가 된다. 그러면 쿠킹해 둔 변형을 아무도 못 찾는다(값을 적은
        // `SW_FORWARD=1` 은 맞고 값 없는 머티리얼 define 만 어긋나 눈에 덜 띈다). 두 오버로드가 같은 문자열을 보도록 여기서 맞춘다.
        vector<string> listSorted;
        listSorted.reserve( listPermutation.size() );
        for ( const string& define : listPermutation )
        {
            if ( define.empty() )
                continue;
            if ( define.find( '=' ) == string::npos )
            {
                StringBuilder<constant::buffer::kMax128> defineBuilder;
                defineBuilder.appendFormat( "%#=1", define );
                listSorted.push_back( string( defineBuilder.view() ) );
            }
            else
                listSorted.push_back( define );
        }
        VectorUtil::sort( listSorted );
        return listSorted;
    }

    uint64 ShaderCooker::computePermutationHash( const vector<ShaderMacroDefine>& listDefine )
    {
        return computePermutationHash( makeCanonicalDefines( listDefine ) );
    }

    vector<string> ShaderCooker::makeCanonicalDefines( const vector<ShaderMacroDefine>& listDefine )
    {
        vector<string> listCanonical;
        listCanonical.reserve( listDefine.size() );
        for ( const ShaderMacroDefine& define : listDefine )
        {
            if ( define._name.empty() )
                continue;
            // `FOO` 와 `FOO=1` 은 컴파일러에게 같은 것이다(문자열 판 `computePermutationHash` 와 같은 규칙).
            StringBuilder<constant::buffer::kMax128> defineBuilder;
            defineBuilder.appendFormat( "%#=%#", define._name, define._value.empty() ? string_view( "1" ) : string_view( define._value ) );
            listCanonical.push_back( string( defineBuilder.view() ) );
        }
        VectorUtil::sort( listCanonical );
        return listCanonical;
    }

    string ShaderCooker::computeBinaryFileName( string_view stemLower, ShaderStage stage,
                                                string_view entryPoint, uint64 permutationHash, string_view ext )
    {
        const string_view stageTag          = getShaderStageInfo( stage )._pTag;
        const string_view defaultEntryPoint = getShaderStageInfo( stage )._pEntryPoint;
        const bool        bStdEntry         = entryPoint.empty() || StringUtil::equals( entryPoint, defaultEntryPoint );

        StringBuilder<constant::buffer::kMax128> basePart;
        basePart.append( stemLower );
        basePart.append( '_' );
        if ( bStdEntry )
            basePart.append( stageTag );
        else
            basePart.append( StringUtil::toLower( entryPoint ) );

        // 해시 64 비트를 다 적는다(16 자리) — 32 비트로 자르면 같은 (폴더, 스템, 스테이지) 안의 변형끼리 이름이 겹칠 확률이 올라가고, 겹치면 틀린 셰이더가 조용히 실린다.
        if ( permutationHash != 0 )
            basePart.appendFormat( "_%#", Fmt( permutationHash, Format( ShaderCookerInternal::kPermutationHashHexDigits, Format::Padding::Zero ).hex() ) );

        basePart.append( ext );
        return string( basePart.view() );
    }

    vector<ShaderBinaryNameCollision> ShaderCooker::findBinaryNameCollisions( const vector<ShaderCookRequest>& listRequest )
    {
        // 키 = (소문자 셰이더 경로, 확장자 없는 바이너리 이름). 같은 키에 정본 define 이 다르면 한 파일을 두 변형이 덮는다.
        struct Claim
        {
            size_t         _requestIndex;
            vector<string> _listCanonical;
        };
        unordered_map<string, Claim>      mapClaim;
        vector<ShaderBinaryNameCollision> listCollision;
        for ( size_t index = 0; index < listRequest.size(); ++index )
        {
            const ShaderCookRequest&                 request  = listRequest[index];
            const string                             fileName = computeBinaryFileName( getStemLower( request._shaderPath ), request._stage, request._entryPoint, request._permutationHash, string_view{} );
            StringBuilder<constant::buffer::kMax256> keyBuilder;
            keyBuilder.append( StringUtil::toLower( FileUtil::normalizeSeparators( request._shaderPath ) ) ).append( '\n' ).append( fileName );
            string         key           = string( keyBuilder.view() );
            vector<string> listCanonical = makeCanonicalDefines( request._listPermutation );
            const auto     foundIt       = mapClaim.find( key );
            if ( foundIt == mapClaim.end() )
            {
                mapClaim.emplace( std::move( key ), Claim{ index, std::move( listCanonical ) } );
                continue;
            }
            if ( ( foundIt->second._listCanonical == listCanonical ) == false )
                listCollision.push_back( ShaderBinaryNameCollision{ foundIt->second._requestIndex, index, fileName } );
        }
        return listCollision;
    }

    string_view ShaderCooker::getSubfolderForFormat( ShaderTargetFormat format )
    {
        static_assert( ShaderCookerInternal::isEveryFormatCovered(), "Config/Engine/CookContract.json needs exactly one rhi_backends row per ShaderTargetFormat" );
        // 표는 모든 형식을 한 번씩 담는다(위 검사) — 못 찾는 값은 `Count` 이거나 범위 밖이라 기본 백엔드 폴더로 물러나지 않고 멈춘다.
        for ( const ShaderCookerInternal::BackendFolder& row : ShaderCookerInternal::kArrBackendFolder )
        {
            if ( row._format == format )
                return row._folder;
        }
        SW_FATAL( "ShaderCooker::getSubfolderForFormat: unknown shader target format %#", static_cast<uint32>( format ) );
    }

    string_view ShaderCooker::getExtensionForFormat( ShaderTargetFormat format )
    {
        const ShaderTargetFormatInfo* const pInfo = ShaderTargetFormatTable::find( format );
        if ( pInfo != nullptr )
            return pInfo->_pExtension;
        SW_FATAL( "ShaderCooker::getExtensionForFormat: unknown shader target format %#", static_cast<uint32>( format ) );
    }

    ShaderTargetFormat ShaderCooker::getFormatForSubfolder( string_view subfolder )
    {
        if ( subfolder.empty() )
            return ShaderTargetFormat::Count;
        for ( const ShaderCookerInternal::BackendFolder& row : ShaderCookerInternal::kArrBackendFolder )
        {
            if ( row._folder == subfolder )
                return row._format;
        }
        return ShaderTargetFormat::Count;
    }

    ShaderCompileDesc ShaderCooker::makeCompileDesc( const ShaderSourceID& source, string_view entryPoint, ShaderStage stage, ShaderTargetFormat targetFormat,
                                                     const ShaderEnginePolicy& enginePolicy, const vector<string>& listPermutation )
    {
        ShaderCompileDesc desc{};
        desc._source       = source;
        desc._entryPoint   = entryPoint;
        desc._stage        = stage;
        desc._targetFormat = targetFormat;
        desc._enginePolicy = enginePolicy;
        for ( const string& permutationDefine : listPermutation )
        {
            if ( permutationDefine.empty() == false )
                desc._listDefine.push_back( ShaderMacroDefine::parse( permutationDefine ) );
        }
        return desc;
    }

    bool ShaderCooker::cookShader( const ShaderCompileDesc& desc, string_view outputPath, ShaderCookResult& outResult )
    {
        outResult               = ShaderCookResult{};
        outResult._sourcePath   = desc._source.str();
        outResult._outputPath   = string( outputPath );
        outResult._entryPoint   = desc._entryPoint;
        outResult._stage        = desc._stage;
        outResult._targetFormat = desc._targetFormat;

        ShaderCompileResult compileResult = ShaderCompiler::compileHLSL( desc );
        if ( compileResult._bSuccess == false || compileResult._bytecode.empty() )
        {
            SW_LOG_WARNING_ID( "ShaderCooker.FailedCompileShader", "Failed to compile shader '%#' [%#] for %#: %#", desc._source.c_str(), desc._entryPoint, getSubfolderForFormat( desc._targetFormat ),
                               compileResult._errorMessage.c_str() );
            return false;
        }

        // 무변경 재쿠킹이 파일을 건드리지 않는다 — 내용 해시는 도장(크기 · 시각)이 그대로면 파일을 다시 읽지 않는다.
        const AbsolutePath outputFile = FileUtil::toDiskPath( outputPath );
        const bool         bCurrent   = FileUtil::isRegularFile( outputFile ) && ContentHash::computeFile( outputPath ) == compileResult._record._outputHash;
        if ( bCurrent == false )
        {
            if ( FileUtil::ensureParentDirectoryExists( outputFile ) == false || FileUtil::writeFile( outputFile, compileResult._bytecode.data(), compileResult._bytecode.size() ) == false )
            {
                SW_LOG_ERROR_ID( "ShaderCooker.FailedWriteCookedBytecode", "Failed to write cooked bytecode to %#", outputPath );
                return false;
            }
            SW_LOG_INFO_ID( "ShaderCooker.CookedShaderUBytes", "Cooked shader '%#' [%#] -> '%#' (%# bytes)", desc._source.c_str(), desc._entryPoint, outputPath,
                            static_cast<uint64>( compileResult._bytecode.size() ) );
            outResult._bWritten = SW_TRUE;
        }
        outResult._bytecode = std::move( compileResult._bytecode );
        outResult._bSuccess = SW_TRUE;
        return true;
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( ShaderBinary, ShaderCooker::kAssetType );
} // namespace sw
