#include "pch.h"

#include "Engine/Graphics/Material/Material.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Hash/HashUtil.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Asset/AssetLoadProfiler.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Material/MaterialPacking.h"
#include "Engine/Graphics/Material/MaterialParameterCollection.h"
#include "Engine/Graphics/Material/MaterialPermutation.h"
#include "Engine/Graphics/Material/MaterialRenderProxy.h"
#include "Engine/Graphics/Material/MaterialXML.h"
#include "Engine/Graphics/RHI/Diagnostics/BindlessIndexValidator.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionData.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

namespace sw
{
    MaterialProperty::MaterialProperty() noexcept
        : _type{ MaterialPropertyType::Unknown }
        , _shaderType{ MaterialPropertyType::Unknown }
        , _min{ 0.0f }
        , _max{ 1.0f }
        , _offset{ 0 }
        , _size{ 0 }
        , _textureIndex{ kInvalidDescriptorIndex }
        , _bHDR{ SW_FALSE }
        , _bSRGB{ SW_TRUE }
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

    bool MaterialPermutationDef::hasAlwaysDefine( string_view define ) const
    {
        return VectorUtil::containsIf( _listAlwaysDefine, [define]( const string& item )
        { return string_view( item ) == define; } );
    }

    Material* Material::resolveDrawMaterial( Material* pOwn, const MaterialInstance* pInstance, Material* pSceneDefault )
    {
        Material* pMaterial = pOwn;
        if ( pMaterial == nullptr && pInstance != nullptr )
            pMaterial = pInstance->getParent();
        if ( pMaterial == nullptr )
            pMaterial = pSceneDefault;
        return pMaterial;
    }

    shared_ptr<Material> Material::create()
    {
        return sw::make_shared<Material>( CreateKey{} );
    }

    Material::Material( CreateKey )
        : _assetPath{}
        , _desc{}
        , _data{}
        , _renderProxy{ MaterialRenderProxy::create() }
        , _renderData{}
        , _renderDataGeneration{ 0 }
        , _elementStride{ 0 }
        , _bufferGeneration{}
        , _shaderLayoutCacheGeneration{ 0 }
        , _shaderLayoutBackend{}
        , _pRHIDevice{ nullptr }
        , _textureReloadGeneration{ 0 }
        , _listAcquiredTexturePath{}
        , _listTextureLoad{}
        , _listBorrowedTexturePath{}
        , _listMaterialTextureSRV{}
        , _listMaterialTextureName{}
        , _listCollectionBinding{}
        , _listPropertyName{}
        , _listCachedDefine{}
        , _cachedPermutationHash{ 0 }
        , _cachedShaderPathHash{ 0 }
        , _bDefinesDirty{ SW_TRUE }
        , _bShaderPathHashDirty{ SW_TRUE }
        , _bShaderLayoutSynced{ SW_FALSE }
        , _reservedMaterial{ 0 }
    {
    }

    Material::~Material()
    {
        if ( _pRHIDevice != nullptr )
            releaseRHI( _pRHIDevice );
    }

