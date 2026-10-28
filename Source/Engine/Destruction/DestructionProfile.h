/**
 * @file DestructionProfile.h
 * @brief 파괴 재질 테이블(`*.destruction.xml`)입니다. 깊이별 변형 임계값, 연결 세기, 지지 세기, 밀도, 충격에서 변형으로의 변환, 파편 예산을 사람이 고치는 데이터로 둡니다.
 * @details 같은 `.fracture` 를 콘크리트나 나무로 다르게 부수려면 테이블만 바꾸면 된다(다시 쿠킹하지 않는다). 모르는 원소나 속성, 숫자가 아닌 값은 로드 오류다.
 * @code
 *     <DestructionProfile density="2000" physicsMaterial="Default">
 *       <Strain thresholds="60 90 40"/>              <!-- 깊이 0(루트), 1, 2 순서이고, 더 깊으면 마지막 값을 쓴다 -->
 *       <Links strength="400" supportStrength="30000"/>
 *       <Impact impulseToStrain="0.4" minImpulse="40" radius="0.35"/>
 *       <Debris lifetime="10" maxBodies="96" smallVolume="0.002" fadeTime="0.5" sleepRemoveTime="1.5" keepCollisionVolume="0.05" hullShrink="0.01"/>
 *       <Network poseRate="10"/>                      <!-- 선택. 덩어리(keepCollisionVolume 이상)의 자세를 서버가 보내는 빈도(Hz) -->
 *     </DestructionProfile>
 * @endcode
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/FileSystem/AssetID.h"

namespace sw
{
    class CharacterDataReader;
    class XMLNode;

    /** @brief 파괴 재질 테이블 하나입니다. 파일 머리말을 참고하세요. 단위는 변형(피해와 같은 단위), 미터, 킬로그램, 초, 뉴턴입니다. */
    struct SW_API DestructionProfile
    {
        vector<float32> _listStrainThreshold; ///< 깊이별 변형 임계값이다. 이만큼 쌓이면 묶음은 자식으로 갈라지고 잎은 떨어져 나간다. 더 깊으면 마지막 값을 쓴다.
        hashed_string   _physicsMaterial;     ///< 조각 바디의 물리 재질 이름이다.
        float32         _density;             ///< 킬로그램/세제곱미터다(2D 는 /제곱미터). 지지 하중과 바디 질량에 쓴다.
        float32         _linkStrength;        ///< 연결이 끊기는 변형이다(맞닿은 넓이 1 제곱미터당).
        float32         _supportStrength;     ///< 연결이 버티는 하중이다(뉴턴, 맞닿은 넓이 1 제곱미터당). 넘으면 무게로 무너진다.
        float32         _impulseToStrain;     ///< 충돌 충격량(뉴턴초) 1 당 변형이다.
        float32         _minImpulse;          ///< 이보다 작은 충격량은 피해로 보지 않는다.
        float32         _impactRadius;        ///< 충돌 피해가 퍼지는 반경이다(미터).
        float32         _debrisLifetime;      ///< 작은 파편이 사라지기 시작하는 시간(초)
        float32         _smallDebrisVolume;   ///< 이보다 작은 떨어진 덩어리는 작은 파편으로 본다(수명과 페이드를 적용한다).
        float32         _fadeTime;            ///< 사라질 때 줄어드는 시간이다(초).
        float32         _sleepRemoveTime;     ///< 잠든 덩어리가 이만큼 쉬면 바디를 제거하고 정적 렌더링에 합친다(초).
        float32         _keepCollisionVolume; ///< 이보다 큰 덩어리는 쉬어도 바디(충돌)를 남긴다. 네트워크에서도 이 값이 덩어리(서버가 자세를 복제하고 플레이어와 충돌한다)와 파편(클라이언트의 꾸밈이며 Debris 레이어)을 가른다.
        float32         _hullShrink;          ///< 조각 껍질을 무게 중심 쪽으로 줄이는 거리다(미터). 이웃 조각과 겹쳐 튀지 않게 한다.
        uint32          _maxDebrisBody;       ///< 한 오브젝트가 동시에 가질 수 있는 떨어진 덩어리 바디의 상한이다. 넘으면 오래된 작은 것부터 사라진다.
        float32         _networkPoseRate;     ///< 서버가 움직이는 덩어리의 자세를 보내는 빈도(Hz)다. 멈추면 최종 자세를 한 번 확정한다.

        DestructionProfile();

        /** @brief 깊이의 임계값을 반환합니다. 테이블이 비면 무한(갈라지지 않음)입니다. */
        float32 getStrainThreshold( uint32 depth ) const;
        /** @brief 텍스트에서 읽습니다. 오류는 모아 로그로 남기고 false 를 반환하며, 이때 값은 기본값입니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );
        /** @brief 리소스 경로를 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );

    private:
        void readRoot( const XMLNode& root, CharacterDataReader& reader );
    };
} // namespace sw
