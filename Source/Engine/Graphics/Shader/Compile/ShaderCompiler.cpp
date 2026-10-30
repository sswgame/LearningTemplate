#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Module/ModuleLibraryUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/fixed_string.h"

#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EnginePlatformHeaders.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Reflection/SPIRVConstants.h"

#include <algorithm>

#if defined( SW_HAS_DXC_API )
    #include <dxcapi.h>
#endif
#if defined( SW_PLATFORM_WINDOWS ) && !defined( SW_HAS_DXC_API )
    // SW_HAS_DXC_API 는 EnginePlatformHeaders.h 가 정한다 — 빠지면 DXC 경로가 조용히 꺼지고 테스트는 "컴파일러 없음" 으로 건너뛴다.
    #error "SW_HAS_DXC_API is not defined on Windows - include Engine/Foundation/EnginePlatformHeaders.h"
#endif

namespace sw
{
    // 안쪽 도우미(캐시 쓰기 · 컴파일 단계)도 로그를 남기므로 맨 앞에 둔다.
    SW_LOG_CALLER( "ShaderCompiler" );

    namespace
    {
        struct ShaderCompilerInternal
        {
            /** @brief 셰이더 #include 검색 디렉터리(engine/shaders, common/shaders, 소스 폴더)를 모읍니다. */
            [[nodiscard]] static bool collectShaderIncludeDirs( const string& shaderAbsPath, vector<string>& outListDir )
            {
                outListDir.clear();
                const string shaderDir = FileUtil::getDirectoryPart( shaderAbsPath );
                if ( shaderDir.empty() == false )
                    outListDir.push_back( FileUtil::normalizeSeparators( shaderDir ) );

                // 쿠킹 스탬프도 같은 도메인의 헤더를 본다(`ShaderCooker::collectIncludeRootDomains`). 도메인 종류는 우선순위 설정에서 온다.
                vector<string> listDomain;
                if ( ShaderCooker::collectIncludeRootDomains( engine::getSubsystem<AssetPaths>().getSearchPriority(), listDomain ) == false )
                    return false;
                for ( const string& domain : listDomain )
                {
                    const string domainShaders = engine::getSubsystem<AssetPaths>().findLooseDomainFolder( domain, "shaders" ).getDiskPath();
                    if ( domainShaders.empty() == false )
                        outListDir.push_back( domainShaders );
                }
                return true;
            }

            /** @brief 컴파일러가 연 파일 목록을 정렬하고 중복을 없앱니다(`ShaderCompileResult::_listIncludedFile` 의 규칙). */
            [[nodiscard]] static vector<string> sortUniqueIncludedFile( vector<string> listFile )
            {
                VectorUtil::sortUnique( listFile );
                return listFile;
            }

#if defined( SW_PLATFORM_WINDOWS )
            /**
             * @brief 여러 루트를 도는 ID3DInclude 입니다(common/shaders → engine/shaders/binding.hlsli 등). 연 파일을 적어 둡니다.
             * @details 기록은 이 객체 안이다 — 컴파일 호출마다 하나를 만들어 쓰므로 병렬 컴파일에서도 서로 섞이지 않는다(전역 · static 기록 금지).
             */
            class MultiRootD3DInclude final : public ID3DInclude
            {
            public:
                explicit MultiRootD3DInclude( vector<string> listRoot )
                    : _listRoot{ std::move( listRoot ) }
                    , _listIncludedFile{}
                {
                }

                /** @brief 지금까지 연 파일을 정렬 · 중복 제거해 돌려주고 기록을 비웁니다. */
                [[nodiscard]] vector<string> takeIncludedFiles() { return sortUniqueIncludedFile( std::move( _listIncludedFile ) ); }

                STDMETHOD( Open )
                ( D3D_INCLUDE_TYPE includeType, LPCSTR pFileName, LPCVOID pParentData,
                  LPCVOID* ppData, UINT* pBytes ) override
                {
                    (void)includeType;
                    (void)pParentData;
                    const bool bInvalidIncludeArguments = pFileName == nullptr || ppData == nullptr || pBytes == nullptr;
                    if ( bInvalidIncludeArguments )
                        return E_FAIL;

                    for ( const string& root : _listRoot )
                    {
                        const string candidate = FileUtil::normalizeSeparators( FileUtil::joinPath( root, pFileName ) );
                        if ( FileUtil::isRegularFile( candidate ) == false )
                            continue;

                        vector<uint8> bytes;
                        if ( FileUtil::readFile( candidate, bytes ) == false || bytes.empty() )
                            continue;

                        uint8* pHeap = static_cast<uint8*>( Memory::allocate( bytes.size() ) );
                        if ( pHeap == nullptr )
                            return E_OUTOFMEMORY;
                        Memory::copy( pHeap, bytes.data(), bytes.size() );
                        *ppData = pHeap;
                        *pBytes = static_cast<UINT>( bytes.size() );
                        _listIncludedFile.push_back( candidate ); // 읽은 파일만 적는다(찾지 못한 후보나 못 읽은 것은 컴파일 결과에 닿지 않았다)
                        return S_OK;
                    }
                    return E_FAIL;
                }

                STDMETHOD( Close )
                ( LPCVOID pData ) override
                {
                    if ( pData != nullptr )
                        Memory::free( const_cast<void*>( pData ) );
                    return S_OK;
                }

            private:
                vector<string> _listRoot;
                vector<string> _listIncludedFile;
            };
#endif

