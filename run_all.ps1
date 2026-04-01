Get-ChildItem -Path mapb -Filter *.txt | Sort-Object Name | ForEach-Object {
    .\build\sqx.exe $_.Name 8
}
