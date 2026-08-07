#pragma once
/**
 * @file GlobalVariableManager.h
 * @brief 인게임 치트, 디버그 변수, 환경 설정 등을 관리하는 전역 변수 매니저 시스템
 */
#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Utility/Delegate/Delegate.h"

namespace sw
{
	struct GlobalVariableInfo;
	SW_DECLARE_DELEGATE( void, GlobalVariableChangedDelegate, GlobalVariableInfo* );

	enum class GlobalVariableType : uint8
	{
		Bool,
		Int32,
		Float,
		String,
		Enum
	};

	struct SW_API GlobalVariableInfo
	{
		std::string										_name;
		GlobalVariableType								_type  = GlobalVariableType::Bool;
		void*											_pData = nullptr;
		std::variant<bool, int32, float32, std::string> _defaultValue;
		std::string										_description;
		std::string										_enumType;
		std::string										_moduleName;

		GlobalVariableChangedDelegate _onValueChanged;

		/**
		 * @brief ValueAsBool을(를) 반환합니다
		 */
		bool getValueAsBool() const;
		/**
		 * @brief ValueAsInt을(를) 반환합니다
		 */
		int32 getValueAsInt() const;
		/**
		 * @brief ValueAsFloat을(를) 반환합니다
		 */
		float32 getValueAsFloat() const;
		/**
		 * @brief 값을 문자열로 반환합니다
		 */
		std::string getValueAsString() const;

		/**
		 * @brief 문자열로 전역 변수 값을 설정합니다
		 */
		bool setValueFromString( const std::string_view strValue );
		/**
		 * @brief 기본값으로 되돌립니다
		 */
		void resetToDefault();
	};

	class SW_API GlobalVariableManager
	{
	public:
		bool initialize();
		void registerToCommandLine( class CommandLineManager* pCmdLineManager );
		void updateFromCommandLine( class CommandLineManager* pCmdLineManager );
		void shutdown() { resetAllToDefault(); }

		/**
		 * @brief Variable을(를) 등록합니다
		 */
		bool registerVariable( const std::string_view name, GlobalVariableType type, void* pData, const std::variant<bool, int32, float32, std::string>& defaultValue, const std::string_view description, const std::string_view enumType = "", const std::string_view moduleName = "" );

		void registerPendingVariables( const std::string_view moduleName, struct GlobalVariableRegistrar* pHead );
		void unregisterVariablesByModule( const std::string_view moduleName );

		/**
		 * @brief 전역 변수를 이름으로 찾습니다
		 */
		GlobalVariableInfo*										   findVariable( const std::string& name );
		const std::unordered_map<std::string, GlobalVariableInfo>& getAllVariables() const;

		/**
		 * @brief 문자열로 전역 변수 값을 설정합니다
		 */
		bool setValueFromString( const std::string& name, const std::string& strValue );
		/**
		 * @brief 기본값으로 되돌립니다
		 */
		bool resetToDefault( const std::string& name );
		/**
		 * @brief 모든 값을 기본값으로 되돌립니다
		 */
		void resetAllToDefault();

	public:
		GlobalVariableManager()											 = default;
		virtual ~GlobalVariableManager()								 = default;
		GlobalVariableManager( const GlobalVariableManager& )			 = delete;
		GlobalVariableManager& operator=( const GlobalVariableManager& ) = delete;

	private:
		std::unordered_map<std::string, GlobalVariableInfo> _variables;
	};

	struct SW_API GlobalVariableRegistrar
	{
		std::string										_name;
		GlobalVariableType								_type;
		void*											_pData;
		std::variant<bool, int32, float32, std::string> _defaultValue;
		std::string										_description;
		std::string										_enumType;
		std::string										_moduleName;
		GlobalVariableRegistrar*						_next;

		static GlobalVariableRegistrar*& getHead();