            /**
             * @brief OpenGL 용 SPIR-V 의 Vulkan 전용 내장 변수를 GL 것으로 바꿉니다(InstanceIndex→InstanceId, VertexIndex→VertexId).
             * @details DXC 는 SV_InstanceID/SV_VertexID 를 Vulkan 의 InstanceIndex/VertexIndex 로 냅니다. ARB_gl_spirv 는 그 둘을
             *          지원하지 않아 바꾸지 않으면 드라이버가 조용히 0 을 반환합니다(인스턴스드 드로우가 모두 원소 0 을 읽어 한
             *          자리에 겹칩니다). 지금 셰이더는 인스턴스 자리를 정점 스트림(`SW_INSTANCESLOT`)으로 받아 SV_InstanceID 를 쓰지
             *          않습니다 — 다시 쓰게 되면 gl_InstanceID 는 startInstance 를 빼고 센다는 점(Vulkan InstanceIndex 와 다르다)에
             *          주의하십시오. OpDecorate(71) / OpMemberDecorate(72) 의 BuiltIn(11) 만 손댑니다.
             */
            static void patchSPIRVBuiltinsForOpenGL( vector<uint8>& ioBytecode )
            {
                if ( ioBytecode.size() < 20 || ( ioBytecode.size() % 4 ) != 0 )
                    return;
                uint32*      pWord     = reinterpret_cast<uint32*>( ioBytecode.data() );
                const size_t wordCount = ioBytecode.size() / 4;
                if ( pWord[0] != spirv::kMagic )
                    return;
                size_t offset = 5;
                while ( offset < wordCount )
                {
                    const uint32 opcode = pWord[offset] & 0xFFFFu;
                    const uint32 length = pWord[offset] >> 16;
                    if ( length == 0 || offset + length > wordCount )
                        break;
                    uint32*    pValue             = nullptr;
                    const bool bMemberBuiltIn     = opcode == spirv::kOpMemberDecorate && length >= 5 && pWord[offset + 3] == spirv::kDecorationBuiltIn;
                    const bool bBuiltInDecoration = opcode == spirv::kOpDecorate && length >= 4 && pWord[offset + 2] == spirv::kDecorationBuiltIn;
                    if ( bBuiltInDecoration )
                        pValue = &pWord[offset + 3];
                    else if ( bMemberBuiltIn )
                        pValue = &pWord[offset + 4];
                    if ( pValue != nullptr )
                    {
                        if ( *pValue == spirv::kBuiltInInstanceIndex )
                            *pValue = spirv::kBuiltInInstanceID;
                        else if ( *pValue == spirv::kBuiltInVertexIndex )
                            *pValue = spirv::kBuiltInVertexID;
                    }
                    offset += length;
                }
            }

            /**
             * @brief 셰이더 스테이지와 타깃 포맷에 맞는 프로파일 문자열을 반환합니다.
             * @details DX11 은 SM5.0, DX12 · Vulkan · OpenGL 은 네이티브 bindless 와 Descriptor Indexing 을 위해 SM6.6 입니다. 스테이지마다
             *          다른 것은 `ShaderStageInfo` 표의 줄이고, SM5 에 없는 스테이지(메시 · 앰플리피케이션)는 vs_5_0 으로 폴백합니다.
             */
            static const utf8* getTargetProfile( ShaderStage stage, ShaderTargetFormat targetFormat )
            {
                const ShaderStageInfo& info = getShaderStageInfo( stage );
                if ( targetFormat == ShaderTargetFormat::DXBC_D3D11 )
                    return info._pProfileSm5 != nullptr ? info._pProfileSm5 : getShaderStageInfo( ShaderStage::Vertex )._pProfileSm5;
                return info._pProfileSm6;
            }

#if defined( SW_HAS_DXC_API )
    #if defined( SW_PLATFORM_WINDOWS )
            template <typename T>
            using DxcComPtr = Microsoft::WRL::ComPtr<T>;

            template <typename T>
            static T** dxcAddressOf( DxcComPtr<T>& ptr )
            {
                return ptr.GetAddressOf();
            }

            template <typename T>
            static T* dxcGet( const DxcComPtr<T>& ptr )
            {
                return ptr.Get();
            }
    #else
            template <typename T>
            using DxcComPtr = CComPtr<T>;

            template <typename T>
            static T** dxcAddressOf( DxcComPtr<T>& ptr )
            {
                return &ptr;
            }

            template <typename T>
            static T* dxcGet( const DxcComPtr<T>& ptr )
            {
                return ptr;
            }
    #endif
#endif

            /**
             * @brief 컴파일 결과 캐시 폴더입니다 — 실행 파일 폴더의 `Saved/ShaderCompileCache`(이 기계의 파생 데이터, 언리얼 DerivedDataCache).
             * @details 콘텐츠 트리(`Content/`)에 두지 않는다 — 올리는 단위가 아니고, 자동화 실행은 콘텐츠 아래 쓰기를 막아 캐시를 채우지 못한 채 매번 다시 컴파일했다.
             *          재정의 캐시(`ShaderCache::makeLocalCachePath`)와 같은 바탕이다.
             */
            static string getShaderCacheDirectory()
            {
                return FileUtil::normalizeSeparators( FileUtil::joinPath( ProcessRoots::get()._saved.str(), "ShaderCompileCache" ) );
            }

