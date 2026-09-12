/**
 * @file GlobalVariableManager.h
 * @brief 인게임 치트, 디버그 변수, 환경 설정 등을 관리하는 전역 변수 매니저 시스템
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Delegate/Delegate.h"

namespace sw
{
    /** @brief 커맨드라인 파서. registerToCommandLine / updateFromCommandLine 에 넘깁니다. */
    class CommandLineManager;

    /** @brief 등록된 전역 변수 한 항목입니다. */
    struct GlobalVariableInfo;
    SW_DECLARE_DELEGATE( void, GlobalVariableChangedDelegate, GlobalVariableInfo* );

    // ------------------------------------------------------------------------------
    // 1) GlobalVariableType / GlobalVariableInfo — 이름·타입·기본값·콜백
    // ------------------------------------------------------------------------------
    /** @brief 전역 변수 저장 타입입니다. Enum 은 int32 로 둡니다. */
    enum class GlobalVariableType : uint8
    {
        Boolean,
        Int32,
        Float,
        String,
        Enum
    };

    /** @brief 한 전역 변수의 메타와 현재 값 포인터입니다. */
    struct SW_API GlobalVariableInfo
    {
        string                                     _name;
        GlobalVariableType                         _type = GlobalVariableType::Boolean;
        void*                                      _pData{ nullptr };
        std::variant<bool, int32, float32, string> _defaultValue;
        string                                     _description;
        string                                     _enumType;
        string                                     _moduleName;
        uint32                                     _typeSize{ 4 };

        GlobalVariableChangedDelegate _onValueChanged;

        /** @brief Boolean 이면 *_pData, 아니면 false 입니다. */
        bool getValueAsBool() const;
        /** @brief Int32/Enum 이면 *_pData, 아니면 0 입니다. */
        int32 getValueAsInt() const;
        /** @brief Float 이면 *_pData, 아니면 0 입니다. */
        float32 getValueAsFloat() const;
        /** @brief 타입에 맞게 문자열로 바꿉니다. */
        string getValueAsString() const;

        /** @brief *_pData 에 Boolean 값을 설정하고 콜백을 호출합니다. */
        bool setValueAsBool( bool val );
        /** @brief *_pData 에 Int32 또는 Enum 값을 설정하고 콜백을 호출합니다. */
        bool setValueAsInt( int32 val );
        /** @brief *_pData 에 Float 값을 설정하고 콜백을 호출합니다. */
        bool setValueAsFloat( float32 val );
        /** @brief *_pData 에 String 값을 설정하고 콜백을 호출합니다. */
        bool setValueAsString( string_view val );

        /** @brief 문자열을 파싱해 *_pData 에 쓰고 콜백을 호출합니다. */
        bool setValueFromString( string_view strValue );
        /** @brief *_pData 를 _defaultValue 로 되돌립니다. */
        void resetToDefault();
    };

    // ------------------------------------------------------------------------------
    // 2) GlobalVariableManager — 등록 · 커맨드라인 동기화 · 모듈 단위 해제
    // ------------------------------------------------------------------------------
    /** @brief 이름→Info 맵을 소유하고 모듈 핫리로드 때 떼어 냅니다. */
    class SW_API GlobalVariableManager
    {
    public:
        /** @brief 빈 맵으로 둡니다. */
        GlobalVariableManager() = default;
        /** @brief 맵만 버리며 사용자 데이터는 해제하지 않습니다. */
        virtual ~GlobalVariableManager() = default;
        /** @brief 복사를 금지합니다. */
        GlobalVariableManager( const GlobalVariableManager& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        GlobalVariableManager& operator=( const GlobalVariableManager& ) = delete;

        /** @brief 모든 변수를 기본값으로 되돌립니다. */
        void shutdown()
        {
            resetAllToDefault();
            _pCmdLineManager = nullptr;
        }

        /** @brief 커맨드라인 매니저에 변수들을 등록합니다. */
        void registerToCommandLine( class CommandLineManager* pCmdLineManager );

        /** @brief 커맨드라인 인자 값으로 전역 변수들을 업데이트합니다. */
        void updateFromCommandLine( const CommandLineManager* pCmdLineManager );

        /** @brief 새로운 전역 변수를 등록합니다. */
        bool registerVariable( string_view name, GlobalVariableType type, void* pData, const std::variant<bool, int32, float32, string>& defaultValue, string_view description, string_view enumType = "", string_view moduleName = "", uint32 typeSize = 4 );

        /** @brief pHead로 시작하는 연결 리스트(특정 모듈의 변수들)를 등록합니다. */
        void registerPendingVariables( string_view moduleName, const struct GlobalVariableRegistrar* pHead );

        /** @brief 특정 모듈 이름으로 등록된 변수들을 해제합니다. */
        void unregisterVariablesByModule( string_view moduleName );

        /** @brief 문자열을 파싱하여 변수 값을 설정합니다. */
        bool setValueFromString( string_view name, string_view strValue );

        /** @brief 특정 변수를 기본값으로 초기화합니다. */
        bool resetToDefault( string_view name );

        /** @brief 모든 변수를 기본값으로 초기화합니다. */
        void resetAllToDefault();

        /** @brief 이름으로 전역 변수 정보를 찾습니다. */
        GlobalVariableInfo* findVariable( string_view name );

        /** @brief 등록된 변수 이름 목록을 스냅샷으로 반환합니다. (thread-safe) */
        vector<string> collectVariableNames() const;

        /** @brief 등록된 변수 수를 반환합니다. (thread-safe) */
        uint32 getVariableCount() const;

    private:
        mutable std::shared_mutex                 _mutex;
        unordered_map<string, GlobalVariableInfo> _mapVariable;
        /**
         * @brief `registerToCommandLine` 이 물려 준 파서. 늦게 등록되는 변수의 보류값을 여기서 꺼낸다.
         * @details 소유하지 않는다 — 둘 다 `EngineLoop` 이 들고 있고, 선언 순서상 이 매니저가 먼저
         *          파괴된다. 모듈은 `CommandLineManager` 를 서비스로 못 받으므로(gameAllowed=0)
         *          보류값을 꺼내는 경로는 이 포인터뿐이다.
         */
        class CommandLineManager* _pCmdLineManager{ nullptr };
    };

    // ------------------------------------------------------------------------------
    // 3) GlobalVariableRegistrar — 정적 초기화로 모듈 로컬 리스트에 연결
    //    Core 는 getHead(), 다른 모듈은 SW_GVM_MODULE_HEAD
    // ------------------------------------------------------------------------------
    /** @brief 정적 객체가 모듈 리스트에 자신을 붙입니다. */
    struct SW_API GlobalVariableRegistrar
    {
        string                                     _name;
        GlobalVariableType                         _type;
        void*                                      _pData;
        std::variant<bool, int32, float32, string> _defaultValue;
        string                                     _description;
        string                                     _enumType;
        string                                     _moduleName;
        uint32                                     _typeSize;
        GlobalVariableRegistrar*                   _pNext;

        /**
         * @brief Core::getHead() 리스트에 연결합니다. Core 번역 단위 전용입니다.
         */
        GlobalVariableRegistrar( const utf8* pName, GlobalVariableType type, void* pData, const std::variant<bool, int32, float32, string>& defaultValue, const utf8* pDescription, const utf8* pEnumType = "", const utf8* pModuleName = "", uint32 typeSize = 4 );

        /**
         * @brief 모듈 로컬 리스트 헤드에 연결합니다. 핫 리로드에 안전합니다.
         */
        GlobalVariableRegistrar( GlobalVariableRegistrar*& pModuleHead, const utf8* pName, GlobalVariableType type, void* pData, const std::variant<bool, int32, float32, string>& defaultValue, const utf8* pDescription, const utf8* pEnumType = "", const utf8* pModuleName = "", uint32 typeSize = 4 );

        /** @brief Engine.dll(Core OBJECT) 전용 등록 리스트 헤드입니다. 다른 모듈은 자체 헤드를 써야 합니다. */
        static GlobalVariableRegistrar*& getHead();

    private:
        /** @brief 모듈 로컬 리스트에 연결합니다. */
        void linkTo( GlobalVariableRegistrar*& pModuleHead );
    };

    /** @brief Engine.dll GVM 심볼을 모듈 이름 "Engine"으로 등록합니다. */
} // namespace sw

