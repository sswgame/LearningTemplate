#include "pch.h"

#include "Engine/Graphics/Material/MaterialInstance.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialPacking.h"
#include "Engine/Graphics/Material/MaterialPermutation.h"
#include "Engine/Graphics/Material/MaterialRenderProxy.h"
#include "Engine/Graphics/Material/MaterialXML.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionData.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

namespace sw
{
    namespace
    {
        struct MaterialInstanceInternal
        {
            /// @brief 리플렉션의 상수버퍼 변수 · 구조화 요소 변수 · 리소스 바인딩 중에 이름이 같은 것이 있는지 봅니다.
            static bool reflectionHasParam( const ShaderReflectionData& reflectionData, hashed_string paramName )
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
            }

            /**
             * @brief 부모의 매개변수 모양(셰이더 타입)이 쓰려는 값의 모양과 맞는지 봅니다. 어긋나면 로그 id 와 함께 false 입니다.
             * @details float4 글("0.5 0 0 0")을 스칼라(Float/Range)에 쓰면 패킹이 못 읽는 글로 거절한다. 부모에 없는 이름은 다른 변형이 쓸 수 있어 통과시킨다.
             *          쓸 때가 아니라 렌더 사본을 만들 때(`rebuildRenderData`, 게임 스레드, 부모 초기화 뒤) 본다 — 부모의 속성 목록은 디바이스 초기화가 채우고
             *          맞추므로 쓸 때 읽으면 비어 있거나(검사가 조용히 빠진다) 비동기 씬 로드 워커에서 게임 스레드의 초기화와 겹친다(언리얼 MID 도 쓸 때는 이름 · 값만 둔다).
             */
            [[nodiscard]] static bool canAcceptShape( const Material* pParent, hashed_string name, bool bScalar )
            {
                const MaterialProperty* pProp = pParent != nullptr ? pParent->findProperty( name ) : nullptr;
                if ( pProp == nullptr )
                    return true;
                const MaterialPropertyType shaderType = pProp->_shaderType;
                const bool                 bIsScalar  = shaderType == MaterialPropertyType::Float;
                const bool                 bIsVector  = shaderType == MaterialPropertyType::Float2 || shaderType == MaterialPropertyType::Float3 || shaderType == MaterialPropertyType::Float4;
                if ( ( bScalar && bIsVector ) || ( bScalar == false && bIsScalar ) )
                {
                    SW_LOG_ERROR_ID( "Material.ParameterShapeMismatch", "Material parameter '%#' is %# and cannot take a %# value", pProp->_name, MaterialPacking::typeToString( shaderType ),
                                     bScalar ? "scalar" : "vector" );
                    return false;
                }
                return true;
            }

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
    SW_ASSET_TYPE( MaterialInstance, MaterialInstance::kAssetType );

    shared_ptr<MaterialInstance> MaterialInstance::create( Material* pParentMaterial )
    {
        // 인자가 Material* 이라 ADL 이 std::make_shared 를 끌어온다. 한정해야 sw 것이 잡힌다.
        return sw::make_shared<MaterialInstance>( CreateKey{}, pParentMaterial );
    }

    MaterialInstance::MaterialInstance( CreateKey, Material* pParentMaterial )
        : _pParentMaterial{ pParentMaterial }
        , _desc{}
        , _listValueOverride{}
        , _listTextureOverride{}
        , _listKeywordOverride{}
        , _listMultiCompileOverride{}
        , _qualityOverride{ MaterialQualityLevel::Count }
        , _renderProxy{ MaterialRenderProxy::create() }
        , _renderData{}
        , _parentBufferGeneration{ 0 }
        , _overrideRevision{}
        , _renderDataOverrideRevision{ 0 }
        , _pTextureDevice{ nullptr }
        , _textureReloadGeneration{ 0 }
        , _listCachedDefine{}
        , _cachedPermutationHash{ 0 }
        , _parentPermutationHash{ 0 }
        , _bDefinesDirty{ SW_TRUE }
        , _instReserved{ 0 }
        , _renderResourceHook{ make_unique<RHIRenderResourceHook<MaterialInstance>>( *this ) }
    {
    }

    MaterialInstance::~MaterialInstance()
    {
        // 텍스처를 요청한 디바이스가 죽었으면 clearRHI 가 이미 비웠다 — 남아 있으면 살아 있는 디바이스다. 상수 버퍼는 프록시가 놓는다.
        releaseTextureOverrides();
    }