            static string computeCachePath( const ShaderCompileDesc& desc, const string& absPathStr )
            {
                const string cacheDir = getShaderCacheDirectory();
                if ( cacheDir.empty() )
                    return "";

                // 못 읽으면 내용 없이 해시한다 — 컴파일도 같은 파일을 읽다 실패하므로 그 캐시 자리는 쓰이지 않는다.
                vector<uint8> sourceBytes;
                (void)FileUtil::readFile( absPathStr, sourceBytes ); // 못 읽으면 내용 없이 해시한다(위 설명)

                uint64 hash = HashUtil::computeStringHash64( desc._filePath, false );
                hash        = HashUtil::computeStringHash64( desc._entryPoint, false, hash );
                hash        = HashUtil::computeStringHash64( to_string( static_cast<uint32>( desc._stage ) ), false, hash );
                hash        = HashUtil::computeStringHash64( to_string( static_cast<uint32>( desc._targetFormat ) ), false, hash );
                for ( const auto& def : desc._listDefine )
                {
                    hash = HashUtil::computeStringHash64( def._name, false, hash );
                    hash = HashUtil::computeStringHash64( def._value, false, hash );
                }
                if ( sourceBytes.empty() == false )
                {
                    const string_view sourceStr( reinterpret_cast<const utf8*>( sourceBytes.data() ), sourceBytes.size() );
                    hash = HashUtil::computeStringHash64( sourceStr, false, hash );
                }
                // 주 소스 바이트만 해시하면 **include 된 .hlsli 는 키에 없다**. common.hlsli 의 상수버퍼 바인딩을 고쳐도
                // 이 캐시가 옛 바이트를 그대로 돌려줬고, ShaderCache 는 그것을 "새로 컴파일한 것" 으로 알고 라이브 캐시에
                // 다시 써서 GL 이 set 10 짜리 옛 SPIR-V 를 계속 썼다. 헤더는 키에 넣지 않는다 — 어느 헤더를 열었는지는 컴파일하고 나서야 알아서
                // (키는 컴파일 전에 필요하다), 컴파일할 때 연 헤더와 그 내용 해시를 캐시 파일 옆의 의존 기록에 적고 맞출 때 그 기록이 지금 헤더와 같은지 본다
                // (`ShaderCooker::readIncludeRecord`). 전역 헤더 해시를 키에 섞으면 헤더 하나에 모든 셰이더의 캐시가 무효가 된다.
                // 도구 지문은 키에 섞는다 — DXC 나 옵션을 바꾸면 옛 바이트를 돌려주면 안 된다.
                hash = HashUtil::computeStringHash64( to_string( ShaderCooker::getToolchainFingerprint() ), false, hash );
                // 디버그 코드젠 여부도 키다. 같은 소스라도 바이트코드가 다르다.
                // string_view 로 넘긴다. 리터럴을 그대로 주면 (pStr, length, bIgnoreCase) 오버로드에 묶여 length=0 · seed 무시로
                // 키가 상수가 된다(실제로 그래서 모든 셰이더가 한 파일을 공유했다).
                hash = HashUtil::computeStringHash64( string_view{ desc._bDebugCodegen != SW_FALSE ? "dbg" : "opt" }, false, hash );

                fixed_string<constant::kMaxBuffer64> buf;
                formatstring( buf.data(), buf.capacity(), "%#.bin", Fmt( static_cast<uint64>( hash ), Format( 16, Format::Padding::Zero ).hex() ) );
                return FileUtil::joinPath( cacheDir, buf.c_str() );
            }

            /**
             * @brief 디스크 캐시가 켜져 있고 자리가 있으면 바이트코드와 의존 기록(연 헤더와 내용 해시)을 씁니다. 못 쓰면 경고만 남깁니다(다음 컴파일이 다시 만든다).
             * @details 옛 의존 기록을 먼저 지운다 — 바이트코드를 쓰다 멈추면 새 바이트코드 옆에 옛 기록이 남고, 헤더가 옛 내용으로 돌아간 날 그 기록이 맞아 버린다.
             */
            static void saveToCacheIfEnabled( const string& cachePath, const ShaderCompileResult& result )
            {
                const bool bDiskCacheWritable = cachePath.empty() == false && result._bytecode.empty() == false;
                if ( bDiskCacheWritable == false )
                    return;
                const string recordPath = cachePath + ShaderCooker::kIncludeRecordSuffix;
                if ( FileUtil::removeFile( recordPath ) == false ) // 없으면 성공이다 - 묻고 지우기를 따로 하지 않는다
                {
                    SW_LOG_WARNING( "Could not replace the shader cache record '%#' - the cache entry is skipped", recordPath );
                    return;
                }
                if ( FileUtil::ensureParentDirectoryExists( cachePath ) == false || FileUtil::writeFile( cachePath, result._bytecode.data(), result._bytecode.size() ) == false )
                {
                    SW_LOG_WARNING( "Could not write the shader cache '%#'", cachePath );
                    return;
                }
                if ( ShaderCooker::writeIncludeRecord( recordPath, result._listIncludedFile ) == false )
                    SW_LOG_WARNING( "Could not write the shader cache record '%#'", recordPath );
            }

#if defined( SW_PLATFORM_WINDOWS )
            /** @brief FXC 로 DXBC(D3D11, 행 우선)를 컴파일합니다. 실패하면 오류 글을 담고 false 입니다. */
            [[nodiscard]] static bool compileDxbc( const ShaderCompileDesc& desc, const string& absPathStr, const string& profile, ShaderCompileResult& outResult )
            {
                UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
                if ( desc._bDebugCodegen != SW_FALSE )
                    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

                Microsoft::WRL::ComPtr<ID3DBlob> codeBlob;
                Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
                wstring                          wPath = StringUtil::utf8ToUTF16( absPathStr.c_str() );

                vector<D3D_SHADER_MACRO> listMacro;
                listMacro.reserve( desc._listDefine.size() + 3 );
                listMacro.push_back( { "DX11", "1" } );
                if ( desc._stage == ShaderStage::Compute )
                    listMacro.push_back( { "SW_STAGE_COMPUTE", "1" } );
                for ( const ShaderMacroDefine& def : desc._listDefine )
                {
                    if ( def._name.empty() )
                        continue;
                    listMacro.push_back( { def._name.c_str(), def._value.c_str() } );
                }
                listMacro.push_back( { nullptr, nullptr } );

                vector<string> listIncludeDir;
                if ( collectShaderIncludeDirs( absPathStr, listIncludeDir ) == false )
                {
                    outResult._errorMessage = "Shader include domains cannot be found (see the _listResourcePriority error above).";
                    return false;
                }
                MultiRootD3DInclude includeHandler( std::move( listIncludeDir ) );

                HRESULT hr = D3DCompileFromFile(
                    wPath.c_str(),
                    listMacro.data(),
                    &includeHandler,
                    desc._entryPoint.c_str(),
                    profile.c_str(),
                    compileFlags,
                    0,
                    codeBlob.GetAddressOf(),
                    errorBlob.GetAddressOf() );

                if ( FAILED( hr ) )
                {
                    if ( errorBlob != nullptr )
                        outResult._errorMessage = static_cast<const utf8*>( errorBlob->GetBufferPointer() );
                    else
                        outResult._errorMessage = "Unknown DXBC compilation error.";
                    SW_LOG_ERROR( "%#", outResult._errorMessage.c_str() );
                    return false;
                }

                const uint8* pData = static_cast<const uint8*>( codeBlob->GetBufferPointer() );
                outResult._bytecode.assign( pData, pData + codeBlob->GetBufferSize() );
                outResult._listIncludedFile = includeHandler.takeIncludedFiles();
                outResult._bSuccess         = true;
                SW_LOG_TRACE( "Target: D3D11 (DXBC Row-Major), Size: %# bytes", static_cast<uint32>( outResult._bytecode.size() ) );
                return true;
            }
#endif // SW_PLATFORM_WINDOWS

#if defined( SW_HAS_DXC_API )
            /** @brief DXC 컴파일 한 번의 결과입니다. */
            enum class DxcOutcome : uint8
            {
                Compiled, ///< 바이트코드를 얻었다
                Rejected, ///< 셰이더 오류로 실패했다(오류 글을 담았다) — 다른 컴파일러로 넘기지 않는다
                NoOutput, ///< DXC 를 못 쓰거나 출력이 없었다
            };