/** @brief App/Editor/Game/Test 헤드 헤더에서 SW_GLOBAL_VARIABLE_* 앞에 재정의하세요. */
#ifndef SW_GVM_MODULE_HEAD
    #define SW_GVM_MODULE_HEAD() ( ::sw::GlobalVariableRegistrar::getHead() )
#endif

// 아래 정의들에서 `name` 은 **선언자 이름**이자 `#name`(문자열화), `sw_reg_##name`(붙여쓰기)로
// 쓰인다 — 셋 다 괄호를 씌우면 깨진다. 값 인자(`defaultVal`)는 괄호·static_cast 로 이미 막아 두었다.
// NOLINTBEGIN(bugprone-macro-parentheses)
/** @brief bool 전역 변수를 정의하고 모듈 리스트에 등록합니다. */
#define SW_GLOBAL_VARIABLE_BOOL( name, defaultVal, desc )       \
    extern bool                          name;                  \
    bool                                 name = ( defaultVal ); \
    static ::sw::GlobalVariableRegistrar sw_reg_##name( SW_GVM_MODULE_HEAD(), #name, ::sw::GlobalVariableType::Boolean, &name, static_cast<bool>( defaultVal ), desc )

/** @brief int32 전역 변수를 정의하고 모듈 리스트에 등록합니다. */
#define SW_GLOBAL_VARIABLE_INT( name, defaultVal, desc )        \
    extern int32                         name;                  \
    int32                                name = ( defaultVal ); \
    static ::sw::GlobalVariableRegistrar sw_reg_##name( SW_GVM_MODULE_HEAD(), #name, ::sw::GlobalVariableType::Int32, &name, static_cast<int32>( defaultVal ), desc )

