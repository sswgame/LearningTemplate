#include "pch.h"

#include "Engine/Graphics/Material/Material.h"

#include "Core/Common/HashUtil.h"
#include "Core/Concurrency/mutex.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/Material/MaterialUtil.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/Support/BindlessIndexValidator.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Resource/AssetLoadProfiler.h"
#include "Engine/Resource/AssetManager.h"

namespace sw
{
    SW_LOG_CALLER( "Material" );

    MaterialProperty::MaterialProperty() noexcept
        : _type{ MaterialPropertyType::Unknown }
        , _shaderType{ MaterialPropertyType::Unknown }
        , _min{ 0.0f }
        , _max{ 1.0f }
        , _offset{ 0 }
        , _size{ 0 }
        , _textureIndex{ kInvalidDescriptorIndex }
        , _bHdr{ SW_FALSE }
        , _bSrgb{ SW_TRUE }
        , _bHidden{ SW_FALSE }
        , _bAdvanced{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    MaterialStaticSwitch::MaterialStaticSwitch() noexcept
        : _bEnabled{ SW_FALSE }
        , _bShaderFeature{ SW_TRUE }
        , _reserved{ 0 }
    {
    }

    shared_ptr<Material> Material::create()
    {
        return sw::make_shared<Material>( CreateKey{} );
    }

    Material::Material( CreateKey )
        : _assetPath{}
        , _desc{}
        , _data{}
        , _constantBuffer{ 0 }
        , _descriptorIndex{ kInvalidDescriptorIndex }
        , _elementStride{ 0 }
        , _bufferGeneration{ 0 }
        , _shaderLayoutCacheGeneration{ 0 }
        , _shaderLayoutBackend{}
        , _pRHIDevice{ nullptr }
        , _textureReloadGeneration{ 0 }
        , _listAcquiredTexturePath{}
        , _listTextureLoad{}
        , _textureLoadStart{ AssetLoadStart::OnCallingThread }
        , _listBorrowedTexturePath{}
        , _listMaterialTextureSrv{}
        , _listMaterialTextureName{}
        , _blendMode{ RHIBlendMode::Opaque }
        , _asyncLoadState{ sw::make_shared<AsyncLoadState>() }
        , _listCachedDefine{}
        , _cachedPermutationHash{ 0 }
        , _cachedShaderPathHash{ 0 }
        , _bDefinesDirty{ SW_TRUE }
        , _bShaderPathHashDirty{ SW_TRUE }
        , _bShaderLayoutSynced{ SW_FALSE }
        , _reservedMaterial{ 0 }
    {
        _asyncLoadState->_pMaterial = this;
    }

    Material::~Material()
    {
        if ( _pRHIDevice != nullptr )
            releaseRHI( _pRHIDevice );
        if ( _asyncLoadState != nullptr )
        {
            std::scoped_lock<mutex> lock{ _asyncLoadState->_mutex };
            _asyncLoadState->_pMaterial = nullptr;
        }
    }

    bool Material::initialize( IRHIDevice* pRHI, string_view assetRelativePath )
    {
        if ( pRHI == nullptr )
            return false;

        _pRHIDevice = pRHI;
        _assetPath  = assetRelativePath;

        // 못 읽은 머티리얼은 빈 값으로 올리지 않고 실패한다. 대신 그릴 것(마젠타 `missingmaterial`)은 부르는 쪽 한 곳이 정한다(`MeshComponent::resolveMaterialAsset`).
        AssetLoadScope loadScope( "Material", assetRelativePath );
        if ( loadFromFile( assetRelativePath ) == false )
        {
            SW_LOG_ERROR( "Material file '%#' could not be loaded", assetRelativePath );
            return false;
        }
        loadScope.beginPhase( AssetLoadPhase::Upload );

        // 상수 버퍼는 256 바이트 단위다(D3D12 CBV 정렬). 파라미터가 없는 머티리얼도 한 단위를 잡는다 — 셰이더가 원소 하나를 읽는다.
        constexpr uint32 kConstantBufferAlignment = 256u;
        const uint32     bufferSize               = MathUtil::align( MathUtil::max( static_cast<uint32>( _data._bytes.size() ), 1u ), kConstantBufferAlignment );
        _data._bytes.resize( bufferSize, 0 );
        ++_bufferGeneration; // 크기를 256 정렬로 맞췄다

        _constantBuffer = pRHI->getResourceFactory()->createConstantBuffer( bufferSize );
        if ( _constantBuffer == 0 )
        {
            SW_LOG_ERROR( "Failed to create Constant Buffer!" );
            return false;
        }

        pRHI->getResourceFactory()->updateConstantBuffer( _constantBuffer, _data._bytes.data(), bufferSize );
        _descriptorIndex = pRHI->getResourceFactory()->registerBindlessBufferSrv( _constantBuffer );

        // 텍스처는 CB 가 생긴 뒤에 푼다. setTextureParameter 가 인덱스를 CB 에 바로 올리기 때문이다.
        if ( resolveTextureAssets( pRHI ) == false )
        {
            releaseRHI( pRHI ); // 반쯤 올린 상태(CB 만 있음)를 "올라갔다"(`isRHIValid`)로 남기지 않는다
            return false;
        }

        SW_LOG_INFO( "Initialized '%#' with Bindless Descriptor Index %#", _desc._name.c_str(), _descriptorIndex );
        loadScope.setBytes( _data._bytes.size() );
        if ( _descriptorIndex != kInvalidDescriptorIndex )
            loadScope.setSucceeded();
        return _descriptorIndex != kInvalidDescriptorIndex;
    }

    bool Material::resolveTextureAssets( IRHIDevice* pRHI )
    {
        if ( pRHI == nullptr || engine::areEngineServicesBound() == false )
            return true; // 엔진 서비스 없이 도는 도구 · 테스트는 텍스처를 풀지 않는다(텍스처 캐시가 없다)

        // DX12 · Vulkan 은 셰이더가 전역 bindless 인덱스로 힙 · 배열을 직접 읽는다. DX11 · GL 은 그게 안 되므로
        // (SM5.0 은 리소스 배열 동적 인덱싱이 없고, GL 은 SPIR-V 라 ARB_bindless_texture 를 못 쓴다) 엔진이
        // 머티리얼 텍스처를 t16..t31 고정 슬롯에 바인딩하고 CB 에는 **서수**를 넣는다.
        const bool    bNativeBindless = pRHI->supportsNativeBindlessSampling();
        TextureCache& textures        = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
        _textureReloadGeneration      = textures.getReloadGeneration();
        for ( const MaterialProperty& prop : _data._listProperty )
        {
            if ( MaterialUtil::isTextureType( prop._type ) == false )
                continue;
            if ( prop._assetPath.empty() )
            {
#if !defined( SW_SHIPPING )
                // 텍스처가 붙지 않은 필드는 머티리얼 데이터에 kInvalidDescriptorIndex 로 실려 셰이더로 간다(-gv_validateBindlessIndex=2 로 자리를 남긴다).
                if ( BindlessIndexValidator::isTracingAbsent() )
                    (void)BindlessIndexValidator::noteAbsent( hashed_string( _desc._name.c_str() ), hashed_string( prop._name.c_str() ), 0 ); // 자리마다 한 번만 남긴다
#endif
                continue;
            }
            const uint32 ordinal = static_cast<uint32>( _listMaterialTextureSrv.size() );
            if ( bNativeBindless == false && ordinal >= shaderslot::kMaterialTextureCount )
            {
                SW_LOG_WARNING( "Material '%#': this backend binds at most %# material textures - '%#' stays white.",
                                _desc._name.c_str(), shaderslot::kMaterialTextureCount, prop._name.c_str() );
                continue;
            }

            // 씬의 머티리얼(`MaterialCache`)은 진짜 텍스처를 워커가 읽는다 — 다 읽기 전에는 로드 중 텍스처(회색). 직접 만든 머티리얼(도구 · 테스트 · 미리보기)은 그 자리에서 읽는다
            // (`setTextureLoadStart`). 못 읽으면 마젠타 체커를 빌린다 — 흰색이면 화면에서 빠진 것을 알 수 없다(Shipping 도 같다).
            TextureHandle load = textures.request( prop._assetPath, pRHI, _textureLoadStart );
            if ( _textureLoadStart == AssetLoadStart::OnCallingThread )
                (void)load.wait(); // 업로드(완료 단계)까지 이 자리에서 — 머티리얼은 디바이스를 쓰는 스레드에서 초기화한다
            string           borrowedPath;
            const Texture2D* pTexture = load.isReady() ? load.getBlock()->getTexture() : nullptr;
            if ( pTexture == nullptr )
            {
                const bool bLoading = load.isLoading();
                if ( bLoading == false )
                {
                    SW_LOG_ERROR( "Material '%#': texture '%#' for '%#' could not be loaded (%#) — sampling the missing-texture checker.", _desc._name.c_str(),
                                  prop._assetPath.c_str(), prop._name.c_str(), load.getError().c_str() );
                    load.reset();
                }
                borrowedPath = bLoading ? engine::getSubsystem<EngineDefaultAssets>()._loadingTexture.get() : engine::getSubsystem<EngineDefaultAssets>()._missingTexture.get();
                pTexture     = textures.acquire( borrowedPath, pRHI );
                if ( pTexture == nullptr )
                {
                    // 로드 중 텍스처와 체커는 엔진 필수 데이터다. 그것마저 없으면 흰색으로 그리지 않고 머티리얼 초기화가 실패한다.
                    SW_LOG_ERROR( "Material '%#': texture '%#' for '%#' is not ready and the stand-in texture '%#' could not be loaded either", _desc._name.c_str(),
                                  prop._assetPath.c_str(), prop._name.c_str(), borrowedPath.c_str() );
                    return false;
                }
            }

            _listAcquiredTexturePath.push_back( prop._assetPath );
            _listTextureLoad.push_back( std::move( load ) );
            _listBorrowedTexturePath.push_back( std::move( borrowedPath ) );
            _listMaterialTextureSrv.push_back( pTexture->getSrv() );
            _listMaterialTextureName.emplace_back( prop._name.c_str() );
            setTextureParameter( pRHI, hashed_string( prop._name.c_str() ), bNativeBindless ? pTexture->getSrv() : ordinal );
        }
        return true;
    }

    bool Material::refreshTextureBindings()
    {
        const bool bTextureRefreshUnavailable = _listAcquiredTexturePath.empty() || _pRHIDevice == nullptr || engine::areEngineServicesBound() == false;
        if ( bTextureRefreshUnavailable )
            return false;
        TextureCache& textures   = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
        const uint32  generation = textures.getReloadGeneration();
        if ( generation == _textureReloadGeneration )
            return false;
        _textureReloadGeneration = generation;

        // resolveTextureAssets 가 프로퍼티 순서대로(빌리지 못한 것은 건너뛰고) 쌓았으므로 같은 순서로 맞춰 간다. 슬롯 바인딩 백엔드(DX11 · GL)의
        // 바이트에는 인덱스가 아니라 서수가 실려 있어 바꿀 것이 슬롯 목록뿐이다.
        const bool bNativeBindless = _pRHIDevice->supportsNativeBindlessSampling();
        bool       bChanged{ false };
        size_t     ordinal{ 0 };
        for ( const MaterialProperty& prop : _data._listProperty )
        {
            const bool bOrdinalOutOfRange = ordinal >= _listAcquiredTexturePath.size() || ordinal >= _listBorrowedTexturePath.size() || ordinal >= _listMaterialTextureSrv.size();
            if ( bOrdinalOutOfRange )
                break;
            if ( MaterialUtil::isTextureType( prop._type ) == false || prop._assetPath != _listAcquiredTexturePath[ordinal] )
                continue;
            resolveArrivedTexture( textures, ordinal );
            const string&            boundPath = _listBorrowedTexturePath[ordinal].empty() ? _listAcquiredTexturePath[ordinal] : _listBorrowedTexturePath[ordinal];
            const Texture2D*         pTexture  = textures.find( boundPath );
            const RHIDescriptorIndex srv       = pTexture != nullptr ? pTexture->getSrv() : kInvalidDescriptorIndex;
            if ( srv != _listMaterialTextureSrv[ordinal] )
            {
                _listMaterialTextureSrv[ordinal] = srv;
                if ( bNativeBindless )
                    setTextureParameter( _pRHIDevice, hashed_string( prop._name.c_str() ), srv );
                bChanged = true;
            }
            ++ordinal;
        }
        return bChanged;
    }

    void Material::resolveArrivedTexture( TextureCache& textures, size_t ordinal )
    {
        TextureHandle& load       = _listTextureLoad[ordinal];
        string&        borrowed   = _listBorrowedTexturePath[ordinal];
        const bool     bNoArrival = load.isValid() == false || load.isLoading() || borrowed.empty();
        if ( bNoArrival )
            return;
        // 로드가 끝났다 — 대신 빌린 로드 중 텍스처를 놓고 진짜로 바꾼다. 실패면 마젠타 체커를 빌린다(흰색이면 빠진 것을 알 수 없다).
        string standIn = std::move( borrowed );
        borrowed.clear();
        if ( load.hasFailed() )
        {
            SW_LOG_ERROR( "Material '%#': texture '%#' could not be loaded (%#) — sampling the missing-texture checker.", _desc._name.c_str(),
                          _listAcquiredTexturePath[ordinal].c_str(), load.getError().c_str() );
            load.reset();
            borrowed = engine::getSubsystem<EngineDefaultAssets>()._missingTexture.get();
            if ( textures.acquire( borrowed, _pRHIDevice ) == nullptr )
            {
                // 체커는 엔진 필수 데이터다 — 못 읽으면 로드 중 텍스처를 그대로 둔다(오류는 남았다).
                SW_LOG_ERROR( "Material '%#': the missing-texture checker '%#' could not be loaded either", _desc._name.c_str(), borrowed.c_str() );
                borrowed = std::move( standIn );
                return;
            }
        }
        textures.release( standIn, _pRHIDevice );
    }

    uint32 Material::findTextureSlot( hashed_string name ) const
    {
        for ( size_t slot = 0; slot < _listMaterialTextureName.size(); ++slot )
        {
            if ( _listMaterialTextureName[slot] == name )
                return static_cast<uint32>( slot );
        }
        return kInvalidTextureSlot;
    }

    void Material::collectTexturePaths( vector<string>& outListPath ) const
    {
        for ( const MaterialProperty& prop : _data._listProperty )
        {
            if ( MaterialUtil::isTextureType( prop._type ) && prop._assetPath.empty() == false )
                outListPath.push_back( prop._assetPath );
        }
    }

    void Material::releaseTextureAssets( IRHIDevice* pRHI )
    {
        if ( _listAcquiredTexturePath.empty() )
            return;
        if ( engine::areEngineServicesBound() )
        {
            TextureCache& textures = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
            for ( const string& path : _listBorrowedTexturePath )
            {
                if ( path.empty() == false )
                    textures.release( path, pRHI );
            }
        }
        _listAcquiredTexturePath.clear();
        _listTextureLoad.clear(); // 진짜 텍스처의 참조는 핸들이 쥐었다 — 놓으면 캐시에 돌려준다
        _listBorrowedTexturePath.clear();
        _listMaterialTextureSrv.clear();
        _listMaterialTextureName.clear();
        for ( MaterialProperty& prop : _data._listProperty )
        {
            if ( MaterialUtil::isTextureType( prop._type ) == false || prop._assetPath.empty() )
                continue;
            // _value 도 비운다. setTextureParameter 가 인덱스를 문자열로도 남기므로, 그대로 두면
            // 다음 패킹이 이미 해제된 인덱스를 숫자 오버라이드로 복원한다.
            prop._textureIndex = kInvalidDescriptorIndex;
            prop._value.clear();
        }
    }

    bool Material::initRHI( IRHIDevice* pDevice )
    {
        // 통보 순서는 정해져 있지 않다. 이미 올라가 있으면 그대로 둔다.
        if ( isRHIValid() )
            return true;
        // 한 번도 initialize 되지 않은 머티리얼이다. 복원할 내용 자체가 없다.
        if ( pDevice == nullptr || _assetPath.empty() )
            return true;
        return initialize( pDevice, _assetPath );
    }

    void Material::forgetRHI( IRHIDevice* pDevice )
    {
        if ( _pRHIDevice != pDevice )
            return;

        // 빌린 텍스처도 **여기서 놓는다.** 널 디바이스로 부르면 `TextureCache` 는 참조 수만 줄이고
        // GPU 호출은 하지 않는다(디바이스가 이미 없으므로 그것이 맞다).
        //
        // 주의: `releaseRHI` 와 이 함수는 같은 상태를 남겨야 한다. 목록이 남은 채 `initRHI` 가 오면 `resolveTextureAssets` 가
        // 목록에 **덧붙여** `ordinal` 이 0 이 아닌 값에서 시작하고, 네이티브 bindless 가 없는 백엔드(DX11 · GL)는 그 서수를
        // t16..t31 고정 슬롯 번호로 쓰므로 **엉뚱한 텍스처를 읽거나**, 한도를 넘어 흰색으로 남는다.
        releaseTextureAssets( nullptr );

        // 디바이스가 이미 없다. GPU 자원은 그와 함께 갔다. 핸들만 비운다(destroy 를 부르면 해제 후 사용이다).
        _constantBuffer  = 0;
        _descriptorIndex = kInvalidDescriptorIndex;
        _pRHIDevice      = nullptr;
    }

    void Material::releaseRHI( IRHIDevice* pRHI )
    {
        // 남의 디바이스가 죽는 통보라면 내 것이 아니다.
        const bool bDeviceNotMine = pRHI != nullptr && _pRHIDevice != nullptr && _pRHIDevice != pRHI;
        if ( bDeviceNotMine )
            return;

        releaseTextureAssets( pRHI );
        if ( pRHI != nullptr )
        {
            // 마지막 소유를 게임 스레드가 놓을 수 있다(GPUScene 후보 · 걷은 뷰) — 렌더 스레드가 병렬 기록 중이면 핸들 반환을 그 프레임 뒤로 미룬다.
            if ( _descriptorIndex != kInvalidDescriptorIndex )
                pRHI->releaseHandle( RHIHandleKind::BindlessResource, _descriptorIndex );
            if ( _constantBuffer != 0 )
                pRHI->releaseHandle( RHIHandleKind::Buffer, _constantBuffer );
        }
        _constantBuffer  = 0;
        _descriptorIndex = kInvalidDescriptorIndex;
        _pRHIDevice      = nullptr;
    }

    bool Material::ensureShaderLayout( IRHIDevice* pDevice )
    {
        // 머티리얼 바이트의 기준은 .material 의 프로퍼티 순서가 아니라 **셰이더의 MaterialData 원소 레이아웃**이다(언리얼도
        // 머티리얼 파라미터 레이아웃을 셰이더에서 가져온다). 로드 경로에서도 맞춰야 한다 — XML 순서로 패킹하면 stride 가 0 이고,
        // 그러면 GPUScene 이 CB 크기(256)를 stride 로 써서 원소 1 부터 어긋난다.
        //
        // "맞췄다" 표시는 다시 로드(XML 순서로 다시 쌓는다)가 풀고(applyDescToRuntime), 리플렉션 캐시를 비우면 낡는다.
        // 표시가 남으면 XML 순서 바이트와 옛 stride 가 함께 올라간다.
        if ( pDevice == nullptr || _desc._shaderPath.empty() )
            return false;
        const RHIBackend backend = pDevice->getBackendType();
        if ( isShaderLayoutSynced( backend ) )
            return _elementStride != 0;
        _bShaderLayoutSynced         = SW_TRUE;
        _shaderLayoutBackend         = backend;
        _shaderLayoutCacheGeneration = ShaderReflectionLibrary::getCacheGeneration();

        ShaderReflectionData reflection{};
        if ( reflectMaterialSchema( _desc._shaderPath, RHI::getShaderTargetFormat( pDevice->getBackendType() ), reflection ) == false )
        {
            SW_LOG_ERROR( "Shader reflection for material '%#' not found ('%#') - clearing the bytes and not uploading.", _desc._name.c_str(), _desc._shaderPath.c_str() );
            discardBytesAfterLayoutError();
            return false;
        }
        const bool bAllPacked = syncPropertiesFromReflection( reflection );
        return bAllPacked && _elementStride != 0;
    }

    bool Material::reflectMaterialSchema( const string& shaderPath, ShaderTargetFormat targetFormat, ShaderReflectionData& outReflection )
    {
        ShaderCompileDesc desc{};
        desc._filePath     = shaderPath;
        desc._entryPoint   = getShaderStageInfo( ShaderStage::Pixel )._pEntryPoint;
        desc._stage        = ShaderStage::Pixel;
        desc._targetFormat = targetFormat;
        if ( ShaderReflectionLibrary::getOrReflect( desc, outReflection ) == false )
            return false;
        // 머티리얼을 정점 셰이더만 읽는 셰이더(바람에 흔드는 식생 · 파도를 옮기는 물)는 픽셀 스테이지에 스키마가 없다 — 정점 스테이지에서 찾는다.
        // 두 스테이지가 함께 읽는 셰이더는 GL(ARB_gl_spirv)이 링크를 거절하므로 스키마는 한 스테이지에만 있다.
        if ( hasMaterialSchema( outReflection ) == false )
        {
            ShaderCompileDesc    vertexDesc = desc;
            ShaderReflectionData vertexReflection{};
            vertexDesc._entryPoint = getShaderStageInfo( ShaderStage::Vertex )._pEntryPoint;
            vertexDesc._stage      = ShaderStage::Vertex;
            if ( ShaderReflectionLibrary::getOrReflect( vertexDesc, vertexReflection ) && hasMaterialSchema( vertexReflection ) )
                outReflection = std::move( vertexReflection );
        }
        return true;
    }

    bool Material::isShaderLayoutSynced( RHIBackend backend ) const
    {
        return _bShaderLayoutSynced != SW_FALSE && _shaderLayoutBackend == backend &&
               _shaderLayoutCacheGeneration == ShaderReflectionLibrary::getCacheGeneration();
    }

    bool Material::hasMaterialSchema( const ShaderReflectionData& reflectionData )
    {
        for ( const ShaderBufferInfo& element : reflectionData._listStructuredElement )
        {
            if ( element._name == shaderslot::resname::kMaterials && element._listVariable.empty() == false )
                return true;
        }
        for ( const ShaderBufferInfo& cb : reflectionData._listConstantBuffer )
        {
            if ( cb._name == shaderslot::cbname::kMaterial && cb._listVariable.empty() == false )
                return true;
        }
        return false;
    }

    bool Material::syncPropertiesFromReflection( const ShaderReflectionData& reflectionData )
    {
        if ( reflectionData._listConstantBuffer.empty() && reflectionData._listStructuredElement.empty() )
            return true;

        // 스키마 우선순위: GPUScene 머티리얼 데이터(g_SwMaterials 구조버퍼 원소) → MaterialCB. 둘 다 없으면 아래에서 경고하고 끝낸다.
        const ShaderBufferInfo* pSchemaCb = nullptr;
        for ( const ShaderBufferInfo& element : reflectionData._listStructuredElement )
        {
            if ( element._name == shaderslot::resname::kMaterials && element._listVariable.empty() == false )
            {
                pSchemaCb      = &element;
                _elementStride = element._totalSize;
                break;
            }
        }
        if ( pSchemaCb == nullptr )
        {
            _elementStride = 0;
            for ( const ShaderBufferInfo& cb : reflectionData._listConstantBuffer )
            {
                if ( cb._name == shaderslot::cbname::kMaterial && cb._listVariable.empty() == false )
                {
                    pSchemaCb = &cb;
                    break;
                }
            }
        }
        // 주의: "멤버가 있는 첫 상수버퍼" 로 폴백하지 않는다 — **PassCB 를 머티리얼 레이아웃으로 착각**해 조용히 엉뚱한
        // 오프셋에 값을 써 넣는다. 모든 셰이더가 SW_MATERIAL_BEGIN/END(g_SwMaterials) 아니면 MaterialCB 를 선언하므로,
        // 못 찾으면 조용히 틀리지 않고 알린다.
        if ( pSchemaCb == nullptr )
        {
            SW_LOG_ERROR( "The shader of material '%#' has no material schema ('%#') - declare SW_MATERIAL_BEGIN/END or MaterialCB.",
                          _desc._name.c_str(), _desc._shaderPath.c_str() );
            discardBytesAfterLayoutError();
            return false;
        }

        if ( _data._listProperty.empty() )
        {
            fillPropertiesFromSchema( *pSchemaCb );
            return true;
        }
        const bool bAllPacked = matchPropertiesToSchema( *pSchemaCb );
        packPropertiesIntoBytes( pSchemaCb->_totalSize );
        _desc._listProperty = _data._listProperty;
        // 불일치는 오류다 — 일부만 맞춘 바이트(적지 않은 칸의 0 = Vulkan 의 첫 실제 텍스처)를 올리지 않는다. 바이트가 비면 GPUScene 은 건너뛰고
        // 인스턴스는 updateRHI 가 false 를 돌려준다.
        if ( bAllPacked == false )
            discardBytesAfterLayoutError();
        return bAllPacked;
    }

    void Material::discardBytesAfterLayoutError()
    {
        _data._bytes.clear();
        _elementStride = 0;
        ++_bufferGeneration;
    }

    void Material::fillPropertiesFromSchema( const ShaderBufferInfo& schema )
    {
        for ( const ShaderVariableInfo& var : schema._listVariable )
        {
            MaterialProperty prop{};
            prop._name       = var._name;
            prop._offset     = var._offset;
            prop._size       = var._size;
            prop._shaderType = MaterialUtil::shaderTypeFromReflectionName( var._type, var._size );
            prop._type       = prop._shaderType;
            // 텍스처 인덱스 필드(이름 규칙 — MaterialUtil::isTextureIndexMember)는 텍스처 속성으로 둔다. uint 로 두면 0 이 들어가는데,
            // 0 은 Vulkan 의 첫 실제 텍스처 · DX11 · GL 의 서수 0 이다. 텍스처 속성은 붙은 것이 없으면 kInvalidDescriptorIndex 를 넣는다.
            if ( MaterialUtil::isTextureIndexMember( var._name, var._type, var._size ) )
                prop._type = MaterialPropertyType::Texture2D;
            _data._listProperty.push_back( prop );
        }
        rebuildPackedBuffer();
        // 순서대로 다시 쌓은 뒤 리플렉션 오프셋을 되돌린다
        for ( MaterialProperty& prop : _data._listProperty )
        {
            for ( const ShaderVariableInfo& var : schema._listVariable )
            {
                if ( var._name == prop._name )
                {
                    prop._offset = var._offset;
                    prop._size   = var._size;
                    break;
                }
            }
        }
        packPropertiesIntoBytes( schema._totalSize );
        _desc._listProperty = _data._listProperty;
        SW_LOG_TRACE( "Filled %# properties from shader reflection.", _data._listProperty.size() );
    }

    bool Material::matchPropertiesToSchema( const ShaderBufferInfo& schema )
    {
        bool bAllPacked{ true };
        for ( MaterialProperty& prop : _data._listProperty )
        {
            // 키워드 · UI 전용 필드는 머티리얼 원소의 멤버가 아니다.
            // 텍스처는 멤버다(`uint ...Map` — 인덱스 · 서수). 다른 프로퍼티처럼 리플렉션 오프셋을 받는다 — 건너뛰면 XML 순서로 쌓은 자리에
            // 남아, XML 순서가 셰이더 구조체와 다른 머티리얼(toon)에서 인덱스가 남의 칸에 들어가고 제 칸은 0(첫 실제 텍스처)이 된다.
            if ( MaterialUtil::isNonBufferType( prop._type ) )
                continue;

            const ShaderVariableInfo* pVar = nullptr;
            for ( const ShaderVariableInfo& candidate : schema._listVariable )
            {
                if ( candidate._name == prop._name )
                {
                    pVar = &candidate;
                    break;
                }
            }
            if ( pVar == nullptr )
            {
                SW_LOG_ERROR( "Material '%#' lists property '%#' that the shader struct does not have - remove it from the .material file (%#)",
                              _desc._name.c_str(), prop._name.c_str(), _desc._shaderPath.c_str() );
                bAllPacked = false;
                continue;
            }
            const ShaderVariableInfo&  var       = *pVar;
            const MaterialPropertyType reflected = MaterialUtil::shaderTypeFromReflectionName( var._type, var._size );
            if ( prop._shaderType == MaterialPropertyType::Unknown )
                prop._shaderType = reflected;
            else if ( MaterialUtil::packedSizeOf( prop._shaderType ) != 0 && MaterialUtil::packedSizeOf( prop._shaderType ) != var._size )
            {
                // 기본 셰이더 타입으로 바꿨을 때 크기가 리플렉션과 맞으면 그 타입으로 바꾼다
                if ( MaterialUtil::packedSizeOf( MaterialUtil::defaultShaderTypeFor( prop._type ) ) == var._size )
                    prop._shaderType = MaterialUtil::defaultShaderTypeFor( prop._type );
                else if ( reflected != MaterialPropertyType::Unknown )
                    prop._shaderType = reflected;
                else
                {
                    SW_LOG_ERROR( "Material '%#' property '%#' size mismatch (shaderType %# bytes vs shader %#, %#)",
                                  _desc._name.c_str(), prop._name.c_str(), MaterialUtil::packedSizeOf( prop._shaderType ), var._size, _desc._shaderPath.c_str() );
                    bAllPacked = false;
                }
            }
            prop._offset = var._offset;
            prop._size   = var._size;
        }
        // 셰이더 구조체엔 있는데 머티리얼에 없는 필드는 값을 적은 사람이 없다 — 0 으로 채우지 않고 알린다(텍스처 인덱스의 0 은 Vulkan 의 첫 실제 텍스처다).
        for ( const ShaderVariableInfo& var : schema._listVariable )
        {
            bool bListed{ false };
            for ( const MaterialProperty& prop : _data._listProperty )
            {
                if ( prop._name == var._name )
                {
                    bListed = true;
                    break;
                }
            }
            if ( bListed == false )
            {
                SW_LOG_ERROR( "Material '%#' does not list shader field '%#' - add it to the .material file (%#)", _desc._name.c_str(), var._name.c_str(), _desc._shaderPath.c_str() );
                bAllPacked = false;
            }
        }
        return bAllPacked;
    }

    void Material::packPropertiesIntoBytes( uint32 minimumByteSize )
    {
        uint32 maxEnd = minimumByteSize;
        for ( const MaterialProperty& prop : _data._listProperty )
        {
            if ( MaterialUtil::isNonBufferType( prop._type ) == false )
                maxEnd = MathUtil::max( maxEnd, prop._offset + prop._size );
        }
        _data._bytes.assign( maxEnd, 0 );
        for ( MaterialProperty& prop : _data._listProperty )
        {
            MaterialUtil::packPropertyIntoBuffer( prop, _data._bytes );
        }

        const uint32 alignedTotal = MathUtil::align( static_cast<uint32>( _data._bytes.size() ), 256u );
        _data._bytes.resize( alignedTotal, 0 );
        ++_bufferGeneration;
    }

    bool Material::rebuildPackedBuffer()
    {
        _data._bytes.clear();

        for ( MaterialProperty& prop : _data._listProperty )
        {
            if ( MaterialUtil::isNonBufferType( prop._type ) )
            {
                prop._offset = 0;
                prop._size   = 0;
                continue;
            }

            if ( prop._shaderType == MaterialPropertyType::Unknown )
                prop._shaderType = MaterialUtil::defaultShaderTypeFor( prop._type );

            // 이 순회는 비버퍼 타입을 0 으로 되돌리고 셰이더 타입을 채우는 것까지다. 오프셋 패킹은 아래 두 번째 순회가 한다.
        }

        bool anyExplicitOffset{ false };
        for ( const MaterialProperty& prop : _data._listProperty )
        {
            if ( MaterialUtil::isNonBufferType( prop._type ) == false && prop._offset != 0 )
            {
                anyExplicitOffset = true;
                break;
            }
        }
        // 리플렉션이 오프셋을 준 재질은 그 값을 그대로 쓰고, 하나도 없을 때만 HLSL 규칙으로
        // 순서대로 쌓는다.
        const bool useSequential = ( anyExplicitOffset == false );

        uint32 currentOffset{ 0 };
        uint32 maxEnd{ 0 };
        for ( MaterialProperty& prop : _data._listProperty )
        {
            if ( MaterialUtil::isNonBufferType( prop._type ) )
                continue;

            if ( prop._shaderType == MaterialPropertyType::Unknown )
                prop._shaderType = MaterialUtil::defaultShaderTypeFor( prop._type );

            uint32 packSize = MaterialUtil::packedSizeOf( prop._shaderType );
            if ( prop._size != 0 )
                packSize = prop._size;
            if ( packSize == 0 )
                packSize = 4;
            prop._size = packSize;

            if ( useSequential )
            {
                currentOffset = MaterialUtil::alignOffset( currentOffset, packSize );
                prop._offset  = currentOffset;
                currentOffset += packSize;
            }

            maxEnd = MathUtil::max( maxEnd, prop._offset + prop._size );
        }

        _data._bytes.assign( maxEnd, 0 );
        bool bAllPacked{ true };
        for ( MaterialProperty& prop : _data._listProperty )
        {
            if ( MaterialUtil::packPropertyIntoBuffer( prop, _data._bytes ) == false )
                bAllPacked = false;
        }

        const uint32 alignedTotal = MathUtil::align( static_cast<uint32>( _data._bytes.size() ), 256u );
        if ( alignedTotal > _data._bytes.size() )
            _data._bytes.resize( alignedTotal, 0 );
        ++_bufferGeneration;

        _desc._listProperty = _data._listProperty;
        return bAllPacked;
    }

    bool Material::resetParameterToDefault( IRHIDevice* pRHI, hashed_string name )
    {
        MaterialProperty* prop = findProperty( name );
        if ( prop == nullptr )
            return false;
        return setParameter( pRHI, name, prop->_defaultValue );
    }

    bool Material::packNamedValueIntoBuffer( hashed_string name, string_view value, vector<uint8>& inoutBuffer ) const
    {
        const MaterialProperty* pSrc = findProperty( name );
        if ( pSrc == nullptr )
            return false;
        MaterialProperty prop = *pSrc;
        prop._value           = value;
        return MaterialUtil::packPropertyIntoBuffer( prop, inoutBuffer );
    }

    bool Material::packTextureIntoBuffer( hashed_string name, RHIDescriptorIndex descIdx, vector<uint8>& inoutBuffer ) const
    {
        const MaterialProperty* pProp = findProperty( name );
        if ( pProp == nullptr || MaterialUtil::isNonBufferType( pProp->_type ) )
            return false;

        const uint32 packSize = pProp->_size != 0 ? pProp->_size : 4u;
        if ( inoutBuffer.size() < pProp->_offset + packSize )
            inoutBuffer.resize( pProp->_offset + packSize, 0 );

        const uint32 uIdx = static_cast<uint32>( descIdx );
        Memory::copy( inoutBuffer.data() + pProp->_offset, &uIdx, sizeof( uIdx ) );
        return true;
    }

    bool Material::packRawDataIntoBuffer( hashed_string name, const void* pData, uint32 byteSize, vector<uint8>& inoutBuffer ) const
    {
        if ( pData == nullptr || byteSize == 0 )
            return false;

        const MaterialProperty* pProp = findProperty( name );
        if ( pProp == nullptr || MaterialUtil::isNonBufferType( pProp->_type ) )
            return false;

        const uint32 copySize = MathUtil::min( pProp->_size != 0 ? pProp->_size : byteSize, byteSize );
        if ( inoutBuffer.size() < pProp->_offset + copySize )
            inoutBuffer.resize( pProp->_offset + copySize, 0 );

        Memory::copy( inoutBuffer.data() + pProp->_offset, pData, copySize );
        return true;
    }

    bool Material::setParameter( IRHIDevice* pRHI, hashed_string name, string_view value )
    {
        MaterialProperty* prop = findProperty( name );
        if ( prop == nullptr )
            return false;
        // 쓰지 못한 글(모르는 enum 이름 등)은 프로퍼티 값으로도 남기지 않는다 — 버퍼와 저장되는 값이 어긋나지 않게.
        string previousValue = std::move( prop->_value );
        prop->_value         = value;
        if ( MaterialUtil::packPropertyIntoBuffer( *prop, _data._bytes ) == false )
        {
            prop->_value = std::move( previousValue );
            return false;
        }
        ++_bufferGeneration;
        if ( pRHI != nullptr && _constantBuffer != 0 )
            pRHI->getResourceFactory()->updateConstantBuffer( _constantBuffer, _data._bytes.data(), static_cast<uint32>( _data._bytes.size() ) );
        _desc._listProperty = _data._listProperty;

        if ( prop->_type == MaterialPropertyType::Keyword || prop->_type == MaterialPropertyType::Bool )
            _bDefinesDirty = SW_TRUE;

        return true;
    }

    bool Material::setTextureParameter( IRHIDevice* pRHI, hashed_string name, RHIDescriptorIndex descIdx )
    {
        for ( MaterialProperty& prop : _data._listProperty )
        {
            if ( hashed_string( prop._name.c_str() ) != name )
                continue;
            if ( MaterialUtil::isTextureType( prop._type ) == false && prop._shaderType != MaterialPropertyType::Uint )
                return false;
            prop._textureIndex = descIdx;
            prop._value        = to_string( descIdx );
            if ( MaterialUtil::packPropertyIntoBuffer( prop, _data._bytes ) == false )
                return false;
            ++_bufferGeneration;
            if ( pRHI != nullptr && _constantBuffer != 0 )
                pRHI->getResourceFactory()->updateConstantBuffer( _constantBuffer, _data._bytes.data(), static_cast<uint32>( _data._bytes.size() ) );
            _desc._listProperty = _data._listProperty;
            return true;
        }
        return false;
    }

    void Material::setQualityLevel( MaterialQualityLevel level )
    {
        if ( _desc._permutations._quality != level )
        {
            _desc._permutations._quality = level;
            _bDefinesDirty               = SW_TRUE;
            MaterialUtil::bumpPermutationGeneration();
        }
    }

    void Material::setStaticSwitch( hashed_string name, bool bEnabled )
    {
        for ( MaterialStaticSwitch& ss : _desc._permutations._listStaticSwitch )
        {
            if ( hashed_string( ss._name.c_str() ) == name || hashed_string( ss._keyword.c_str() ) == name )
            {
                if ( ss._bEnabled != bEnabled )
                {
                    ss._bEnabled   = bEnabled;
                    _bDefinesDirty = SW_TRUE;
                    MaterialUtil::bumpPermutationGeneration();
                }
                return;
            }
        }
        MaterialStaticSwitch entry{};
        entry._name     = name.c_str() ? name.c_str() : "";
        entry._keyword  = entry._name;
        entry._bEnabled = bEnabled;
        _desc._permutations._listStaticSwitch.push_back( std::move( entry ) );
        _bDefinesDirty = SW_TRUE;
    }

    void Material::setMultiCompile( hashed_string name, string_view selectedOption )
    {
        for ( MaterialMultiCompile& mc : _desc._permutations._listMultiCompile )
        {
            if ( hashed_string( mc._name.c_str() ) == name )
            {
                if ( mc._selected != selectedOption )
                {
                    mc._selected   = string( selectedOption );
                    _bDefinesDirty = SW_TRUE;
                    MaterialUtil::bumpPermutationGeneration();
                }
                return;
            }
        }
        MaterialMultiCompile multiCompile{};
        multiCompile._name     = name.c_str() ? name.c_str() : "";
        multiCompile._selected = string( selectedOption );
        if ( selectedOption.empty() == false )
            multiCompile._listOption.push_back( string( selectedOption ) );
        _desc._permutations._listMultiCompile.push_back( std::move( multiCompile ) );
        _bDefinesDirty = SW_TRUE;
    }

    void Material::setBlendMode( RHIBlendMode mode )
    {
        _blendMode       = mode;
        _desc._blendMode = MaterialUtil::blendModeToString( mode );
    }

    bool Material::setScalarParameter( IRHIDevice* pRHI, hashed_string name, float32 value )
    {
        for ( MaterialProperty& prop : _data._listProperty )
        {
            if ( hashed_string( prop._name.c_str() ) != name )
                continue;
            prop._value = to_string( value );
            if ( MaterialUtil::packPropertyIntoBuffer( prop, _data._bytes ) == false )
                return false;
            ++_bufferGeneration;
            if ( pRHI != nullptr && _constantBuffer != 0 )
                pRHI->getResourceFactory()->updateConstantBuffer( _constantBuffer, _data._bytes.data(), static_cast<uint32>( _data._bytes.size() ) );
            return true;
        }
        return false;
    }

    const MaterialProperty* Material::findProperty( hashed_string name ) const
    {
        for ( const MaterialProperty& prop : _data._listProperty )
        {
            if ( hashed_string( prop._name.c_str() ) == name )
                return &prop;
        }
        return nullptr;
    }

    MaterialProperty* Material::findProperty( hashed_string name )
    {
        for ( MaterialProperty& prop : _data._listProperty )
        {
            if ( hashed_string( prop._name.c_str() ) == name )
                return &prop;
        }
        return nullptr;
    }

    const void* Material::getParameterData( string_view name ) const
    {
        for ( const MaterialProperty& prop : _data._listProperty )
        {
            if ( prop._name == name && MaterialUtil::isNonBufferType( prop._type ) == false )
                return _data._bytes.data() + prop._offset;
        }
        return nullptr;
    }

    const vector<string>& Material::getCachedShaderDefines() const
    {
        if ( _bDefinesDirty != SW_FALSE )
        {
            _listCachedDefine.clear();
            const MaterialPermutationDesc& permutationDesc = _desc._permutations;

            for ( const string& defineStr : permutationDesc._listAlwaysDefine )
            {
                MaterialUtil::appendUniqueDefine( _listCachedDefine, defineStr );
            }

            MaterialUtil::appendQualityDefines( permutationDesc._quality, _listCachedDefine );
            MaterialUtil::appendUniqueDefine( _listCachedDefine, string( "SHADER_LOD=" ) + to_string( permutationDesc._shaderLOD ) );
            MaterialUtil::appendUsageDefines( permutationDesc._usage, _listCachedDefine );

            for ( const MaterialStaticSwitch& entry : permutationDesc._listStaticSwitch )
            {
                if ( entry._bEnabled != 0 )
                {
                    if ( entry._keyword.empty() == false )
                        MaterialUtil::appendUniqueDefine( _listCachedDefine, entry._keyword );
                }
                else if ( entry._keywordOff.empty() == false )
                    MaterialUtil::appendUniqueDefine( _listCachedDefine, entry._keywordOff );
            }

            for ( const MaterialMultiCompile& mc : permutationDesc._listMultiCompile )
            {
                if ( mc._selected.empty() == false )
                    MaterialUtil::appendUniqueDefine( _listCachedDefine, mc._selected );
            }

            for ( const MaterialProperty& prop : _data._listProperty )
            {
                if ( prop._type != MaterialPropertyType::Keyword && prop._type != MaterialPropertyType::Bool )
                    continue;
                if ( prop._shaderKeyword.empty() )
                    continue;
                bool bKeywordOn{ false };
                if ( MaterialUtil::parseBoolToken( prop._value, prop._name, bKeywordOn ) && bKeywordOn ) // 읽지 못한 값은 parseBoolToken 이 오류로 남긴다(로드가 이미 거부한다)
                    MaterialUtil::appendUniqueDefine( _listCachedDefine, prop._shaderKeyword );
            }

            std::sort( _listCachedDefine.begin(), _listCachedDefine.end() );
            _cachedPermutationHash = MaterialUtil::hashDefines( _listCachedDefine );
            _bDefinesDirty         = SW_FALSE;
        }
        return _listCachedDefine;
    }

    void Material::setShaderPath( string_view shaderPath )
    {
        if ( _desc._shaderPath == shaderPath )
            return;
        _desc._shaderPath     = shaderPath;
        _bShaderPathHashDirty = SW_TRUE;
    }

    uint64 Material::getShaderPathHash() const
    {
        if ( _bShaderPathHashDirty != SW_FALSE )
        {
            _cachedShaderPathHash = HashUtil::computeStringHash64( _desc._shaderPath, false, HashUtil::kFnvOffset64 );
            _bShaderPathHashDirty = SW_FALSE;
        }
        return _cachedShaderPathHash;
    }

    uint64 Material::getPermutationHash() const
    {
        if ( _bDefinesDirty != SW_FALSE )
            getCachedShaderDefines();
        return _cachedPermutationHash;
    }

    bool Material::getScalarParameter( hashed_string name, float32& outValue ) const
    {
        for ( const MaterialProperty& prop : _data._listProperty )
        {
            if ( hashed_string( prop._name.c_str() ) != name )
                continue;
            if ( prop._offset + 4 > _data._bytes.size() )
                return false;
            Memory::copy( &outValue, _data._bytes.data() + prop._offset, 4 );
            return true;
        }
        return false;
    }

} // namespace sw