    bool Material::initialize( IRHIDevice* pRHI, const AssetID& assetID )
    {
        const string_view assetRelativePath = assetID.view();
        if ( pRHI == nullptr )
            return false;

        // 결과가 정해지기 전에는 그릴 수 없다. 어느 실패 경로로 나가든 마지막 값은 아래 `finishInitState` 가 정한다.
        setInitState( MaterialInitState::Pending );
        struct InitStateFinish
        {
            Material& _material;
            bool      _bSucceeded{ false };
            ~InitStateFinish() { _material.setInitState( _bSucceeded ? MaterialInitState::Ready : MaterialInitState::Failed ); }
        } finishInitState{ *this };

        _pRHIDevice = pRHI;
        _assetPath  = assetRelativePath;

        // 못 읽은 머티리얼은 빈 값으로 올리지 않고 실패한다. 대신 그릴 것(마젠타 `missingmaterial`)은 부르는 쪽 한 곳이 정한다(`MeshComponent::resolveMaterialAsset`).
        AssetLoadScope loadScope( "Material", assetRelativePath );
        if ( loadFromAsset( assetID ) == false )
        {
            SW_LOG_ERROR_ID( "Material.MaterialFileCouldNot", "Material file '%#' could not be loaded", assetRelativePath );
            return false;
        }
        loadScope.beginPhase( AssetLoadPhase::Upload );

        // 상수 버퍼는 256 바이트 단위다(D3D12 CBV 정렬). 파라미터가 없는 머티리얼도 한 단위를 잡는다 — 셰이더가 원소 하나를 읽는다.
        constexpr uint32 kConstantBufferAlignment = 256u;
        const uint32     bufferSize               = MathUtil::align( MathUtil::max( static_cast<uint32>( _data._bytes.size() ), 1u ), kConstantBufferAlignment );
        _data._bytes.resize( bufferSize, 0 );
        _bufferGeneration.bump(); // 크기를 256 정렬로 맞췄다

        if ( resolveTextureAssets( pRHI ) == false )
        {
            releaseRHI( pRHI ); // 반쯤 빌린 텍스처를 남기지 않는다
            return false;
        }

        // 셰이더 레이아웃도 여기(게임 스레드)서 맞춘다. 결과(레거시 MaterialCB 는 stride 0)는 사본(`getRenderBinding`)이 실어 간다.
        (void)ensureShaderLayout( pRHI->getBackendType() );

        SW_LOG_INFO_ID( "Material.InitializedBytes", "Initialized '%#' (%# bytes, element stride %#)", _desc._name.c_str(), _data._bytes.size(), _elementStride );
        loadScope.setBytes( _data._bytes.size() );
        loadScope.setSucceeded();
        finishInitState._bSucceeded = true;
        return true;
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
            if ( MaterialPacking::isTextureType( prop._type ) == false )
                continue;
            if ( prop._assetPath.empty() )
            {
                // 텍스처가 붙지 않은 필드는 머티리얼 데이터에 kInvalidDescriptorIndex 로 실려 셰이더로 간다(`BindlessIndexCheck::TraceAbsent` 면 자리를 남긴다).
                if ( BindlessIndexValidator::isTracingAbsent() )
                    (void)pRHI->getBindlessIndexReportLog().recordAbsent( hashed_string( _desc._name.c_str() ), hashed_string( prop._name.c_str() ), 0 ); // 자리마다 한 번만 남긴다
                continue;
            }
            const uint32 ordinal = static_cast<uint32>( _listMaterialTextureSRV.size() );
            if ( bNativeBindless == false && ordinal >= shaderslot::kMaterialTextureCount )
            {
                SW_LOG_WARNING_ID( "Material.MaterialBackendBindsMost", "Material '%#': this backend binds at most %# material textures - '%#' stays white.",
                                   _desc._name.c_str(), shaderslot::kMaterialTextureCount, prop._name.c_str() );
                continue;
            }

            // 진짜 텍스처는 늘 워커가 읽고, 다 읽기 전에는 로드 중 텍스처(회색)를 그리다가 `refreshTextureBindings` 가 바꾼다 — 언리얼 · 유니티도 스트리밍이 끝날
            // 때까지 기본 텍스처를 그린다(초기화하는 스레드가 디스크를 기다리지 않는다). 못 읽으면 마젠타 체커를 빌린다 — 흰색이면 화면에서 빠진 것을 알 수 없다(Shipping 도 같다).
            TextureHandle    load = textures.request( prop._assetPath, AssetLoadStart::Async );
            string           borrowedPath;
            const Texture2D* pTexture = load.isReady() ? load.getOperation()->getTexture() : nullptr;
            if ( pTexture == nullptr )
            {
                const bool bLoading = load.isLoading();
                if ( bLoading == false )
                {
                    SW_LOG_ERROR_ID( "Material.MaterialTextureCouldNot", "Material '%#': texture '%#' for '%#' could not be loaded (%#) — sampling the missing-texture checker.", _desc._name.c_str(),
                                     prop._assetPath.c_str(), prop._name.c_str(), load.getError().c_str() );
                    load.reset();
                }
                borrowedPath = bLoading ? engine::getSubsystem<EngineDefaultAssets>()._loadingTexture.get() : engine::getSubsystem<EngineDefaultAssets>()._missingTexture.get();
                pTexture     = textures.acquire( borrowedPath );
                if ( pTexture == nullptr )
                {
                    // 로드 중 텍스처와 체커는 엔진 필수 데이터다. 그것마저 없으면 흰색으로 그리지 않고 머티리얼 초기화가 실패한다.
                    SW_LOG_ERROR_ID( "Material.MaterialTextureNotReady", "Material '%#': texture '%#' for '%#' is not ready and the stand-in texture '%#' could not be loaded either", _desc._name.c_str(),
                                     prop._assetPath.c_str(), prop._name.c_str(), borrowedPath.c_str() );
                    return false;
                }
            }

            _listAcquiredTexturePath.push_back( prop._assetPath );
            _listTextureLoad.push_back( std::move( load ) );
            _listBorrowedTexturePath.push_back( std::move( borrowedPath ) );
            _listMaterialTextureSRV.push_back( pTexture->getSRV() );
            _listMaterialTextureName.emplace_back( prop._name.c_str() );
            setTextureParameter( hashed_string( prop._name.c_str() ), bNativeBindless ? pTexture->getSRV() : ordinal );
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
            const bool bOrdinalOutOfRange = ordinal >= _listAcquiredTexturePath.size() || ordinal >= _listBorrowedTexturePath.size() || ordinal >= _listMaterialTextureSRV.size();
            if ( bOrdinalOutOfRange )
                break;
            if ( MaterialPacking::isTextureType( prop._type ) == false || prop._assetPath != _listAcquiredTexturePath[ordinal] )
                continue;
            resolveArrivedTexture( textures, ordinal );
            const string&            boundPath = _listBorrowedTexturePath[ordinal].empty() ? _listAcquiredTexturePath[ordinal] : _listBorrowedTexturePath[ordinal];
            const Texture2D*         pTexture  = textures.find( boundPath );
            const RHIDescriptorIndex srv       = pTexture != nullptr ? pTexture->getSRV() : kInvalidDescriptorIndex;
            if ( srv != _listMaterialTextureSRV[ordinal] )
            {
                _listMaterialTextureSRV[ordinal] = srv;
                if ( bNativeBindless )
                    setTextureParameter( hashed_string( prop._name.c_str() ), srv );
                else
                    _bufferGeneration.bump(); // 슬롯 바인딩 백엔드는 슬롯 목록만 바뀐다 — 렌더 사본이 그 목록을 싣는다
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
            SW_LOG_ERROR_ID( "Material.MaterialTextureCouldNot2", "Material '%#': texture '%#' could not be loaded (%#) — sampling the missing-texture checker.", _desc._name.c_str(),
                             _listAcquiredTexturePath[ordinal].c_str(), load.getError().c_str() );
            load.reset();
            borrowed = engine::getSubsystem<EngineDefaultAssets>()._missingTexture.get();
            if ( textures.acquire( borrowed ) == nullptr )
            {
                // 체커는 엔진 필수 데이터다 — 못 읽으면 로드 중 텍스처를 그대로 둔다(오류는 남았다).
                SW_LOG_ERROR_ID( "Material.MaterialMissingTextureChecker", "Material '%#': the missing-texture checker '%#' could not be loaded either", _desc._name.c_str(), borrowed.c_str() );
                borrowed = std::move( standIn );
                return;
            }
        }
        textures.release( standIn );
    }

    optional<uint32> Material::findTextureSlot( hashed_string name ) const
    {
        for ( size_t slot = 0; slot < _listMaterialTextureName.size(); ++slot )
        {
            if ( _listMaterialTextureName[slot] == name )
                return static_cast<uint32>( slot );
        }
        return nullopt;
    }

    void Material::collectTexturePaths( vector<string>& outListPath ) const
    {
        for ( const MaterialProperty& prop : _data._listProperty )
        {
            if ( MaterialPacking::isTextureType( prop._type ) && prop._assetPath.empty() == false )
                outListPath.push_back( prop._assetPath );
        }
    }

    void Material::releaseTextureAssets()
    {
        if ( _listAcquiredTexturePath.empty() )
            return;
        if ( engine::areEngineServicesBound() )
        {
            TextureCache& textures = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
            for ( const string& path : _listBorrowedTexturePath )
            {
                if ( path.empty() == false )
                    textures.release( path );
            }
        }
        _listAcquiredTexturePath.clear();
        _listTextureLoad.clear(); // 진짜 텍스처의 참조는 핸들이 쥐었다 — 놓으면 캐시에 돌려준다
        _listBorrowedTexturePath.clear();
        _listMaterialTextureSRV.clear();
        _listMaterialTextureName.clear();
        _bufferGeneration.bump(); // 슬롯 목록이 비었다 — 놓은 텍스처의 SRV 를 렌더 사본에 남기지 않는다
        for ( MaterialProperty& prop : _data._listProperty )
        {
            if ( MaterialPacking::isTextureType( prop._type ) == false || prop._assetPath.empty() )
                continue;
            // _value 도 비운다. setTextureParameter 가 인덱스를 문자열로도 남기므로, 그대로 두면
            // 다음 패킹이 이미 해제된 인덱스를 숫자 오버라이드로 복원한다.
            prop._textureIndex = kInvalidDescriptorIndex;
            prop._value.clear();
        }
    }