    void MaterialInstance::clearRHI( IRHIDevice* pDevice )
    {
        // 텍스처는 디바이스와 함께 갔다(TextureCache 의 Texture2D 도 같은 통보를 받는다). 참조만 놓는다.
        if ( _pTextureDevice == pDevice )
            releaseTextureOverrides();
    }

    void MaterialInstance::releaseRHI( IRHIDevice* pRHI )
    {
        if ( pRHI == nullptr || _pTextureDevice == pRHI )
            releaseTextureOverrides();
    }

    bool MaterialInstance::loadFromAsset( const AssetID& assetID )
    {
        XMLDocument doc;
        if ( doc.loadResource( assetID ) == false )
            return false;
        return loadFromXML( doc.saveToString() );
    }

    bool MaterialInstance::saveToFile( const AbsolutePath& absPath ) const
    {

        syncDescOverrides();

        XMLDocument doc;
        XMLNode     root = doc.appendRoot( "MaterialInstanceDef" );
        AssetXMLFormat::writeXMLVersion( root, kAssetType._formatVersion );
        XMLFields::writeFields( root, MaterialInstanceRootParams{ _desc._name, _desc._parentPath } );
        if ( _desc._quality.empty() == false )
            root.appendAttribute( "quality", string_view( _desc._quality ) ); // 적지 않음이 "덮어쓰지 않음"이다

        XMLNode overrides = root.appendChild( "_overrides" );
        for ( const MaterialInstanceDef::Override& overrideItem : _desc._listOverride )
        {
            XMLFields::writeFields( overrides.appendChild( "item" ), MaterialOverrideParams{ overrideItem._name, overrideItem._value, overrideItem._assetPath } );
        }

        if ( _desc._listKeyword.empty() == false )
        {
            XMLNode list = root.appendChild( "_keywords" );
            for ( const MaterialInstanceDef::KeywordOverride& keywordItem : _desc._listKeyword )
            {
                XMLFields::writeFields( list.appendChild( "item" ), MaterialKeywordOverrideParams{ keywordItem._name, keywordItem._bEnabled } );
            }
        }
        if ( _desc._listMultiCompile.empty() == false )
        {
            XMLNode list = root.appendChild( "_multiCompiles" );
            for ( const MaterialInstanceDef::MultiCompileOverride& multiCompileItem : _desc._listMultiCompile )
            {
                XMLFields::writeFields( list.appendChild( "item" ), MaterialMultiCompileParams{ multiCompileItem._name, multiCompileItem._selected } );
            }
        }

        return doc.saveToFile( absPath.str() );
    }

    MaterialRenderBinding MaterialInstance::getRenderBinding()
    {
        if ( _pParentMaterial == nullptr )
            return MaterialRenderBinding{};
        // 부모를 직접 그리지 않아도 부모의 레이아웃 · 텍스처 SRV 를 맞춘다 — 그래야 아래 세대 비교가 부모의 새 바이트를 본다.
        _pParentMaterial->syncRenderState();
        // 텍스처는 부모가 빌린 디바이스로 요청한다. 부모가 아직 초기화 전이면 디바이스가 없어 덮어쓴 텍스처를 미룬다(그동안 부모 텍스처가 보인다).
        IRHIDevice* pDevice = _pParentMaterial->getRHIDevice();
        // 텍스처 덮어쓰기의 새 SRV(다시 올림 · 비동기 로드 끝)는 텍스처 캐시의 세대가 알린다 — 장면 스냅샷도 그 세대를 함께 본다.
        const bool bTextureChanged = pDevice != nullptr && syncTextureOverrides( pDevice );
        // 부모 값이 바뀌었으면(값 · 레이아웃 · 다시 로드 · 텍스처 슬롯) 사본도 낡았다. 덮어쓰기가 바뀔 때만 다시 만들면 덮어쓰지 않은 파라미터에서
        // 부모의 값 변경과 다시 맞춘 레이아웃을 놓친다.
        const bool bParentChanged   = _parentBufferGeneration != _pParentMaterial->getBufferGeneration();
        const bool bOverrideChanged = _overrideRevision.get() != _renderDataOverrideRevision;
        if ( bTextureChanged || bParentChanged || bOverrideChanged || _renderData == nullptr )
            rebuildRenderData( pDevice );
        return MaterialRenderBinding{ _renderProxy, _renderData };
    }

