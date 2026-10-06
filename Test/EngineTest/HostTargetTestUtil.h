/**
 * @file HostTargetTestUtil.h
 * @brief 빌드 타깃 · 호스트 타깃에 따라 달라지는 리소스를 다루는 시험 도우미입니다(전용 서버 패키지에는 텍스처 · 셰이더 바이너리 · 오디오가 없다).
 */
#pragma once
#include "Engine/Module/ModuleCatalog.h"
#include "Engine/Resource/ResourceUtil.h"

namespace test
{
    /** @brief 빌드 타깃 · 호스트 타깃 시험 도우미입니다. */
    struct HostTargetTestUtil
    {
        /**
         * @brief 클라이언트 코드가 없는 빌드(전용 서버 — `SW_TARGET_TYPE=Server`)면 true 입니다.
         * @details 타깃은 빌드 마스크(다른 TU 의 함수)로 묻는다 — 상수를 견주면 쓰지 않는 갈래가 "닿지 않는 코드" 경고가 된다.
         */
        static bool isServerOnlyBuild() { return ( sw::ModuleCatalog::getBuildTargetMask() & static_cast<uint8>( sw::ModuleTarget::Client ) ) == 0; }

        /**
         * @brief 전용 서버 빌드에서 @p resourceId 를 찾을 수 없으면 true 입니다.
         * @details 서버 패키지는 쿠킹 표 `target_excluded_asset_kinds` 의 종류(텍스처 · 셰이더 바이너리 · 오디오)를 담지 않는다. 클라이언트 코드가 든
         *          빌드에서 파일이 없으면 결함이므로 false 다 — 그 시험은 돌아서 진다.
         */
        static bool isLeftOutOfServerPackage( sw::string_view resourceId ) { return isServerOnlyBuild() && sw::ResourceUtil::hasResource( resourceId ) == false; }
    };
} // namespace test

namespace test
{
    /** @brief 시험 동안 호스트 타깃(`ResourceUtil::setHostTarget`)을 바꾸고 끝에 하네스 값(빈 값 — 아무것도 빼지 않는다)으로 되돌립니다. */
    struct ScopedHostTarget
    {
        explicit ScopedHostTarget( sw::string_view buildTargetName ) { sw::ResourceUtil::setHostTarget( buildTargetName ); }
        ~ScopedHostTarget() { sw::ResourceUtil::setHostTarget( "" ); }

        ScopedHostTarget( const ScopedHostTarget& )            = delete;
        ScopedHostTarget& operator=( const ScopedHostTarget& ) = delete;
    };
} // namespace test