    bool Material::initRHI( IRHIDevice* pDevice )
    {
        // 통보 순서는 정해져 있지 않다. 이미 초기화되어 있으면 그대로 둔다.
        if ( getInitState() == MaterialInitState::Ready )
            return true;
        // 한 번도 initialize 되지 않은 머티리얼이다. 복원할 내용 자체가 없다.
        if ( pDevice == nullptr || _assetPath.empty() )
            return true;
        return initialize( pDevice, AssetID{ _assetPath } );
    }

    void Material::clearRHI( IRHIDevice* pDevice )
    {
        if ( _pRHIDevice != pDevice )
            return;

        // 빌린 텍스처도 **여기서 놓는다.** 널 디바이스로 부르면 `TextureCache` 는 참조 수만 줄이고
        // GPU 호출은 하지 않는다(디바이스가 이미 없으므로 그것이 맞다).
        //
        // 주의: `releaseRHI` 와 이 함수는 같은 상태를 남겨야 한다. 목록이 남은 채 `initRHI` 가 오면 `resolveTextureAssets` 가
        // 목록에 **덧붙여** `ordinal` 이 0 이 아닌 값에서 시작하고, 네이티브 bindless 가 없는 백엔드(DX11 · GL)는 그 서수를
        // t16..t31 고정 슬롯 번호로 쓰므로 **엉뚱한 텍스처를 읽거나**, 한도를 넘어 흰색으로 남는다.
        releaseTextureAssets();
        // 상수 버퍼는 프록시의 것이다 — 프록시가 같은 통보를 받는다.
        _pRHIDevice = nullptr;
        setInitState( MaterialInitState::NotRequested );
    }

    void Material::releaseRHI( IRHIDevice* pRHI )
    {
        // 남의 디바이스가 죽는 통보라면 내 것이 아니다.
        const bool bDeviceNotMine = pRHI != nullptr && _pRHIDevice != nullptr && _pRHIDevice != pRHI;
        if ( bDeviceNotMine )
            return;

        releaseTextureAssets();
        // 상수 버퍼는 프록시의 것이다 — 프록시가 같은 통보를 받는다.
        _pRHIDevice = nullptr;
        setInitState( MaterialInitState::NotRequested );
    }

    void Material::syncRenderState()
    {
        // 레이아웃을 셰이더에 맞춘다 — 다시 로드(`loadFromXML`, 에디터 미리보기)는 XML 순서로 다시 쌓고, 셰이더 리플렉션 캐시가 비면(핫 리로드)
        // 맞춘 값이 낡는다. 맞춰져 있으면 비교만 한다. 디바이스가 없는(초기화 전) 머티리얼은 그리지 않으므로(`MaterialInitState`) 맞출 백엔드가 없다.
        if ( _pRHIDevice != nullptr )
            (void)ensureShaderLayout( _pRHIDevice->getBackendType() ); // 실패 오류는 맞추는 쪽이 남겼고, 바이트가 비어 렌더 스레드가 건너뛴다
        (void)refreshTextureBindings();                                // 다시 올리거나 비동기 로드가 끝난 텍스처의 새 SRV(캐시 세대가 그대로면 비교만)
        syncParameterCollections();                                    // 묶음의 값이 바뀌었으면 프로퍼티를 다시 패킹한다(묶은 칸이 없으면 빈 순회)
    }

    void Material::rebuildCollectionBindings()
    {
        _listCollectionBinding.clear();
        for ( const MaterialProperty& prop : _data._listProperty )
        {
            if ( prop._collectionPath.empty() )
                continue;
            CollectionBinding& binding = _listCollectionBinding.emplace_back();
            binding._property          = hashed_string( string_view( prop._name ) );
            binding._parameter         = hashed_string( string_view( prop._collectionParameter ) );
            binding._path              = prop._collectionPath;
        }
    }

    void Material::syncParameterCollections()
    {
        for ( CollectionBinding& binding : _listCollectionBinding )
        {
            if ( binding._bFailed )
                continue;
            if ( binding._collection == nullptr )
                binding._collection = MaterialParameterCollection::acquire( AssetID{ binding._path } );
            binding._bFailed = binding._collection == nullptr ? SW_TRUE : SW_FALSE; // 이유는 읽기가 남겼다 — 프로퍼티는 파일의 값으로 그린다
            // 레이아웃 오류로 바이트를 비웠으면 쓰지 않는다 — 틀린 바이트가 GPU 에 올라가지 않게 한 상태를 되살리지 않는다.
            if ( binding._bFailed || binding._appliedGeneration == binding._collection->getGeneration() || _data._bytes.empty() )
                continue;
            const float4*           pValue = binding._collection->findValue( binding._parameter );
            const MaterialProperty* pProp  = findProperty( binding._property );
            uint32                  count{ 0 };
            if ( pProp != nullptr )
            {
                const bool bScalar = pProp->_type == MaterialPropertyType::Float || pProp->_type == MaterialPropertyType::Range;
                const bool bVector = pProp->_type == MaterialPropertyType::Float4 || pProp->_type == MaterialPropertyType::Color;
                count              = bScalar ? 1u : pProp->_type == MaterialPropertyType::Float2 ? 2u
                                              : pProp->_type == MaterialPropertyType::Float3     ? 3u
                                              : bVector                                          ? 4u
                                                                                                 : 0u;
            }
            if ( pValue == nullptr || count == 0 )
            {
                SW_LOG_ERROR_ID( "Material.CollectionBindingInvalid", "Material '%#': property '%#' binds '%#' of '%#' - no such parameter, or the property is not a float, vector or colour", _desc._name, binding._property.c_str(), binding._parameter.c_str(), binding._path );
                binding._bFailed = SW_TRUE;
                continue;
            }
            if ( setParameter( binding._property, MaterialParameterCollection::makePropertyText( *pValue, count ) ) )
                binding._appliedGeneration = binding._collection->getGeneration();
            else
                binding._bFailed = SW_TRUE; // 패커가 이유를 남겼다
        }
    }

    MaterialRenderBinding Material::getRenderBinding()
    {
        syncRenderState();
        if ( _renderData == nullptr || _renderDataGeneration != _bufferGeneration.get() )
        {
            shared_ptr<MaterialRenderData> data = sw::make_shared<MaterialRenderData>();
            data->_bytes                        = _data._bytes;
            data->_elementStride                = _elementStride;
            const size_t slotCount              = MathUtil::min( _listMaterialTextureSRV.size(), static_cast<size_t>( shaderslot::kMaterialTextureCount ) );
            for ( size_t slot = 0; slot < slotCount; ++slot )
            {
                data->_arrTextureSRV[slot] = _listMaterialTextureSRV[slot];
            }
            _renderData           = std::move( data );
            _renderDataGeneration = _bufferGeneration.get();
        }
        return MaterialRenderBinding{ _renderProxy, _renderData };
    }

