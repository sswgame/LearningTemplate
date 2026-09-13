#include "pch.h"

#include "ReflectionTest/TestReflectionFixtures.h"

#include "Engine/Reflection/ReflectionCore.h"

// 수동 TypeInfo 등록은 **이 TU 하나** 에서만 일어난다 (헤더 머리 주석 참고).

namespace
{
    /** @brief DummyActor 등 수동 TypeInfo 를 레지스트리에 등록합니다. */
    void RegisterTypes( sw::TypeRegistry& registry )
    {
        {
            sw::TypeInfo info;
            info._name               = sw::hashed_string( "DummyBase" );
            info._fullyQualifiedName = sw::hashed_string( "sw::DummyBase" );
            info._parentFQN          = sw::hashed_string( "" );
            info._size               = sizeof( sw::DummyBase );
            registry.registerClass( info );
        }

        {
            sw::TypeInfo info;
            info._name               = sw::hashed_string( "DummyActor" );
            info._fullyQualifiedName = sw::hashed_string( "sw::DummyActor" );
            info._parentFQN          = sw::hashed_string( "sw::DummyBase" );
            info._size               = sizeof( sw::DummyActor );
            info._listProperty =
                {
                    {   sw::hashed_string( "_hp" ),   sw::hashed_string( "int32" ),
                     SW_OFFSET_OF( sw::DummyActor,    _hp ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
                    { sw::hashed_string( "_name" ),  sw::hashed_string( "string" ),
                     SW_OFFSET_OF( sw::DummyActor,  _name ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
                    {sw::hashed_string( "_speed" ), sw::hashed_string( "float32" ),
                     SW_OFFSET_OF( sw::DummyActor, _speed ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
            };
            registry.registerClass( info );
        }

        {
            sw::TypeInfo info;
            info._name               = sw::hashed_string( "ComplexData" );
            info._fullyQualifiedName = sw::hashed_string( "sw::ComplexData" );
            info._parentFQN          = sw::hashed_string( "" );
            info._size               = sizeof( sw::ComplexData );
            info._listProperty =
                {
                    { sw::hashed_string( "_id" ), sw::hashed_string( "int32" ),
                     SW_OFFSET_OF( sw::ComplexData, _id ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr },
                    { sw::hashed_string( "_title" ), sw::hashed_string( "string" ),
                     SW_OFFSET_OF( sw::ComplexData, _title ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr },
                    { sw::hashed_string( "_flags" ), sw::hashed_string( "sw::DummyBitFlag" ),
                     SW_OFFSET_OF( sw::ComplexData, _flags ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr },
                    { sw::hashed_string( "_listScore" ), sw::hashed_string( "vector" ),
                     SW_OFFSET_OF( sw::ComplexData, _listScore ), true, sw::ContainerKind::Sequence, sw::hashed_string( "int32" ), sw::hashed_string(),
                     sw::make_shared<sw::VectorWrapper<sw::vector<int32>>>() },
                    { sw::hashed_string( "_mapStat" ), sw::hashed_string( "map" ),
                     SW_OFFSET_OF( sw::ComplexData, _mapStat ), true, sw::ContainerKind::Map, sw::hashed_string( "int32" ), sw::hashed_string( "string" ),
                     sw::make_shared<sw::MapWrapper<sw::map<sw::string, int32>>>() },
            };
            registry.registerClass( info );
        }

        {
            sw::TypeInfo info;
            info._name               = sw::hashed_string( "NarrowEnumHost" );
            info._fullyQualifiedName = sw::hashed_string( "sw::NarrowEnumHost" );
            info._parentFQN          = sw::hashed_string( "" );
            info._size               = sizeof( sw::NarrowEnumHost );
            info._listProperty =
                {
                    { sw::hashed_string( "_mode" ), sw::hashed_string( "sw::NarrowEnum" ),
                     SW_OFFSET_OF( sw::NarrowEnumHost, _mode ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr },
            };
            registry.registerClass( info );
        }
    }

    /** @brief DummyType / DummyBitFlag 를 레지스트리에 등록합니다. */
    void RegisterEnums( sw::TypeRegistry& registry )
    {

        {
            sw::EnumInfo info;
            info._name               = sw::hashed_string( "DummyType" );
            info._fullyQualifiedName = sw::hashed_string( "sw::DummyType" );
            info._size               = static_cast<uint8>( sizeof( sw::DummyType ) );
            info._bIsBitFlag         = SW_FALSE;
            info._mapNameToValue =
                {
                    { sw::hashed_string( "None" ), 0},
                    {sw::hashed_string( "TypeA" ), 1},
                    {sw::hashed_string( "TypeB" ), 2},
            };
            info._mapValueToName =
                {
                    {0,  sw::hashed_string( "None" )},
                    {1, sw::hashed_string( "TypeA" )},
                    {2, sw::hashed_string( "TypeB" )},
            };
            registry.registerEnum( info );
        }

        {
            sw::EnumInfo info;
            info._name               = sw::hashed_string( "DummyBitFlag" );
            info._fullyQualifiedName = sw::hashed_string( "sw::DummyBitFlag" );
            info._size               = static_cast<uint8>( sizeof( sw::DummyBitFlag ) );
            info._bIsBitFlag         = SW_TRUE;
            info._mapNameToValue =
                {
                    {   sw::hashed_string( "None" ), 0},
                    {sw::hashed_string( "OptionA" ), 1},
                    {sw::hashed_string( "OptionB" ), 2},
                    {sw::hashed_string( "OptionC" ), 4},
            };
            info._mapValueToName =
                {
                    {0,    sw::hashed_string( "None" )},
                    {1, sw::hashed_string( "OptionA" )},
                    {2, sw::hashed_string( "OptionB" )},
                    {4, sw::hashed_string( "OptionC" )},
            };
            registry.registerEnum( info );
        }

        {
            sw::EnumInfo info;
            info._name               = sw::hashed_string( "NarrowEnum" );
            info._fullyQualifiedName = sw::hashed_string( "sw::NarrowEnum" );
            info._size               = static_cast<uint8>( sizeof( sw::NarrowEnum ) );
            info._bIsBitFlag         = SW_FALSE;
            info._mapNameToValue =
                {
                    {sw::hashed_string( "Zero" ), 0},
                    { sw::hashed_string( "One" ), 1},
                    { sw::hashed_string( "Two" ), 2},
            };
            info._mapValueToName =
                {
                    {0, sw::hashed_string( "Zero" )},
                    {1,  sw::hashed_string( "One" )},
                    {2,  sw::hashed_string( "Two" )},
            };
            registry.registerEnum( info );
        }
    }
} // namespace

struct RegistrarInit
{
    /** @brief 정적 초기화에서 Type/Enum registrar 를 연결합니다. */
    RegistrarInit()
    {
        static sw::TypeRegistrar s_regType( &RegisterTypes );
        static sw::EnumRegistrar s_regEnum( &RegisterEnums );
    }
};

static RegistrarInit s_RegistrarInit;