            /**
             * @brief include 파일을 `FileUtil` 로 읽어 DXC 에 넘기고 `LoadSource` 가 연 파일을 적는 `IDxcIncludeHandler` 입니다.
             * @details DXC 기본 처리기(`CreateDefaultIncludeHandler`)는 자기 파일 API 로 디스크를 읽는다 - 엔진 파일 입구(경로 · 진단 · 디스크 흉내)를 건너뛰므로 쓰지 않는다.
             * @details 컴파일 호출마다 하나를 만든다. 기록은 이 객체 안이고 전역 · static 이 아니라서 컴파일을 병렬로 돌려도 섞이지 않는다.
             *          전이 include 도 DXC 가 이 처리기를 거쳐 읽으므로 모두 잡힌다. 참조 수로 사는 COM 객체다 — `make_unique(...).release()` 로 만들어
             *          `Attach` 로 소유권을 `DxcComPtr` 에 넘기고, 마지막 `Release()` 가 푼다. 기록은 소유자가 살아 있는 동안 읽는다.
             */
            class RecordingDXCInclude final : public IDxcIncludeHandler
            {
            public:
                explicit RecordingDXCInclude( DxcComPtr<IDxcUtils> pUtils )
                    : _pUtils{ std::move( pUtils ) }
                    , _listIncludedFile{}
                    , _refCount{ 1 }
                {
                }

                RecordingDXCInclude( const RecordingDXCInclude& )            = delete;
                RecordingDXCInclude& operator=( const RecordingDXCInclude& ) = delete;

                /** @brief 지금까지 연 파일을 정렬 · 중복 제거해 돌려주고 기록을 비웁니다. */
                [[nodiscard]] vector<string> takeIncludedFiles() { return sortUniqueIncludedFile( std::move( _listIncludedFile ) ); }

                HRESULT STDMETHODCALLTYPE LoadSource( LPCWSTR pFilename, IDxcBlob** ppIncludeSource ) override
                {
                    const bool bInvalidArguments = pFilename == nullptr || ppIncludeSource == nullptr || _pUtils == nullptr;
                    if ( bInvalidArguments )
                        return E_FAIL;
                    *ppIncludeSource = nullptr;
                    // DXC 는 include 폴더마다 후보 이름을 물어본다 - 없는 후보는 오류 로그 없이 실패로 돌려줘 다음 폴더로 넘어가게 한다.
                    const string path = FileUtil::normalizeSeparators( StringUtil::utf16ToUTF8( pFilename ) );
                    if ( FileUtil::isRegularFile( path ) == false )
                        return E_FAIL;
                    vector<uint8> bytes;
                    if ( FileUtil::readFile( path, bytes ) == false )
                        return E_FAIL;
                    DxcComPtr<IDxcBlobEncoding> blob;
                    const HRESULT               hr = _pUtils->CreateBlob( bytes.data(), static_cast<UINT32>( bytes.size() ), DXC_CP_UTF8, dxcAddressOf( blob ) );
                    if ( FAILED( hr ) )
                        return hr;
                    *ppIncludeSource = blob.Detach();
                    _listIncludedFile.push_back( path ); // DXC 가 찾아 읽은 이름이다(후보를 못 찾은 호출은 적지 않는다)
                    return S_OK;
                }

                ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>( ++_refCount ); }

                ULONG STDMETHODCALLTYPE Release() override
                {
                    const uint32 remaining = _refCount.fetch_sub( 1 ) - 1;
                    if ( remaining == 0 )
                        sw_delete( this );
                    return static_cast<ULONG>( remaining );
                }

                HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void** ppOutObject ) override
                {
                    if ( ppOutObject == nullptr )
                        return E_INVALIDARG;
                    *ppOutObject          = nullptr;
                    const bool bSupported = IsEqualGUID( riid, __uuidof( IUnknown ) ) || IsEqualGUID( riid, __uuidof( IDxcIncludeHandler ) );
                    if ( bSupported == false )
                        return E_NOINTERFACE;
                    *ppOutObject = static_cast<IDxcIncludeHandler*>( this );
                    AddRef();
                    return S_OK;
                }

            private:
                DxcComPtr<IDxcUtils> _pUtils;
                vector<string>       _listIncludedFile;
                atomic<uint32>       _refCount;
            };

            /** @brief DXC 라이브러리의 `DxcCreateInstance` 주소를 찾습니다(`ShaderCompiler::findDXCCreateInstance` 가 한 번 부른다). 빌드가 DXC 를 실행 파일 옆에 둔다(배포 구성의 테스트는 TestBin) - 다른 곳을 찾아다니지 않는다. */
            static void* loadDXCCreateInstance()
            {
                const string                      libraryText = FileUtil::joinPath( FileUtil::getDirectoryPart( FileUtil::getExecutablePath() ), ModuleLibraryUtil::formatSharedLibraryName( "dxcompiler" ) );
                const std::optional<AbsolutePath> libraryPath = AbsolutePath::tryMake( libraryText );
                if ( libraryPath.has_value() == false )
                {
                    SW_LOG_ERROR( "DXC path is not absolute: %#", libraryText.c_str() );
                    return nullptr;
                }
                void* const pLibrary = ModuleLibraryUtil::loadLibraryAt( *libraryPath );
                if ( pLibrary == nullptr )
                    return nullptr;
                void* const pCreateInstance = ModuleLibraryUtil::getDynamicSymbol( pLibrary, "DxcCreateInstance" );
                if ( pCreateInstance == nullptr )
                    SW_LOG_WARNING( "%# loaded but DxcCreateInstance not found", libraryText.c_str() );
                return pCreateInstance;
            }