    bool Material::ensureShaderLayout( RHIBackend backend )
    {
        // 머티리얼 바이트의 기준은 .material 의 프로퍼티 순서가 아니라 **셰이더의 MaterialData 원소 레이아웃**이다(언리얼도
        // 머티리얼 파라미터 레이아웃을 셰이더에서 가져온다). 로드 경로에서도 맞춰야 한다 — XML 순서로 패킹하면 stride 가 0 이고,
        // 그러면 GPUScene 이 CB 크기(256)를 stride 로 써서 원소 1 부터 어긋난다.
        //
        // "맞췄다" 표시는 다시 로드(XML 순서로 다시 쌓는다)가 풀고(applyDescToRuntime), 리플렉션 캐시를 비우면 낡는다.
        // 표시가 남으면 XML 순서 바이트와 옛 stride 가 함께 올라간다.
        if ( _desc._shaderPath.empty() )
            return false;
        if ( isShaderLayoutSynced( backend ) )
            return _elementStride != 0;
        _bShaderLayoutSynced         = SW_TRUE;
        _shaderLayoutBackend         = backend;
        _shaderLayoutCacheGeneration = ShaderReflectionLibrary::getCacheGeneration();

        ShaderReflectionData reflection{};
        if ( reflectMaterialSchema( _desc._shaderPath, RHI::getShaderTargetFormat( backend ), reflection ) == false )
        {
            SW_LOG_ERROR_ID( "Material.ShaderReflectionMaterialNot", "Shader reflection for material '%#' not found ('%#') - clearing the bytes and not uploading.", _desc._name.c_str(), _desc._shaderPath.c_str() );
            discardBytesAfterLayoutError();
            return false;
        }
        const bool bAllPacked = syncPropertiesFromReflection( reflection );
        return bAllPacked && _elementStride != 0;
    }

    bool Material::reflectMaterialSchema( const string& shaderPath, ShaderTargetFormat targetFormat, ShaderReflectionData& outReflection )
    {
        ShaderCompileDesc desc{};
        desc._source             = ShaderSourceID{ shaderPath };
        desc._entryPoint         = getShaderStageInfo( ShaderStage::Pixel )._pEntryPoint;
        desc._stage              = ShaderStage::Pixel;
        desc._targetFormat       = targetFormat;
        ShaderCache& shaderCache = engine::getSubsystem<ShaderCache>();
        if ( shaderCache.getReflection( desc, outReflection ) == false )
            return false;
        // 머티리얼을 정점 셰이더만 읽는 셰이더(바람에 흔드는 식생 · 파도를 옮기는 물)는 픽셀 스테이지에 스키마가 없다 — 정점 스테이지에서 찾는다.
        // 두 스테이지가 함께 읽는 셰이더는 GL(ARB_gl_spirv)이 링크를 거절하므로 스키마는 한 스테이지에만 있다.
        if ( hasMaterialSchema( outReflection ) == false )
        {
            ShaderCompileDesc    vertexDesc = desc;
            ShaderReflectionData vertexReflection{};
            vertexDesc._entryPoint = getShaderStageInfo( ShaderStage::Vertex )._pEntryPoint;
            vertexDesc._stage      = ShaderStage::Vertex;
            if ( shaderCache.getReflection( vertexDesc, vertexReflection ) && hasMaterialSchema( vertexReflection ) )
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
            SW_LOG_ERROR_ID( "Material.NoSchema", "The shader of material '%#' has no material schema ('%#') - declare SW_MATERIAL_BEGIN/END or MaterialCB.",
                             _desc._name.c_str(), _desc._shaderPath.c_str() );
            discardBytesAfterLayoutError();
            return false;
        }

        if ( _data._listProperty.empty() )
        {
            fillPropertiesFromSchema( *pSchemaCb );
            _bufferGeneration.bump(); // 위에서 stride 가 바뀌었다 — 사본이 stride 를 싣는다(예전에는 다음 값 변경까지 옛 stride 사본이 남을 수 있었다)
            return true;
        }
        const bool bAllPacked = matchPropertiesToSchema( *pSchemaCb );
        packPropertiesIntoBytes( pSchemaCb->_totalSize );
        _desc._listProperty = _data._listProperty;
        // 불일치는 오류다 — 일부만 맞춘 바이트(적지 않은 칸의 0 = Vulkan 의 첫 실제 텍스처)를 올리지 않는다. 바이트가 비면 렌더 사본도 비어
        // 렌더 스레드가 그 원소와 상수 버퍼를 건너뛴다(인스턴스 사본도 부모 바이트에서 만든다).
        if ( bAllPacked == false )
            discardBytesAfterLayoutError();
        return bAllPacked;
    }

    void Material::discardBytesAfterLayoutError()
    {
        _data._bytes.clear();
        _elementStride = 0;
        _bufferGeneration.bump();
    }

    void Material::fillPropertiesFromSchema( const ShaderBufferInfo& schema )
    {
        for ( const ShaderVariableInfo& var : schema._listVariable )
        {
            if ( MaterialPacking::isPaddingMember( var._name ) )
                continue; // 채움 칸은 머티리얼 속성이 아니다
            MaterialProperty prop{};
            prop._name       = var._name;
            prop._offset     = var._offset;
            prop._size       = var._size;
            prop._shaderType = MaterialPacking::shaderTypeFromReflectionName( var._type, var._size );
            prop._type       = prop._shaderType;
            // 텍스처 인덱스 필드(이름 규칙 — MaterialPacking::isTextureIndexMember)는 텍스처 속성으로 둔다. uint 로 두면 0 이 들어가는데,
            // 0 은 Vulkan 의 첫 실제 텍스처 · DX11 · GL 의 서수 0 이다. 텍스처 속성은 붙은 것이 없으면 kInvalidDescriptorIndex 를 넣는다.
            if ( MaterialPacking::isTextureIndexMember( var._name, var._type ) )
                prop._type = MaterialPropertyType::Texture2D;
            _data._listProperty.push_back( prop );
        }
        rebuildPropertyNames();
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
        SW_LOG_TRACE_ID( "Material.FilledPropertiesShaderReflection", "Filled %# properties from shader reflection.", _data._listProperty.size() );
    }

