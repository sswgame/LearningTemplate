/**
 * @file MaterialInstance.h
 * @brief 마스터 Material 위에 드로우나 액터 단위로 덮어쓰는 오버라이드를 정의합니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/RHI/RHIResidentBuffer.h"

namespace sw
{
    class Texture2D;

    /**
     * @class MaterialInstance
     * @brief 마스터 Material 위에 드로우나 액터 단위로 덮어쓰는 오버라이드입니다(UE 의 MaterialInstanceDynamic).
     */
    class SW_API MaterialInstance final : public RHIRenderResource
    {
    public:
        /** @brief 머티리얼 인스턴스 에셋 종류입니다(등록은 `MaterialInstance.cpp` 의 `SW_ASSET_TYPE`). 경로에 정해진 접미사가 없어 접미사를 두지 않는다. 판 0. */
        static constexpr AssetTypeDef kAssetType{ "MaterialInstance", nullptr, 0, {}, 0, {} };

        /**
         * @brief 생성 키입니다. create() 만 만들 수 있습니다.
         * @details 생성자가 이 키를 요구하므로 `make_shared<MaterialInstance>()` 도 스택의 `MaterialInstance x;` 도 **컴파일되지 않는다.**
         *          모든 MaterialInstance 가 Engine 안에서 shared_ptr 로 만들어진다는 것을 컴파일러가 보장한다. 그래서 렌더 패킷이
         *          소유권을 가질 수 있고, 제어 블록이 모듈 DLL 에 있는 일이 없다. 린트가 아니라 타입이 지킨다.
         */
        struct CreateKey
        {
        private:
            CreateKey() = default;
            friend class MaterialInstance;
        };
        /** @brief create() 전용 생성자입니다. 마스터 머티리얼에 연결됩니다. */
        MaterialInstance( CreateKey, Material* pParentMaterial );
        /**
         * @brief Engine.dll 안에서 shared_ptr 로 만듭니다.
         * @details 제어 블록(소멸 코드)은 make_shared 를 부른 DLL 에 있다. 렌더 패킷(GPUScene 스냅샷)이 소유권을 함께
         *          싣고 있으므로 게임 모듈이 만든 인스턴스를 엔진이 마지막까지 가질 수 있고, 모듈이 언로드된 뒤 놓으면
         *          없는 코드로 뛰어든다. 여기서 만들면 누가 마지막에 놓든 Engine 코드가 실행된다.
         */
        static shared_ptr<MaterialInstance> create( Material* pParentMaterial );
        /** @brief 오버라이드 CB 를 정리합니다. */
        ~MaterialInstance() override;

        /** @brief 복사를 금지합니다. */
        MaterialInstance( const MaterialInstance& ) = delete;
        /** @brief 대입을 금지합니다. */
        MaterialInstance& operator=( const MaterialInstance& ) = delete;

        /** @brief (RHIRenderResource) 살아 있는 디바이스에 상수 버퍼를 반환합니다. */
        void releaseRHI( IRHIDevice* pDevice ) override;
        /** @brief (RHIRenderResource) 디바이스가 이미 없을 때 부릅니다. 핸들만 버립니다. */
        void forgetRHI( IRHIDevice* pDevice ) override;

        /** @brief 오버라이드만 있는 MaterialInstanceDesc XML 을 로드합니다. 부모는 따로 설정합니다. */
        [[nodiscard]] bool loadFromFile( string_view assetRelativePath );
        /** @brief 인스턴스 XML 을 저장합니다. */
        [[nodiscard]] bool saveToFile( string_view assetRelativePath ) const;
        /**
         * @brief CPU 버퍼(부모 기본값에 오버라이드를 더한 것)를 만들고 인스턴스 CB 를 만들거나 갱신합니다.
         * @return 인스턴스 CB 와 bindless 인덱스가 준비되었으면 true 입니다. 부모가 없거나 부모 버퍼가 비어 있거나 버퍼를 만들지 못하면 false 입니다.
         */
        bool updateRHI( IRHIDevice* pRHI );
        /** @brief 오버라이드를 모두 지웁니다. */
        void clearOverrides();
        /** @brief 키워드를 켭니다. */
        void enableKeyword( hashed_string keyword );
        /** @brief 키워드를 끕니다. */
        void disableKeyword( hashed_string keyword );

        /** @brief 부모 머티리얼을 설정합니다. */
        void setParent( Material* pParentMaterial );
        /** @brief 인스턴스 이름을 설정합니다. */
        void setName( string_view name ) { _desc._name = name; }
        /** @brief 일반 오버라이드입니다(Enum/BitFlag/Color/Range/Bool/스칼라 텍스트). */
        void setParameter( hashed_string name, string_view value );
        /** @brief 스칼라 파라미터를 설정합니다. */
        void setScalarParameter( hashed_string name, float32 value );
        /** @brief 벡터 파라미터를 설정합니다. */
        void setVectorParameter( hashed_string name, const float4& value );
        /**
         * @brief 텍스처 파라미터를 **텍스처 에셋**으로 덮어씁니다(리소스 경로, 예: `engine/textures/perlin.dds`). 빈 경로면 덮어쓰기를 지웁니다.
         * @details 언리얼 MIC 의 `SetTextureParameterValue( UTexture* )`, 유니티 `MaterialPropertyBlock.SetTexture( Texture )` 처럼 디스크립터
         *          인덱스가 아니라 텍스처 자체를 가리킨다. 게임 스레드는 원하는 경로만 적고, 렌더 스레드의 `updateRHI` 가 TextureCache 로 빌리고
         *          반환한다(그 텍스처를 읽는 프레임이 렌더 스레드에 있다). 셰이더에 넣는 값(네이티브 bindless 의 SRV 인덱스, DX11 과 GL 의 슬롯
         *          서수)은 그때마다 지금 텍스처에서 읽으므로 텍스처를 다시 올려도(핫 리로드) 따라간다.
         *          주의: 디스크립터 인덱스를 직접 가지고 있으면 다시 올린 뒤 반환한 슬롯을 읽고, DX11 과 GL 에서는 그 인덱스가 슬롯 서수로 읽혀 엉뚱한
         *          슬롯을 가리킨다.
         */
        void setTextureParameter( hashed_string name, string_view textureAssetPath );
        /** @brief 품질 레벨을 설정합니다. */
        void setQualityLevel( MaterialQualityLevel level );
        /** @brief 멀티컴파일 옵션을 선택합니다. */
        void setMultiCompile( hashed_string name, string_view selectedOption );

        /** @brief 부모 머티리얼을 반환합니다. */
        Material* getParent() const { return _pParentMaterial; }
        /** @brief 인스턴스 디스크립터를 반환합니다. */
        const MaterialInstanceDesc& getDesc() const { return _desc; }
        /** @brief 이름으로 찾은 파라미터 텍스트를 읽습니다. */
        bool getParameter( hashed_string name, string& outValue ) const;
        /** @brief 스칼라 파라미터를 반환합니다. */
        float32 getScalarParameter( hashed_string name, float32 defaultValue = 0.0f ) const;
        /** @brief 텍스처 파라미터의 에셋 경로입니다. 덮어썼으면 그 경로, 아니면 부모 프로퍼티의 경로, 둘 다 없으면 빈 문자열입니다. */
        string getTextureParameter( hashed_string name ) const;
        /**
         * @brief 이 인스턴스로 그릴 때의 텍스처 슬롯(DX11 과 GL 의 t5..)에 바인딩할 SRV 를 채웁니다. 부모의 슬롯 위에 덮어쓴 텍스처를 적용합니다.
         * @details 렌더 스레드가 `updateRHI` 뒤에 부른다(GPUScene). 슬롯 바인딩 백엔드는 배치마다 슬롯을 바인딩하고, 인스턴스가 있는 배치는 그
         *          인스턴스의 것이다(합치기는 네이티브 bindless 에서만 켜진다).
         */
        void collectTextureSlotSrvs( RHIDescriptorIndex* pOutSlot, uint32 slotCount ) const;
        /** @brief 캐시된 셰이더 define 의 사본입니다 — 캐시는 `_overrideMutex` 아래이고 렌더 · 게임 스레드가 함께 고치므로 참조를 잠금 밖으로 내주지 않습니다. */
        vector<string> getCachedShaderDefines() const;
        /** @brief 퍼뮤테이션 해시를 반환합니다. */
        uint64 getPermutationHash() const;
        /** @brief bindless 디스크립터 인덱스를 반환합니다. */
        RHIDescriptorIndex getDescriptorIndex() const;
        /**
         * @brief 상수 버퍼 핸들입니다(0이면 아직 없습니다). 핸들은 세대를 포함하므로 "다시 만들었는가" 를 이 값으로 판단합니다.
         * @details 디스크립터 인덱스로는 판단할 수 없다. DX11 과 GL 은 인덱스를 즉시 회수해 다음 등록이 같은 번호를 받는다.
         */
        RHIBufferHandle getConstantBufferHandle() const { return _constant._buffer; }
        /** @brief 인스턴스 패킹 버퍼를 반환합니다. */
        const vector<uint8>& getBuffer() const { return _bytes; }
        /** @brief 파라미터가 오버라이드됐으면 true 입니다. */
        bool isParameterOverridden( hashed_string name ) const;
        /** @brief 리플렉션과 파라미터를 대조합니다. */
        bool validateParametersWithReflection( const ShaderReflectionData& reflectionData ) const;

    private:
        /**
         * @struct TextureOverride
         * @brief 텍스처 덮어쓰기 하나입니다. 게임 스레드는 `_assetPath` 만 쓰고, 렌더 스레드가 빌린 것(`_acquiredPath`, `_pTexture`)을 맞춥니다.
         */
        struct TextureOverride
        {
            hashed_string _name;                ///< 텍스처 프로퍼티 이름
            string        _assetPath;           ///< 원하는 텍스처 경로. 비면 지울 덮어쓰기이며 렌더 스레드가 반환한 뒤 뺀다.
            string        _acquiredPath;        ///< 렌더 스레드가 마지막으로 맞춘 경로. 빌리지 못했어도 적어 두어 매 프레임 다시 시도하지 않는다.
            Texture2D*    _pTexture{ nullptr }; ///< 빌린 텍스처. 다시 올려도 객체는 그대로라 SRV 를 그때마다 읽는다.
        };

        /** @brief 런타임 오버라이드를 Desc 에 다시 씁니다. */
        void syncDescOverrides() const;
        /**
         * @brief 텍스처 덮어쓰기를 원하는 상태로 맞춥니다(렌더 스레드, `updateRHI`). 바뀐 경로는 반환하고 새로 빌리며, 지운 것은 뺍니다.
         * @return 다시 패킹해야 하면 true 입니다(빌린 것이 바뀌었거나 텍스처가 다시 올라왔습니다).
         */
        bool syncTextureOverrides( IRHIDevice* pRHI );
        /** @brief 빌린 텍스처를 모두 반환합니다. `pRHI` 가 nullptr 이면(디바이스가 이미 없다) 참조만 놓습니다. */
        void releaseTextureOverrides( IRHIDevice* pRHI );
        /**
         * @brief 덮어쓴 텍스처의 슬롯 번호입니다. 부모가 그 프로퍼티에 텍스처를 빌렸으면 그 슬롯을, 아니면 부모 슬롯 뒤에 덮어쓰기 순서대로 이어 붙입니다.
         * @return 슬롯 수(`shaderslot::kMaterialTextureCount`)를 넘거나 덮어쓰기가 없으면 Material::kInvalidTextureSlot 입니다.
         */
        uint32 findTextureSlot( hashed_string name ) const;
        /** @brief XML 텍스트에서 인스턴스를 로드합니다. */
        [[nodiscard]] bool loadFromXML( string_view xmlText );
        /** @brief 정의 캐시가 더러우면 다시 만듭니다(`getCachedShaderDefines` · `getPermutationHash` 의 잠금 안 부분). */
        void refreshCachedDefinesLocked() const SW_REQUIRES_LOCK( _overrideMutex );

        Material*            _pParentMaterial;
        MaterialInstanceDesc _desc;

        /**
         * @brief 덮어쓰기 목록과 더티 비트를 게임 스레드 세터와 렌더 스레드의 `updateRHI`, `collectTextureSlotSrvs` 가 나눠 쓰는 락입니다.
         * @details 지형, 식생, 물처럼 매 틱 값을 넣는 컴포넌트가 있어, 렌더 스레드가 목록을 읽는 동안 세터가 목록을 다시 잡을 수 있다.
         */
        mutable mutex _overrideMutex;

        vector<pair<hashed_string, string>>            _listValueOverride;
        vector<pair<hashed_string, float32>>           _listScalarOverride;
        vector<pair<hashed_string, array<float32, 4>>> _listVectorOverride;
        vector<TextureOverride>                        _listTextureOverride;
        vector<pair<hashed_string, bool>>              _listKeywordOverride;
        vector<pair<hashed_string, string>>            _listMultiCompileOverride;
        MaterialQualityLevel                           _qualityOverride;

        vector<uint8> _bytes;
        /** @brief 상수 버퍼입니다. 어느 디바이스의 것인지를 세대로 압니다(RHIResidentBuffer). 인덱스는 이 버퍼의 것입니다. */
        RHIResidentBuffer                   _constant;
        RHIDescriptorIndex _descriptorIndex SW_GUARDED_BY( _overrideMutex );
        /**
         * @brief `_constant` 를 **만들 때 준 바이트 수**입니다.
         * @details `updateConstantBuffer( 버퍼, 데이터, 크기 )` 에는 적혀 있지 않은 전제가 있다. 그
         *          크기는 버퍼를 만들 때 준 크기를 넘으면 안 된다. 네 백엔드 중 셋은 그것을
         *          검사하지 않고 그대로 복사하므로(GL 만 API 가 막아 준다) 넘기면 프레임 슬롯 밖까지
         *          쓴다. 부모 머티리얼의 상수 버퍼는 **셰이더를 다시 쿠킹하는 동안 커질 수 있으므로**
         *          (레이아웃이 바뀐다) 만들 때의 크기를 가지고 있다가 커지면 다시 만든다.
         */
        uint32 _constantByteSize SW_GUARDED_BY( _overrideMutex );
        /** @brief `_bytes` 를 복사할 때의 부모 `getBufferGeneration()` 입니다. 다르면 부모 값이나 레이아웃이 바뀐 것이라 다시 복사합니다. */
        uint32 _parentBufferGeneration SW_GUARDED_BY( _overrideMutex );
        /** @brief 텍스처 덮어쓰기를 빌린 디바이스입니다. 반환할 때 씁니다(디바이스가 죽으면 forgetRHI 가 비웁니다). */
        IRHIDevice* _pTextureDevice;
        /** @brief 덮어쓴 텍스처를 패킹할 때의 `TextureCache::getReloadGeneration()` 입니다. 다르면 다시 올라온 텍스처의 새 SRV 를 다시 패킹합니다. */
        uint32 _textureReloadGeneration;

        mutable vector<string> _listCachedDefine SW_GUARDED_BY( _overrideMutex );
        mutable uint64 _cachedPermutationHash    SW_GUARDED_BY( _overrideMutex );
        mutable uint64 _parentPermutationHash    SW_GUARDED_BY( _overrideMutex );
        mutable uint8                            _bDefinesDirty : 1 SW_GUARDED_BY( _overrideMutex );
        uint8                                    _bGPUDirty     : 1;
        [[maybe_unused]] uint8                   _instReserved  : 6;
    };
} // namespace sw
