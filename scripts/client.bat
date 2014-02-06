echo Run with -W 102400 on Windows to avoid file-alloc stall!
REM .\swift -t 127.0.0.1:6778 -g 0.0.0.0:8192 -W 102400
.\swift -t http://127.0.0.1:5578/announce -g 0.0.0.0:8192 -W 102400