    bool Material::matchPropertiesToSchema( const ShaderBufferInfo& schema )
    {
        bool bAllPacked{ true };
        for ( MaterialProperty& prop : _data._listProperty )
        {
            // 키워드 · UI 전용 필드는 머티리얼 원소의 멤버가 아니다.
            // 텍스처는 멤버다(`uint ...Map` — 인덱스 · 서수). 다른 프로퍼티처럼 리플렉션 오프셋을 받는다 — 건너뛰면 XML 순서로 쌓은 자리에
            // 남아, XML 순서가 셰이더 구조체와 다른 머티리얼(toon)에서 인덱스가 남의 칸에 들어가고 제 칸은 0(첫 실제 텍스처)이 된다.
            if ( MaterialPacking::isNonBufferType( prop._type ) )
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
                SW_LOG_ERROR_ID( "Material.PropertyNotInShader", "Material '%#' lists property '%#' that the shader struct does not have - remove it from the .material file (%#)",
                                 _desc._name.c_str(), prop._name.c_str(), _desc._shaderPath.c_str() );
                bAllPacked = false;
                continue;
            }
            const ShaderVariableInfo& var = *pVar;
            // 칸 형식은 셰이더가 정한다 — `.material` 에는 적지 않는다(언리얼 머티리얼 파라미터 · 유니티 ShaderLab Properties 도 칸 형식을 셰이더에서 얻는다). 리플렉션 전의 기본 형식(`defaultShaderTypeFor`)은 여기서 바뀐다.
            const MaterialPropertyType reflected = MaterialPacking::shaderTypeFromReflectionName( var._type, var._size );
            if ( reflected != MaterialPropertyType::Unknown )
                prop._shaderType = reflected;
            else if ( MaterialPacking::packedSizeOf( prop._shaderType ) != 0 && MaterialPacking::packedSizeOf( prop._shaderType ) != var._size )
            {
                SW_LOG_ERROR_ID( "Material.MaterialPropertySizeMismatch", "Material '%#' property '%#' size mismatch (shaderType %# bytes vs shader %#, %#)",
                                 _desc._name.c_str(), prop._name.c_str(), MaterialPacking::packedSizeOf( prop._shaderType ), var._size, _desc._shaderPath.c_str() );
                bAllPacked = false;
            }
            prop._offset = var._offset;
            prop._size   = var._size;
        }
        // 셰이더 구조체엔 있는데 머티리얼에 없는 필드는 값을 적은 사람이 없다 — 0 으로 채우지 않고 알린다(텍스처 인덱스의 0 은 Vulkan 의 첫 실제 텍스처다).
        for ( const ShaderVariableInfo& var : schema._listVariable )
        {
            bool bListed = MaterialPacking::isPaddingMember( var._name ); // 채움 칸은 적지 않는다
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
                SW_LOG_ERROR_ID( "Material.ShaderFieldNotListed", "Material '%#' does not list shader field '%#' - add it to the .material file (%#)", _desc._name.c_str(), var._name.c_str(), _desc._shaderPath.c_str() );
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
            if ( MaterialPacking::isNonBufferType( prop._type ) == false )
                maxEnd = MathUtil::max( maxEnd, prop._offset + prop._size );
        }
        _data._bytes.assign( maxEnd, 0 );
        for ( MaterialProperty& prop : _data._listProperty )
        {
            MaterialPacking::packPropertyIntoBuffer( prop, _data._bytes );
        }