		GlobalVariableRegistrar( const utf8* name, GlobalVariableType type, void* pData, const std::variant<bool, int32, float32, std::string>& defaultValue, const utf8* description, const utf8* enumType = "", const utf8* moduleName = "" );
	};
} // namespace sw

/** @brief SW_GLOBAL_VARIABLE_BOOL 매크로 정의입니다. */
#define SW_GLOBAL_VARIABLE_BOOL( name, defaultVal, desc )   \
	extern bool							 name;              \
	bool								 name = defaultVal; \
	static ::sw::GlobalVariableRegistrar sw_reg_##name( #name, ::sw::GlobalVariableType::Bool, &name, bool( defaultVal ), desc )

/** @brief SW_GLOBAL_VARIABLE_INT 매크로 정의입니다. */
#define SW_GLOBAL_VARIABLE_INT( name, defaultVal, desc )    \
	extern int32						 name;              \
	int32								 name = defaultVal; \
	static ::sw::GlobalVariableRegistrar sw_reg_##name( #name, ::sw::GlobalVariableType::Int32, &name, int32( defaultVal ), desc )

/** @brief SW_GLOBAL_VARIABLE_INT32 매크로 정의입니다. */
#define SW_GLOBAL_VARIABLE_INT32( name, defaultVal, desc ) SW_GLOBAL_VARIABLE_INT( name, defaultVal, desc )

/** @brief SW_GLOBAL_VARIABLE_FLOAT 매크로 정의입니다. */
#define SW_GLOBAL_VARIABLE_FLOAT( name, defaultVal, desc )  \
	extern float32						 name;              \
	float32								 name = defaultVal; \
	static ::sw::GlobalVariableRegistrar sw_reg_##name( #name, ::sw::GlobalVariableType::Float, &name, float32( defaultVal ), desc )

/** @brief SW_GLOBAL_VARIABLE_STRING 매크로 정의입니다. */
#define SW_GLOBAL_VARIABLE_STRING( name, defaultVal, desc ) \
	extern std::string					 name;              \
	std::string							 name = defaultVal; \
	static ::sw::GlobalVariableRegistrar sw_reg_##name( #name, ::sw::GlobalVariableType::String, &name, std::string( defaultVal ), desc )

/** @brief SW_GLOBAL_VARIABLE_ENUM 매크로 정의입니다. */
#define SW_GLOBAL_VARIABLE_ENUM( name, enumType, defaultVal, desc ) \
	extern enumType						 name;                      \
	enumType							 name = defaultVal;         \
	static ::sw::GlobalVariableRegistrar sw_reg_##name( #name, ::sw::GlobalVariableType::Enum, &name, int32( defaultVal ), desc, #enumType )

/** @brief SW_EXTERN_GLOBAL_VARIABLE_BOOL 매크로 정의입니다. */
#define SW_EXTERN_GLOBAL_VARIABLE_BOOL( name ) extern bool name
/** @brief SW_EXTERN_GLOBAL_VARIABLE_INT 매크로 정의입니다. */
#define SW_EXTERN_GLOBAL_VARIABLE_INT( name ) extern int32 name
/** @brief SW_EXTERN_GLOBAL_VARIABLE_INT32 매크로 정의입니다. */
#define SW_EXTERN_GLOBAL_VARIABLE_INT32( name ) extern int32 name
/** @brief SW_EXTERN_GLOBAL_VARIABLE_FLOAT 매크로 정의입니다. */
#define SW_EXTERN_GLOBAL_VARIABLE_FLOAT( name ) extern float32 name
/** @brief SW_EXTERN_GLOBAL_VARIABLE_STRING 매크로 정의입니다. */
#define SW_EXTERN_GLOBAL_VARIABLE_STRING( name ) extern std::string name
/** @brief SW_EXTERN_GLOBAL_VARIABLE_ENUM 매크로 정의입니다. */
#define SW_EXTERN_GLOBAL_VARIABLE_ENUM( name, enumType ) extern enumType name
