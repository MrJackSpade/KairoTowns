@echo off
if not exist "%~dp0shared\gradle\wrapper\gradle-wrapper.jar" (
  echo Initialize the pinned shared project: git submodule update --init shared 1>&2
  exit /b 1
)
call "%~dp0shared\gradlew.bat" -p "%~dp0." %*
exit /b %ERRORLEVEL%
