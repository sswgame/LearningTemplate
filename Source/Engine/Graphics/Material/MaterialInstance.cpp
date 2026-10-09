#include "pch.h"

#include "Engine/Graphics/Material/MaterialInstance.h"

#include "Core/Math/VectorMath.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialUtil.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Serialization/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        struct MaterialInstanceInternal
        {
            template <typename T>
            static void insertOrAssign( vector<pair<hashed_string, T>>& listPair, hashed_string key, const T& val )
            {
                for ( auto& pair : listPair )
                {
                    if ( pair.first == key )
                    {
                        pair.second = val;
                        return;
                    }
                }
                listPair.push_back( { key, val } );
            }

            template <typename T>
            static const T* findValue( const vector<pair<hashed_string, T>>& listPair, hashed_string key )
            {
                for ( const auto& pair : listPair )
                {
                    if ( pair.first == key )
                        return &pair.second;
                }
                return nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    shared_ptr<MaterialInstance> MaterialInstance::create( Material* pParentMaterial )
    {
        // 인자가 Material* 이라 ADL 이 std::make_shared 를 끌어온다. 한정해야 sw 것이 잡힌다.
        return sw::make_shared<MaterialInstance>( CreateKey{}, pParentMaterial );
    }

    MaterialInstance::MaterialInstance( CreateKey, Material* pParentMaterial )
        : _pParentMaterial{ pParentMaterial }
        , _desc{}
        , _overrideMutex{}
        , _listValueOverride{}
        , _listScalarOverride{}
        , _listVectorOverride{}
        , _listTextureOverride{}
        , _listKeywordOverride{}
        , _listMultiCompileOverride{}
        , _qualityOverride{ MaterialQualityLevel::Count }
        , _bytes{}
        , _constant{}
        , _descriptorIndex{ kInvalidDescriptorIndex }
        , _constantByteSize{ 0 }
        , _parentBufferGeneration{ 0 }
        , _pTextureDevice{ nullptr }
        , _textureReloadGeneration{ 0 }
        , _listCachedDefine{}
        , _cachedPermutationHash{ 0 }
        , _parentPermutationHash{ 0 }
        , _bDefinesDirty{ SW_TRUE }
        , _bGpuDirty{ SW_TRUE }
        , _instReserved{ 0 } {}

    MaterialInstance::~MaterialInstance()
    {
        // 소멸은 디바이스가 죽은 뒤에도 일어난다(씬 teardown 순서). 든 디바이스 포인터는 생 포인터라
        // 살아 있는지 스스로 알 수 없으므로 세대를 함께 본다. Mesh::releaseVertexBuffer 와 같은 함정이다.
        if ( IRHIDevice* pLiveDevice = _constant.getLiveDevice() )
            releaseRhi( pLiveDevice );
        // 텍스처를 빌린 디바이스가 죽었으면 forgetRhi 가 이미 비웠다 — 남아 있으면 살아 있는 디바이스다.
        releaseTextureOverrides( _pTextureDevice );
        _constant.forget();
        _descriptorIndex = kInvalidDescriptorIndex;
    }

    void MaterialInstance::forgetRhi( IRHIDevice* pDevice )
    {
        std::scoped_lock<mutex> lock{ _overrideMutex };
        // 텍스처는 디바이스와 함께 갔다(TextureCache 의 Texture2D 도 같은 통보를 받는다). 참조만 놓는다.
        if ( _pTextureDevice == pDevice )
            releaseTextureOverrides( nullptr );
        if ( _constant._pDevice != pDevice )
            return;
        // 디바이스가 이미 없다. 상수버퍼는 그와 함께 갔다.
        _constant.forget();
        _descriptorIndex  = kInvalidDescriptorIndex;
        _constantByteSize = 0;
        _bGpuDirty        = SW_TRUE;
    }

    void MaterialInstance::releaseRhi( IRHIDevice* pRhi )
    {
        std::scoped_lock<mutex> lock{ _overrideMutex };
        if ( pRhi == nullptr || _pTextureDevice == pRhi )
            releaseTextureOverrides( pRhi );
        // 디바이스가 죽기 **전에** 오는 통보다. 제대로 돌려준다. 남의 디바이스 것이면 내 것이 아니다.
        if ( pRhi != nullptr && _constant._buffer != 0 && _constant._pDevice != pRhi )
            return;

        if ( pRhi != nullptr )
        {
            // 마지막 소유를 게임 스레드가 놓을 수 있다(GpuScene 후보 · 걷은 뷰) — 렌더 스레드가 병렬 기록 중이면 핸들 반환을 그 프레임 뒤로 미룬다.
            if ( _descriptorIndex != kInvalidDescriptorIndex )
                pRhi->releaseHandle( RHIHandleKind::BindlessResource, _descriptorIndex );
            if ( _constant._buffer != 0 )
                pRhi->releaseHandle( RHIHandleKind::Buffer, _constant._buffer );
        }
        _constant.forget();
        _descriptorIndex  = kInvalidDescriptorIndex;
        _constantByteSize = 0;
        _bytes.clear();
        _bGpuDirty = SW_TRUE;
    }

    bool MaterialInstance::loadFromFile( string_view assetRelativePath )
    {
        XmlDocument doc;
        if ( doc.loadPath( assetRelativePath ) == false )
            return false;
        return loadFromXml( doc.saveToString() );
    }

    bool MaterialInstance::saveToFile( string_view assetRelativePath ) const
    {
        string absPath = ResourceUtil::getResourcePath( assetRelativePath );
        if ( absPath.empty() )
            absPath = assetRelativePath;

        syncDescOverrides();

        XmlDocument doc;
        XmlNode     root = doc.appendRoot( "MaterialInstanceDesc" );
        AssetFormatRegistry::writeXmlVersion( root, AssetFormatVersions::kMaterialInstance );
        MaterialUtil::appendAttribute( root, "name", _desc._name );
        if ( _desc._parentPath.empty() == false )
            MaterialUtil::appendAttribute( root, "parentPath", _desc._parentPath );
        if ( _desc._quality.empty() == false )
            MaterialUtil::appendAttribute( root, "quality", _desc._quality );

        XmlNode overrides = root.appendChild( "_overrides" );
        for ( const MaterialInstanceDesc::Override& overrideItem : _desc._listOverride )
        {
            XmlNode item = overrides.appendChild( "item" );
            MaterialUtil::appendAttribute( item, "name", overrideItem._name );
            MaterialUtil::appendAttribute( item, "value", overrideItem._value );
            if ( overrideItem._assetPath.empty() == false )
                MaterialUtil::appendAttribute( item, "assetPath", overrideItem._assetPath );
        }

        if ( _desc._listKeyword.empty() == false )
        {
            XmlNode list = root.appendChild( "_keywords" );
            for ( const MaterialInstanceDesc::KeywordOverride& keywordItem : _desc._listKeyword )
            {
                XmlNode item = list.appendChild( "item" );
                MaterialUtil::appendAttribute( item, "name", keywordItem._name );
                MaterialUtil::appendBoolAttr( item, "bEnabled", keywordItem._bEnabled );
            }
        }
        if ( _desc._listMultiCompile.empty() == false )
        {
            XmlNode list = root.appendChild( "_multiCompiles" );
            for ( const MaterialInstanceDesc::MultiCompileOverride& multiCompileItem : _desc._listMultiCompile )
            {
                XmlNode item = list.appendChild( "item" );
                MaterialUtil::appendAttribute( item, "name", multiCompileItem._name );
                MaterialUtil::appendAttribute( item, "selected", multiCompileItem._selected );
            }
        }

        return doc.saveFile( absPath );
    }

    bool MaterialInstance::updateRhi( IRHIDevice* pRhi )
    {
        if ( pRhi == nullptr || _pParentMaterial == nullptr )
            return false;
        std::scoped_lock<mutex> lock{ _overrideMutex };

        // 백엔드가 바뀌었으면 상수버퍼 · 인덱스는 옛 디바이스 것이다. 잊고 새로 만든다(destroy 는 해제 후 사용이다).
        // 핸들이 0 이 아닌 것과 "이 디바이스 것" 은 다른 말이다. RHIResidentBuffer 가 세대로 가른다.
        if ( _constant._buffer != 0 && _constant.isResident() == false )
        {
            _constant.forget();
            _descriptorIndex  = kInvalidDescriptorIndex;
            _constantByteSize = 0;
            _bGpuDirty        = SW_TRUE;
        }

        // 부모 레이아웃을 **먼저** 셰이더에 맞춘다. 복사한 뒤에 맞추면 첫 프레임의 인스턴스가 XML 순서 바이트를 들고 있다.
        (void)_pParentMaterial->ensureShaderLayout( pRhi );
        // 부모 바이트가 바뀌었으면(값 · 레이아웃 · 다시 로드) 복사본도 낡았다. 인스턴스가 더러워질 때만 다시 복사하면 오버라이드가
        // 없는 파라미터에서 부모의 값 변경과 다시 맞춘 레이아웃을 놓친다.
        if ( _parentBufferGeneration != _pParentMaterial->getBufferGeneration() )
            _bGpuDirty = SW_TRUE;
        if ( syncTextureOverrides( pRhi ) )
            _bGpuDirty = SW_TRUE;

        if ( _bGpuDirty == SW_FALSE && _constant._buffer != 0 && _descriptorIndex != kInvalidDescriptorIndex )
            return true;

        _bytes                  = _pParentMaterial->getBuffer();
        _parentBufferGeneration = _pParentMaterial->getBufferGeneration();
        if ( _bytes.empty() )
            return false;

        for ( const auto& [name, value] : _listValueOverride )
        {
            _pParentMaterial->packNamedValueIntoBuffer( name, value, _bytes );
        }

        // 덮어쓴 텍스처: 네이티브 bindless 는 지금 SRV 인덱스를, 슬롯 바인딩 백엔드(DX11 · GL)는 슬롯 서수를 넣는다(부모와 같은 규칙).
        const bool bNativeBindless = pRhi->supportsNativeBindlessSampling();
        for ( const TextureOverride& texture : _listTextureOverride )
        {
            if ( texture._pTexture == nullptr )
                continue;
            const uint32 value = bNativeBindless ? texture._pTexture->getSrv() : findTextureSlot( texture._name );
            if ( value != kInvalidDescriptorIndex && value != Material::kInvalidTextureSlot )
                _pParentMaterial->packTextureIntoBuffer( texture._name, value, _bytes );
        }

        const uint32 size = static_cast<uint32>( _bytes.size() );

        // 부모의 상수버퍼는 **셰이더를 다시 구우면 커질 수 있다**(레이아웃이 바뀐다). 그때 이전
        // 버퍼를 그대로 쓰면 `updateConstantBuffer` 가 만들 때보다 큰 크기로 복사한다. 그 함수는
        // 크기를 검사하지 않으므로(GL 만 API 가 막아 준다) 프레임 슬롯 밖까지 쓴다.
        // 커졌으면 버리고 다시 만든다.
        if ( _constant._buffer != 0 && size > _constantByteSize )
        {
            if ( _descriptorIndex != kInvalidDescriptorIndex )
                pRhi->releaseHandle( RHIHandleKind::BindlessResource, _descriptorIndex );
            pRhi->releaseHandle( RHIHandleKind::Buffer, _constant._buffer );
            _constant.forget();
            _descriptorIndex  = kInvalidDescriptorIndex;
            _constantByteSize = 0;
        }

        if ( _constant._buffer == 0 )
        {
            const RHIBufferHandle constantBuffer = pRhi->getResourceFactory()->createConstantBuffer( size );
            if ( constantBuffer == 0 )
                return false;
            _constant.adopt( pRhi, constantBuffer );
            _descriptorIndex  = pRhi->getResourceFactory()->registerBindlessResource( constantBuffer );
            _constantByteSize = size;
        }
        pRhi->getResourceFactory()->updateConstantBuffer( _constant._buffer, _bytes.data(), size );
        _bGpuDirty = SW_FALSE;
        return _descriptorIndex != kInvalidDescriptorIndex;
    }

    bool MaterialInstance::syncTextureOverrides( IRHIDevice* pRhi )
    {
        if ( _listTextureOverride.empty() || engine::areEngineServicesBound() == false )
            return false;
        TextureCache& textures = engine::getAssetManager().getTextureManager();
        bool          bChanged{ false };
        // 디바이스가 바뀌었으면 옛 디바이스로 빌린 것을 돌려주고 새로 빌린다(옛 것이 이미 죽었으면 forgetRhi 가 먼저 와서 비웠다).
        if ( _pTextureDevice != nullptr && _pTextureDevice != pRhi )
        {
            releaseTextureOverrides( _pTextureDevice );
            bChanged = true;
        }
        _pTextureDevice = pRhi;

        for ( TextureOverride& texture : _listTextureOverride )
        {
            if ( texture._acquiredPath == texture._assetPath )
                continue;
            if ( texture._pTexture != nullptr )
                textures.release( texture._acquiredPath, pRhi );
            texture._pTexture     = nullptr;
            texture._acquiredPath = texture._assetPath;
            bChanged              = true;
            if ( texture._assetPath.empty() )
                continue;
            texture._pTexture = textures.acquire( texture._assetPath, pRhi );
            if ( texture._pTexture == nullptr )
                SW_LOG_WARNING( "MaterialInstance '%#': texture '%#' for '%#' could not be loaded — the parent's texture stays.", _desc._name.c_str(),
                                texture._assetPath.c_str(), texture._name.c_str() );
        }
        // 지운 덮어쓰기(빈 경로)는 돌려준 뒤 뺀다.
        _listTextureOverride.erase( std::remove_if( _listTextureOverride.begin(), _listTextureOverride.end(),
                                                    []( const TextureOverride& texture )
        { return texture._assetPath.empty() && texture._pTexture == nullptr; } ),
                                    _listTextureOverride.end() );

        // 다시 올린 텍스처는 같은 객체에 새 SRV 를 받았다 — 다시 패킹한다.
        const uint32 reloadGeneration = textures.getReloadGeneration();
        if ( reloadGeneration != _textureReloadGeneration )
        {
            _textureReloadGeneration = reloadGeneration;
            bChanged                 = true;
        }
        return bChanged;
    }

    void MaterialInstance::releaseTextureOverrides( IRHIDevice* pRhi )
    {
        const bool bServicesBound = engine::areEngineServicesBound();
        for ( TextureOverride& texture : _listTextureOverride )
        {
            if ( texture._pTexture != nullptr && bServicesBound )
                engine::getAssetManager().getTextureManager().release( texture._acquiredPath, pRhi );
            texture._pTexture = nullptr;
            texture._acquiredPath.clear();
        }
        _pTextureDevice = nullptr;
        _bGpuDirty      = SW_TRUE;
    }

    uint32 MaterialInstance::findTextureSlot( hashed_string name ) const
    {
        if ( _pParentMaterial == nullptr )
            return Material::kInvalidTextureSlot;
        const uint32 parentSlot = _pParentMaterial->findTextureSlot( name );
        if ( parentSlot != Material::kInvalidTextureSlot )
            return parentSlot;
        // 부모에 없는 프로퍼티는 부모 슬롯 뒤에 덮어쓰기 순서대로 잇는다. 지울 것(빈 경로)은 자리를 차지하지 않는다.
        uint32 slot = static_cast<uint32>( _pParentMaterial->getMaterialTextureSrvs().size() );
        for ( const TextureOverride& texture : _listTextureOverride )
        {
            if ( texture._assetPath.empty() || _pParentMaterial->findTextureSlot( texture._name ) != Material::kInvalidTextureSlot )
                continue;
            if ( texture._name == name )
                return slot < shaderslot::kMaterialTextureCount ? slot : Material::kInvalidTextureSlot;
            ++slot;
        }
        return Material::kInvalidTextureSlot;
    }

    void MaterialInstance::collectTextureSlotSrvs( RHIDescriptorIndex* pOutSlot, uint32 slotCount ) const
    {
        std::scoped_lock<mutex> lock{ _overrideMutex };
        if ( pOutSlot == nullptr || _pParentMaterial == nullptr )
            return;
        const vector<RHIDescriptorIndex>& listParentSrv = _pParentMaterial->getMaterialTextureSrvs();
        for ( uint32 slot = 0; slot < slotCount; ++slot )
        {
            pOutSlot[slot] = slot < listParentSrv.size() ? listParentSrv[slot] : kInvalidDescriptorIndex;
        }
        for ( const TextureOverride& texture : _listTextureOverride )
        {
            if ( texture._pTexture == nullptr )
                continue;
            const uint32 slot = findTextureSlot( texture._name );
            if ( slot < slotCount )
                pOutSlot[slot] = texture._pTexture->getSrv();
        }
    }

    void MaterialInstance::clearOverrides()
    {
        std::scoped_lock<mutex> lock{ _overrideMutex };
        _listValueOverride.clear();
        _listScalarOverride.clear();
        _listVectorOverride.clear();
        // 텍스처는 렌더 스레드가 돌려준 뒤 뺀다(syncTextureOverrides) — 여기서 지우면 빌린 참조를 잃는다.
        for ( TextureOverride& texture : _listTextureOverride )
        {
            texture._assetPath.clear();
        }
        _listKeywordOverride.clear();
        _listMultiCompileOverride.clear();
        _qualityOverride = MaterialQualityLevel::Count;
        _bDefinesDirty   = SW_TRUE;
        MaterialUtil::bumpPermutationGeneration();
        _bGpuDirty = SW_TRUE;
    }

    void MaterialInstance::enableKeyword( hashed_string keyword )
    {
        std::scoped_lock<mutex> lock{ _overrideMutex };
        MaterialInstanceInternal::insertOrAssign( _listKeywordOverride, keyword, true );
        _bDefinesDirty = SW_TRUE;
        MaterialUtil::bumpPermutationGeneration();
        _bGpuDirty = SW_TRUE;
    }

    void MaterialInstance::disableKeyword( hashed_string keyword )
    {
        std::scoped_lock<mutex> lock{ _overrideMutex };
        MaterialInstanceInternal::insertOrAssign( _listKeywordOverride, keyword, false );
        _bDefinesDirty = SW_TRUE;
        MaterialUtil::bumpPermutationGeneration();
        _bGpuDirty = SW_TRUE;
    }

    void MaterialInstance::setParent( Material* pParentMaterial )
    {
        std::scoped_lock<mutex> lock{ _overrideMutex };
        _pParentMaterial = pParentMaterial;
        _bDefinesDirty   = SW_TRUE;
        MaterialUtil::bumpPermutationGeneration();
        _bGpuDirty = SW_TRUE;
    }

    void MaterialInstance::setParameter( hashed_string name, string_view value )
    {
        std::scoped_lock<mutex> lock{ _overrideMutex };
        MaterialInstanceInternal::insertOrAssign( _listValueOverride, name, string( value ) );
        _bGpuDirty = SW_TRUE;
    }

    void MaterialInstance::setScalarParameter( hashed_string name, float32 value )
    {
        std::scoped_lock<mutex> lock{ _overrideMutex };
        MaterialInstanceInternal::insertOrAssign( _listScalarOverride, name, value );
        MaterialInstanceInternal::insertOrAssign( _listValueOverride, name, to_string( value ) );
        _bGpuDirty = SW_TRUE;
    }

    void MaterialInstance::setVectorParameter( hashed_string name, const float4& value )
    {
        std::scoped_lock<mutex> lock{ _overrideMutex };
        const array<float32, 4> val = { value._x, value._y, value._z, value._w };
        MaterialInstanceInternal::insertOrAssign( _listVectorOverride, name, val );
        StringBuilder<constant::kMaxBuffer64> sb;
        sb.appendFormat( "%# %# %# %#", value._x, value._y, value._z, value._w );
        MaterialInstanceInternal::insertOrAssign( _listValueOverride, name, string{ sb.c_str(), sb.size() } );
        _bGpuDirty = SW_TRUE;
    }

    void MaterialInstance::setTextureParameter( hashed_string name, string_view textureAssetPath )
    {
        // 게임 스레드는 원하는 경로만 적는다. 빌리고 돌려주는 것은 렌더 스레드의 updateRhi 다(syncTextureOverrides).
        std::scoped_lock<mutex> lock{ _overrideMutex };
        _bGpuDirty = SW_TRUE;
        for ( TextureOverride& texture : _listTextureOverride )
        {
            if ( texture._name != name )
                continue;
            texture._assetPath = string( textureAssetPath );
            // 같은 경로를 다시 주면 지난번에 빌리지 못한 것을 다시 시도한다(파일을 고친 뒤).
            if ( texture._pTexture == nullptr )
                texture._acquiredPath.clear();
            return;
        }
        if ( textureAssetPath.empty() )
            return;
        TextureOverride texture{};
        texture._name      = name;
        texture._assetPath = string( textureAssetPath );
        _listTextureOverride.push_back( std::move( texture ) );
    }

    void MaterialInstance::setQualityLevel( MaterialQualityLevel level )
    {
        std::scoped_lock<mutex> lock{ _overrideMutex };
        if ( _qualityOverride != level )
        {
            _qualityOverride = level;
            _bDefinesDirty   = SW_TRUE;
            MaterialUtil::bumpPermutationGeneration();
            _bGpuDirty = SW_TRUE;
        }
    }

    void MaterialInstance::setMultiCompile( hashed_string name, string_view selectedOption )
    {
        std::scoped_lock<mutex> lock{ _overrideMutex };
        MaterialInstanceInternal::insertOrAssign( _listMultiCompileOverride, name, string( selectedOption ) );
        _bDefinesDirty = SW_TRUE;
        MaterialUtil::bumpPermutationGeneration();
        _bGpuDirty = SW_TRUE;
    }

    bool MaterialInstance::getParameter( hashed_string name, string& outValue ) const
    {
        const string* val = MaterialInstanceInternal::findValue( _listValueOverride, name );
        if ( val != nullptr )
        {
            outValue = *val;
            return true;
        }
        if ( _pParentMaterial != nullptr )
        {
            const MaterialProperty* prop = _pParentMaterial->findProperty( name );
            if ( prop != nullptr )
            {
                outValue = prop->_value.empty() == false ? prop->_value : prop->_defaultValue;
                return true;
            }
        }
        return false;
    }

    float32 MaterialInstance::getScalarParameter( hashed_string name, float32 defaultValue ) const
    {
        const float32* pVal = MaterialInstanceInternal::findValue( _listScalarOverride, name );
        if ( pVal != nullptr )
            return *pVal;
        if ( _pParentMaterial != nullptr )
        {
            float32 value = defaultValue;
            if ( _pParentMaterial->getScalarParameter( name, value ) )
                return value;
        }
        return defaultValue;
    }

    const float32* MaterialInstance::getVectorParameter( hashed_string name ) const
    {
        const array<float32, 4>* pVal = MaterialInstanceInternal::findValue( _listVectorOverride, name );
        if ( pVal != nullptr )
            return pVal->data();
        if ( _pParentMaterial != nullptr )
        {
            const void* pData = _pParentMaterial->getParameterData( name.c_str() ? name.c_str() : "" );
            return pData ? reinterpret_cast<const float32*>( pData ) : nullptr;
        }
        return nullptr;
    }

    string MaterialInstance::getTextureParameter( hashed_string name ) const
    {
        for ( const TextureOverride& texture : _listTextureOverride )
        {
            if ( texture._name == name && texture._assetPath.empty() == false )
                return texture._assetPath;
        }
        if ( _pParentMaterial != nullptr )
        {
            const MaterialProperty* pProp = _pParentMaterial->findProperty( name );
            if ( pProp != nullptr )
                return pProp->_assetPath;
        }
        return string{};
    }

    bool MaterialInstance::isKeywordEnabled( hashed_string keyword ) const
    {
        const bool* pVal = MaterialInstanceInternal::findValue( _listKeywordOverride, keyword );
        if ( pVal != nullptr )
            return *pVal;
        if ( _pParentMaterial == nullptr )
            return false;
        const vector<string>& defs = _pParentMaterial->getCachedShaderDefines();
        const utf8*           pKey = keyword.c_str();
        if ( pKey == nullptr )
            return false;
        for ( const string& defineStr : defs )
        {
            if ( defineStr == pKey )
                return true;
        }
        return false;
    }

    const vector<string>& MaterialInstance::getCachedShaderDefines() const
    {
        uint64 currentParentHash = _pParentMaterial != nullptr ? _pParentMaterial->getPermutationHash() : 0;
        if ( _parentPermutationHash != currentParentHash )
        {
            _parentPermutationHash = currentParentHash;
            _bDefinesDirty         = SW_TRUE;
        }

        if ( _bDefinesDirty == SW_TRUE )
        {
            _listCachedDefine.clear();
            if ( _pParentMaterial != nullptr )
                _listCachedDefine = _pParentMaterial->getCachedShaderDefines();

            if ( _qualityOverride != MaterialQualityLevel::Count )
            {
                _listCachedDefine.erase( std::remove_if( _listCachedDefine.begin(), _listCachedDefine.end(),
                                                         []( string_view defineStr )
                { return StringUtil::startsWith( defineStr, "MATERIAL_QUALITY" ); } ),
                                         _listCachedDefine.end() );
                MaterialUtil::appendQualityDefines( _qualityOverride, _listCachedDefine );
            }

            for ( const auto& [name, selected] : _listMultiCompileOverride )
            {
                if ( _pParentMaterial != nullptr )
                {
                    for ( const MaterialMultiCompile& mc : _pParentMaterial->getPermutations()._listMultiCompile )
                    {
                        if ( hashed_string( mc._name.c_str() ) != name )
                            continue;
                        for ( const string& option : mc._listOption )
                        {
                            _listCachedDefine.erase( std::remove( _listCachedDefine.begin(), _listCachedDefine.end(), option ), _listCachedDefine.end() );
                        }
                        break;
                    }
                }
                MaterialUtil::appendUniqueDefine( _listCachedDefine, selected );
            }

            for ( const auto& [keyword, enabled] : _listKeywordOverride )
            {
                const utf8* pKey = keyword.c_str();
                if ( pKey == nullptr )
                    continue;
                _listCachedDefine.erase( std::remove( _listCachedDefine.begin(), _listCachedDefine.end(), string( pKey ) ), _listCachedDefine.end() );
                if ( enabled )
                    MaterialUtil::appendUniqueDefine( _listCachedDefine, pKey );
            }

            std::sort( _listCachedDefine.begin(), _listCachedDefine.end() );
            _cachedPermutationHash = MaterialUtil::hashDefines( _listCachedDefine );
            _bDefinesDirty         = SW_FALSE;
        }
        return _listCachedDefine;
    }

    uint64 MaterialInstance::getPermutationHash() const
    {
        if ( _bDefinesDirty == SW_TRUE )
            getCachedShaderDefines();
        return _cachedPermutationHash;
    }

    RHIDescriptorIndex MaterialInstance::getDescriptorIndex() const
    {
        if ( _descriptorIndex != kInvalidDescriptorIndex )
            return _descriptorIndex;
        if ( _pParentMaterial != nullptr )
            return _pParentMaterial->getDescriptorIndex();
        return kInvalidDescriptorIndex;
    }

    bool MaterialInstance::isParameterOverridden( hashed_string name ) const
    {
        if ( MaterialInstanceInternal::findValue( _listValueOverride, name ) != nullptr )
            return true;
        if ( MaterialInstanceInternal::findValue( _listScalarOverride, name ) != nullptr )
            return true;
        if ( MaterialInstanceInternal::findValue( _listVectorOverride, name ) != nullptr )
            return true;
        for ( const TextureOverride& texture : _listTextureOverride )
        {
            if ( texture._name == name && texture._assetPath.empty() == false )
                return true;
        }
        if ( MaterialInstanceInternal::findValue( _listKeywordOverride, name ) != nullptr )
            return true;
        if ( MaterialInstanceInternal::findValue( _listMultiCompileOverride, name ) != nullptr )
            return true;
        return false;
    }

    bool MaterialInstance::validateParametersWithReflection( const ShaderReflectionData& reflectionData ) const
    {
        auto checkParam = [&]( hashed_string paramName ) -> bool
        {
            for ( const ShaderBufferInfo& cb : reflectionData._listConstantBuffer )
            {
                for ( const ShaderVariableInfo& var : cb._listVariable )
                {
                    if ( hashed_string( var._name.c_str() ) == paramName )
                        return true;
                }
            }
            for ( const ShaderBufferInfo& element : reflectionData._listStructuredElement )
            {
                for ( const ShaderVariableInfo& var : element._listVariable )
                {
                    if ( hashed_string( var._name.c_str() ) == paramName )
                        return true;
                }
            }
            for ( const ShaderReflectedBinding& resourceBinding : reflectionData._listResource )
            {
                if ( hashed_string( resourceBinding._name.c_str() ) == paramName )
                    return true;
            }
            return false;
        };

        for ( const auto& [name, val] : _listValueOverride )
        {
            (void)val;
            if ( checkParam( name ) == false )
                return false;
        }
        return true;
    }

    void MaterialInstance::syncDescOverrides() const
    {
        auto self = const_cast<MaterialInstance*>( this );
        self->_desc._listOverride.clear();
        for ( const auto& [name, value] : _listValueOverride )
        {
            MaterialInstanceDesc::Override overrideItem{};
            overrideItem._name  = name.c_str() ? name.c_str() : "";
            overrideItem._value = value;
            self->_desc._listOverride.push_back( std::move( overrideItem ) );
        }
        // 텍스처 덮어쓰기는 값이 아니라 에셋 경로다(`assetPath`). 날 인덱스는 다시 올리면 낡으므로 저장하지 않는다.
        for ( const TextureOverride& texture : _listTextureOverride )
        {
            if ( texture._assetPath.empty() )
                continue;
            MaterialInstanceDesc::Override overrideItem{};
            overrideItem._name      = texture._name.c_str() ? texture._name.c_str() : "";
            overrideItem._assetPath = texture._assetPath;
            self->_desc._listOverride.push_back( std::move( overrideItem ) );
        }
        self->_desc._listKeyword.clear();
        for ( const auto& [name, enabled] : _listKeywordOverride )
        {
            MaterialInstanceDesc::KeywordOverride keywordItem{};
            keywordItem._name     = name.c_str() ? name.c_str() : "";
            keywordItem._bEnabled = enabled;
            self->_desc._listKeyword.push_back( std::move( keywordItem ) );
        }
        self->_desc._listMultiCompile.clear();
        for ( const auto& [name, selected] : _listMultiCompileOverride )
        {
            MaterialInstanceDesc::MultiCompileOverride multiCompileItem{};
            multiCompileItem._name     = name.c_str() ? name.c_str() : "";
            multiCompileItem._selected = selected;
            self->_desc._listMultiCompile.push_back( std::move( multiCompileItem ) );
        }
        if ( _qualityOverride != MaterialQualityLevel::Count )
            self->_desc._quality = MaterialUtil::qualityToString( _qualityOverride );
        else
            self->_desc._quality.clear();
        if ( _pParentMaterial != nullptr )
            self->_desc._parentPath.clear(); // 런타임 부모다. 경로가 필요하면 부르는 쪽이 채운다.
    }

    bool MaterialInstance::loadFromXml( string_view xmlText )
    {
        XmlDocument doc;
        if ( doc.parse( xmlText ) == false )
            return false;

        XmlNode root = doc.getRoot( "MaterialInstanceDesc" );
        if ( root.isValid() == false )
            return false;

        if ( AssetFormatRegistry::upgradeXmlWithActiveRegistry( AssetKind::MaterialInstance, doc, root, AssetFormatVersions::kMaterialInstance ) == false )
            return false;

        _desc             = MaterialInstanceDesc{};
        _desc._name       = MaterialUtil::fieldText( root, "name" );
        _desc._parentPath = MaterialUtil::fieldText( root, "parentPath" );
        _desc._quality    = MaterialUtil::fieldText( root, "quality" );

        XmlNode overrides = root.findChild( "_overrides" );
        if ( overrides.isValid() )
        {
            for ( XmlNode item = overrides.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
            {
                MaterialInstanceDesc::Override overrideItem{};
                overrideItem._name      = MaterialUtil::fieldText( item, "name" );
                overrideItem._value     = MaterialUtil::fieldText( item, "value" );
                overrideItem._assetPath = MaterialUtil::fieldText( item, "assetPath" );
                if ( overrideItem._name.empty() == false )
                    _desc._listOverride.push_back( std::move( overrideItem ) );
            }
        }
        XmlNode keywords = root.findChild( "_keywords" );
        if ( keywords.isValid() )
        {
            for ( XmlNode item = keywords.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
            {
                MaterialInstanceDesc::KeywordOverride keywordItem{};
                keywordItem._name     = MaterialUtil::fieldText( item, "name" );
                keywordItem._bEnabled = MaterialUtil::parseBoolField( item, "bEnabled", true );
                if ( keywordItem._name.empty() == false )
                    _desc._listKeyword.push_back( std::move( keywordItem ) );
            }
        }
        XmlNode multiCompileNode = root.findChild( "_multiCompiles" );
        if ( multiCompileNode.isValid() )
        {
            for ( XmlNode item = multiCompileNode.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
            {
                MaterialInstanceDesc::MultiCompileOverride multiCompileItem{};
                multiCompileItem._name     = MaterialUtil::fieldText( item, "name" );
                multiCompileItem._selected = MaterialUtil::fieldText( item, "selected" );
                if ( multiCompileItem._name.empty() == false )
                    _desc._listMultiCompile.push_back( std::move( multiCompileItem ) );
            }
        }

        _listValueOverride.clear();
        _listKeywordOverride.clear();
        _listMultiCompileOverride.clear();
        for ( TextureOverride& texture : _listTextureOverride )
        {
            texture._assetPath.clear(); // 렌더 스레드가 돌려준 뒤 뺀다
        }

        // `assetPath` 가 있는 항목은 텍스처 덮어쓰기다. 빠뜨리면 .materialinstance 의 텍스처가 조용히 부모 것으로 남는다.
        for ( const MaterialInstanceDesc::Override& overrideItem : _desc._listOverride )
        {
            if ( overrideItem._assetPath.empty() == false )
                setTextureParameter( hashed_string( overrideItem._name.c_str() ), overrideItem._assetPath );
            else
                MaterialInstanceInternal::insertOrAssign( _listValueOverride, hashed_string( overrideItem._name.c_str() ), overrideItem._value );
        }
        for ( const MaterialInstanceDesc::KeywordOverride& keywordItem : _desc._listKeyword )
        {
            MaterialInstanceInternal::insertOrAssign( _listKeywordOverride, hashed_string( keywordItem._name.c_str() ), keywordItem._bEnabled );
        }
        for ( const MaterialInstanceDesc::MultiCompileOverride& multiCompileItem : _desc._listMultiCompile )
        {
            MaterialInstanceInternal::insertOrAssign( _listMultiCompileOverride, hashed_string( multiCompileItem._name.c_str() ), multiCompileItem._selected );
        }
        if ( _desc._quality.empty() == false )
            _qualityOverride = MaterialUtil::parseQuality( _desc._quality );
        _bGpuDirty = SW_TRUE;
        return true;
    }

} // namespace sw