            /** @brief 타깃(SPIR-V 레이아웃 · 레지스터 시프트, 타깃 매크로)과 스테이지에 따른 DXC 인자를 더합니다. 넣는 글은 모두 정적 수명입니다. */
            static void appendDXCTargetArguments( const ShaderCompileDesc& desc, vector<LPCWSTR>& listArgument )
            {
                if ( desc._targetFormat == ShaderTargetFormat::SPIRV_Vulkan )
                {
                    // 타깃 버전은 디바이스가 요구하는 최소 버전과 같은 값이다(constant::kVulkanRequiredMajor · kVulkanRequiredMinor). 이 타깃에서 DXC 는 HLSL `discard` 를
                    // OpDemoteToHelperInvocation 으로 낸다 — 1.3 디바이스의 필수 기능이다.
                    static const wstring s_targetEnv = StringUtil::utf8ToUTF16(
                        ( string( "-fspv-target-env=vulkan" ) + to_string( constant::kVulkanRequiredMajor ) + "." +
                          to_string( constant::kVulkanRequiredMinor ) )
                            .c_str() );
                    listArgument.push_back( L"-spirv" );
                    listArgument.push_back( s_targetEnv.c_str() );
                    listArgument.push_back( L"-fvk-use-dx-position-w" );
                    // cbuffer · StructuredBuffer 를 DX 와 같은 규칙으로 패킹한다. 머티리얼 데이터 원소(g_SwMaterials)를
                    // 엔진이 한 레이아웃으로 채우므로 백엔드마다 stride · 오프셋이 달라지면 안 된다(std430 은 float3 을 16 정렬).
                    listArgument.push_back( L"-fvk-use-dx-layout" );
                    // 세트 0 의 binding = 레지스터 종류별 시프트 + 번호(bindingslots.hlsli 6). 셰이더는 register(b#/t#/u#) 만
                    // 쓰고 [[vk::binding]] 을 적지 않는다. 파이프라인 레이아웃(VulkanRHIDeviceDescriptor.cpp)이 같은 값으로 만든다.
                    static const wstring s_bShift = StringUtil::utf8ToUTF16( to_string( shaderslot::vk::kBShift ).c_str() );
                    static const wstring s_tShift = StringUtil::utf8ToUTF16( to_string( shaderslot::vk::kTShift ).c_str() );
                    static const wstring s_uShift = StringUtil::utf8ToUTF16( to_string( shaderslot::vk::kUShift ).c_str() );
                    listArgument.push_back( L"-fvk-b-shift" );
                    listArgument.push_back( s_bShift.c_str() );
                    listArgument.push_back( L"0" );
                    listArgument.push_back( L"-fvk-t-shift" );
                    listArgument.push_back( s_tShift.c_str() );
                    listArgument.push_back( L"0" );
                    listArgument.push_back( L"-fvk-u-shift" );
                    listArgument.push_back( s_uShift.c_str() );
                    listArgument.push_back( L"0" );
                }
                else if ( desc._targetFormat == ShaderTargetFormat::SPIRV_OpenGL )
                {
                    listArgument.push_back( L"-spirv" );
                    listArgument.push_back( L"-fspv-target-env=vulkan1.1" );
                    listArgument.push_back( L"-fvk-use-dx-position-w" );
                    listArgument.push_back( L"-fvk-use-dx-layout" ); // Vulkan 과 같은 이유. 네 백엔드 원소 레이아웃 일치
                    listArgument.push_back( L"-fvk-b-shift" );
                    listArgument.push_back( L"16" );
                    listArgument.push_back( L"0" );
                    // GL 의 u# → SSBO binding 은 계약(SW_GL_UAV_BINDING0)이 정한다. common.hlsli 의 OPENGL 용
                    // RW 버퍼 매크로가 명시 binding 을 두지 않아 이 시프트가 적용된다. b/t 는 명시 binding 이라 시프트와 무관하다.
                    static const wstring s_uavShift = StringUtil::utf8ToUTF16( to_string( shaderslot::gl::kUAVBinding0 ).c_str() );
                    listArgument.push_back( L"-fvk-u-shift" );
                    listArgument.push_back( s_uavShift.c_str() );
                    listArgument.push_back( L"0" );
                }

                listArgument.push_back( L"-D" );
                if ( desc._targetFormat == ShaderTargetFormat::DXIL_D3D12 )
                {
                    listArgument.push_back( L"DX12=1" );
                    // 네이티브 bindless 텍스처(common.hlsli SW_NATIVE_BINDLESS): 텍스처만 무제한 배열(t0 space1)이고, 버퍼는
                    // space0 슬롯이다(CB 는 루트 CBV, t · u 는 디스크립터 테이블). SM6.6 힙 직접 인덱싱(ResourceDescriptorHeap)은 쓰지 않는다.
                    listArgument.push_back( L"-D" );
                    listArgument.push_back( L"SW_BINDLESS=1" );
                }
                else if ( desc._targetFormat == ShaderTargetFormat::SPIRV_Vulkan )
                {
                    listArgument.push_back( L"VULKAN=1" );
                    // 네이티브 bindless 텍스처는 세트 1 배열이다. 버퍼는 세트 0 의 슬롯(시프트된 binding)이다.
                    listArgument.push_back( L"-D" );
                    listArgument.push_back( L"SW_BINDLESS=1" );
                }
                else if ( desc._targetFormat == ShaderTargetFormat::SPIRV_OpenGL )
                    listArgument.push_back( L"OPENGL=1" );
                else if ( desc._targetFormat == ShaderTargetFormat::DXBC_D3D11 )
                    listArgument.push_back( L"DX11=1" );
                if ( desc._stage == ShaderStage::Compute )
                {
                    // 컴퓨트에서만 선언되는 자원(RW 텍스처 배열 · 슬롯, binding.hlsli "텍스처 읽기" 절)의 스위치.
                    listArgument.push_back( L"-D" );
                    listArgument.push_back( L"SW_STAGE_COMPUTE=1" );
                }
            }

