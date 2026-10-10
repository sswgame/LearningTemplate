/**
 * @file DestructionProfile.h
 * @brief 파괴 재질 표(`*.destruction.xml`) — 깊이별 변형 문턱 · 연결 세기 · 지지 세기 · 밀도 · 충격 → 변형 · 파편 예산을 사람이 고치는 데이터로 둡니다.
 * @details 같은 `.fracture` 를 콘크리트 · 나무로 다르게 부수려면 표만 바꿉니다(다시 쿠킹하지 않는다). 모르는 원소 · 속성 · 숫자가 아닌 숫자는 로드 오류입니다.
 * @code
 *     <DestructionProfile density="2000" physicsMaterial="Default">
 *       <Strain thresholds="60 90 40"/>              <!-- 깊이 0(뿌리) · 1 · 2 …, 더 깊으면 마지막 값 -->
 *       <Links strength="400" supportStrength="30000"/>
 *       <Impact impulseToStrain="0.4" minImpulse="40" radius="0.35"/>
 *       <Debris lifetime="10" maxBodies="96" smallVolume="0.002" fadeTime="0.5" sleepRemoveTime="1.5" keepCollisionVolume="0.05" hullShrink="0.01"/>
 *       <Network poseRate="10"/>                      <!-- 선택. 덩어리(keepCollisionVolume 이상) 자세를 서버가 보내는 빈도(Hz) -->
 *     </DestructionProfile>
 * @endcode
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class CharacterDataReader;
    class XMLNode;

    /** @brief 파괴 재질 표 하나입니다. 파일 머리말 참고. 단위: 변형(피해와 같은 단위) · 미터 · 킬로그램 · 초 · 뉴턴. */
    struct SW_API DestructionProfile
    {
        /** @brief 엔진 기본 표 경로입니다(컴포넌트가 표를 적지 않으면 이것). */
        static constexpr string_view kDefaultPath = "engine/destruction/default.destruction.xml";

        vector<float32> _listStrainThreshold; ///< 깊이별 변형 문턱(이만큼 쌓이면 묶음이 자식으로 갈라진다 · 잎은 떨어져 나간다). 더 깊으면 마지막 값
        hashed_string   _physicsMaterial;     ///< 조각 바디의 물리 재질 이름
        float32         _density;             ///< 킬로그램/세제곱미터(2D 는 /제곱미터) — 지지 하중 · 바디 질량
        float32         _linkStrength;        ///< 연결이 끊기는 변형(맞닿은 넓이 1 제곱미터당)
        float32         _supportStrength;     ///< 연결이 버티는 하중(뉴턴, 맞닿은 넓이 1 제곱미터당) — 넘으면 무게로 무너진다
        float32         _impulseToStrain;     ///< 부딪힘 충격량(뉴턴초) 1 당 변형
        float32         _minImpulse;          ///< 이보다 작은 충격량은 피해가 아니다
        float32         _impactRadius;        ///< 부딪힘 피해가 퍼지는 반경(미터)
        float32         _debrisLifetime;      ///< 작은 파편이 사라지기 시작하는 시간(초)
        float32         _smallDebrisVolume;   ///< 이보다 작은 떨어진 덩어리는 작은 파편(수명 · 페이드)
        float32         _fadeTime;            ///< 사라질 때 줄어드는 시간(초)
        float32         _sleepRemoveTime;     ///< 잠든 덩어리가 이만큼 쉬면 바디를 빼고 정적 그림에 합친다(초)
        float32         _keepCollisionVolume; ///< 이보다 큰 덩어리는 쉬어도 바디(충돌)를 남긴다. 네트워크에서도 이 선이 덩어리(서버 자세 복제 · 플레이어와 부딪힘)와 파편(클라이언트 꾸밈 · Debris 레이어)을 가른다
        float32         _hullShrink;          ///< 조각 껍질을 무게 중심 쪽으로 줄이는 거리(미터) — 이웃 조각과 겹쳐 튀지 않게
        uint32          _maxDebrisBody;       ///< 한 오브젝트가 동시에 드는 떨어진 덩어리 바디의 상한(넘으면 오래된 작은 것부터 사라진다)
        float32         _networkPoseRate;     ///< 네트워크 — 서버가 움직이는 덩어리 자세를 보내는 빈도(Hz). 멈추면 최종 자세를 한 번 확정한다

        DestructionProfile();

        /** @brief 깊이의 문턱입니다. 표가 비면 무한(갈라지지 않는다)입니다. */
        float32 getStrainThreshold( uint32 depth ) const;
        /** @brief 글에서 읽습니다. 오류는 모아 로그로 내고 false 입니다(그때 값은 기본값). */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );
        /** @brief 리소스 경로를 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path );

    private:
        void readRoot( const XMLNode& root, CharacterDataReader& reader );
    };
} // namespace sw