/** @brief float32 전역 변수를 정의하고 모듈 리스트에 등록합니다. */
#define SW_GLOBAL_VARIABLE_FLOAT( name, defaultVal, desc )      \
    extern float32                       name;                  \
    float32                              name = ( defaultVal ); \
    static ::sw::GlobalVariableRegistrar sw_reg_##name( SW_GVM_MODULE_HEAD(), #name, ::sw::GlobalVariableType::Float, &name, static_cast<float32>( defaultVal ), desc )

/** @brief sw::string 전역 변수를 정의하고 모듈 리스트에 등록합니다. */
#define SW_GLOBAL_VARIABLE_STRING( name, defaultVal, desc )     \
    extern sw::string                    name;                  \
    sw::string                           name = ( defaultVal ); \
    static ::sw::GlobalVariableRegistrar sw_reg_##name( SW_GVM_MODULE_HEAD(), #name, ::sw::GlobalVariableType::String, &name, sw::string{ ( defaultVal ) }, desc )

/** @brief enum 전역 변수를 정의하고 모듈 리스트에 등록합니다. */
#define SW_GLOBAL_VARIABLE_ENUM( name, enumType, defaultVal, desc ) \
    extern enumType                      name;                      \
    enumType                             name = ( defaultVal );     \
    static ::sw::GlobalVariableRegistrar sw_reg_##name( SW_GVM_MODULE_HEAD(), #name, ::sw::GlobalVariableType::Enum, &name, static_cast<int32>( defaultVal ), desc, #enumType, "", static_cast<uint32>( sizeof( enumType ) ) )

// NOLINTEND(bugprone-macro-parentheses)

/** @brief 다른 TU 에서 bool 전역 변수를 참조합니다. */
#define SW_EXTERN_GLOBAL_VARIABLE_BOOL( name ) extern bool name
/** @brief 다른 TU 에서 int32 전역 변수를 참조합니다. */
#define SW_EXTERN_GLOBAL_VARIABLE_INT( name ) extern int32 name
/** @brief 다른 TU 에서 float32 전역 변수를 참조합니다. */
#define SW_EXTERN_GLOBAL_VARIABLE_FLOAT( name ) extern float32 name
/** @brief 다른 TU 에서 sw::string 전역 변수를 참조합니다. */
#define SW_EXTERN_GLOBAL_VARIABLE_STRING( name ) extern sw::string name
/** @brief 다른 TU 에서 enum 전역 변수를 참조합니다. */
#define SW_EXTERN_GLOBAL_VARIABLE_ENUM( name, enumType ) extern enumType name

