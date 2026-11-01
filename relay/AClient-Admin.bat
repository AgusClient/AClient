@echo off
if exist "C:\Program Files\Google\Chrome\Application\chrome.exe" (
    start "" "C:\Program Files\Google\Chrome\Application\chrome.exe" --app="https://supabase.com/dashboard/project/hmrgihzjynnnappzbvdd/editor"
) else if exist "C:\Program Files (x86)\Google\Chrome\Application\chrome.exe" (
    start "" "C:\Program Files (x86)\Google\Chrome\Application\chrome.exe" --app="https://supabase.com/dashboard/project/hmrgihzjynnnappzbvdd/editor"
) else if exist "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" (
    start "" "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" --app="https://supabase.com/dashboard/project/hmrgihzjynnnappzbvdd/editor"
) else (
    start "" "https://supabase.com/dashboard/project/hmrgihzjynnnappzbvdd/editor"
)