            /**
             * @brief DXC 로 DXIL · SPIR-V 를 컴파일합니다. 성공하면 바이트코드(OpenGL 이면 내장 변수를 바꾼 것)를 담습니다.
             * @details 생성 · 소스 읽기 · 인자 · 컴파일 · 결과 읽기의 순서가 곧 의미이고 인자 글의 수명이 이 함수 안이라 더 나누지 않는다.
             */
            static DxcOutcome compileDXC( const ShaderCompileDesc& desc, const string& absPathStr, const string& profile, ShaderCompileResult& outResult )
            {
                DxcCreateInstanceProc fnDXCCreateInstance = reinterpret_cast<DxcCreateInstanceProc>( ShaderCompiler::findDXCCreateInstance() );
                if ( fnDXCCreateInstance == nullptr )
                {
                    SW_LOG_ERROR( "DxcCreateInstance unavailable (libdxcompiler not loaded)" );
                    return DxcOutcome::NoOutput;
                }

    #if defined( SW_SANITIZER_THREAD )
                // libdxcompiler 는 계측되지 않은 서드파티라 TSan 이 그 안의 원자 연산 · 지연 초기화 동기화(ManagedStatic · 락 객체 게시)를 보지 못한다.
                // 병렬 쿠킹의 컴파일이 겹치면 DXC 안에서 할당한 블록을 다른 스레드가 해제하는 것으로 보여 경쟁 9 건이 난다(2026-10-25 CI TSan).
                // 억제 목록은 libc 프레임만 남은 보고를 못 맞추고 엔진 콜백의 진짜 경쟁까지 가리므로, 이 구성에서만 DXC 호출을 직렬로 돌려 선후를 TSan 이 보게 한다.
                // 다른 구성은 그대로 병렬이다. 이 구성에서는 DXC 안쪽의 경쟁을 검사하지 않는다.
                // libdxcompiler 를 부르는 다른 자리 `ShaderReflectionDx`(DXIL 리플렉션)는 Windows 전용 파일이고 TSan 구성은 리눅스 전용(Sanitizer.cmake)이라 그 구성에 없다.
                // 리눅스에서 DXIL 리플렉션을 켜면 이 직렬화를 두 호출처가 함께 쓰는 한 함수로 올린다(뮤텍스를 하나 더 두면 서로를 막지 못한다).
                static mutex      s_mutexDXCSerial;
                ScopedLock<mutex> dxcSerialLock{ s_mutexDXCSerial };
    #endif

                DxcComPtr<IDxcUtils>     utils;
                DxcComPtr<IDxcCompiler3> compiler;
                HRESULT                  hrInit = fnDXCCreateInstance( CLSID_DxcUtils, IID_PPV_ARGS( dxcAddressOf( utils ) ) );
                if ( SUCCEEDED( hrInit ) )
                    fnDXCCreateInstance( CLSID_DxcCompiler, IID_PPV_ARGS( dxcAddressOf( compiler ) ) );
                else
                    SW_LOG_ERROR( "DxcCreateInstance(CLSID_DxcUtils) failed: 0x%#", hrInit );
                if ( utils == nullptr || compiler == nullptr )
                    return DxcOutcome::NoOutput;

                // 소유권은 includeHandler 로 넘기고, 기록은 pIncludeRecorder 로 읽는다(둘 다 이 호출 안에서만 산다).
                RecordingDXCInclude*          pIncludeRecorder = make_unique<RecordingDXCInclude>( utils ).release();
                DxcComPtr<IDxcIncludeHandler> includeHandler;
                includeHandler.Attach( pIncludeRecorder );

                // 소스는 엔진 파일 입구로 읽는다(DXC `LoadFile` 은 자기 파일 API 다). 실패 로그는 readFile 이 남긴다.
                const wstring wPath = StringUtil::utf8ToUTF16( absPathStr.c_str() );
                vector<uint8> sourceBytes;
                if ( FileUtil::readFile( absPathStr, sourceBytes ) == false )
                    return DxcOutcome::NoOutput;

                wstring wEntryPoint = StringUtil::utf8ToUTF16( desc._entryPoint.c_str() );
                wstring wProfile    = StringUtil::utf8ToUTF16( profile.c_str() );

                // 인자 글의 수명: 아래 지역 벡터(경로 · 매크로)와 정적 글이 Compile 이 끝날 때까지 산다.
                vector<LPCWSTR> listArgument;
                vector<wstring> listDefineArg;
                listDefineArg.reserve( desc._listDefine.size() );

                listArgument.push_back( wPath.c_str() );
                listArgument.push_back( L"-E" );
                listArgument.push_back( wEntryPoint.c_str() );
                listArgument.push_back( L"-T" );
                listArgument.push_back( wProfile.c_str() );
                listArgument.push_back( L"-Zpr" );

                vector<string>  listIncludeDir;
                vector<wstring> listIncludeDirW;
                if ( collectShaderIncludeDirs( absPathStr, listIncludeDir ) == false )
                {
                    outResult._errorMessage = "Shader include domains cannot be found (see the _listResourcePriority error above).";
                    return DxcOutcome::Rejected;
                }
                listIncludeDirW.reserve( listIncludeDir.size() );
                for ( const string& dir : listIncludeDir )
                {
                    listIncludeDirW.push_back( StringUtil::utf8ToUTF16( dir.c_str() ) );
                    listArgument.push_back( L"-I" );
                    listArgument.push_back( listIncludeDirW.back().c_str() );
                }

                appendDXCTargetArguments( desc, listArgument );

                for ( const ShaderMacroDefine& def : desc._listDefine )
                {
                    if ( def._name.empty() )
                        continue;
                    string defineStr = def._name;
                    defineStr += "=";
                    defineStr += def._value;
                    listDefineArg.push_back( StringUtil::utf8ToUTF16( defineStr.c_str() ) );
                    listArgument.push_back( L"-D" );
                    listArgument.push_back( listDefineArg.back().c_str() );
                }

                // 디버그 코드젠은 빌드 구성이 아니라 **요청**이 정한다 (ShaderCompileDesc::_bDebugCodegen).
                if ( desc._bDebugCodegen != SW_FALSE )
                {
                    listArgument.push_back( L"-Zi" );
                    listArgument.push_back( L"-Od" );
                }

                DxcBuffer sourceBuffer{};
                sourceBuffer.Ptr      = sourceBytes.data();
                sourceBuffer.Size     = sourceBytes.size();
                sourceBuffer.Encoding = DXC_CP_UTF8;

                DxcComPtr<IDxcResult> compileResult;
                HRESULT               hrCompile = compiler->Compile(
                    &sourceBuffer,
                    listArgument.data(),
                    static_cast<uint32>( listArgument.size() ),
                    dxcGet( includeHandler ),
                    IID_PPV_ARGS( dxcAddressOf( compileResult ) ) );
                if ( FAILED( hrCompile ) )
                {
                    SW_LOG_ERROR( "DXC compiler->Compile failed with HRESULT: 0x%#", hrCompile );
                    return DxcOutcome::NoOutput;
                }

                HRESULT status = S_OK;
                compileResult->GetStatus( &status );

                DxcComPtr<IDxcBlobUtf8> errors;
                compileResult->GetOutput( DXC_OUT_ERRORS, IID_PPV_ARGS( dxcAddressOf( errors ) ), nullptr );
                if ( errors != nullptr && errors->GetStringLength() > 0 )
                    outResult._errorMessage = errors->GetStringPointer();
                if ( FAILED( status ) )
                {
                    SW_LOG_ERROR( "%#", outResult._errorMessage.c_str() );
                    return DxcOutcome::Rejected;
                }

                DxcComPtr<IDxcBlob> shaderBlob;
                compileResult->GetOutput( DXC_OUT_OBJECT, IID_PPV_ARGS( dxcAddressOf( shaderBlob ) ), nullptr );
                if ( shaderBlob == nullptr || shaderBlob->GetBufferSize() == 0 )
                    return DxcOutcome::NoOutput;

                const uint8* pData = static_cast<const uint8*>( shaderBlob->GetBufferPointer() );
                outResult._bytecode.assign( pData, pData + shaderBlob->GetBufferSize() );
                if ( desc._targetFormat == ShaderTargetFormat::SPIRV_OpenGL )
                    patchSPIRVBuiltinsForOpenGL( outResult._bytecode );
                outResult._listIncludedFile = pIncludeRecorder->takeIncludedFiles();
                outResult._bSuccess         = true;
    #if defined( SW_DEBUG )
                const utf8* pFormatName = ( desc._targetFormat == ShaderTargetFormat::SPIRV_Vulkan )
                                            ? "Vulkan (SPIR-V, Vulkan API target)"
                                        : ( desc._targetFormat == ShaderTargetFormat::SPIRV_OpenGL )
                                            ? "OpenGL (SPIR-V vulkan1.1)"
                                            : "D3D12 (DXIL Row-Major)";
                SW_LOG_TRACE( "Target: %#", pFormatName );
    #endif
                return DxcOutcome::Compiled;
            }

