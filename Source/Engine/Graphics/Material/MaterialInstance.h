/**
 * @file MaterialInstance.h
 * @brief 마스터 Material 위의 드로우/액터 단위 오버라이드
 */
#pragma once
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/RHI/RHIResidentBuffer.h"

namespace sw
{
    /**
     * @class MaterialInstance
     * @brief 마스터 Material 위의 드로우/액터 오버라이드 (UE MaterialInstanceDynamic).
     */
    class SW_API MaterialInstance final : public RHIRenderResource
    {
    public:
        /**
         * @brief 생성 열쇠 — create() 만 만들 수 있다.
         * @details 생성자가 이 열쇠를 요구하므로 `make_shared<MaterialInstance>()` 도 스택의 `MaterialInstance x;` 도 **컴파일되지 않는다.**
         *          모든 MaterialInstance 이 Engine 안에서 shared_ptr 로 태어난다는 것을 컴파일러가 보장한다 — 렌더 패킷이
         *          소유를 빌릴 수 있고, 제어 블록이 모듈 DLL 에 사는 일이 없다. 린트가 아니라 타입이 지킨다.
         */
        struct CreateKey
        {
        private:
            CreateKey() = default;
            friend class MaterialInstance;
        };
        /** @brief create() 전용 생성자 — 마스터 머티리얼에 붙습니다. */
        MaterialInstance( CreateKey, Material* pParentMaterial );
        /**
         * @brief Engine.dll 안에서 shared_ptr 로 만듭니다.
         * @details 제어 블록(소멸 코드)은 make_shared 를 부른 DLL 에 산다. 렌더 패킷(GpuScene 스냅샷)이 소유를 함께
         *          실으므로 게임 모듈이 만든 인스턴스를 엔진이 마지막까지 들 수 있고, 모듈이 내려간 뒤 놓으면
         *          없는 코드로 뛰어든다. 여기서 만들면 누가 마지막에 놓든 Engine 코드다.
         */
        static shared_ptr<MaterialInstance> create( Material* pParentMaterial );
        /** @brief 오버라이드 CB를 정리합니다. */
        ~MaterialInstance();

        /** @brief 복사를 금지합니다. */
        MaterialInstance( const MaterialInstance& ) = delete;
        /** @brief 대입을 금지합니다. */
        MaterialInstance& operator=( const MaterialInstance& ) = delete;

        /** @brief 종료합니다. */
        void shutdown( IRHIDevice* pRhi );

        /** @brief (RHIRenderResource) 살아 있는 디바이스에 상수버퍼를 돌려줍니다. */
        void releaseRhi( IRHIDevice* pDevice ) override;
        /** @brief (RHIRenderResource) 디바이스가 이미 없을 때 — 핸들만 잊습니다. */
        void forgetRhi() override;

        /** @brief 오버라이드만 있는 MaterialInstanceDesc XML을 로드합니다. 부모는 따로 설정. */
        bool loadFromFile( string_view assetRelativePath );
        /** @brief 인스턴스 XML을 저장합니다. */
        bool saveToFile( string_view assetRelativePath ) const;
        /**
         * @brief CPU 버퍼 = 부모 기본값 + 오버라이드. 인스턴스 CB를 만들거나 갱신합니다.
         * @return 드로우용 bindless 인덱스. 실패/오버라이드 없으면 부모로 폴백.
         */
        bool applyToGpu( IRHIDevice* pRhi );
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
        /** @brief 일반 오버라이드 (Enum/BitFlag/Color/Range/Bool/스칼라 텍스트). */
        void setParameter( hashed_string name, string_view value );
        /** @brief 스칼라 파라미터를 설정합니다. */
        void setScalarParameter( hashed_string name, float32 value );
        /** @brief 벡터 파라미터를 설정합니다. */
        void setVectorParameter( hashed_string name, const float4& value );
        /** @brief 텍스처 파라미터를 설정합니다. */
        void setTextureParameter( hashed_string name, RHIDescriptorIndex descIdx );
        /** @brief 품질 레벨을 설정합니다. */
        void setQualityLevel( MaterialQualityLevel level );
        /** @brief 멀티컴파일 옵션을 고릅니다. */
        void setMultiCompile( hashed_string name, string_view selectedOption );

        /** @brief 부모 머티리얼을 반환합니다. */
        Material* getParent() const { return _pParentMaterial; }
        /** @brief 인스턴스 디스크립터를 반환합니다. */
        const MaterialInstanceDesc& getDesc() const { return _desc; }
        /** @brief 이름 파라미터 텍스트를 읽습니다. */
        bool getParameter( hashed_string name, string& outValue ) const;
        /** @brief 스칼라 파라미터를 반환합니다. */
        float32 getScalarParameter( hashed_string name, float32 defaultValue = 0.0f ) const;
        /** @brief 벡터 파라미터를 반환합니다. */
        const float32* getVectorParameter( hashed_string name ) const;
        /** @brief 텍스처 파라미터를 반환합니다. */
        RHIDescriptorIndex getTextureParameter( hashed_string name ) const;
        /** @brief 키워드가 켜져 있으면 true. */
        bool isKeywordEnabled( hashed_string keyword ) const;
        /** @brief 캐시된 셰이더 define을 반환합니다. */
        const vector<string>& getCachedShaderDefines() const;
        /** @brief 셰이더 키워드 목록을 모읍니다. */
        vector<string> collectShaderKeywords() const { return getCachedShaderDefines(); }
        /** @brief permutation 해시를 반환합니다. */
        uint64 getPermutationHash() const;
        /** @brief bindless 디스크립터 인덱스를 반환합니다. */
        RHIDescriptorIndex getDescriptorIndex() const;
        /** @brief 인스턴스 패킹 버퍼를 반환합니다. */
        const vector<uint8>& getBuffer() const { return _listBuffer; }
        /** @brief 파라미터가 오버라이드됐으면 true. */
        bool isParameterOverridden( hashed_string name ) const;
        /** @brief 리플렉션과 파라미터를 대조합니다. */
        bool validateParametersWithReflection( const ShaderReflectionData& reflectionData ) const;

    private:
        /** @brief 런타임 오버라이드를 Desc에 다시 씁니다. */
        void syncDescOverrides() const;
        /** @brief XML 텍스트에서 인스턴스를 로드합니다. */
        bool loadFromXml( string_view xmlText );

        Material*            _pParentMaterial;
        MaterialInstanceDesc _desc;

        vector<pair<hashed_string, string>>             _listValueOverride;
        vector<pair<hashed_string, float32>>            _listScalarOverride;
        vector<pair<hashed_string, array<float32, 4>>>  _listVectorOverride;
        vector<pair<hashed_string, RHIDescriptorIndex>> _listTextureOverride;
        vector<pair<hashed_string, bool>>               _listKeywordOverride;
        vector<pair<hashed_string, string>>             _listMultiCompileOverride;
        MaterialQualityLevel                            _qualityOverride;

        vector<uint8> _listBuffer;
        /** @brief 상수버퍼 — 어느 디바이스의 것인지를 세대로 안다 (RHIResidentBuffer). 인덱스는 이 버퍼의 것이다. */
        RHIResidentBuffer  _constant;
        RHIDescriptorIndex _descriptorIndex;

        mutable vector<string> _listCachedDefine;
        mutable uint64         _cachedPermutationHash;
        mutable uint64         _parentPermutationHash;
        mutable uint8          _bDefinesDirty : 1;
        uint8                  _bGpuDirty     : 1;
        [[maybe_unused]] uint8 _instReserved  : 6;
    };
} // namespace sw