    void MaterialInstance::rebuildRenderData( IRHIDevice* pDevice )
    {
        shared_ptr<MaterialRenderData> data = sw::make_shared<MaterialRenderData>();
        data->_bytes                        = _pParentMaterial->getBuffer();
        data->_elementStride                = _pParentMaterial->getElementStride();
        _parentBufferGeneration             = _pParentMaterial->getBufferGeneration();
        _renderDataOverrideRevision         = _overrideRevision.get();
        // 부모 바이트가 비었으면(레이아웃 오류 — 오류는 부모가 남겼다) 빈 사본이다. 렌더 스레드는 빈 바이트를 올리지 않는다.
        if ( data->_bytes.empty() == false )
        {
            dropShapeMismatchedOverrides();
            for ( const ValueOverride& valueOverride : _listValueOverride )
            {
                if ( valueOverride._kind == ValueKind::Text )
                    _pParentMaterial->packNamedValueIntoBuffer( valueOverride._name, valueOverride._text, data->_bytes );
                else
                    _pParentMaterial->packNamedFloatsIntoBuffer( valueOverride._name, valueOverride._arrValue, valueOverride.getFloatCount(), data->_bytes );
            }
            // 덮어쓴 텍스처: 네이티브 bindless 는 지금 SRV 인덱스를, 슬롯 바인딩 백엔드(DX11 · GL)는 슬롯 서수를 넣는다(부모와 같은 규칙).
            const bool bNativeBindless = pDevice != nullptr && pDevice->supportsNativeBindlessSampling();
            for ( const TextureOverride& texture : _listTextureOverride )
            {
                const Texture2D* pTexture = texture.findReadyTexture();
                if ( pTexture == nullptr )
                    continue;
                const optional<uint32> textureBinding = bNativeBindless ? optional<uint32>{ pTexture->getSRV() } : findTextureSlot( texture._name );
                if ( textureBinding.has_value() && *textureBinding != kInvalidDescriptorIndex )
                    _pParentMaterial->packTextureIntoBuffer( texture._name, *textureBinding, data->_bytes );
            }
        }
        fillTextureSlots( data->_arrTextureSRV, shaderslot::kMaterialTextureCount );
        _renderData = std::move( data );
    }

    void MaterialInstance::dropShapeMismatchedOverrides()
    {
        // 어긋난 값은 한 번 알리고 버린다(쓸 때 거절하던 것과 같은 결과) — 남겨 두면 사본을 만들 때마다 다시 알린다.
        const Material* pParent = _pParentMaterial;
        VectorUtil::removeIf( _listValueOverride, [pParent]( const ValueOverride& valueOverride )
        { return valueOverride._kind != ValueKind::Text && MaterialInstanceInternal::canAcceptShape( pParent, valueOverride._name, valueOverride._kind == ValueKind::Scalar ) == false; } );
    }

    string MaterialInstance::ValueOverride::makeText() const
    {
        return _kind == ValueKind::Text ? _text : MaterialPacking::formatFloats( _arrValue, getFloatCount() );
    }

    MaterialInstance::ValueOverride* MaterialInstance::findValueOverride( hashed_string name )
    {
        for ( ValueOverride& valueOverride : _listValueOverride )
        {
            if ( valueOverride._name == name )
                return &valueOverride;
        }
        return nullptr;
    }

    void MaterialInstance::assignFloats( hashed_string name, ValueKind kind, const float32* pValue, uint32 count )
    {
        ValueOverride* pOverride = findValueOverride( name );
        if ( pOverride == nullptr )
        {
            pOverride        = &_listValueOverride.emplace_back();
            pOverride->_name = name;
        }
        else if ( pOverride->_kind == kind && Memory::compare( pOverride->_arrValue, pValue, count * sizeof( float32 ) ) == 0 )
        {
            return;
        }
        pOverride->_text.clear();
        pOverride->_kind = kind;
        Memory::copy( pOverride->_arrValue, pValue, count * sizeof( float32 ) );
        markRenderDirty();
    }

