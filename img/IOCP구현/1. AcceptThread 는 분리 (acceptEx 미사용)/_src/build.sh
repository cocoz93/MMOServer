#!/usr/bin/env bash
# ==========================================================================
# 인포그래픽 PNG 재생성 스크립트 (HTML -> 3x PNG, 헤드리스 Chrome/Edge)
#   사용법:  bash build.sh
#   결과:    같은 폴더의 01.html 을 렌더해 상위 폴더의 01_accept_thread_separation.png 를 덮어씁니다.
# 논리크기 x DSF3 = 최종 해상도 (PPT용 고해상도, Pretendard 폰트 임베드):
#   01 : 1200x600  -> 3600x1800  (01_accept_thread_separation.png)
# ==========================================================================
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WINDIR="$(cygpath -m "$DIR")"   # Chrome 는 C:/... 형태 경로 필요

CHROME="/c/Program Files/Google/Chrome/Application/chrome.exe"
[ -f "$CHROME" ] || CHROME="/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
[ -f "$CHROME" ] || { echo "Chrome/Edge 를 찾을 수 없습니다"; exit 1; }

PROFILE_DIR="$(cygpath -m "${TEMP:-/tmp}")/mmo-infographic-chrome-profile"   # 실행 중인 일반 Chrome 프로필과 충돌 방지

render () {  # <name> <W> <H> <상위폴더_최종파일명>
  local name="$1" w="$2" h="$3" out="$4"
  rm -f "$DIR/$name.png"
  local url="file:///$WINDIR/$name.html"
  url="${url// /%20}"   # 폴더명 공백을 인코딩 안 하면 최신 Chrome 헤드리스가 "Multiple targets"로 거부함
  "$CHROME" --headless --disable-gpu --hide-scrollbars --allow-file-access-from-files --force-device-scale-factor=3 \
    "--user-data-dir=$PROFILE_DIR" --screenshot="$WINDIR/$name.png" --window-size="$w,$h" "$url"
  [ -f "$DIR/$name.png" ] || { echo "render failed for $name (스크린샷이 생성되지 않음)"; exit 1; }
  cp "$DIR/$name.png" "$DIR/../$out"
  echo "  built  ../$out   (논리 ${w}x${h}, 3x 출력)"
}

echo "[build] using: $CHROME"
render 01 1200 600 "01_accept_thread_separation.png"
echo "[build] done."
