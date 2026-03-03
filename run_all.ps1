Get-ChildItem -Path mapb -Filter *.txt | Sort-Object Name | ForEach-Object {
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "Running: $($_.Name)" -ForegroundColor Yellow
    Write-Host "========================================" -ForegroundColor Cyan
    .\build\sqx.exe $_.Name
}