    bool MaterialInstance::syncTextureOverrides( IRHIDevice* pRHI )
    {
        if ( _listTextureOverride.empty() || engine::areEngineServicesBound() == false )
            return false;
        TextureCache& textures = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
        bool          bChanged{ false };
        // 디바이스가 바뀌었으면 옛 디바이스로 요청한 것을 놓고 새로 요청한다(옛 것이 이미 죽었으면 clearRHI 가 먼저 와서 비웠다).
        if ( _pTextureDevice != nullptr && _pTextureDevice != pRHI )
        {
            releaseTextureOverrides();
            bChanged = true;
        }
        _pTextureDevice = pRHI;

        for ( TextureOverride& texture : _listTextureOverride )
        {
            if ( texture._acquiredPath != texture._assetPath )
            {
                texture._load.reset();
                texture._acquiredPath = texture._assetPath;
                bChanged              = true;
                // 게임 스레드가 디스크를 기다리지 않게 워커가 읽는다 — 다 읽기 전 프레임은 부모의 텍스처가 보인다(`Material` 의 텍스처 자리와 같은 길, 언리얼 MID 도
                // 스트리밍 중인 텍스처 파라미터는 낮은 밉 · 기본 텍스처로 그린다). 업로드가 끝나면 리로드 세대가 올라 아래에서 다시 패킹한다.
                if ( texture._assetPath.empty() == false )
                    texture._load = textures.request( texture._assetPath, AssetLoadStart::Async );
            }
            if ( texture._load.hasFailed() )
            {
                SW_LOG_WARNING_ID( "MaterialInstance.MaterialInstanceTextureCouldNot", "MaterialInstance '%#': texture '%#' for '%#' could not be loaded (%#) — the parent's texture stays.", _desc._name.c_str(),
                                   texture._assetPath.c_str(), texture._name.c_str(), texture._load.getError().c_str() );
                texture._load.reset(); // 경로는 적어 두었다 — 다시 묻지 않는다(같은 경로를 다시 주면 `setTextureParameter` 가 다시 시도하게 한다)
            }
        }
        // 지운 덮어쓰기(빈 경로)는 놓은 뒤 뺀다.
        VectorUtil::removeIf( _listTextureOverride, []( const TextureOverride& texture )
        { return texture._assetPath.empty() && texture._load.isValid() == false; } );

        // 다시 올린 텍스처는 같은 객체에 새 SRV 를 받았고, 비동기 로드가 끝난 텍스처는 처음 SRV 를 받았다 — 다시 패킹한다.
        const uint32 reloadGeneration = textures.getReloadGeneration();
        if ( reloadGeneration != _textureReloadGeneration )
        {
            _textureReloadGeneration = reloadGeneration;
            bChanged                 = true;
        }
        return bChanged;
    }

    void MaterialInstance::releaseTextureOverrides()
    {
        for ( TextureOverride& texture : _listTextureOverride )
        {
            texture._load.reset(); // 참조는 핸들이 쥐었다 — 놓으면 캐시에 돌려준다(`Material::releaseTextureAssets` 와 같다)
            texture._acquiredPath.clear();
        }
        _pTextureDevice = nullptr;
        markRenderDirty();
    }

    optional<uint32> MaterialInstance::findTextureSlot( hashed_string name ) const
    {
        if ( _pParentMaterial == nullptr )
            return nullopt;
        const optional<uint32> parentSlot = _pParentMaterial->findTextureSlot( name );
        if ( parentSlot.has_value() )
            return parentSlot;
        // 부모에 없는 프로퍼티는 부모 슬롯 뒤에 덮어쓰기 순서대로 잇는다. 지울 것(빈 경로)은 자리를 차지하지 않는다.
        uint32 slot = static_cast<uint32>( _pParentMaterial->getMaterialTextureSRVs().size() );
        for ( const TextureOverride& texture : _listTextureOverride )
        {
            if ( texture._assetPath.empty() || _pParentMaterial->findTextureSlot( texture._name ).has_value() )
                continue;
            if ( texture._name == name )
                return slot < shaderslot::kMaterialTextureCount ? optional<uint32>{ slot } : nullopt;
            ++slot;
        }
        return nullopt;
    }

    void MaterialInstance::fillTextureSlots( RHIDescriptorIndex* pOutSlot, uint32 slotCount ) const
    {
        if ( pOutSlot == nullptr || _pParentMaterial == nullptr )
            return;
        const vector<RHIDescriptorIndex>& listParentSRV = _pParentMaterial->getMaterialTextureSRVs();
        for ( uint32 slot = 0; slot < slotCount; ++slot )
        {
            pOutSlot[slot] = slot < listParentSRV.size() ? listParentSRV[slot] : kInvalidDescriptorIndex;
        }
        for ( const TextureOverride& texture : _listTextureOverride )
        {
            const Texture2D* pTexture = texture.findReadyTexture();
            if ( pTexture == nullptr )
                continue;
            const optional<uint32> slot = findTextureSlot( texture._name );
            if ( slot.has_value() && *slot < slotCount )
                pOutSlot[*slot] = pTexture->getSRV();
        }
    }

