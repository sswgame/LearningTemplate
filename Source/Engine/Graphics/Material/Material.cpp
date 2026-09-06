#include "pch.h"

#include "Engine/Graphics/Material/Material.h"

#include "Core/Concurrency/mutex.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/MaterialUtil.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Graphics/Shader/ShaderReflection.h"
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

    Material::Material()
        : _desc{}
        , _data{}
        , _constantBuffer{ 0 }
        , _descriptorIndex{ kInvalidDescriptorIndex }
        , _pRHIDevice{ nullptr }
        , _listAcquiredTexturePath{}
        , _blendMode{ RHIBlendMode::Opaque }
        , _asyncLoadState{ sw::make_shared<AsyncLoadState>() }
        , _listCachedDefine{}
        , _cachedPermutationHash{ 0 }
        , _bDefinesDirty{ SW_TRUE }
        , _reservedMaterial{ 0 }
    {
        _asyncLoadState->_pMaterial = this;
    }

    Material::~Material()
    {
        if ( _pRHIDevice != nullptr )
            shutdown( _pRHIDevice );
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
        TextureCache& textures = engine::getResourceManager().getTextureManager();
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
            _listAcquiredTexturePath.push_back( prop._assetPath );
            setTextureProperty( pRhi, hashed_string( prop._name.c_str() ), pTexture->getSrv() );
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

    void Material::shutdown( IRHIDevice* pRhi )
    {
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

    void Material::reloadShader( IRHIDevice* pRhi, const ShaderCompileResult& result )
    {
        if ( pRhi == nullptr || result._bSuccess == false )
            return;

        if ( result._bytecode.empty() == false )
        {
            ShaderTargetFormat fmt = ShaderTargetFormat::DXIL_D3D12;
            if ( result._bytecode.size() >= sizeof( uint32 ) )
            {
                uint32 magic{ 0 };
                Memory::copy( &magic, result._bytecode.data(), sizeof( uint32 ) );
                if ( magic == 0x07230203u )
                    fmt = ShaderTargetFormat::SPIRV_Vulkan;
            }
            const ShaderReflectionData reflection = ShaderReflection::reflect( result._bytecode, fmt );
            syncPropertiesFromReflection( reflection );
        }

        if ( _constantBuffer != 0 )
        {
            pRhi->getResource()->updateConstantBuffer( _constantBuffer, _data._listBuffer.data(), static_cast<uint32>( _data._listBuffer.size() ) );
            SW_LOG_INFO( "HotRefresh '%#': Shader recompile detected, Constant Buffer re-uploaded. (Bytecode: %# bytes)",
                         _desc._name.c_str(), result._bytecode.size() );
        }
    }

    bool Material::syncPropertiesFromReflection( const ShaderReflectionData& reflectionData )
    {
        if ( reflectionData._listConstantBuffer.empty() )
            return true;

        const ShaderBufferInfo* pSchemaCb = nullptr;
        for ( const ShaderBufferInfo& cb : reflectionData._listConstantBuffer )
        {
            if ( cb._listVariable.empty() == false )
            {
                pSchemaCb = &cb;
                break;
            }
        }
        if ( pSchemaCb == nullptr )
            return true;

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

        bool ok{ true };
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
                        ok = false;
                    }
                }

                prop._offset = var._offset;
                prop._size   = var._size;
                break;
            }
            if ( found == false )
            {
                SW_LOG_WARNING( "Property '%#' missing in shader reflection.", prop._name.c_str() );
                ok = false;
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
        return ok;
    }

    bool Material::rebuildPackedBuffer()
    {
        uint32 currentOffset{ 0 };
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

            uint32 packSize = prop._size;
            if ( packSize == 0 )
                packSize = MaterialUtil::packedSizeOf( prop._shaderType );
            if ( packSize == 0 )
                packSize = 4;

            // If offsets already assigned (reflection), keep them; else sequential HLSL-like pack
            if ( prop._offset == 0 && currentOffset != 0 )
            {
                // first property may legitimately be 0; only auto-assign when all zeros
            }
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
        // Also treat single property at 0 with size set from reflection as explicit if multiple props have mixed
        bool useSequential{ true };
        if ( anyExplicitOffset )
            useSequential = false;
        else
        {
            // If more than one prop has non-zero size and first has offset 0 only ??sequential
            useSequential = true;
        }

        currentOffset = 0;
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
        bool ok{ true };
        for ( MaterialProperty& prop : _data._listProperty )
        {
            if ( MaterialUtil::packPropertyIntoBuffer( prop, _data._listBuffer ) == false )
                ok = false;
        }

        const uint32 alignedTotal = MathUtil::align( static_cast<uint32>( _data._listBuffer.size() ), 256u );
        if ( alignedTotal > _data._listBuffer.size() )
            _data._listBuffer.resize( alignedTotal, 0 );

        _desc._listProperty = _data._listProperty;
        return ok;
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
        MaterialMultiCompile mc{};
        mc._name     = name.c_str() ? name.c_str() : "";
        mc._selected = string( selectedOption );
        if ( selectedOption.empty() == false )
            mc._listOption.push_back( string( selectedOption ) );
        _desc._permutations._listMultiCompile.push_back( std::move( mc ) );
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