    #if defined( SW_PLATFORM_WINDOWS )
            /** @brief DXC 가 출력을 못 낸 DX 타깃을 FXC 로 컴파일합니다(연 헤더를 적는 include 처리기, 타깃 매크로 없음). 성공하면 true 입니다. */
            [[nodiscard]] static bool compileWithFxc( const ShaderCompileDesc& desc, const string& absPathStr, const string& profile, ShaderCompileResult& outResult )
            {
                UINT                             compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
                Microsoft::WRL::ComPtr<ID3DBlob> codeBlob;
                Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
                wstring                          wPath( absPathStr.begin(), absPathStr.end() );

                // 표준 파일 include(`D3D_COMPILE_STANDARD_FILE_INCLUDE`)는 어느 헤더를 열었는지 알려 주지 않는다. 최신 판정이 그 목록에 기대므로 같은 루트를 도는 처리기를 쓴다.
                vector<string> listIncludeDir;
                if ( collectShaderIncludeDirs( absPathStr, listIncludeDir ) == false )
                    return false;
                MultiRootD3DInclude includeHandler( std::move( listIncludeDir ) );

                vector<D3D_SHADER_MACRO> listMacro;
                listMacro.reserve( desc._listDefine.size() + 1 );
                for ( const ShaderMacroDefine& def : desc._listDefine )
                {
                    if ( def._name.empty() )
                        continue;
                    listMacro.push_back( { def._name.c_str(), def._value.c_str() } );
                }
                listMacro.push_back( { nullptr, nullptr } );

                HRESULT hrFallback = D3DCompileFromFile(
                    wPath.c_str(),
                    listMacro.data(),
                    &includeHandler,
                    desc._entryPoint.c_str(),
                    profile.c_str(),
                    compileFlags,
                    0,
                    codeBlob.GetAddressOf(),
                    errorBlob.GetAddressOf() );
                if ( FAILED( hrFallback ) )
                    return false;

                const uint8* pData = static_cast<const uint8*>( codeBlob->GetBufferPointer() );
                outResult._bytecode.assign( pData, pData + codeBlob->GetBufferSize() );
                outResult._listIncludedFile = includeHandler.takeIncludedFiles();
                outResult._bSuccess         = true;
                SW_LOG_TRACE( "Target: D3D12/11 (Row-Major)" );
                return true;
            }
    #endif // SW_PLATFORM_WINDOWS
#endif     // SW_HAS_DXC_API
        };
    } // namespace
} // namespace sw

namespace sw
{

    void ShaderCompiler::appendEngineDefines( vector<ShaderMacroDefine>& inoutList )
    {
        // 에뮬 엔진 텍스처 칸 표(shaderslot::emulated) — binding.hlsli 의 Bindless::emulatedSlot 이 읽는다.
        for ( uint32 ordinal = 0; ordinal < shaderslot::kEngineTextureCount; ++ordinal )
        {
            ShaderMacroDefine define;
            define._name  = string( shaderslot::emulated::kSlotMacroPrefix ) + shaderslot::emulated::kEngineTextureNames[ordinal];
            define._value = to_string( ordinal );
            inoutList.push_back( std::move( define ) );
        }
    }

    void* ShaderCompiler::findDXCCreateInstance()
    {
#if defined( SW_HAS_DXC_API )
        static void* const s_pCreateInstance = ShaderCompilerInternal::loadDXCCreateInstance();
        return s_pCreateInstance;
#else
        return nullptr;
#endif
    }

