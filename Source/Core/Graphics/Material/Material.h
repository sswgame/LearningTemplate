#pragma once
#include "Core/Common/Common.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Graphics/Shader/ShaderCompiler.h"
#include "Core/Utility/Task/TaskTypes.h"

/**
 * @file Material.h
 * @brief RHI Bindless 머티리얼 리소스 및 동적 머티리얼 인스턴스(MaterialInstance) 정의
 */

namespace sw
{
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
		std::string			 name;   ///< 파라미터 변수 이름
		MaterialPropertyType type;   ///< 데이터 타입
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
		std::vector<uint8>			  buffer;     ///< Constant Buffer 사본 바이트 배열
	};

	/**
	 * @class Material
	 * @brief .material 리소스 파일을 로드하고 GPU CBuffer 및 Bindless Descriptor를 관리하는 마스터 머티리얼
	 */
	class SW_API Material
	{
	public:
		Material()	= default;

		/**
		 * @brief .material 파일로부터 머티리얼 정보를 읽어 GPU 자원 생성 및 Bindless 등록
		 * @param rhi RHI 디바이스 포인터
		 * @param assetRelativePath 에셋 상대 경로
		 */
		bool initialize( IRHIDevice* rhi, const std::string& assetRelativePath = "Material/DefaultMaterial.material" );

		/** @brief .material 텍스트 로드 */
		bool loadFromFile( const std::string& assetRelativePath );

		/** @brief TaskManager 비동기 로드 */
		TaskHandle loadFromFileAsync( const std::string& assetRelativePath );

		/** @brief .material 텍스트 저장 */
		bool saveToFile( const std::string& assetRelativePath ) const;

		/** @brief 머티리얼 파라미터 프로퍼티 목록 반환 */
		const std::vector<MaterialProperty>& getProperties() const { return _data.properties; }

		/** @brief Constant Buffer 데이터 수신 */
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
		std::string		   _name	   = "DefaultMaterial";
		std::string		   _shaderPath = "Shaders/BindlessTriangle.hlsl";
		MaterialData	   _data{};
		RHIBufferHandle	   _constantBuffer	= 0;
		RHIDescriptorIndex _descriptorIndex = kInvalidDescriptorIndex;
		IRHIDevice*		   _rhiDevice		= nullptr;
	};

	/**
	 * @class MaterialInstance
	 * @brief Master Material을 참조하며 개별 파라미터 오버라이드를 수행하는 동적 인스턴스
	 */
	class SW_API MaterialInstance
	{
	public:
		MaterialInstance() = default;
		/**
		 * @brief MaterialInstance 처리를 수행합니다.
		 */
		explicit MaterialInstance( Material* parentMaterial );

		/**
		 * @brief setParent 처리를 수행합니다.
		 */
		void	  setParent( Material* parentMaterial );
		Material* getParent() const { return _parentMaterial; }

		/**
		 * @brief setScalarParameter 처리를 수행합니다.
		 */
		void	setScalarParameter( hashed_string name, float32 value );
		/**
		 * @brief getScalarParameter 처리를 수행합니다.
		 */
		float32 getScalarParameter( hashed_string name, float32 defaultValue = 0.0f ) const;

		/**
		 * @brief setVectorParameter 처리를 수행합니다.
		 */
		void		   setVectorParameter( hashed_string name, const float32 color[4] );
		const float32* getVectorParameter( hashed_string name ) const;

		/**
		 * @brief setTextureParameter 처리를 수행합니다.
		 */
		void			   setTextureParameter( hashed_string name, RHIDescriptorIndex descIdx );
		/**
		 * @brief getTextureParameter 처리를 수행합니다.
		 */
		RHIDescriptorIndex getTextureParameter( hashed_string name ) const;

		/**
		 * @brief isParameterOverridden 처리를 수행합니다.
		 */
		bool isParameterOverridden( hashed_string name ) const;
		/**
		 * @brief validateParametersWithReflection 처리를 수행합니다.
		 */
		bool validateParametersWithReflection( const struct ShaderReflectionData& reflectionData ) const;
		/**
		 * @brief clearOverrides 처리를 수행합니다.
		 */
		void clearOverrides();

	private:
		Material*												  _parentMaterial = nullptr;
		std::unordered_map<hashed_string, float32>				  _scalarOverrides;
		std::unordered_map<hashed_string, std::array<float32, 4>> _vectorOverrides;
		std::unordered_map<hashed_string, RHIDescriptorIndex>	  _textureOverrides;
	};
}