        const uint32 alignedTotal = MathUtil::align( static_cast<uint32>( _data._bytes.size() ), 256u );
        _data._bytes.resize( alignedTotal, 0 );
        _bufferGeneration.bump();
    }

    bool Material::rebuildPackedBuffer()
    {
        _data._bytes.clear();

        for ( MaterialProperty& prop : _data._listProperty )
        {
            if ( MaterialPacking::isNonBufferType( prop._type ) )
            {
                prop._offset = 0;
                prop._size   = 0;
                continue;
            }

            if ( prop._shaderType == MaterialPropertyType::Unknown )
                prop._shaderType = MaterialPacking::defaultShaderTypeFor( prop._type );

            // 이 순회는 비버퍼 타입을 0 으로 되돌리고 셰이더 타입을 채우는 것까지다. 오프셋 패킹은 아래 두 번째 순회가 한다.
        }

        bool anyExplicitOffset{ false };
        for ( const MaterialProperty& prop : _data._listProperty )
        {
            if ( MaterialPacking::isNonBufferType( prop._type ) == false && prop._offset != 0 )
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
            if ( MaterialPacking::isNonBufferType( prop._type ) )
                continue;

            if ( prop._shaderType == MaterialPropertyType::Unknown )
                prop._shaderType = MaterialPacking::defaultShaderTypeFor( prop._type );

            uint32 packSize = MaterialPacking::packedSizeOf( prop._shaderType );
            if ( prop._size != 0 )
                packSize = prop._size;
            if ( packSize == 0 )
                packSize = 4;
            prop._size = packSize;

            if ( useSequential )
            {
                currentOffset = MaterialPacking::alignOffset( currentOffset, packSize );
                prop._offset  = currentOffset;
                currentOffset += packSize;
            }

            maxEnd = MathUtil::max( maxEnd, prop._offset + prop._size );
        }

        _data._bytes.assign( maxEnd, 0 );
        bool bAllPacked{ true };
        for ( MaterialProperty& prop : _data._listProperty )
        {
            if ( MaterialPacking::packPropertyIntoBuffer( prop, _data._bytes ) == false )
                bAllPacked = false;
        }

        const uint32 alignedTotal = MathUtil::align( static_cast<uint32>( _data._bytes.size() ), 256u );
        if ( alignedTotal > _data._bytes.size() )
            _data._bytes.resize( alignedTotal, 0 );
        _bufferGeneration.bump();

        _desc._listProperty = _data._listProperty;
        return bAllPacked;
    }

    bool Material::resetParameterToDefault( hashed_string name )
    {
        MaterialProperty* prop = findProperty( name );
        if ( prop == nullptr )
            return false;
        return setParameter( name, prop->_defaultValue );
    }

    bool Material::packNamedValueIntoBuffer( hashed_string name, string_view value, vector<uint8>& inoutBuffer ) const
    {
        const MaterialProperty* pProp = findProperty( name );
        if ( pProp == nullptr )
            return false;
        return MaterialPacking::packValueIntoBuffer( *pProp, value, inoutBuffer );
    }

    bool Material::packNamedFloatsIntoBuffer( hashed_string name, const float32* pValue, uint32 count, vector<uint8>& inoutBuffer ) const
    {
        const MaterialProperty* pProp = findProperty( name );
        if ( pProp == nullptr )
            return false;
        return MaterialPacking::packFloatsIntoBuffer( *pProp, pValue, count, inoutBuffer );
    }

    bool Material::packTextureIntoBuffer( hashed_string name, RHIDescriptorIndex descIdx, vector<uint8>& inoutBuffer ) const
    {
        const MaterialProperty* pProp = findProperty( name );
        if ( pProp == nullptr || MaterialPacking::isNonBufferType( pProp->_type ) )
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
        if ( pProp == nullptr || MaterialPacking::isNonBufferType( pProp->_type ) )
            return false;

        const uint32 copySize = MathUtil::min( pProp->_size != 0 ? pProp->_size : byteSize, byteSize );
        if ( inoutBuffer.size() < pProp->_offset + copySize )
            inoutBuffer.resize( pProp->_offset + copySize, 0 );

        Memory::copy( inoutBuffer.data() + pProp->_offset, pData, copySize );
        return true;
    }

    bool Material::setParameter( hashed_string name, string_view value )
    {
        MaterialProperty* prop = findProperty( name );
        if ( prop == nullptr )
            return false;
        // 쓰지 못한 글(모르는 enum 이름 등)은 프로퍼티 값으로도 남기지 않는다 — 버퍼와 저장되는 값이 어긋나지 않게.
        string previousValue = std::move( prop->_value );
        prop->_value         = value;
        if ( MaterialPacking::packPropertyIntoBuffer( *prop, _data._bytes ) == false )
        {
            prop->_value = std::move( previousValue );
            return false;
        }
        _bufferGeneration.bump();
        _desc._listProperty = _data._listProperty;

        if ( prop->_type == MaterialPropertyType::Keyword || prop->_type == MaterialPropertyType::Bool )
            _bDefinesDirty = SW_TRUE;

        return true;
    }

    bool Material::setTextureParameter( hashed_string name, RHIDescriptorIndex descIdx )
    {
        MaterialProperty* pProp = findProperty( name );
        if ( pProp == nullptr )
            return false;
        if ( MaterialPacking::isTextureType( pProp->_type ) == false && pProp->_shaderType != MaterialPropertyType::Uint )
            return false;
        pProp->_textureIndex = descIdx;
        pProp->_value        = to_string( descIdx );
        if ( MaterialPacking::packPropertyIntoBuffer( *pProp, _data._bytes ) == false )
            return false;
        _bufferGeneration.bump();
        _desc._listProperty = _data._listProperty;
        return true;
    }

    void Material::setQualityLevel( MaterialQualityLevel level )
    {
        if ( _desc._permutations._quality != level )
        {
            _desc._permutations._quality = level;
            _bDefinesDirty               = SW_TRUE;
            MaterialPermutation::bumpPermutationGeneration();
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
                    MaterialPermutation::bumpPermutationGeneration();
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
                    MaterialPermutation::bumpPermutationGeneration();
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

    bool Material::setScalarParameter( hashed_string name, float32 value )
    {
        MaterialProperty* pProp = findProperty( name );
        if ( pProp == nullptr )
            return false;
        pProp->_value = to_string( value );
        if ( MaterialPacking::packPropertyIntoBuffer( *pProp, _data._bytes ) == false )
            return false;
        _bufferGeneration.bump();
        return true;
    }

    const MaterialProperty* Material::findProperty( hashed_string name ) const
    {
        for ( size_t index = 0; index < _listPropertyName.size(); ++index )
        {
            if ( _listPropertyName[index] == name )
                return &_data._listProperty[index];
        }
        return nullptr;
    }

    MaterialProperty* Material::findProperty( hashed_string name )
    {
        for ( size_t index = 0; index < _listPropertyName.size(); ++index )
        {
            if ( _listPropertyName[index] == name )
                return &_data._listProperty[index];
        }
        return nullptr;
    }

    const void* Material::getParameterData( string_view name ) const
    {
        for ( const MaterialProperty& prop : _data._listProperty )
        {
            if ( prop._name == name && MaterialPacking::isNonBufferType( prop._type ) == false )
                return _data._bytes.data() + prop._offset;
        }
        return nullptr;
    }

    const vector<string>& Material::getCachedShaderDefines() const
    {
        if ( _bDefinesDirty != SW_FALSE )
        {
            _listCachedDefine.clear();
            const MaterialPermutationDef& permutationDesc = _desc._permutations;

            for ( const string& defineStr : permutationDesc._listAlwaysDefine )
            {
                MaterialPermutation::appendUniqueDefine( _listCachedDefine, defineStr );
            }

            MaterialPermutation::appendQualityDefines( permutationDesc._quality, _listCachedDefine );
            MaterialPermutation::appendUniqueDefine( _listCachedDefine, string( "SHADER_LOD=" ) + to_string( permutationDesc._shaderLOD ) );
            MaterialPermutation::appendUsageDefines( permutationDesc._usage, _listCachedDefine );

            // 칸이 내는 define — 쿠커도 이 목록을 읽는다.
            if ( _desc._alphaMode == MaterialAlphaMode::Mask )
                MaterialPermutation::appendUniqueDefine( _listCachedDefine, kMaterialAlphaCutoffDefine );
            if ( _desc._bTwoSidedLighting != SW_FALSE )
                MaterialPermutation::appendUniqueDefine( _listCachedDefine, kMaterialTwoSidedDefine );

            for ( const MaterialStaticSwitch& entry : permutationDesc._listStaticSwitch )
            {
                if ( entry._bEnabled != 0 )
                {
                    if ( entry._keyword.empty() == false )
                        MaterialPermutation::appendUniqueDefine( _listCachedDefine, entry._keyword );
                }
                else if ( entry._keywordOff.empty() == false )
                    MaterialPermutation::appendUniqueDefine( _listCachedDefine, entry._keywordOff );
            }

            for ( const MaterialMultiCompile& mc : permutationDesc._listMultiCompile )
            {
                if ( mc._selected.empty() == false )
                    MaterialPermutation::appendUniqueDefine( _listCachedDefine, mc._selected );
            }

            for ( const MaterialProperty& prop : _data._listProperty )
            {
                if ( prop._type != MaterialPropertyType::Keyword && prop._type != MaterialPropertyType::Bool )
                    continue;
                if ( prop._shaderKeyword.empty() )
                    continue;
                bool bKeywordOn{ false };
                if ( MaterialPacking::parseBoolToken( prop._value, prop._name, "Material.BooleanKeywordUnreadable", bKeywordOn ) && bKeywordOn ) // 읽지 못한 값은 parseBoolToken 이 오류로 남긴다(로드가 이미 거부한다)
                    MaterialPermutation::appendUniqueDefine( _listCachedDefine, prop._shaderKeyword );
            }

            VectorUtil::sort( _listCachedDefine );
            _cachedPermutationHash        = MaterialPermutation::hashDefines( _listCachedDefine );
            const uint64 renderFieldsHash = getRenderFieldsHash();
            if ( renderFieldsHash != 0 )
                _cachedPermutationHash = HashUtil::combine( _cachedPermutationHash, renderFieldsHash );
            _bDefinesDirty = SW_FALSE;
        }
        return _listCachedDefine;
    }

    uint64 Material::getRenderFieldsHash() const
    {
        const bool bNoFields = _desc._renderQueue == 0 && _desc._renderStateOverride.hasAny() == false;
        return bNoFields ? 0u : HashUtil::combine( _desc._renderStateOverride.computeHash(), static_cast<uint64>( static_cast<uint32>( _desc._renderQueue ) ) + 1u );
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
            _cachedShaderPathHash = HashUtil::computeStringHash64( _desc._shaderPath, false, HashUtil::kFNVOffset64 );
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

    bool Material::loadFromAsset( const AssetID& assetID )
    {
        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
            return false;
        return loadComposed( text, assetID.view(), ResourceContext::forSchema( &MaterialXML::makeInheritanceSchema ).withAsset( assetID ) );
    }

    bool Material::loadFromDiskFile( string_view diskPath )
    {
        XMLDocument doc;
        if ( doc.loadFromFile( diskPath ) == false )
            return false;
        return loadFromXML( doc.saveToString() );
    }

    bool Material::saveToFile( const AbsolutePath& absPath ) const
    {

        XMLDocument doc;
        if ( doc.parse( saveToString() ) == false )
            return false;
        return doc.saveToFile( absPath.str() );
    }

    string Material::saveToString() const
    {
        const string fullText = saveFullToString();
        if ( _desc._parentPath.empty() )
            return fullText;
        // 부모를 가리키는 머티리얼은 부모와 다른 칸만 적는다 — 에디터 저장이 부모 구조를 펼쳐 견본 칸을 다시 베끼지 않게.
        // 부모도 같은 직렬화로 펼친 글과 맞댄다 — 손으로 쓴 원문(`0.0` · 빠진 기본 속성)과 비교하면 같은 값도 다르게 보인다.
        const shared_ptr<Material> parent = Material::create();
        string                     deltaText;
        if ( parent->loadFromAsset( AssetID{ _desc._parentPath } ) &&
             DataInheritance::makeDelta( fullText, parent->saveFullToString(), MaterialXML::makeInheritanceSchema(), _desc._parentPath, deltaText ) )
            return deltaText;
        // 모든 칸을 적어 부모를 끊지 않는다 — 빈 글을 돌려 저장이 실패로 드러나게 한다.
        SW_LOG_ERROR_ID( "Material.SaveParentDeltaFailed", "Material '%#' could not be written as a delta of parent '%#' - it is not saved", _desc._name, _desc._parentPath );
        return string{};
    }

    string Material::saveFullToString() const
    {
        this->syncDescFromRuntime();

        XMLDocument doc;
        XMLNode     root = doc.appendRoot( "MaterialDef" );

        AssetXMLFormat::writeXMLVersion( root, Material::kAssetType._formatVersion );
        // 루트 칸은 기본값도 모두 쓴다 — 부모와 다른 칸만 남기는 차이(`DataInheritance::makeDelta`)가 적지 않은 칸을 부모 값으로 읽는다.
        MaterialRootParams rootParams;
        rootParams._name              = _desc._name;
        rootParams._shaderPath        = _desc._shaderPath;
        rootParams._alphaMode         = _desc._alphaMode;
        rootParams._renderQueue       = _desc._renderQueue;
        rootParams._bTwoSidedLighting = _desc._bTwoSidedLighting != SW_FALSE;
        rootParams._blendPreset       = _desc._blendPreset;
        // 문턱값의 정본은 프로퍼티다(에디터가 고친다). Mask 가 아니면 칸은 쓰지 않는 값이라 기본값으로 둔다(읽을 때 다른 값은 오류다).
        if ( _desc._alphaMode == MaterialAlphaMode::Mask && getScalarParameter( hashed_string( kAlphaCutoffProperty ), rootParams._alphaCutoff ) == false )
            rootParams._alphaCutoff = _desc._alphaCutoff;
        XMLFields::writeEveryField( root, rootParams );
        MaterialXML::appendRenderFields( root, _desc );

        XMLNode props = root.appendChild( "_properties" );

        for ( const MaterialProperty& prop : _data._listProperty )
        {
            XMLNode item = props.appendChild( "item" );

            MaterialXML::appendAttribute( item, "name", prop._name );
            MaterialXML::appendAttribute( item, "type", MaterialPacking::typeToString( prop._type ) );
            if ( prop._defaultValue.empty() == false )
                MaterialXML::appendAttribute( item, "defaultValue", prop._defaultValue );
            if ( prop._value.empty() == false && prop._value != prop._defaultValue )
                MaterialXML::appendAttribute( item, "value", prop._value );
            if ( prop._assetPath.empty() == false )
                MaterialXML::appendAttribute( item, "assetPath", prop._assetPath );
            if ( prop._enumType.empty() == false )
                MaterialXML::appendAttribute( item, "enumType", prop._enumType );
            if ( prop._displayName.empty() == false )
                MaterialXML::appendAttribute( item, "displayName", prop._displayName );
            if ( prop._group.empty() == false )
                MaterialXML::appendAttribute( item, "group", prop._group );
            if ( prop._tooltip.empty() == false )
                MaterialXML::appendAttribute( item, "tooltip", prop._tooltip );
            if ( prop._shaderKeyword.empty() == false )
                MaterialXML::appendAttribute( item, "shaderKeyword", prop._shaderKeyword );
            if ( prop._collectionPath.empty() == false )
            {
                MaterialXML::appendAttribute( item, "collection", prop._collectionPath );
                MaterialXML::appendAttribute( item, "collectionParameter", prop._collectionParameter );
            }
            if ( prop._type == MaterialPropertyType::Range )
            {
                item.appendAttribute( "min", prop._min );
                item.appendAttribute( "max", prop._max );
            }
            if ( prop._type == MaterialPropertyType::Color )
            {
                MaterialXML::appendBoolAttribute( item, "bHdr", prop._bHDR );
                MaterialXML::appendBoolAttribute( item, "bSrgb", prop._bSRGB );
            }
            if ( MaterialPacking::isTextureType( prop._type ) )
                MaterialXML::appendBoolAttribute( item, "bSrgb", prop._bSRGB );
            if ( prop._bHidden )
                MaterialXML::appendBoolAttribute( item, "bHidden", true );
            if ( prop._bAdvanced )
                MaterialXML::appendBoolAttribute( item, "bAdvanced", true );

            if ( prop._listEnumEntry.empty() == false )
            {
                XMLNode list = item.appendChild( "_enumEntries" );
                for ( const MaterialEnumEntry& enumEntry : prop._listEnumEntry )
                {
                    XMLNode eItem = list.appendChild( "item" );
                    MaterialXML::appendAttribute( eItem, "name", enumEntry._name );
                    eItem.appendAttribute( "value", enumEntry._value );
                }
            }
        }

        MaterialXML::appendPermutationNode( root, _desc._permutations );
        return doc.saveToString();
    }

    bool Material::loadFromXML( string_view xmlText )
    {
        return loadComposed( xmlText, "<material>", ResourceContext::forSchema( &MaterialXML::makeInheritanceSchema ) );
    }

    bool Material::loadComposed( string_view xmlText, string_view sourceLabel, const ResourceContext& context )
    {
        // 부모를 가리키면(`_base="..."`) 부모 칸 위에 자식이 적은 칸만 덮은 한 벌로 펼친 뒤 읽는다 — 손 머티리얼이 견본 칸을 베끼지 않게.
        const string parentPath = DataInheritance::findBasePath( xmlText ); // 저장이 부모와의 차이로 쓴다
        string       flatText;
        bool         bComposed = false;
        if ( ResourceComposer::compose( xmlText, sourceLabel, context, flatText, bComposed ) == false )
            return false;
        XMLDocument doc;
        if ( doc.parse( bComposed ? string_view( flatText ) : xmlText ) == false )
            return false;

        XMLNode root = doc.getRoot( "MaterialDef" );
        if ( root.isValid() == false )
            return false;

        if ( AssetXMLFormat::upgradeToCurrentXMLVersion( Material::kAssetType, root ) == false )
            return false;

        // 루트 칸(이름 · 셰이더 · 알파 모드)은 스키마로 읽는다 — 적지 않은 알파 모드는 불투명이고, 모르는 이름은 로드 오류다(불투명으로 조용히 가지 않는다). 렌더 상태 칸은 뒤의 `parseRenderFields` 가 읽는다.
        MaterialRootParams rootParams;
        const string_view  sourceName{ root.getAttributeText( "name" ) };
        if ( XMLFields::readFields( root, rootParams, sourceName, { "formatVersion" }, &MaterialXML::isRenderStateAttribute ) == false )
            return false;
        _desc             = MaterialDef{};
        _desc._name       = rootParams._name;
        _desc._parentPath = parentPath;
        setShaderPath( rootParams._shaderPath );

        XMLNode props = root.findChild( "_properties" );
        if ( props.isValid() )
        {
            for ( XMLNode item = props.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
            {
                MaterialProperty prop{};
                if ( MaterialXML::parsePropertyNode( item, prop ) == false )
                    return false;
                if ( prop._name.empty() == false )
                    _desc._listProperty.push_back( std::move( prop ) );
            }
        }

        if ( MaterialXML::parsePermutationNode( root, _desc._permutations ) == false )
            return false;
        if ( MaterialXML::parseRenderFields( root, rootParams, sourceName, _desc ) == false )
            return false;
        // 그리기 목록과 셰이더가 어긋난 머티리얼은 읽지 않는다 — 불투명 목록의 굴절은 사본이 없는 패스에서 빈 색을 읽고(언리얼: 불투명의 SceneColor 는 컴파일 오류),
        // 블렌드 define 과 알파 모드가 다르면 알파가 새거나 불투명 목록에서 섞인다.
        const bool bBlend       = _desc._alphaMode == MaterialAlphaMode::Blend;
        const bool bTranslucent = _desc._permutations.hasAlwaysDefine( kTranslucentDefine );
        if ( bTranslucent != bBlend || ( readsSceneColorCopy() && bBlend == false ) )
        {
            SW_LOG_ERROR_ID( "Material.DrawListMismatch", "Material '%#' (%#): alphaMode Blend %#, %# %#, %# %# - refraction needs Blend, and %# goes with Blend only", _desc._name, sourceLabel,
                             bBlend ? "yes" : "no", kTranslucentDefine, bTranslucent ? "yes" : "no", kRefractionDefine, readsSceneColorCopy() ? "yes" : "no", kTranslucentDefine );
            return false;
        }

        // 적은 문턱값은 셰이더가 읽는 프로퍼티로 간다. 프로퍼티가 없는 셰이더(문턱이 코드에 있다)에는 기본값 밖의 값을 받아 줄 곳이 없다.
        if ( root.findAttribute( "alphaCutoff" ) != nullptr )
        {
            bool bApplied = false;
            for ( MaterialProperty& prop : _desc._listProperty )
            {
                if ( string_view( prop._name ) != kAlphaCutoffProperty )
                    continue;
                prop._value = to_string( _desc._alphaCutoff );
                bApplied    = true;
            }
            if ( bApplied == false && _desc._alphaCutoff != MaterialRootParams{}._alphaCutoff )
            {
                SW_LOG_ERROR_ID( "Material.AlphaCutoffNoProperty", "%#: alphaCutoff has nowhere to go - the material has no %# property", sourceName, kAlphaCutoffProperty );
                return false;
            }
        }

        _bDefinesDirty = SW_TRUE;
        applyDescToRuntime();
        return true;
    }

    void Material::applyDescToRuntime()
    {
        _data._listProperty = _desc._listProperty;
        rebuildPropertyNames();
        // 다시 로드하면 프로퍼티가 XML 순서로 다시 쌓인다 — 셰이더 레이아웃에 맞춘 상태도 함께 풀어야 다음 ensureShaderLayout 이 다시 맞춘다.
        // 표시가 남으면 XML 순서 바이트와 옛 stride 가 함께 GPUScene 에 올라간다(셰이더가 읽는 자리에 다른 프로퍼티 값).
        _bShaderLayoutSynced = SW_FALSE;
        _elementStride       = 0;
        rebuildCollectionBindings();
        rebuildPackedBuffer();
    }

    void Material::rebuildPropertyNames()
    {
        _listPropertyName.clear();
        _listPropertyName.reserve( _data._listProperty.size() );
        for ( const MaterialProperty& prop : _data._listProperty )
        {
            _listPropertyName.push_back( hashed_string( prop._name.c_str() ) );
        }
    }

    void Material::syncDescFromRuntime() const
    {
        auto self                 = const_cast<Material*>( this );
        self->_desc._listProperty = _data._listProperty;
        self->_desc._name         = _desc._name;
        self->setShaderPath( _desc._shaderPath );
    }
} // namespace sw