    ShaderCompileResult ShaderCompiler::compileHLSL( const ShaderCompileDesc& requestDesc )
    {
        using Internal = ShaderCompilerInternal;
        // 엔진 define 은 모든 컴파일에 더한다(appendEngineDefines) — 값이 도구 지문에 섞여 표를 바꾸면 모든 셰이더 캐시가 다시 컴파일된다.
        ShaderCompileDesc desc = requestDesc;
        appendEngineDefines( desc._listDefine );
        ShaderCompileResult result{};
        result._bSuccess = false;
        result._bytecode.reserve( 4096 );

        // 소스는 절대 경로(쿠커)이거나 리소스 id(런타임)다 - 글의 모양으로 가른다. 작업 폴더에 같은 이름 파일이 있는지로 먼저 추측하지 않는다(그 파일을 컴파일할 수 있었다).
        const string absPathStr = AbsolutePath::isStrictAbsolute( desc._filePath ) ? FileUtil::normalizeSeparators( desc._filePath )
                                                                                   : engine::getSubsystem<AssetPaths>().findLooseFile( desc._filePath ).getDiskPath();

        if ( absPathStr.empty() || FileUtil::isRegularFile( absPathStr ) == false )
        {
            result._errorMessage = "Shader source file not found: " + desc._filePath;
            // 부르는 쪽이 존재 여부를 먼저 검사하는 것이 정상이다. 없는 파일은 ERROR 가 아니라 조용히 실패한다.
            return result;
        }

        // 같은 이름의 헤더가 엔진 헤더를 가리면 어느 쪽이 쓰였는지 알 수 없다 — 캐시 적중보다 먼저, 오류로 멈춘다.
        vector<string> listShadowed;
        if ( ShaderCooker::findShadowedHeaders( engine::getSubsystem<AssetPaths>().getRootFolderPath(), engine::getSubsystem<AssetPaths>().getSearchPriority(), absPathStr, listShadowed ) == false )
        {
            for ( const string& problem : listShadowed )
            {
                result._errorMessage += problem + "\n";
            }
            if ( result._errorMessage.empty() )
                result._errorMessage = "Shader include domains cannot be found (see the _listResourcePriority error above).";
            SW_LOG_ERROR( "%#", result._errorMessage );
            return result;
        }

        const string cachePath = Internal::computeCachePath( desc, absPathStr );
        if ( cachePath.empty() == false ) // 의존 기록이 없으면(없는 항목 · 쓰는 중) 읽기 한 번이 "없음" 으로 답한다
        {
            // 의존 기록이 없거나 연 헤더 중 하나라도 달라졌으면 맞은 것이 아니다 — 그 셰이더만 다시 컴파일한다(다른 헤더를 쓰는 셰이더의 캐시는 그대로다).
            vector<string> listIncludedFile;
            vector<uint8>  cachedBytes;
            const bool     bRecordCurrent = ShaderCooker::readIncludeRecord( cachePath + ShaderCooker::kIncludeRecordSuffix, listIncludedFile );
            const bool     bBytesRead     = bRecordCurrent && FileUtil::readFile( cachePath, cachedBytes );
            if ( bBytesRead && cachedBytes.empty() == false )
            {
                result._bytecode         = std::move( cachedBytes );
                result._listIncludedFile = std::move( listIncludedFile );
                result._bSuccess         = true;
                return result;
            }
        }

        const string profile = Internal::getTargetProfile( desc._stage, desc._targetFormat );

#if defined( SW_PLATFORM_WINDOWS )
        if ( desc._targetFormat == ShaderTargetFormat::DXBC_D3D11 )
        {
            if ( Internal::compileDxbc( desc, absPathStr, profile, result ) )
                Internal::saveToCacheIfEnabled( cachePath, result );
            return result;
        }
#else
        // DXBC 를 낼 수 있는 건 FXC(d3dcompiler) 뿐이고 그건 Windows 전용이다. 위 블록이 통째로
        // 빠지는 플랫폼에서 이 타깃이 그대로 아래 DXC 로 흘러가면 vs_5_0 같은 SM5 프로파일을
        // 모른다며 "invalid profile vs_5_0" 로 죽는다. 그 메시지로는 "이 플랫폼엔 D3D11 이 없다" 는
        // 진짜 이유가 드러나지 않아 부르는 쪽이 환경 문제와 셰이더 문제를 구분하지 못한다.
        if ( desc._targetFormat == ShaderTargetFormat::DXBC_D3D11 )
        {
            result._bSuccess     = false;
            result._errorMessage = "DXBC (D3D11) target is unavailable on this platform (requires Windows D3DCompiler).";
            return result;
        }
#endif // SW_PLATFORM_WINDOWS

#if defined( SW_HAS_DXC_API )
        const Internal::DxcOutcome outcome = Internal::compileDXC( desc, absPathStr, profile, result );
        if ( outcome == Internal::DxcOutcome::Compiled )
        {
            Internal::saveToCacheIfEnabled( cachePath, result );
            return result;
        }
        if ( outcome == Internal::DxcOutcome::Rejected )
            return result;
    #if defined( SW_PLATFORM_WINDOWS )
        // FXC 는 DXBC 만 만든다. SPIR-V 타깃(Vulkan · OpenGL)에는 절대 폴백하지 않는다.
        const bool bFxcTarget = desc._targetFormat != ShaderTargetFormat::SPIRV_Vulkan && desc._targetFormat != ShaderTargetFormat::SPIRV_OpenGL;
        if ( bFxcTarget && Internal::compileWithFxc( desc, absPathStr, profile, result ) )
        {
            Internal::saveToCacheIfEnabled( cachePath, result );
            return result;
        }
    #endif // SW_PLATFORM_WINDOWS
#endif     // SW_HAS_DXC_API

        if ( result._errorMessage.empty() )
            result._errorMessage = "Failed to compile shader with DXC and D3DCompiler (hrCompile/hrLoad failed)";
        SW_LOG_ERROR( "%# (Path: %#)", result._errorMessage.c_str(), desc._filePath.c_str() );
        return result;
    }
} // namespace sw
