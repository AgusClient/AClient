@echo off
if exist "C:\Program Files\Google\Chrome\Application\chrome.exe" (
    start "" "C:\Program Files\Google\Chrome\Application\chrome.exe" --app="https://dry-water-e748.agus-maya52.workers.dev/admin"
) else if exist "C:\Program Files (x86)\Google\Chrome\Application\chrome.exe" (
    start "" "C:\Program Files (x86)\Google\Chrome\Application\chrome.exe" --app="https://dry-water-e748.agus-maya52.workers.dev/admin"
) else if exist "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" (
    start "" "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" --app="https://dry-water-e748.agus-maya52.workers.dev/admin"
) else (
    start "" "https://dry-water-e748.agus-maya52.workers.dev/admin"
)
