/**
 * @file SocketSetComponent.h
 * @brief 오브젝트 하나의 소켓과 마커 테이블(`*.sockets.xml`)을 가지고, 이름에서 월드 변환을 구하는 컴포넌트와 조회 도우미입니다.
 * @details 소켓은 같은 오브젝트의 애니메이션 유닛(`SkeletalMeshComponent`)의 현재 본을 따르고, 유닛이 없으면(문, 레버, 상자) 오브젝트 루트 기준이다.
 *          상호작용의 맞춤 마커(`Front`, `Handle`), 알림의 이펙트와 칼 끝 위치, 무기의 총구가 같은 테이블에서 나온다. 부모 본 이름은 시작할 때 유닛의
 *          스켈레톤과 대조한다(모르는 본은 오류). 조회 도우미(`SocketLookupUtil`)는 테이블에 없는 이름을 유닛의 본 이름으로도 받는다(`foot.l`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObject;
    class SkeletalMeshComponent;
    class SocketSet;

    /**
     * @class SocketSetComponent
     * @brief 소켓 에셋 하나를 이 오브젝트의 소켓 테이블로 씁니다. 에셋은 공유 캐시(`SocketSetCache`)에서 받고, 파일을 고치면 다시 받습니다.
     */
    REFLECT( Category = "Character", DisplayName = "Socket Set", Tooltip = "Named sockets and markers (*.sockets.xml) of this object, relative to its bones or root" )
    class SW_API SocketSetComponent : public Component
    {
    public:
        REFLECT_BODY();

        SocketSetComponent();
        virtual ~SocketSetComponent() override = default;

        void onBeginPlay() override;
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 소켓 에셋 경로를 바꾸고 다시 읽습니다. */
        void          setSocketSetPath( string_view path );
        const string& getSocketSetPath() const { return _socketSetPath; }
        /** @brief 소켓 에셋입니다(읽지 못했으면 nullptr). 파일이 다시 읽혔으면 새 내용입니다. */
        const SocketSet* getSocketSet() const;

        /**
         * @brief 소켓의 유닛 공간 변환을 구합니다(부모 본, 가상 본, 루트). @p pUnit 이 없으면 본을 부모로 둔 소켓은 구하지 못합니다.
         * @return 테이블에 없는 이름이거나 부모를 찾지 못하면 false 를 반환합니다.
         */
        bool computeSocketUnitTransform( const hashed_string& socketName, const SkeletalMeshComponent* pUnit, float4x4& outUnitTransform ) const;

    private:
        /** @brief 에셋을 (다시) 받습니다. */
        void loadSocketSet();
        /** @brief 부모 본 이름을 유닛 스켈레톤과 대조합니다(모르는 본은 오류). */
        void validateAgainstUnit() const;

        PROPERTY( Category = "Socket", DisplayName = "Socket Set", AssetPath, AssetType = "SocketSet", Tooltip = "Sockets and markers asset (*.sockets.xml)" )
        string _socketSetPath;

        shared_ptr<const SocketSet> _socketSet; ///< 캐시가 제자리에서 다시 읽으므로, 파일을 고치면 이 객체가 새 내용이다.
    };
} // namespace sw

namespace sw
{
    /** @brief 오브젝트의 소켓과 본 이름을 월드 변환으로 변환합니다(전부 static, 게임 스레드). */
    struct SW_API SocketLookupUtil
    {
        /**
         * @brief @p object 의 소켓 @p name 의 월드 변환을 구합니다. 소켓 테이블(`SocketSetComponent`)에서 먼저 찾고, 없으면 유닛의 본 이름으로 찾습니다.
         * @details 유닛 공간은 `SkeletalMeshComponent` 의 월드 변환 아래에 있고, 유닛이 없으면 오브젝트 루트 아래에 있다.
         * @return 테이블에도 본에도 없으면 false 를 반환합니다.
         */
        static bool findSocketWorldTransform( const GameObject& object, const hashed_string& name, float4x4& outWorldTransform );
        /** @brief 소켓의 월드 위치를 반환합니다. 찾지 못하면 오브젝트 루트 위치를 반환하고 false 를 반환합니다. */
        static bool findSocketWorldPosition( const GameObject& object, const hashed_string& name, float3& outPosition );
    };
} // namespace sw