    void MaterialInstance::clearOverrides()
    {
        _listValueOverride.clear();
        // 텍스처는 돌려준 뒤 뺀다(syncTextureOverrides) — 여기서 지우면 빌린 참조를 잃는다.
        for ( TextureOverride& texture : _listTextureOverride )
        {
            texture._assetPath.clear();
        }
        _listKeywordOverride.clear();
        _listMultiCompileOverride.clear();
        _qualityOverride = MaterialQualityLevel::Count;
        _bDefinesDirty   = SW_TRUE;
        MaterialPermutation::bumpPermutationGeneration();
        markRenderDirty();
    }

    void MaterialInstance::enableKeyword( hashed_string keyword )
    {
        MaterialInstanceInternal::insertOrAssign( _listKeywordOverride, keyword, true );
        _bDefinesDirty = SW_TRUE;
        MaterialPermutation::bumpPermutationGeneration();
        markRenderDirty();
    }

    void MaterialInstance::disableKeyword( hashed_string keyword )
    {
        MaterialInstanceInternal::insertOrAssign( _listKeywordOverride, keyword, false );
        _bDefinesDirty = SW_TRUE;
        MaterialPermutation::bumpPermutationGeneration();
        markRenderDirty();
    }

    void MaterialInstance::setParent( Material* pParentMaterial )
    {
        _pParentMaterial = pParentMaterial;
        _bDefinesDirty   = SW_TRUE;
        MaterialPermutation::bumpPermutationGeneration();
        markRenderDirty();
    }

    void MaterialInstance::setParameter( hashed_string name, string_view value )
    {
        ValueOverride* pOverride = findValueOverride( name );
        if ( pOverride == nullptr )
        {
            pOverride        = &_listValueOverride.emplace_back();
            pOverride->_name = name;
        }
        else if ( pOverride->_kind == ValueKind::Text && pOverride->_text == value )
        {
            return;
        }
        pOverride->_kind = ValueKind::Text;
        pOverride->_text = value;
        markRenderDirty();
    }

    void MaterialInstance::setScalarParameter( hashed_string name, float32 value )
    {
        assignFloats( name, ValueKind::Scalar, &value, 1 );
    }

    void MaterialInstance::setVectorParameter( hashed_string name, const float4& value )
    {
        const float32 arrValue[4] = { value._x, value._y, value._z, value._w };
        assignFloats( name, ValueKind::Vector, arrValue, 4 );
    }

    void MaterialInstance::setTextureParameter( hashed_string name, string_view textureAssetPath )
    {
        // 원하는 경로만 적는다. 빌리고 돌려주는 것은 다음 getRenderBinding 이다(syncTextureOverrides).
        markRenderDirty();
        for ( TextureOverride& texture : _listTextureOverride )
        {
            if ( texture._name != name )
                continue;
            texture._assetPath = string( textureAssetPath );
            // 같은 경로를 다시 주면 지난번에 읽지 못한 것을 다시 시도한다(파일을 고친 뒤). 읽는 중이거나 준비된 것은 그대로 둔다.
            if ( texture._load.isValid() == false )
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
        if ( _qualityOverride != level )
        {
            _qualityOverride = level;
            _bDefinesDirty   = SW_TRUE;
            MaterialPermutation::bumpPermutationGeneration();
            markRenderDirty();
        }
    }

    void MaterialInstance::setMultiCompile( hashed_string name, string_view selectedOption )
    {
        MaterialInstanceInternal::insertOrAssign( _listMultiCompileOverride, name, string( selectedOption ) );
        _bDefinesDirty = SW_TRUE;
        MaterialPermutation::bumpPermutationGeneration();
        markRenderDirty();
    }

    bool MaterialInstance::getParameter( hashed_string name, string& outValue ) const
    {
        for ( const ValueOverride& valueOverride : _listValueOverride )
        {
            if ( valueOverride._name == name )
            {
                outValue = valueOverride.makeText();
                return true;
            }
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

    const vector<string>& MaterialInstance::getCachedShaderDefines() const
    {
        refreshCachedDefines();
        return _listCachedDefine;
    }

    void MaterialInstance::refreshCachedDefines() const
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
                VectorUtil::removeIf( _listCachedDefine, []( string_view defineStr )
                { return StringUtil::startsWith( defineStr, "MATERIAL_QUALITY" ); } );
                MaterialPermutation::appendQualityDefines( _qualityOverride, _listCachedDefine );
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
                            VectorUtil::removeValue( _listCachedDefine, option );
                        }
                        break;
                    }
                }
                MaterialPermutation::appendUniqueDefine( _listCachedDefine, selected );
            }

