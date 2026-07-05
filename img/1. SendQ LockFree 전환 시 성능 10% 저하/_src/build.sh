#!/usr/bin/env bash
# ==========================================================================
# 인포그래픽 PNG 재생성 스크립트 (HTML -> 4x PNG, 헤드리스 Chrome/Edge)
#   사용법:  bash build.sh
#   결과:    같은 폴더의 01~05.html 을 렌더해 상위 폴더의 0N_*.png 5장을 덮어씁니다.
# 논리크기 x DSF4 = 최종 해상도 (PPT용 고해상도, Pretendard 폰트 임베드):
#   01 : 780x560   -> 3120x2240   (01_enqueue_62ns.png)
#   02 : 940x680   -> 3760x2720   (02_ab_normalized.png)
#   03 : 1200x600  -> 4800x2400   (03_before_after.png)
#   04 : 1160x560  -> 4640x2240   (04_spsc.png)
#   05 : 1140x560  -> 4560x2240   (05_dilution.png)
# ==========================================================================
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WINDIR="$(cygpath -m "$DIR")"   # Chrome 는 C:/... 형태 경로 필요
# 격리 프로필: 실행 중인 Chrome 에 붙어 스크린샷이 무시되는 것 방지
PROFILE="$(cygpath -m "$(mktemp -d 2>/dev/null || echo "${TEMP:-/tmp}/ig-render-prof")")"

CHROME="/c/Program Files/Google/Chrome/Application/chrome.exe"
[ -f "$CHROME" ] || CHROME="/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
[ -f "$CHROME" ] || { echo "Chrome/Edge 를 찾을 수 없습니다"; exit 1; }

render () {  # <name> <W> <H> <상위폴더_최종파일명>
  local name="$1" w="$2" h="$3" out="$4"
  rm -f "$DIR/$name.png"
  "$CHROME" --headless=new --disable-gpu --hide-scrollbars --allow-file-access-from-files --user-data-dir="$PROFILE" --force-device-scale-factor=4 \
    --screenshot="$WINDIR/$name.png" --window-size="$w,$h" "file:///$WINDIR/$name.html"
  cp "$DIR/$name.png" "$DIR/../$out"
  echo "  built  ../$out   (논리 ${w}x${h}, 4x 출력)"
}

echo "[build] using: $CHROME"
render 01 780 560 "01_enqueue_62ns.png"
render 02 940 680 "02_ab_normalized.png"
render 03 1200 600 "03_before_after.png"
render 04 1160 560 "04_spsc.png"
render 05 1140 560 "05_dilution.png"
echo "[build] done."
