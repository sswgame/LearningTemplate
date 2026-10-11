/**
 * @file FighterCatalog.h
 * @brief 철권 같은 3D 격투의 캐릭터 데이터입니다. 기반 `MoveCatalog` 의 프레임 데이터에 커맨드(철권 표기), 시작 자세, 조건, 스트링, 콤보 성질을 붙입니다.
 *        체력, 걷기, 몸 크기, 횡이동, 점프 수치도 여기에 둡니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/Combat/FrameData.h"
#include "GameFramework/Base/Actor/Input/InputCommandBuffer.h"
#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 기술을 낼 수 있는 자세입니다. 순서가 비트 번호입니다(`FighterMove::_postureMask`). */
    enum class FighterPosture : uint8
    {
        Standing = 0,
        Crouching,
        Airborne, ///< 점프 중
        Stance    ///< 캐릭터 고유 자세(`FighterMove::_stance` 로 이름을 정한다)
    };

    /** @brief 캐릭터의 기술 하나입니다. 프레임 데이터, 기술을 내는 방법, 맞았을 때의 콤보 성질을 담습니다. */
    struct FighterMove
    {
        MoveFrameData _frame{};                 ///< 기반 프레임 데이터(`_frame._id` 가 이 기술의 id)
        InputCommand  _command{};               ///< 커맨드(우선도 포함)
        hashed_string _stance{};                ///< `Stance` 자세에서만 쓰는 자세 이름
        hashed_string _enterStance{};           ///< 기술이 끝나면 이 자세로 들어간다(비어 있으면 서기나 앉기)
        float32       _pushback{ 0.1f };        ///< 맞거나 막은 쪽이 밀려나는 거리
        uint16        _breakButtons{ 0 };       ///< 잡기 기술에서 이 버튼을 풀기 창 안에 누르면 풀린다
        uint8         _postureMask{ 1 };        ///< `1 << FighterPosture` 값의 합(기본은 서기)
        uint8         _bStringOnly{ SW_FALSE }; ///< 스트링의 뒷부분이라 앞 기술의 캔슬 창 안에서만 나간다
        uint8         _bTracking{ SW_FALSE };   ///< 추적 기술이다. 횡이동하는 상대를 따라간다(직선 기술은 시작 방향으로 고정된다)
        uint8         _bScrew{ SW_FALSE };      ///< 스크류(토네이도)다. 공중 상대를 다시 띄워 멀리 밀어낸다(한 콤보에 한 번)
        uint8         _bBound{ SW_FALSE };      ///< 바운드다. 공중 상대를 바닥에 튕긴다(한 콤보에 한 번)
        uint8         _bGroundHit{ SW_FALSE };  ///< 다운된 상대만 고를 수 있고 다운된 상대를 때린다
        uint8         _bNearWall{ SW_FALSE };   ///< 상대가 벽 근처일 때만 나간다
        uint8         _bRageArt{ SW_FALSE };    ///< 레이지 아츠다. 레이지일 때 한 번 쓸 수 있고, 쓰면 레이지가 끝난다
        uint8         _bHeatBurst{ SW_FALSE };  ///< 히트 발동 기술이다. 히트가 켜진 대전에서 라운드마다 한 번 쓴다

        /** @brief @p posture 에서 낼 수 있는지 반환합니다. 자세 이름은 따로 확인합니다. */
        bool allowsPosture( FighterPosture posture ) const { return ( _postureMask & ( 1u << static_cast<uint32>( posture ) ) ) != 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 캐릭터 하나입니다. 길이 단위는 게임이 정합니다(테스트는 미터를 씁니다). */
    struct SW_GF_API FighterDef
    {
        hashed_string         _id{};
        string                _name{};
        vector<FighterMove>   _listMove{};
        vector<hashed_string> _listStance{};   ///< 기술들이 쓰는 고유 자세 이름(상태 저장은 이 목록의 인덱스를 쓴다)
        InputCommand          _sidestepUp{};   ///< 화면 안쪽 횡이동(기본 "u")
        InputCommand          _sidestepDown{}; ///< 화면 바깥쪽 횡이동(기본 "d,n", 아래로 톡 치는 입력)
        InputCommand          _jump{};         ///< 앞 점프(기본 "u/f")
        int32                 _health{ 170 };
        int32                 _sidestepFrames{ 16 };
        int32                 _jumpFrames{ 40 };
        float32               _walkSpeed{ 0.03f };     ///< 프레임당 이동 거리
        float32               _hurtRadius{ 0.35f };    ///< 허트 캡슐 반지름(옆과 앞뒤)
        float32               _pushRadius{ 0.3f };     ///< 서로 겹치지 않게 하는 몸 반지름
        float32               _standHeight{ 1.8f };    ///< 서 있을 때 허트 높이
        float32               _crouchHeight{ 1.1f };   ///< 앉았을 때 허트 높이
        float32               _sidestepAngle{ 40.0f }; ///< 횡이동 한 번에 상대를 축으로 도는 각도(도 단위)
        float32               _jumpHeight{ 1.0f };
        float32               _jumpDistance{ 0.8f };

        /** @brief id 의 기술 인덱스를 반환합니다. 없으면 −1 입니다. */
        int32 findMoveIndex( const hashed_string& moveID ) const;
        /** @brief 고유 자세 이름의 인덱스를 반환합니다. 없으면 −1 입니다. */
        int32 findStanceIndex( const hashed_string& stance ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class FighterCatalog
     * @brief 다음 형식의 XML 을 읽는 캐릭터 카탈로그입니다.
     *        `<FighterCatalog buttons="1,2,3,4"><Fighter id="kaz" name="Kaz" health="170" walkSpeed="0.03" hurtRadius="0.35" pushRadius="0.3"
     *        standHeight="1.8" crouchHeight="1.1" sidestepAngle="40" sidestepFrames="16" sidestep="u" sidestepDown="d,n" jump="u/f"
     *        jumpFrames="40" jumpHeight="1" jumpDistance="0.8">
     *        <Move id="jab" command="1" priority="0" maxGap="10" from="Standing,Crouching" stance="" enterStance="" stringOnly="false" tracking="false"
     *        screw="false" bound="false" groundHit="false" nearWall="false" rageArt="false" heatBurst="false" breakButtons="1" pushback="0.1"/>
     *        </Fighter></FighterCatalog>`
     * @details 기술의 프레임 데이터는 함께 넘긴 기반 `MoveCatalog` 에서 같은 id 로 복사해 온다(`frames="다른id"` 로 다른 id 를 지정할 수 있다). 프레임 데이터가 없거나 커맨드를 읽지 못하면 경고하고 그 기술만 버린다.
     *          `stance` 가 있으면 시작 자세는 `Stance` 다. `breakButtons` 는 커맨드 표기("1" 또는 "1+2")로 쓴다.
     */
    class SW_GF_API FighterCatalog
    {
    public:
        FighterCatalog();

        [[nodiscard]] bool loadFromResource( string_view path, const MoveCatalog& moveCatalog );
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, const MoveCatalog& moveCatalog, string_view sourceName = {} );
        void               addFighter( const FighterDef& fighter );

        const FighterDef*         findFighter( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<FighterDef>& getFighters() const { return _catalog.getAll(); }

    private:
        uint32 loadRoot( const XMLNode& root, const MoveCatalog& moveCatalog, string_view sourceName );

        GameCatalog<FighterDef> _catalog;
    };
} // namespace sw