            for ( const auto& [keyword, enabled] : _listKeywordOverride )
            {
                const utf8* pKey = keyword.c_str();
                if ( pKey == nullptr )
                    continue;
                VectorUtil::removeValue( _listCachedDefine, string( pKey ) );
                if ( enabled )
                    MaterialPermutation::appendUniqueDefine( _listCachedDefine, pKey );
            }

            VectorUtil::sort( _listCachedDefine );
            _cachedPermutationHash = MaterialPermutation::hashDefines( _listCachedDefine );
            // 렌더 상태 · 그리기 순서는 부모의 것이다 — 부모와 같은 해시 규칙으로 섞어야 다른 렌더 상태의 부모가 한 배치로 합쳐지지 않는다.
            const uint64 renderFieldsHash = _pParentMaterial != nullptr ? _pParentMaterial->getRenderFieldsHash() : 0u;
            if ( renderFieldsHash != 0 )
                _cachedPermutationHash = HashUtil::combine( _cachedPermutationHash, renderFieldsHash );
            _bDefinesDirty = SW_FALSE;
        }
    }

    uint64 MaterialInstance::getPermutationHash() const
    {
        refreshCachedDefines();
        return _cachedPermutationHash;
    }

    bool MaterialInstance::isParameterOverridden( hashed_string name ) const
    {
        for ( const ValueOverride& valueOverride : _listValueOverride )
        {
            if ( valueOverride._name == name )
                return true;
        }
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
        for ( const ValueOverride& valueOverride : _listValueOverride )
        {
            if ( MaterialInstanceInternal::reflectionHasParam( reflectionData, valueOverride._name ) == false )
                return false;
        }
        return true;
    }

    void MaterialInstance::syncDescOverrides() const
    {
        auto self = const_cast<MaterialInstance*>( this );
        self->_desc._listOverride.clear();
        for ( const ValueOverride& valueOverride : _listValueOverride )
        {
            MaterialInstanceDef::Override overrideItem{};
            overrideItem._name  = valueOverride._name.c_str() ? valueOverride._name.c_str() : "";
            overrideItem._value = valueOverride.makeText();
            self->_desc._listOverride.push_back( std::move( overrideItem ) );
        }
        // 텍스처 덮어쓰기는 값이 아니라 에셋 경로다(`assetPath`). 날 인덱스는 다시 올리면 낡으므로 저장하지 않는다.
        for ( const TextureOverride& texture : _listTextureOverride )
        {
            if ( texture._assetPath.empty() )
                continue;
            MaterialInstanceDef::Override overrideItem{};
            overrideItem._name      = texture._name.c_str() ? texture._name.c_str() : "";
            overrideItem._assetPath = texture._assetPath;
            self->_desc._listOverride.push_back( std::move( overrideItem ) );
        }
        self->_desc._listKeyword.clear();
        for ( const auto& [name, enabled] : _listKeywordOverride )
        {
            MaterialInstanceDef::KeywordOverride keywordItem{};
            keywordItem._name     = name.c_str() ? name.c_str() : "";
            keywordItem._bEnabled = enabled;
            self->_desc._listKeyword.push_back( std::move( keywordItem ) );
        }
        self->_desc._listMultiCompile.clear();
        for ( const auto& [name, selected] : _listMultiCompileOverride )
        {
            MaterialInstanceDef::MultiCompileOverride multiCompileItem{};
            multiCompileItem._name     = name.c_str() ? name.c_str() : "";
            multiCompileItem._selected = selected;
            self->_desc._listMultiCompile.push_back( std::move( multiCompileItem ) );
        }
        if ( _qualityOverride != MaterialQualityLevel::Count )
            self->_desc._quality = getEnumName( _qualityOverride );
        else
            self->_desc._quality.clear();
        if ( _pParentMaterial != nullptr )
            self->_desc._parentPath.clear(); // 런타임 부모다. 경로가 필요하면 부르는 쪽이 채운다.
    }

    bool MaterialInstance::loadFromXML( string_view xmlText )
    {
        XMLDocument doc;
        if ( doc.parse( xmlText ) == false )
            return false;

        XMLNode root = doc.getRoot( "MaterialInstanceDef" );
        if ( root.isValid() == false )
            return false;

        if ( AssetXMLFormat::upgradeToCurrentXMLVersion( kAssetType, root ) == false )
            return false;

        // 칸은 스키마(MaterialXML.h)가 정한다. 품질은 적지 않음이 "덮어쓰지 않음"이라 따로 읽는다.
        const string_view              sourceName{ root.getAttributeText( "name" ) };
        MaterialInstanceRootParams     rootParams;
        optional<MaterialQualityLevel> quality;
        if ( XMLFields::readFields( root, rootParams, sourceName, { "formatVersion", "quality" } ) == false ||
             XMLFields::readOptionalEnum( root, "quality", quality, sourceName ) == false )
            return false;
        _desc             = MaterialInstanceDef{};
        _desc._name       = rootParams._name;
        _desc._parentPath = rootParams._parentPath;
        _desc._quality    = quality.has_value() ? string( getEnumName( *quality ) ) : string{};

        XMLNode overrides = root.findChild( "_overrides" );
        if ( overrides.isValid() )
        {
            for ( XMLNode item = overrides.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
            {
                MaterialOverrideParams params;
                if ( XMLFields::readFields( item, params, sourceName ) == false )
                    return false;
                _desc._listOverride.push_back( MaterialInstanceDef::Override{ std::move( params._name ), std::move( params._value ), std::move( params._assetPath ) } );
            }
        }
        XMLNode keywords = root.findChild( "_keywords" );
        if ( keywords.isValid() )
        {
            for ( XMLNode item = keywords.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
            {
                MaterialKeywordOverrideParams params;
                if ( XMLFields::readFields( item, params, sourceName ) == false )
                    return false;
                _desc._listKeyword.push_back( MaterialInstanceDef::KeywordOverride{ std::move( params._name ), params._bEnabled } );
            }
        }
        XMLNode multiCompileNode = root.findChild( "_multiCompiles" );
        if ( multiCompileNode.isValid() )
        {
            for ( XMLNode item = multiCompileNode.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
            {
                MaterialMultiCompileParams params;
                if ( XMLFields::readFields( item, params, sourceName ) == false )
                    return false;
                _desc._listMultiCompile.push_back( MaterialInstanceDef::MultiCompileOverride{ std::move( params._name ), std::move( params._selected ) } );
            }
        }

        _listValueOverride.clear();
        _listKeywordOverride.clear();
        _listMultiCompileOverride.clear();
        for ( TextureOverride& texture : _listTextureOverride )
        {
            texture._assetPath.clear(); // 돌려준 뒤 뺀다(syncTextureOverrides)
        }

        // `assetPath` 가 있는 항목은 텍스처 덮어쓰기다. 빠뜨리면 .materialinstance 의 텍스처가 조용히 부모 것으로 남는다.
        for ( const MaterialInstanceDef::Override& overrideItem : _desc._listOverride )
        {
            if ( overrideItem._assetPath.empty() == false )
                setTextureParameter( hashed_string( overrideItem._name.c_str() ), overrideItem._assetPath );
            else
                setParameter( hashed_string( overrideItem._name.c_str() ), overrideItem._value );
        }
        for ( const MaterialInstanceDef::KeywordOverride& keywordItem : _desc._listKeyword )
        {
            MaterialInstanceInternal::insertOrAssign( _listKeywordOverride, hashed_string( keywordItem._name.c_str() ), keywordItem._bEnabled );
        }
        for ( const MaterialInstanceDef::MultiCompileOverride& multiCompileItem : _desc._listMultiCompile )
        {
            MaterialInstanceInternal::insertOrAssign( _listMultiCompileOverride, hashed_string( multiCompileItem._name.c_str() ), multiCompileItem._selected );
        }
        if ( quality.has_value() )
            _qualityOverride = *quality;
        markRenderDirty();
        return true;
    }

} // namespace sw
