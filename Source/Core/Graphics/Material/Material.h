#pragma once
#include "Core/Common/Common.h"
#include "Core/Graphics/RHI/RHITypes.h"
#include "Core/Utility/Task/TaskTypes.h"

/**
 * @file Material.h
 * @brief RHI Bindless 머티리얼 리소스 및 동적 머티리얼 인스턴스(MaterialInstance) 정의
 */

namespace sw
{
	class IRHIDevice;
	struct ShaderCompileResult;
	/**
	 * @enum MaterialPropertyType
	 * @brief 머티리얼 Constant Buffer 내 프로퍼티 변수 데이터 타입
	 */
	enum class MaterialPropertyType : uint8
	{
		Float,
		Float2,
		Float3,
		Float4,
		Float4x4,
		Uint,
		Uint2,
		Uint3,
		Uint4,
		Int,
		Int2,
		Int3,
		Int4,
		Unknown
	};

	/**
	 * @struct MaterialProperty
	 * @brief 머티리얼 개별 파라미터 정보 (이름, 타입, 오프셋, 크기)
	 */
	struct MaterialProperty
	{
		std::string			 name;	 ///< 파라미터 변수 이름
		MaterialPropertyType type;	 ///< 데이터 타입
		uint32				 offset; ///< CBuffer 내 바이트 오프셋
		uint32				 size;	 ///< 바이트 크기
	};

	/**
	 * @struct MaterialData
	 * @brief 머티리얼 파라미터 스키마 및 이진 데이터 버퍼
	 */
	struct MaterialData
	{
		std::vector<MaterialProperty> properties; ///< 파라미터 메타데이터 리스트
		std::vector<uint8>			  buffer;	  ///< Constant Buffer 사본 바이트 배열
	};

	/**
	 * @class Material
	 * @brief .material 리소스 파일을 로드하고 GPU CBuffer 및 Bindless Descriptor를 관리하는 마스터 머티리얼
	 */
	class SW_API Material
	{
	public:
		Material();
		~Material();

		Material( const Material& )			   = delete;
		Material& operator=( const Material& ) = delete;
		Material( Material&& )				   = delete;
		Material& operator=( Material&& )	   = delete;

		/**
		 * @brief .material 파일로부터 머티리얼 정보를 읽어 GPU 자원 생성 및 Bindless 등록
		 * @param rhi RHI 디바이스 포인터
		 * @param assetRelativePath 에셋 상대 경로
		 */
		bool initialize( IRHIDevice* rhi, const std::string& assetRelativePath = "Material/DefaultMaterial.material" );

		/** @brief .material 텍스트 로드 */
		bool loadFromFile( const std::string& assetRelativePath );

		/** @brief TaskManager 비동기 로드 (파괴 시 대기 태스크는 no-op) */
		TaskHandle loadFromFileAsync( const std::string& assetRelativePath );

		/** @brief .material 텍스트 저장 */
		bool saveToFile( const std::string& assetRelativePath ) const;

		/** @brief 머티리얼 파라미터 프로퍼티 목록 반환 */
		const std::vector<MaterialProperty>& getProperties() const { return _data.properties; }

		/** @brief CPU 측 Constant Buffer 데이터 사본 반환 */
		const std::vector<uint8>& getBuffer() const { return _data.buffer; }

		/** @brief 특정 이름의 프로퍼티 포인터 반환 */
		const void* getPropertyData( const std::string& name ) const;

		/** @brief 프로퍼티 데이터 수정 및 GPU 버퍼 갱신 */
		void setPropertyData( IRHIDevice* rhi, uint32 offset, uint32 size, const void* data );

		/** @brief 소스 셰이더 경로 반환 */
		const std::string& getShaderPath() const { return _shaderPath; }

		/** @brief 머티리얼 이름 반환 */
		const std::string& getName() const { return _name; }

		/** @brief 발급된 Bindless Descriptor Index 반환 */
		RHIDescriptorIndex getDescriptorIndex() const { return _descriptorIndex; }

		/** @brief LiveShaderManager 핫리로드 시 셰이더 갱신 처리 */
		void hotRefreshShader( IRHIDevice* rhi, const ShaderCompileResult& result );

		/** @brief 머티리얼 리소스 해제 */
		void shutdown( IRHIDevice* rhi );

	private:
		/** @brief Async load control block — outlives Material so tasks never UAF on this */
		struct AsyncLoadState
		{
			std::mutex mutex;
			Material*  material = nullptr;
		};

		std::string		   _name	   = "DefaultMaterial";
		std::string		   _shaderPath = "Shaders/BindlessTriangle.hlsl";
		MaterialData	   _data{};
		RHIBufferHandle	   _constantBuffer	= 0;
		RHIDescriptorIndex _descriptorIndex = kInvalidDescriptorIndex;
		IRHIDevice*		   _rhiDevice		= nullptr;
		std::shared_ptr<AsyncLoadState> _asyncLoadState;
	};

	/**
	 * @class MaterialInstance
	 * @brief Master Material을 참조하며 개별 파라미터 오버라이드를 수행하는 동적 인스턴스
	 */
	class SW_API MaterialInstance
	{
	public:
		MaterialInstance() = default;
		/** @brief 부모 Material을 지정하는 생성자 */
		explicit MaterialInstance( Material* parentMaterial );

		/** @brief 부모 Master Material을 설정합니다. */
		void	  setParent( Material* parentMaterial );
		Material* getParent() const { return _parentMaterial; }

		/** @brief 스칼라 파라미터 오버라이드를 설정합니다. */
		void setScalarParameter( hashed_string name, float32 value );
		/** @brief 스칼라 파라미터 값(오버라이드 또는 부모)을 반환합니다. */
		float32 getScalarParameter( hashed_string name, float32 defaultValue = 0.0f ) const;

		/** @brief float4 벡터 파라미터 오버라이드를 설정합니다. */
		void		   setVectorParameter( hashed_string name, const float32 color[4] );
		const float32* getVectorParameter( hashed_string name ) const;

		/** @brief 텍스처 디스크립터 파라미터 오버라이드를 설정합니다. */
		void setTextureParameter( hashed_string name, RHIDescriptorIndex descIdx );
		/** @brief 텍스처 파라미터 디스크립터 인덱스를 반환합니다. */
		RHIDescriptorIndex getTextureParameter( hashed_string name ) const;

		/** @brief 해당 파라미터가 인스턴스에서 오버라이드됐는지 반환합니다. */
		bool isParameterOverridden( hashed_string name ) const;
		/** @brief 셰이더 리플렉션과 파라미터 이름·타입 정합성을 검사합니다. */
		bool validateParametersWithReflection( const struct ShaderReflectionData& reflectionData ) const;
		/** @brief 모든 파라미터 오버라이드를 제거합니다. */
		void clearOverrides();

	private:
		Material*												  _parentMaterial = nullptr;
		std::unordered_map<hashed_string, float32>				  _scalarOverrides;
		std::unordered_map<hashed_string, std::array<float32, 4>> _vectorOverrides;
		std::unordered_map<hashed_string, RHIDescriptorIndex>	  _textureOverrides;
	};
} // namespace sw
