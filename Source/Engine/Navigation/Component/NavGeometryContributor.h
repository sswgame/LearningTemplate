/**
 * @file NavGeometryContributor.h
 * @brief 컴포넌트 종류마다 내비메시 베이크 기하를 내는 함수의 레지스트리입니다. 메시 · 강체로 모양을 알 수 없는 위층 컴포넌트(지형 높이장)가 등록합니다.
 * @details 기하 모으기(`NavGeometryCollector`)는 오브젝트를 훑다가 등록된 종류의 컴포넌트를 만나면 그 함수를 부릅니다. 런타임 베이크와 쿠킹이 같은 길을
 *          지나므로 플레이 전(쿠킹)에도 들어갑니다. 내비게이션(Object 층)은 위층 타입(Environment)을 include 하지 않고, 위층이 자기 .cpp 에서
 *          `SW_NAV_GEOMETRY_CONTRIBUTOR` 한 줄로 등록합니다 — `SW_SCENE_SERVICE` 와 같은 방향입니다.
 *          언리얼 `INavRelevantInterface::GetNavigationData`(컴포넌트가 내비 기하를 낸다)에 해당합니다.
 *
 *          `INavGeometrySource`(오브젝트 하나의 기하를 대신 내는 런타임 등록, 파괴 오브젝트)와 다른 점: 이것은 종류 단위 규칙이라 인스턴스가 등록하지 않아도 되고,
 *          그 오브젝트의 메시 · 강체 기하를 대신하지 않고 더합니다.
 *
 * @note 표는 Engine 이미지 안에서만 채웁니다(핫 리로드 모듈이 등록하면 언로드 뒤 죽은 함수를 가리킨다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    struct TypeInfo;

    class Component;
    class NavMeshGeometry;

    /** @brief 컴포넌트 하나의 월드 기하를 @p outGeometry 에 영역 @p area 로 더합니다. @p component 는 등록한 종류(또는 파생)입니다. */
    using NavGeometryContributeFunction = void ( * )( const Component& component, uint8 area, NavMeshGeometry& outGeometry );
    /** @brief 등록한 컴포넌트 종류의 타입 정보입니다(정적 초기화 순서를 피하려고 쓸 때 묻는다). */
    using NavGeometryTypeFunction = const TypeInfo* (*)();

    /** @struct NavGeometryContribution @brief 등록 줄 하나입니다. 등록한 .cpp 의 정적 저장소에 있습니다. */
    struct NavGeometryContribution
    {
        NavGeometryTypeFunction       _pGetType;
        NavGeometryContributeFunction _pContribute;
    };
} // namespace sw

namespace sw
{
    /** @class NavGeometryContributorRegistry @brief 파일 머리말 참고. 정적 초기화에서 채우고 기하 모으기가 읽습니다. */
    class SW_API NavGeometryContributorRegistry
    {
    public:
        /** @brief 등록 줄을 더합니다. 같은 줄을 두 번 등록하면 무시합니다. */
        static void add( const NavGeometryContribution* pContribution );
        /** @brief 지금까지 등록된 줄입니다(등록 순서). */
        static const vector<const NavGeometryContribution*>& getAll();
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_NAV_GEOMETRY_CONTRIBUTOR` 가 두는 정적 등록자입니다. */
    struct SW_API NavGeometryContributorRegistrar
    {
        explicit NavGeometryContributorRegistrar( const NavGeometryContribution* pContribution );
        NavGeometryContributorRegistrar( const NavGeometryContributorRegistrar& )            = delete;
        NavGeometryContributorRegistrar& operator=( const NavGeometryContributorRegistrar& ) = delete;
    };
} // namespace sw

/**
 * @brief 컴포넌트 종류 @p Type 의 베이크 기하 함수 @p function(`NavGeometryContributeFunction`)을 등록합니다(그 기능의 .cpp, 파일 범위, `namespace sw` 안).
 *        @p Type 은 리플렉션 타입이어야 합니다(`findStaticType<Type>()`).
 */
#define SW_NAV_GEOMETRY_CONTRIBUTOR( Type, function )                                                                                                \
    static const ::sw::NavGeometryContribution         sw_navGeometryContribution_##Type{ []() { return ::sw::findStaticType<Type>(); }, function }; \
    static const ::sw::NavGeometryContributorRegistrar sw_navGeometryContributorRegistrar_##Type{ &sw_navGeometryContribution_##Type };