// ------------------------------------------------------------------------------
// 5) 모듈 로컬 등록 — 로드된 DLL(EditorModule·SWGame)이 자기 변수를 스스로 올리고 내린다
// ------------------------------------------------------------------------------
/**
 * @brief 모듈 전용 등록 리스트와 등록/해제 함수를 **선언**합니다 (모듈의 전역 변수 헤더에서).
 * @param ns `sw` 하위 네임스페이스 이름 (예: `editor`, `game`). 그 안에 `getService` 가 있어야 합니다.
 * @details 모듈의 전역 변수는 Engine.dll 헤드가 아니라 **모듈 로컬 헤드**에 붙어야 한다. 그래야
 *          언로드될 때 `unregisterVariablesByModule` 이 통째로 걷어내고, 매니저가 사라진 DLL 안의
 *          주소를 계속 가리키지 않는다.
 * @note 헤드 매크로를 갈아 끼우는 두 줄은 각 모듈 헤더에 직접 쓴다 — 전처리기 지시자는 매크로 안에
 *       넣을 수 없다.
 *       @code
 *       #undef SW_GVM_MODULE_HEAD
 *       #define SW_GVM_MODULE_HEAD() ( ::sw::game::getGlobalVariableHead() )
 *       SW_DECLARE_MODULE_GLOBAL_VARIABLES( game );
 *       @endcode
 */
#define SW_DECLARE_MODULE_GLOBAL_VARIABLES( ns )                                                             \
    namespace sw::ns                                                                                         \
    {                                                                                                        \
        /** @brief 이 모듈 전용 등록 리스트 헤드. 함수 지역 static 이라 정적 초기화 순서에 걸리지 않는다. */ \
        ::sw::GlobalVariableRegistrar*& getGlobalVariableHead();                                             \
        /** @brief 이 모듈의 전역 변수를 매니저에 올리고 커맨드라인 보류값을 적용받습니다. */                \
        void registerGlobalVariables();                                                                      \
        /** @brief 등록을 해제합니다. 모듈이 내려가기 전에 반드시 불러야 합니다. */                          \
        void unregisterGlobalVariables();                                                                    \
    }                                                                                                        \
    static_assert( true, "뒤따르는 세미콜론을 삼킨다" )

/**
 * @brief `SW_DECLARE_MODULE_GLOBAL_VARIABLES` 로 선언한 것을 **구현**합니다 (모듈의 전역 변수 .cpp 에서).
 * @param ns 선언 때와 같은 네임스페이스 이름.
 * @param moduleName 매니저에 기록할 모듈 이름 (예: `config::kTargetGameModule`).
 * @details 등록 시점이 중요하다 — 커맨드라인은 모듈이 로드되기 **전에** 파싱되므로 `-gv_...` 값은
 *          파서의 보류표에 남아 있고, `registerPendingVariables` 가 도는 그 순간 적용된다. 그래서
 *          모듈은 자기 변수를 읽기 **전에** `registerGlobalVariables()` 를 불러야 한다.
 */
#define SW_IMPLEMENT_MODULE_GLOBAL_VARIABLES( ns, moduleName )                                 \
    namespace sw::ns                                                                           \
    {                                                                                          \
        ::sw::GlobalVariableRegistrar*& getGlobalVariableHead()                                \
        {                                                                                      \
            static ::sw::GlobalVariableRegistrar* s_pHead{ nullptr };                          \
            return s_pHead;                                                                    \
        }                                                                                      \
        void registerGlobalVariables()                                                         \
        {                                                                                      \
            ::sw::GlobalVariableManager* pManager = getService<::sw::GlobalVariableManager>(); \
            if ( pManager == nullptr )                                                         \
                return;                                                                        \
            pManager->registerPendingVariables( moduleName, getGlobalVariableHead() );         \
        }                                                                                      \
        void unregisterGlobalVariables()                                                       \
        {                                                                                      \
            ::sw::GlobalVariableManager* pManager = getService<::sw::GlobalVariableManager>(); \
            if ( pManager == nullptr )                                                         \
                return;                                                                        \
            pManager->unregisterVariablesByModule( moduleName );                               \
        }                                                                                      \
    }                                                                                          \
    static_assert( true, "뒤따르는 세미콜론을 삼킨다" )
