#include "pch.h"

#include "Engine/Graphics/Material/Material.h"

#include "Core/Concurrency/mutex.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/MaterialUtil.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Resource/ResourceManager.h"

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
        , _reserved{ 0 } {}

    MaterialStaticSwitch::MaterialStaticSwitch() noexcept
        : _bEnabled{ SW_FALSE }
        , _bShaderFeature{ SW_TRUE }
        , _reserved{ 0 } {}

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
        , _shaderLayoutBackendMask{ 0 }
        , _pRHIDevice{ nullptr }
        , _listAcquiredTexturePath{}
        , _listMaterialTextureSrv{}
        , _blendMode{ RHIBlendMode::Opaque }
        , _asyncLoadState{ sw::make_shared<AsyncLoadState>() }
        , _listCachedDefine{}
        , _cachedPermutationHash{ 0 }
        , _cachedShaderPathHash{ 0 }
        , _bDefinesDirty{ SW_TRUE }
        , _bShaderPathHashDirty{ SW_TRUE }
        , _reservedMaterial{ 0 }
    {
        _asyncLoadState->_pMaterial = this;
    }

    Material::~Material()
    {
        if ( _pRHIDevice != nullptr )
            releaseRhi( _pRHIDevice );
        if ( _asyncLoadState != nullptr )
        {
            std::scoped_lock<mutex> lock{ _asyncLoadState->_mutex };
            _asyncLoadState->_pMaterial = nullptr;
        }
    }

    bool Material::initialize( IRHIDevice* pRhi, string_view assetRelativePath )
    {
        if ( pRhi == nullptr )
            return false;

        _pRHIDevice = pRhi;
        _assetPath  = assetRelativePath;

        if ( loadFromFile( assetRelativePath ) == false )
            SW_LOG_WARNING( "Failed to load material file '%#'. Using fallback defaults.", assetRelativePath );

        uint32 bufferSize = static_cast<uint32>( _data._listBuffer.size() );
        if ( bufferSize == 0 )
        {
            bufferSize = 256;
            _data._listBuffer.resize( bufferSize, 0 );
        }
        else
        {
            const uint32 alignedSize = MathUtil::align( bufferSize, 256u );
            _data._listBuffer.resize( alignedSize, 0 );
            bufferSize = alignedSize;
        }

        _constantBuffer = pRhi->getResource()->createConstantBuffer( bufferSize );
        if ( _constantBuffer == 0 )
        {
            SW_LOG_ERROR( "Failed to create Constant Buffer!" );
            return false;
        }

        pRhi->getResource()->updateConstantBuffer( _constantBuffer, _data._listBuffer.data(), bufferSize );
        _descriptorIndex = pRhi->getResource()->registerBindlessResource( _constantBuffer );

        // 텍스처는 CB 가 생긴 뒤에 — setTextureProperty 가 인덱스를 CB 에 바로 올린다.
        resolveTextureAssets( pRhi );

        SW_LOG_INFO( "Initialized '%#' with Bindless Descriptor Index %#", _desc._name.c_str(), _descriptorIndex );
        return _descriptorIndex != kInvalidDescriptorIndex;
    }

    void Material::resolveTextureAssets( IRHIDevice* pRhi )
    {
        if ( pRhi == nullptr || engine::areEngineServicesBound() == false )
            return;

        // DX12/Vulkan 은 셰이더가 전역 bindless 인덱스로 직접 힙/배열을 찌른다. DX11/GL 은 그게 안 되므로
        // (SM5.0 은 리소스 배열 동적 인덱싱 없음, GL 은 SPIR-V 라 ARB_bindless_texture 불가) 엔진이
        // 머티리얼 텍스처를 t5..t8 고정 슬롯에 바인딩하고 CB 에는 **서수**를 넣는다.
        const bool    bNativeBindless = pRhi->supportsNativeBindlessSampling();
        TextureCache& textures        = engine::getResourceManager().getTextureManager();
        for ( const MaterialProperty& prop : _data._listProperty )
        {
            if ( MaterialUtil::isTextureType( prop._type ) == false || prop._assetPath.empty() )
                continue;
            Texture2D* pTexture = textures.acquire( prop._assetPath, pRhi );
            if ( pTexture == nullptr )
            {
                SW_LOG_WARNING( "Material '%#': texture '%#' for '%#' could not be loaded — sampling falls back to white.",
                                _desc._name.c_str(), prop._assetPath.c_str(), prop._name.c_str() );
                continue;
            }

            const uint32 ordinal = static_cast<uint32>( _listMaterialTextureSrv.size() );
            if ( bNativeBindless == false && ordinal >= shaderslot::kMaterialTextureCount )
            {
                SW_LOG_WARNING( "Material '%#': 이 백엔드는 머티리얼 텍스처를 %#개까지만 바인딩합니다 — '%#' 는 흰색으로 남습니다.",
                                _desc._name.c_str(), shaderslot::kMaterialTextureCount, prop._name.c_str() );
                textures.release( prop._assetPath, pRhi );
                continue;
            }

            _listAcquiredTexturePath.push_back( prop._assetPath );
            _listMaterialTextureSrv.push_back( pTexture->getSrv() );
            setTextureProperty( pRhi, hashed_string( prop._name.c_str() ), bNativeBindless ? pTexture->getSrv() : ordinal );
        }
    }

    void Material::releaseTextureAssets( IRHIDevice* pRhi )
    {
        if ( _listAcquiredTexturePath.empty() )
            return;
        if ( engine::areEngineServicesBound() )
        {
            TextureCache& textures = engine::getResourceManager().getTextureManager();
            for ( const string& path : _listAcquiredTexturePath )
                textures.release( path, pRhi );
        }
        _listAcquiredTexturePath.clear();
        _listMaterialTextureSrv.clear();
        for ( MaterialProperty& prop : _data._listProperty )
        {
            if ( MaterialUtil::isTextureType( prop._type ) == false || prop._assetPath.empty() )
                continue;
            // _value 도 비운다 — setTextureProperty 가 인덱스를 문자열로도 남기므로, 그대로 두면
            // 다음 패킹이 이미 해제된 인덱스를 숫자 오버라이드로 되살린다.
            prop._textureIndex = kInvalidDescriptorIndex;
            prop._value.clear();
        }
    }

    bool Material::initRhi( IRHIDevice* pDevice )
    {
        // 통보 순서는 정해져 있지 않다 — 이미 올라가 있으면 그대로 둔다.
        if ( isRhiValid() )
            return true;
        // 한 번도 initialize 되지 않은 머티리얼이다. 되살릴 내용 자체가 없다.
        if ( pDevice == nullptr || _assetPath.empty() )
            return true;
        return initialize( pDevice, _assetPath );
    }

    void Material::forgetRhi( IRHIDevice* pDevice )
    {
        if ( _pRHIDevice != pDevice )
            return;
        // 디바이스가 이미 없다 — GPU 자원은 그와 함께 갔다. 핸들만 비운다(destroy 는 해제 후 사용이다).
        _constantBuffer  = 0;
        _descriptorIndex = kInvalidDescriptorIndex;
        _pRHIDevice      = nullptr;
    }

    void Material::releaseRhi( IRHIDevice* pRhi )
    {
        // 남의 디바이스가 죽는 통보라면 내 것이 아니다.
        if ( pRhi != nullptr && _pRHIDevice != nullptr && _pRHIDevice != pRhi )
            return;

        releaseTextureAssets( pRhi );
        if ( pRhi != nullptr )
        {
            if ( _descriptorIndex != kInvalidDescriptorIndex )
                pRhi->getResource()->unregisterBindlessResource( _descriptorIndex );
            if ( _constantBuffer != 0 )
                pRhi->getResource()->destroyBuffer( _constantBuffer );
        }
        _constantBuffer  = 0;
        _descriptorIndex = kInvalidDescriptorIndex;
        _pRHIDevice      = nullptr;
    }

    bool Material::ensureShaderLayout( IRHIDevice* pDevice )
    {
        // 머티리얼 바이트의 정본은 .material 의 프로퍼티 순서가 아니라 **셰이더의 SwMaterialData_t 원소 레이아웃**이다(언리얼도
        // 머티리얼 파라미터 레이아웃을 셰이더에서 가져온다). 예전엔 셰이더 핫리로드 경로에서만 맞췄고 로드 경로에서는 XML
        // 순서로 패킹해 stride 가 0 이었다 — 그러면 GpuScene 이 CB 크기(256)를 stride 로 써서 원소 1 부터 어긋난다.
        if ( pDevice == nullptr || _desc._shaderPath.empty() )
            return false;
        const uint32 backendBit = 1u << static_cast<uint32>( pDevice->getBackendType() );
        if ( ( _shaderLayoutBackendMask & backendBit ) != 0 )
            return _elementStride != 0;
        _shaderLayoutBackendMask |= backendBit;

        ShaderCompileDesc desc{};
        desc._filePath     = _desc._shaderPath;
        desc._entryPoint   = "PSMain";
        desc._stage        = ShaderStage::Pixel;
        desc._targetFormat = RHI::getShaderTargetFormat( pDevice->getBackendType() );
        ShaderReflectionData reflection{};
        if ( ShaderReflectionLibrary::getOrReflect( desc, reflection ) == false )
        {
            SW_LOG_ERROR( "머티리얼 '%#' 의 셰이더 리플렉션을 찾지 못했습니다 ('%#') — XML 순서 패킹으로 남습니다.", _desc._name.c_str(), _desc._shaderPath.c_str() );
            return false;
        }
        syncPropertiesFromReflection( reflection );
        return _elementStride != 0;
    }

    bool Material::syncPropertiesFromReflection( const ShaderReflectionData& reflectionData )
    {
        if ( reflectionData._listConstantBuffer.empty() && reflectionData._listStructuredElement.empty() )
            return true;

        // 스키마 우선순위: GPUScene 머티리얼 데이터(g_SwMaterials 구조버퍼 원소) → MaterialCB → 멤버가 있는 첫 CB(레거시).
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
        // 예전엔 여기에 "멤버가 있는 **첫** 상수버퍼를 머티리얼 스키마로 삼는" 폴백이 있었다. 지금은
        // 모든 셰이더가 SW_MATERIAL_BEGIN/END(g_SwMaterials) 아니면 MaterialCB 를 선언하므로 도달하지
        // 않는다 — 그리고 도달했다면 **PassCB 를 머티리얼 레이아웃으로 착각**해 조용히 엉뚱한 오프셋에
        // 값을 써 넣었을 것이다. 조용히 틀리느니 못 찾았다고 알린다.
        if ( pSchemaCb == nullptr )
        {
            SW_LOG_WARNING( "머티리얼 '%#' 의 셰이더에 머티리얼 스키마가 없습니다 — SW_MATERIAL_BEGIN/END 또는 MaterialCB 를 선언해야 합니다.",
                            _desc._name.c_str() );
            return true;
        }

        if ( _data._listProperty.empty() )
        {
            for ( const ShaderVariableInfo& var : pSchemaCb->_listVariable )
            {
                MaterialProperty prop{};
                prop._name       = var._name;
                prop._offset     = var._offset;
                prop._size       = var._size;
                prop._shaderType = MaterialUtil::shaderTypeFromReflectionName( var._type, var._size );
                prop._type       = prop._shaderType;
                _data._listProperty.push_back( prop );
            }
            rebuildPackedBuffer();
            // restore reflection offsets after sequential rebuild
            for ( MaterialProperty& prop : _data._listProperty )
            {
                for ( const ShaderVariableInfo& var : pSchemaCb->_listVariable )
                {
                    if ( var._name == prop._name )
                    {
                        prop._offset = var._offset;
                        prop._size   = var._size;
                        break;
                    }
                }
            }
            _data._listBuffer.clear();
            uint32 maxEnd = pSchemaCb->_totalSize;
            for ( const MaterialProperty& prop : _data._listProperty )
            {
                maxEnd = MathUtil::max( maxEnd, prop._offset + prop._size );
            }
            _data._listBuffer.assign( maxEnd, 0 );
            for ( MaterialProperty& prop : _data._listProperty )
            {
                MaterialUtil::packPropertyIntoBuffer( prop, _data._listBuffer );
            }
            const uint32 alignedTotal = MathUtil::align( static_cast<uint32>( _data._listBuffer.size() ), 256u );
            _data._listBuffer.resize( alignedTotal, 0 );
            _desc._listProperty = _data._listProperty;
            SW_LOG_TRACE( "Filled %# properties from shader reflection.", _data._listProperty.size() );
            return true;
        }

        bool bAllPacked{ true };
        for ( MaterialProperty& prop : _data._listProperty )
        {
            // Textures / keywords / UI-only fields are not MaterialCB variables.
            if ( MaterialUtil::isNonBufferType( prop._type ) || MaterialUtil::isTextureType( prop._type ) )
                continue;

            bool found{ false };
            for ( const ShaderVariableInfo& var : pSchemaCb->_listVariable )
            {
                if ( var._name != prop._name )
                    continue;
                found = true;

                const MaterialPropertyType reflected =
                    MaterialUtil::shaderTypeFromReflectionName( var._type, var._size );
                if ( prop._shaderType == MaterialPropertyType::Unknown )
                    prop._shaderType = reflected;
                else if ( MaterialUtil::packedSizeOf( prop._shaderType ) != 0 && MaterialUtil::packedSizeOf( prop._shaderType ) != var._size )
                {
                    // Allow conversion if sizes match reflected size after remap
                    if ( MaterialUtil::packedSizeOf( MaterialUtil::defaultShaderTypeFor( prop._type ) ) == var._size )
                        prop._shaderType = MaterialUtil::defaultShaderTypeFor( prop._type );
                    else if ( reflected != MaterialPropertyType::Unknown )
                        prop._shaderType = reflected;
                    else
                    {
                        SW_LOG_WARNING( "Reflection size mismatch for '%#' (shaderType %# bytes vs %#).",
                                        prop._name.c_str(), MaterialUtil::packedSizeOf( prop._shaderType ), var._size );
                        bAllPacked = false;
                    }
                }

                prop._offset = var._offset;
                prop._size   = var._size;
                break;
            }
            if ( found == false )
            {
                SW_LOG_WARNING( "Property '%#' missing in shader reflection.", prop._name.c_str() );
                bAllPacked = false;
            }
        }

        uint32 maxEnd = pSchemaCb->_totalSize;
        for ( const MaterialProperty& prop : _data._listProperty )
        {
            if ( MaterialUtil::isNonBufferType( prop._type ) == false )
                maxEnd = MathUtil::max( maxEnd, prop._offset + prop._size );
        }
        _data._listBuffer.assign( maxEnd, 0 );
        for ( MaterialProperty& prop : _data._listProperty )
        {
            MaterialUtil::packPropertyIntoBuffer( prop, _data._listBuffer );
        }
        const uint32 alignedTotal = MathUtil::align( static_cast<uint32>( _data._listBuffer.size() ), 256u );
        _data._listBuffer.resize( alignedTotal, 0 );
        _desc._listProperty = _data._listProperty;
        return bAllPacked;
    }

    bool Material::rebuildPackedBuffer()
    {
        _data._listBuffer.clear();

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

            // 이 자리에 packSize 계산과 빈 if 블록이 있었다 — 계산한 값을 아무도 읽지 않았고
            // 블록 본문은 주석뿐이었다. 실제 오프셋 패킹은 아래 두 번째 순회가 packSize 를 다시
            // 구해서 한다. 이 순회가 하는 일은 비버퍼 타입을 0 으로 되돌리고 셰이더 타입을
            // 채우는 것까지다.
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

        _data._listBuffer.assign( maxEnd, 0 );
        bool bAllPacked{ true };
        for ( MaterialProperty& prop : _data._listProperty )
        {
            if ( MaterialUtil::packPropertyIntoBuffer( prop, _data._listBuffer ) == false )
                bAllPacked = false;
        }

        const uint32 alignedTotal = MathUtil::align( static_cast<uint32>( _data._listBuffer.size() ), 256u );
        if ( alignedTotal > _data._listBuffer.size() )
            _data._listBuffer.resize( alignedTotal, 0 );

        _desc._listProperty = _data._listProperty;
        return bAllPacked;
    }

    bool Material::resetPropertyToDefault( IRHIDevice* pRhi, hashed_string name )
    {
        MaterialProperty* prop = findProperty( name );
        if ( prop == nullptr )
            return false;
        return setPropertyValue( pRhi, name, prop->_defaultValue );
    }

    void Material::resetAllToDefaults( IRHIDevice* pRhi )
    {
        for ( MaterialProperty& prop : _data._listProperty )
        {
            prop._value = prop._defaultValue;
        }
        rebuildPackedBuffer();
        if ( pRhi != nullptr && _constantBuffer != 0 )
            pRhi->getResource()->updateConstantBuffer( _constantBuffer, _data._listBuffer.data(), static_cast<uint32>( _data._listBuffer.size() ) );
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

    void Material::setPropertyData( IRHIDevice* pRhi, uint32 offset, uint32 size, const void* pData )
    {
        if ( pData == nullptr || offset + size > _data._listBuffer.size() )
            return;

        Memory::copy( _data._listBuffer.data() + offset, pData, size );

        if ( pRhi != nullptr && _constantBuffer != 0 )
            pRhi->getResource()->updateConstantBuffer( _constantBuffer, _data._listBuffer.data(), static_cast<uint32>( _data._listBuffer.size() ) );
    }

    bool Material::setPropertyValue( IRHIDevice* pRhi, hashed_string name, string_view value )
    {
        MaterialProperty* prop = findProperty( name );
        if ( prop == nullptr )
            return false;
        prop->_value = value;
        if ( MaterialUtil::packPropertyIntoBuffer( *prop, _data._listBuffer ) == false )
            return false;
        if ( pRhi != nullptr && _constantBuffer != 0 )
            pRhi->getResource()->updateConstantBuffer( _constantBuffer, _data._listBuffer.data(), static_cast<uint32>( _data._listBuffer.size() ) );
        _desc._listProperty = _data._listProperty;

        if ( prop->_type == MaterialPropertyType::Keyword || prop->_type == MaterialPropertyType::Bool )
            _bDefinesDirty = 1;

        return true;
    }

    bool Material::setTextureProperty( IRHIDevice* pRhi, hashed_string name, RHIDescriptorIndex descIdx )
    {
        for ( MaterialProperty& prop : _data._listProperty )
        {
            if ( hashed_string( prop._name.c_str() ) != name )
                continue;
            if ( MaterialUtil::isTextureType( prop._type ) == false && prop._shaderType != MaterialPropertyType::Uint )
                return false;
            prop._textureIndex = descIdx;
            prop._value        = to_string( descIdx );
            if ( MaterialUtil::packPropertyIntoBuffer( prop, _data._listBuffer ) == false )
                return false;
            if ( pRhi != nullptr && _constantBuffer != 0 )
                pRhi->getResource()->updateConstantBuffer( _constantBuffer, _data._listBuffer.data(), static_cast<uint32>( _data._listBuffer.size() ) );
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
            _bDefinesDirty               = 1;
        }
    }

    void Material::setUsageFlags( MaterialUsageFlags flags )
    {
        if ( _desc._permutations._usage != flags )
        {
            _desc._permutations._usage = flags;
            _bDefinesDirty             = 1;
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
                    _bDefinesDirty = 1;
                }
                return;
            }
        }
        MaterialStaticSwitch entry{};
        entry._name     = name.c_str() ? name.c_str() : "";
        entry._keyword  = entry._name;
        entry._bEnabled = bEnabled;
        _desc._permutations._listStaticSwitch.push_back( std::move( entry ) );
        _bDefinesDirty = 1;
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
                    _bDefinesDirty = 1;
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
        _bDefinesDirty = 1;
    }

    void Material::setBlendMode( RHIBlendMode mode )
    {
        _blendMode       = mode;
        _desc._blendMode = MaterialUtil::blendModeToString( mode );
    }

    bool Material::setParameterFloat( IRHIDevice* pRhi, hashed_string name, float32 value )
    {
        for ( MaterialProperty& prop : _data._listProperty )
        {
            if ( hashed_string( prop._name.c_str() ) != name )
                continue;
            prop._value = to_string( value );
            if ( MaterialUtil::packPropertyIntoBuffer( prop, _data._listBuffer ) == false )
                return false;
            if ( pRhi != nullptr && _constantBuffer != 0 )
                pRhi->getResource()->updateConstantBuffer( _constantBuffer, _data._listBuffer.data(), static_cast<uint32>( _data._listBuffer.size() ) );
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

    const void* Material::getPropertyData( string_view name ) const
    {
        for ( const MaterialProperty& prop : _data._listProperty )
        {
            if ( prop._name == name && MaterialUtil::isNonBufferType( prop._type ) == false )
                return _data._listBuffer.data() + prop._offset;
        }
        return nullptr;
    }

    const vector<string>& Material::getCachedShaderDefines() const
    {
        if ( _bDefinesDirty != 0 )
        {
            _listCachedDefine.clear();
            const MaterialPermutationDesc& perm = _desc._permutations;

            for ( const string& defineStr : perm._listAlwaysDefine )
            {
                MaterialUtil::appendUniqueDefine( _listCachedDefine, defineStr );
            }

            MaterialUtil::appendQualityDefines( perm._quality, _listCachedDefine );
            MaterialUtil::appendUniqueDefine( _listCachedDefine, string( "SHADER_LOD=" ) + to_string( perm._shaderLOD ) );
            MaterialUtil::appendUsageDefines( perm._usage, _listCachedDefine );

            for ( const MaterialStaticSwitch& entry : perm._listStaticSwitch )
            {
                if ( entry._bEnabled != 0 )
                {
                    if ( entry._keyword.empty() == false )
                        MaterialUtil::appendUniqueDefine( _listCachedDefine, entry._keyword );
                }
                else if ( entry._keywordOff.empty() == false )
                    MaterialUtil::appendUniqueDefine( _listCachedDefine, entry._keywordOff );
            }

            for ( const MaterialMultiCompile& mc : perm._listMultiCompile )
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
                if ( MaterialUtil::parseBoolToken( prop._value ) )
                    MaterialUtil::appendUniqueDefine( _listCachedDefine, prop._shaderKeyword );
            }

            std::sort( _listCachedDefine.begin(), _listCachedDefine.end() );
            _cachedPermutationHash = MaterialUtil::hashDefines( _listCachedDefine );
            _bDefinesDirty         = 0;
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
        if ( _bShaderPathHashDirty != 0 )
        {
            _cachedShaderPathHash = StringUtil::computeHash64( _desc._shaderPath, false, StringUtil::kOffset64 );
            _bShaderPathHashDirty = SW_FALSE;
        }
        return _cachedShaderPathHash;
    }

    uint64 Material::getPermutationHash() const
    {
        if ( _bDefinesDirty != 0 )
            getCachedShaderDefines();
        return _cachedPermutationHash;
    }

    bool Material::getParameterFloat( hashed_string name, float32& outValue ) const
    {
        for ( const MaterialProperty& prop : _data._listProperty )
        {
            if ( hashed_string( prop._name.c_str() ) != name )
                continue;
            if ( prop._offset + 4 > _data._listBuffer.size() )
                return false;
            Memory::copy( &outValue, _data._listBuffer.data() + prop._offset, 4 );
            return true;
        }
        return false;
    }

} // namespace sw
