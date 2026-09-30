; NSIS installer for one staged app folder. Driven by packaging/bundle-windows.sh,
; which stages the exe + Qt/MinGW DLLs into dist/<name>/ and then runs makensis
; with -DAPP_NAME/-DAPP_EXE/-DAPP_VERSION/-DSRDIR/-DOUTFILE.
;   pacman -S mingw-w64-x86_64-nsis   # provides makensis

!include "MUI2.nsh"

Unicode true
SetCompressor /SOLID lzma
SetRegView 64             ; $PROGRAMFILES64 + the Uninstall key below are 64-bit
RequestExecutionLevel admin

!define APP_VERSION "0.0.0"
!define APP_PUBLISHER "HebutMas"

Name "${APP_NAME}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${APP_NAME}"
InstallDirRegKey HKLM "Software\${APP_NAME}" "InstallDir"

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

!define UNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"

Section
  SetOutPath "$INSTDIR"
  File /r "${SRCDIR}\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\${APP_NAME}" "InstallDir" "$INSTDIR"

  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk" "$INSTDIR\uninstall.exe"
  CreateShortCut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"

  WriteRegStr    HKLM "${UNKEY}" "DisplayName"     "${APP_NAME}"
  WriteRegStr    HKLM "${UNKEY}" "DisplayVersion"  "${APP_VERSION}"
  WriteRegStr    HKLM "${UNKEY}" "Publisher"       "${APP_PUBLISHER}"
  WriteRegStr    HKLM "${UNKEY}" "DisplayIcon"     "$INSTDIR\${APP_EXE}"
  WriteRegStr    HKLM "${UNKEY}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
  WriteRegDWORD  HKLM "${UNKEY}" "NoModify" 1
  WriteRegDWORD  HKLM "${UNKEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\${APP_NAME}.lnk"
  RMDir /r "$SMPROGRAMS\${APP_NAME}"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKLM "${UNKEY}"
  DeleteRegKey HKLM "Software\${APP_NAME}"
SectionEnd
