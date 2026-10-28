# 전용 서버 컨테이너 이미지 (서버 운영 O6). 빌드 컨텍스트는 `Pack.py --target Server` 가 만든 패키지 폴더다(리눅스 Shipping 서버).
#
#   py -3 Scripts/package/Pack.py --target Server --game <게임> --output Saved/Packages
#   docker build -f deploy/docker/Dockerfile -t <레지스트리>/<게임>-server:<판> Saved/Packages/<패키지 폴더>
#
# 운영 설정(포트 · 접속 문자열)은 이미지에 굽지 않는다 — `/etc/swserver/server.json` 으로 마운트한다(`deploy/kubernetes/server.yaml` 의 ConfigMap).
# 비밀(DB · 캐시 비밀번호, TLS 개인키 암호)은 환경 변수로, TLS 인증서 · 키 파일은 비밀 저장소 볼륨으로 준다(`deploy/README.md`). 이미지에는 비밀이 없다.
FROM debian:bookworm-slim

# 서버는 TLS(OpenSSL)와 libstdc++ 를 동적으로 쓴다. 필요한 최소만 둔다.
RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates libstdc++6 libssl3 \
    && rm -rf /var/lib/apt/lists/*

# 서버는 루트가 아닌 사용자로 돈다. `Saved/`(로그 · 크래시 묶음 · 프레임 이력 덤프)만 쓸 수 있으면 된다 — 쿠버네티스는 이 폴더에 emptyDir 를 얹는다.
RUN useradd --system --uid 10001 --home-dir /srv/game --shell /usr/sbin/nologin swserver
WORKDIR /srv/game
COPY --chown=swserver:swserver . /srv/game
RUN mkdir -p /srv/game/Saved /etc/swserver && chown -R swserver:swserver /srv/game/Saved /etc/swserver && chmod +x /srv/game/Server
USER swserver

# 게임(UDP) · 서비스(TCP) · 운영 HTTP(`/metrics` `/healthz` `/readyz`, 평문 — 클러스터 안에서만 연다)
EXPOSE 7777/udp 7780/tcp 9100/tcp

# 컨테이너 종료 신호(SIGTERM)는 서버의 종료 신호 처리기가 받아 드레인한다(`_drainWaitSeconds`). 셸 없이 서버가 PID 1 이라 신호가 그대로 닿는다.
ENTRYPOINT ["/srv/game/Server"]
CMD ["-server-config=/etc/swserver/server.json"]
