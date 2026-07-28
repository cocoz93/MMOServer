@echo off
rem 릴레이만 실행 (ws://127.0.0.1:9000 -> TCP 127.0.0.1:6000). 접속 로그 표시, Ctrl+C로 종료.
cd /d "%~dp0"
if not exist node_modules\ws ( echo [ws 설치 중...] & npm install )
node relay.js --ws 9000 --server-port 6000 --log
pause