@echo off
chcp 65001 >nul
cd /d "%~dp0"
where py >nul 2>nul
if %errorlevel%==0 (
    set "USB_AUDIO_PYTHON=py -3"
) else (
    set "USB_AUDIO_PYTHON=python"
)

%USB_AUDIO_PYTHON% play_usb_mp3.py --list
echo.
set /p "USB_AUDIO_PORT=请输入新增USB CDC端口号（例如COM7）: "
%USB_AUDIO_PYTHON% -X utf8 play_usb_mp3.py --port "%USB_AUDIO_PORT%" %*
pause
