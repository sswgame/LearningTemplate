/**
 * @file Material.h
 * @brief XML Material asset + MaterialInstance overrides (UE MIC / Unity MaterialPropertyBlock).
 * @details
 *  - Master Material: property schema + `_defaultValue` + permutations
 *  - MaterialInstance: per-use parameter / keyword / multi_compile overrides → own CB
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Graphics/Material/MaterialTypes.h"

namespace sw
{
    struct float4;
    struct ShaderCompileResult;
    struct ShaderReflectionData;

    class IRHIDevice;

    /// @brief 셰이더 permutation과 패킹 CB를 가진 머티리얼 에셋
    class SW_API Material
    {
    public:
        /** @brief 빈 머티리얼 에셋. */
        Material();
        /** @brief GPU 버퍼와 비동기 로드를 정리합니다. */
        ~Material();

        /** @brief 복사를 금지합니다. */
        Material( const Material& ) = delete;
        /** @brief 대입을 금지합니다. */
        Material& operator=( const Material& ) = delete;
        /** @brief 이동을 금지합니다. */
        Material( Material&& ) = delete;
        /** @brief 이동 대입을 금지합니다. */
        Material& operator=( Material&& ) = delete;

        /** @brief 머티리얼 에셋을 로드합니다. 경로는 호출측/GameData. */
        bool initialize( IRHIDevice* pRhi, string_view assetRelativePath );
        /** @brief GPU 리소스를 해제합니다. */
        void shutdown( IRHIDevice* pRhi );

        /** @brief 파일에서 머티리얼을 로드합니다. */
        bool loadFromFile( string_view assetRelativePath );
        /** @brief 파일을 비동기로 로드합니다. */
        TaskHandle loadFromFileAsync( string_view assetRelativePath );
        /** @brief 파일로 저장합니다. */
        bool saveToFile( string_view assetRelativePath ) const;
        /** @brief 현재 디스크립터를 XML 문자열로 만듭니다. */
        string saveToString() const;
        /** @brief XML 텍스트에서 머티리얼을 로드합니다. */
        bool loadFromXml( string_view xmlText );
        /** @brief 컴파일 결과로 셰이더/리플렉션을 다시 붙입니다. */
        void reloadShader( IRHIDevice* pRhi, const ShaderCompileResult& result );
        /** @brief 셰이더 리플렉션에 맞춰 프로퍼티 목록을 맞춥니다. */
        bool syncPropertiesFromReflection( const ShaderReflectionData& reflectionData );
        /** @brief 프로퍼티를 패킹 CB에 다시 씁니다. */
        bool rebuildPackedBuffer();
        /** @brief 한 프로퍼티를 `_defaultValue`로 되돌리고 CB를 다시 올립니다. */
        bool resetPropertyToDefault( IRHIDevice* pRhi, hashed_string name );
        /** @brief 모든 프로퍼티를 기본값으로 되돌립니다. */
        void resetAllToDefaults( IRHIDevice* pRhi );
        /**
         * @brief 이 머티리얼 레이아웃으로 외부 버퍼에 값을 패킹합니다.
         * @details MaterialInstance가 마스터를 건드리지 않고 오버라이드 CB를 만들 때 씁니다.
         */
        bool packNamedValueIntoBuffer( hashed_string name, string_view value, vector<uint8>& inoutBuffer ) const;

        /**
         * @brief 텍스처 디스크립터 인덱스를 대상 버퍼에 직접 패킹합니다 (문자열 변환 없음).
         */
        bool packTextureIntoBuffer( hashed_string name, RHIDescriptorIndex descIdx, vector<uint8>& inoutBuffer ) const;

        /**
         * @brief raw POD 바이너리 데이터를 대상 버퍼의 프로퍼티 오프셋에 직접 씁니다.
         */
        bool packRawDataIntoBuffer( hashed_string name, const void* pData, uint32 byteSize, vector<uint8>& inoutBuffer ) const;

        /** @brief 패킹 버퍼 오프셋에 raw 바이트를 씁니다. */
        void setPropertyData( IRHIDevice* pRhi, uint32 offset, uint32 size, const void* pData );
        /** @brief 이름 프로퍼티 값을 텍스트로 설정합니다. */
        bool setPropertyValue( IRHIDevice* pRhi, hashed_string name, string_view value );
        /** @brief 텍스처 슬롯에 디스크립터를 넣습니다. */
        bool setTextureProperty( IRHIDevice* pRhi, hashed_string name, RHIDescriptorIndex descIdx );
        /**
         * @brief Texture2D 프로퍼티 중 assetPath 가 있는 것을 TextureCache 에서 빌려 SRV 인덱스를 패킹합니다.
         * @details initialize 끝에서 부른다(CB 가 있어야 인덱스가 GPU 에 올라간다). shutdown 이 releaseTextureAssets 로 되돌린다.
         */
        void resolveTextureAssets( IRHIDevice* pRhi );
        /** @brief resolveTextureAssets 가 빌린 텍스처를 캐시에 돌려줍니다. */
        void releaseTextureAssets( IRHIDevice* pRhi );
        /** @brief 품질 레벨을 설정합니다. */
        void setQualityLevel( MaterialQualityLevel level );
        /** @brief 사용 플래그를 설정합니다. */
        void setUsageFlags( MaterialUsageFlags flags );
        /** @brief 정적 스위치를 켭니다/끕니다. */
        void setStaticSwitch( hashed_string name, bool bEnabled );
        /** @brief 멀티컴파일 옵션을 고릅니다. */
        void setMultiCompile( hashed_string name, string_view selectedOption );
        /** @brief 블렌드 모드를 설정합니다. */
        void setBlendMode( RHIBlendMode mode );
        /** @brief float32 파라미터를 설정합니다. */
        bool setParameterFloat( IRHIDevice* pRhi, hashed_string name, float32 value );

        /** @brief 디스크립터를 반환합니다. */
        const MaterialDesc& getDesc() const { return _desc; }
        /** @brief 디스크립터를 반환합니다. */
        MaterialDesc& getDesc() { return _desc; }
        /** @brief permutation 디스크립터를 반환합니다. */
        const MaterialPermutationDesc& getPermutations() const { return _desc._permutations; }
        /** @brief permutation 디스크립터를 반환합니다. */
        MaterialPermutationDesc& getPermutations() { return _desc._permutations; }
        /** @brief 프로퍼티 목록을 반환합니다. */
        const vector<MaterialProperty>& getProperties() const { return _data._listProperty; }
        /** @brief 패킹된 상수 버퍼를 반환합니다. */
        const vector<uint8>& getBuffer() const { return _data._listBuffer; }
        /** @brief 이름 프로퍼티를 찾습니다. */
        const MaterialProperty* findProperty( hashed_string name ) const;
        /** @brief 이름 프로퍼티를 찾습니다. */
        MaterialProperty* findProperty( hashed_string name );
        /** @brief 프로퍼티 raw 데이터를 반환합니다. */
        const void* getPropertyData( string_view name ) const;
        /** @brief 캐시된 셰이더 define을 반환합니다. */
        const vector<string>& getCachedShaderDefines() const;
        /** @brief 셰이더 키워드 목록을 모읍니다. */
        vector<string> collectShaderKeywords() const { return getCachedShaderDefines(); }
        /** @brief permutation 해시를 반환합니다. */
        uint64 getPermutationHash() const;
        /** @brief 셰이더 경로를 반환합니다. */
        const string& getShaderPath() const { return _desc._shaderPath; }
        /** @brief 머티리얼 이름을 반환합니다. */
        const string& getName() const { return _desc._name; }
        /** @brief bindless 디스크립터 인덱스를 반환합니다. */
        RHIDescriptorIndex getDescriptorIndex() const { return _descriptorIndex; }
        /** @brief 상수 버퍼 핸들을 반환합니다. */
        RHIBufferHandle getConstantBuffer() const { return _constantBuffer; }
        /** @brief 블렌드 모드를 반환합니다. */
        RHIBlendMode getBlendMode() const { return _blendMode; }
        /** @brief float32 파라미터를 읽습니다. */
        bool getParameterFloat( hashed_string name, float32& outValue ) const;

    private:
        /// @brief 비동기 셰이더 컴파일 진행 상태
        struct AsyncLoadState
        {
            mutex     _mutex;
            Material* _pMaterial{ nullptr };
        };

        /** @brief Desc를 런타임 상태에 적용합니다. */
        void applyDescToRuntime();
        /** @brief 런타임 프로퍼티를 Desc에 다시 씁니다 (저장용). */
        void syncDescFromRuntime() const;
        /** @brief TaskArgs: AsyncLoadState shared_ptr, path string. */
        static void loadFromFileAsyncJob( const TaskArgs& args );

        MaterialDesc               _desc;
        MaterialData               _data;
        RHIBufferHandle            _constantBuffer;
        RHIDescriptorIndex         _descriptorIndex;
        IRHIDevice*                _pRHIDevice;
        vector<string>             _listAcquiredTexturePath; ///< resolveTextureAssets 가 빌린 경로 — shutdown 때 그대로 돌려준다
        RHIBlendMode               _blendMode;
        shared_ptr<AsyncLoadState> _asyncLoadState;

        mutable vector<string> _listCachedDefine;
        mutable uint64         _cachedPermutationHash;
        mutable uint8          _bDefinesDirty    : 1;
        [[maybe_unused]] uint8 _reservedMaterial : 7;
    };
} // namespace sw
